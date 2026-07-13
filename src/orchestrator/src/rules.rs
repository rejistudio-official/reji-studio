//! Kural motoru — JSON/TOML config dosyaları, hot-reload, condition evaluation.
//!
//! Adaptive bitrate, resolution, FPS controls kuralları burada tanımlanır.
//! Hot-reload: Windows NamedEvent veya file watching ile trigger edilir.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use tracing::{debug, info};

/// Kural değerlendirmesi için metrik snapshot.
#[derive(Debug, Clone, Copy)]
pub struct RuleMetrics {
    pub frame_drop_pct: u32,
    pub gpu_temp_c: i16,
    pub cpu_temp_c: i16,
    pub memory_usage_pct: u32,
    pub cpu_load_pct: u32,
    pub gpu_load_pct: u32,
    pub network_rtt_ms: u16,
    pub network_loss_pct: u8,
}

impl Default for RuleMetrics {
    fn default() -> Self {
        Self {
            frame_drop_pct: 0,
            gpu_temp_c: 0,
            cpu_temp_c: 0,
            memory_usage_pct: 0,
            cpu_load_pct: 0,
            gpu_load_pct: 0,
            network_rtt_ms: 0,
            network_loss_pct: 0,
        }
    }
}

/// Aksiyon tipleri — bitrate, resolution, FPS adaptasyonu.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Serialize, Deserialize)]
#[serde(rename_all = "snake_case")]
pub enum ActionType {
    BitrateReduce,
    BitrateRecover,
    ScaleResolution,
    RestoreResolution,
    CapFps,
    RestoreFps,
    LogOnly,
}

/// Aksiyon — kural değerlendirildiğinde oluşturulan komut.
#[derive(Debug, Clone)]
pub struct Action {
    /// V8/I33b: aksiyonu üreten kuralın ID'si — reject cooldown'ında
    /// (RuleEngine) kuralı bastırmak için pending deposu bu eşlemeyi taşır.
    /// FFI-facing benzersiz aksiyon ID'si artık burada değil; `next_action_id()`
    /// ile healing.rs'te RjAction'a atanır (tick-yerel ID kaldırıldı).
    pub rule_id: String,
    pub action_type: ActionType,
    pub param1: i32,
    pub param2: i32,
    pub timestamp: Instant,
    pub require_approval: bool,
    pub log_only: bool,
    pub is_critical: bool,
}

impl Default for Action {
    fn default() -> Self {
        Self {
            rule_id: String::new(),
            action_type: ActionType::LogOnly,
            param1: 0,
            param2: 0,
            timestamp: Instant::now(),
            require_approval: false,
            log_only: false,
            is_critical: false,
        }
    }
}

/// Kural — condition + action + modes.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Rule {
    pub id: String,
    #[serde(default)]
    pub description: String,
    pub condition: String,  // e.g., "frame_drop_pct > 10"
    pub action: String,
    #[serde(default)]
    pub params: HashMap<String, serde_json::Value>,
    pub modes: Vec<String>,
}

/// Condition string'i özyinelemeli olarak değerlendirir.
///
/// Operatör önceliği (düşükten yükseğe): || → && → tek koşul.
/// Bilinmeyen metrik veya parse hatasında `false` döner.
pub fn eval_condition(condition: &str, metrics: &RuleMetrics) -> bool {
    let condition = condition.trim();

    if condition.contains("||") {
        for part in condition.split("||") {
            if eval_condition(part.trim(), metrics) {
                return true;
            }
        }
        return false;
    }

    if condition.contains("&&") {
        for part in condition.split("&&") {
            if !eval_condition(part.trim(), metrics) {
                return false;
            }
        }
        return true;
    }

    eval_single_condition(condition, metrics).unwrap_or(false)
}

fn eval_single_condition(
    cond: &str,
    metrics: &RuleMetrics,
) -> Result<bool, Box<dyn std::error::Error>> {
    for op in &[">=", "<=", ">", "<", "=="] {
        if let Some(idx) = cond.find(op) {
            let metric_name = cond[..idx].trim();
            let threshold_str = cond[idx + op.len()..].trim();
            let threshold = threshold_str
                .parse::<i32>()
                .map_err(|_| format!("Invalid threshold: {}", threshold_str))?;

            let metric_value = match metric_name {
                "frame_drop_pct"   => metrics.frame_drop_pct as i32,
                "gpu_temp_c"       => metrics.gpu_temp_c as i32,
                "cpu_temp_c"       => metrics.cpu_temp_c as i32,
                "memory_usage_pct" => metrics.memory_usage_pct as i32,
                "cpu_load_pct"     => metrics.cpu_load_pct as i32,
                "gpu_load_pct"     => metrics.gpu_load_pct as i32,
                "network_rtt_ms"   => metrics.network_rtt_ms as i32,
                "network_loss_pct" => metrics.network_loss_pct as i32,
                _ => return Err(format!("Unknown metric: {}", metric_name).into()),
            };

            let result = match *op {
                ">"  => metric_value > threshold,
                "<"  => metric_value < threshold,
                ">=" => metric_value >= threshold,
                "<=" => metric_value <= threshold,
                "==" => metric_value == threshold,
                _    => false,
            };

            return Ok(result);
        }
    }

    Err(format!("Cannot parse condition: {}", cond).into())
}

/// Kural motoru — JSON/TOML kuralları yükler, hot-reload desteği.
#[derive(Debug)]
pub struct RuleEngine {
    rules: Arc<Mutex<Vec<Rule>>>,
    file_path: PathBuf,
    last_reload: Arc<Mutex<Instant>>,
    last_file_mtime: Arc<Mutex<Option<std::time::SystemTime>>>,
    hysteresis_ms: Arc<Mutex<u64>>,
    last_trigger: Arc<Mutex<HashMap<String, Instant>>>,
    /// V8/I33b: Kullanıcı bir aksiyonu CoPilot'ta reddettiğinde, üretici kuralın
    /// ID'si → cooldown bitiş anı. Bu süre dolana dek kural yeniden tetiklenmez
    /// (aksi halde ~1s tick'te yeniden üretilip kullanıcıyı spamler). Hysteresis
    /// ile aynı mekanizma ailesindendir, ayrı harita ile izlenir.
    cooldown_until: Arc<Mutex<HashMap<String, Instant>>>,
}

impl RuleEngine {
    /// Yeni RuleEngine'ı kurur, dosyadan kuralları yükler.
    pub fn new(file_path: impl AsRef<Path>) -> Result<Self, Box<dyn std::error::Error>> {
        let path = file_path.as_ref().to_path_buf();
        let engine = Self {
            rules: Arc::new(Mutex::new(Vec::new())),
            file_path: path,
            // 2s geride başlat — ilk hot_reload() çağrısı throttle'a takılmasın
            last_reload: Arc::new(Mutex::new(Instant::now() - Duration::from_secs(2))),
            last_file_mtime: Arc::new(Mutex::new(None)),
            hysteresis_ms: Arc::new(Mutex::new(0)),
            last_trigger: Arc::new(Mutex::new(HashMap::new())),
            cooldown_until: Arc::new(Mutex::new(HashMap::new())),
        };

        engine.hot_reload()?;
        Ok(engine)
    }

    /// Dosyayı diskten yeniden yükler. Validation + rollback on error.
    pub fn hot_reload(&self) -> Result<(), Box<dyn std::error::Error>> {
        // Throttle: 1s minimum interval
        {
            let now = Instant::now();
            let mut last_reload = self.last_reload.lock().unwrap();
            if now.duration_since(*last_reload).as_millis() < 1000 {
                debug!("hot_reload throttled: <1s since last");
                return Ok(());
            }
            *last_reload = now;
        }

        // File mtime check: unchanged → skip
        let metadata = fs::metadata(&self.file_path)
            .map_err(|e| format!("Cannot stat rules file: {}", e))?;
        let mtime = metadata.modified().ok();

        {
            let mut last_mtime = self.last_file_mtime.lock().unwrap();
            if mtime == *last_mtime {
                debug!("hot_reload skipped: file unchanged");
                return Ok(());
            }
            *last_mtime = mtime;
        }

        // Read & parse
        let content = fs::read_to_string(&self.file_path)
            .map_err(|e| format!("Cannot read rules file: {}", e))?;

        // Try JSON first, then TOML
        let (new_rules, new_hysteresis) = match serde_json::from_str::<RuleFileJson>(&content) {
            Ok(rf) => (rf.rules, rf.hysteresis_ms),
            Err(_) => {
                let toml_data = toml::from_str::<RuleFileTOML>(&content)
                    .map_err(|e| format!("Cannot parse rules as JSON or TOML: {}", e))?;
                (toml_data.rules, toml_data.metadata.hysteresis_ms.unwrap_or(0))
            }
        };

        // Validation: check all rules have required fields
        for rule in &new_rules {
            if rule.id.is_empty() || rule.condition.is_empty() || rule.action.is_empty() {
                return Err("Rule missing required fields (id, condition, action)".into());
            }
        }

        // Rollback: keep old rules on error
        let mut rules = self.rules.lock().unwrap();
        *rules = new_rules;
        *self.hysteresis_ms.lock().unwrap() = new_hysteresis;

        info!(
            file = ?self.file_path,
            count = rules.len(),
            "rules reloaded successfully"
        );
        Ok(())
    }

    /// Metrikler + mode'a göre kuralları değerlendir, aksiyonlar oluştur.
    pub fn evaluate(
        &self,
        metrics: &RuleMetrics,
        mode: &str,
    ) -> Result<Vec<Action>, Box<dyn std::error::Error>> {
        let rules = self.rules.lock().unwrap();
        let hysteresis_ms = *self.hysteresis_ms.lock().unwrap();
        let mut last_trigger = self.last_trigger.lock().unwrap();
        let mut actions = Vec::new();
        let now = Instant::now();

        for rule in rules.iter() {
            // Mode check
            if !rule.modes.contains(&mode.to_string()) {
                continue;
            }

            // V8/I33b: reject cooldown — kullanıcı bu kuralın aksiyonunu CoPilot'ta
            // reddettiyse, süre dolana dek kuralı atla (hysteresis'ten bağımsız,
            // genelde daha uzun pencere).
            {
                let cooldown = self.cooldown_until.lock().unwrap();
                if let Some(&until) = cooldown.get(&rule.id) {
                    if now < until {
                        debug!(rule_id = %rule.id, "reject cooldown suppressed rule");
                        continue;
                    }
                }
            }

            // Hysteresis: aynı kural hysteresis_ms geçmeden tekrar tetiklenemez
            if hysteresis_ms > 0 {
                if let Some(&last) = last_trigger.get(&rule.id) {
                    if now.duration_since(last).as_millis() < hysteresis_ms as u128 {
                        debug!(rule_id = %rule.id, "hysteresis suppressed rule");
                        continue;
                    }
                }
            }

            // HP (healing plumbing): GPU termal metriği stub (query_gpu_thermal_*
            // hep 0 döndürür). gpu_temp_c==0 "0°C" değil "veri yok" demektir; bu
            // yüzden `gpu_temp_c`'ye dayanan kuralları atla. Aksi halde
            // `gpu_thermal_restore` koşulu (`gpu_temp_c < 70`) daima doğru olup
            // CoPilot'ta sürekli temelsiz "çözünürlüğü geri getir?" pending/overlay
            // üretir (alarm yorgunluğu). Gerçek termal okuma (WMI/ADL/NVAPI) geldiğinde
            // gpu_temp_c sıfır-dışı olur ve guard kendiliğinden kalkar.
            if metrics.gpu_temp_c == 0 && rule.condition.contains("gpu_temp_c") {
                debug!(rule_id = %rule.id, "gpu_temp_c stub (0) — termal kural atlandı");
                continue;
            }

            // Condition evaluation
            if eval_condition(&rule.condition, metrics) {
                let action = self.create_action(&rule, metrics)?;
                last_trigger.insert(rule.id.clone(), now);
                actions.push(action);
            }
        }

        let actions = resolve_conflicts(actions);
        Ok(actions)
    }

    /// V8/I33b: `rule_id`'li kurala `duration` boyunca cooldown uygular — bu süre
    /// dolana dek `evaluate()` o kuralı tetiklemez. CoPilot'ta reddedilen
    /// aksiyonun kuralı için `rj_action_reject`'ten çağrılır (aksi halde ~1s
    /// tick'te yeniden üretilip kullanıcıyı spamler).
    pub fn apply_cooldown(&self, rule_id: &str, duration: Duration) {
        let until = Instant::now() + duration;
        self.cooldown_until
            .lock()
            .unwrap()
            .insert(rule_id.to_string(), until);
    }

    fn create_action(
        &self,
        rule: &Rule,
        _metrics: &RuleMetrics,
    ) -> Result<Action, Box<dyn std::error::Error>> {
        let action_type = match rule.action.as_str() {
            "bitrate_reduce" => ActionType::BitrateReduce,
            "bitrate_recover" => ActionType::BitrateRecover,
            "scale_resolution" => ActionType::ScaleResolution,
            "restore_resolution" => ActionType::RestoreResolution,
            "cap_fps" => ActionType::CapFps,
            "restore_fps" => ActionType::RestoreFps,
            "log_only" => ActionType::LogOnly,
            _ => return Err(format!("Unknown action: {}", rule.action).into()),
        };

        // HP2: param1 taşıdığı büyüklük aksiyona göre değişir:
        //  - bitrate aksiyonları: `step_kbps` (kbps, doğrudan)
        //  - resolution aksiyonları: `scale_factor` × 1000 sabit-nokta (frame
        //    handler `param1 / 1000.0` yapar). `restore_resolution` params boş →
        //    varsayılan 1.0 (tam çözünürlük). Eskiden resolution kuralları da
        //    `step_kbps` okuduğundan param1=0 → set_resolution(0.0) no-op'tu.
        let param1 = match action_type {
            ActionType::ScaleResolution | ActionType::RestoreResolution => {
                let scale = rule
                    .params
                    .get("scale_factor")
                    .and_then(|v| v.as_f64())
                    .unwrap_or(1.0);
                (scale * 1000.0).round() as i32
            }
            _ => rule
                .params
                .get("step_kbps")
                .and_then(|v| v.as_i64())
                .unwrap_or(0) as i32,
        };

        let param2 = rule
            .params
            .get("fps_limit")
            .and_then(|v| v.as_i64())
            .unwrap_or(0) as i32;

        let is_critical = rule.action == "bitrate_reduce" || rule.action == "scale_resolution";
        let require_approval = !is_critical;
        let log_only = matches!(action_type, ActionType::LogOnly);

        Ok(Action {
            rule_id: rule.id.clone(),
            action_type,
            param1,
            param2,
            timestamp: Instant::now(),
            require_approval,
            log_only,
            is_critical,
        })
    }
}

/// Çakışan aksiyonları çözer ve öncelik sırasına göre filtreler.
///
/// Öncelik (yüksekten düşüğe):
/// 1. BitrateReduce   — en kritik, hemen uygula
/// 2. CapFps          — ikinci öncelik
/// 3. ScaleResolution — üçüncü
/// 4. BitrateRecover  — sadece sistem stabil ise
/// 5. LogOnly         — her zaman uygula, diğerlerini engellemez
///
/// Çakışma kuralları:
/// BitrateReduce + BitrateRecover     → sadece BitrateReduce
/// CapFps        + RestoreFps         → sadece CapFps
/// ScaleResolution + RestoreResolution → sadece ScaleResolution
pub fn resolve_conflicts(actions: Vec<Action>) -> Vec<Action> {
    let has_reduce  = actions.iter().any(|a| a.action_type == ActionType::BitrateReduce);
    let has_cap_fps = actions.iter().any(|a| a.action_type == ActionType::CapFps);
    let has_scale   = actions.iter().any(|a| a.action_type == ActionType::ScaleResolution);

    let mut result: Vec<Action> = actions.into_iter().filter(|a| {
        match a.action_type {
            ActionType::BitrateRecover    if has_reduce  => false,
            ActionType::RestoreFps        if has_cap_fps => false,
            ActionType::RestoreResolution if has_scale   => false,
            _ => true,
        }
    }).collect();

    result.sort_by_key(|a| match a.action_type {
        ActionType::BitrateReduce   => 0,
        ActionType::CapFps          => 1,
        ActionType::ScaleResolution => 2,
        ActionType::BitrateRecover  => 3,
        ActionType::RestoreFps      => 4,
        ActionType::RestoreResolution => 5,
        ActionType::LogOnly         => 6,
    });

    result
}

// JSON Format
#[derive(Debug, Deserialize)]
struct RuleFileJson {
    rules: Vec<Rule>,
    #[serde(default)]
    hysteresis_ms: u64,
    #[serde(default)]
    default_mode: String,
}

// TOML Format
#[derive(Debug, Deserialize)]
struct RuleFileTOML {
    rules: Vec<Rule>,
    #[serde(default)]
    metadata: TomlMetadata,
}

#[derive(Debug, Deserialize, Default)]
struct TomlMetadata {
    hysteresis_ms: Option<u64>,
    default_mode: Option<String>,
}

impl RuleEngine {
    /// Dosyasız test kurucusu — integration testlerinde kullanılır.
    #[doc(hidden)]
    pub fn new_test(rules: Vec<Rule>, hysteresis_ms: u64) -> Self {
        Self {
            rules: Arc::new(Mutex::new(rules)),
            file_path: PathBuf::from("/nonexistent"),
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
            hysteresis_ms: Arc::new(Mutex::new(hysteresis_ms)),
            last_trigger: Arc::new(Mutex::new(HashMap::new())),
            cooldown_until: Arc::new(Mutex::new(HashMap::new())),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_load_rules_from_home() {
        let _ = tracing_subscriber::fmt()
            .with_max_level(tracing::Level::INFO)
            .with_target(false)
            .try_init();

        let path = std::env::var("USERPROFILE")
            .map(|h| PathBuf::from(h).join(".reji").join("rules.json"))
            .unwrap_or_else(|_| PathBuf::from("rules.json"));

        match RuleEngine::new(&path) {
            Ok(engine) => {
                let rules = engine.rules.lock().unwrap();
                eprintln!("[Rules] Loaded {} rules from {:?}", rules.len(), path);
                for r in rules.iter() {
                    eprintln!("[Rules]   id={:?}  action={:?}  modes={:?}", r.id, r.action, r.modes);
                }
            }
            Err(e) => {
                eprintln!("[Rules] FAILED to load {:?}: {}", path, e);
                panic!("rules.json parse error: {}", e);
            }
        }
    }

    #[test]
    fn test_condition_parse_simple() {
        let metrics = RuleMetrics { frame_drop_pct: 12, ..Default::default() };
        assert!(eval_condition("frame_drop_pct > 10", &metrics));
        assert!(!eval_condition("frame_drop_pct > 15", &metrics));
    }

    #[test]
    fn test_condition_thermal() {
        let metrics = RuleMetrics { gpu_temp_c: 87, ..Default::default() };
        assert!(eval_condition("gpu_temp_c > 85", &metrics));
        assert!(!eval_condition("gpu_temp_c < 85", &metrics));
    }

    #[test]
    fn test_action_creation() {
        let engine = RuleEngine {
            rules: Arc::new(Mutex::new(vec![])),
            file_path: PathBuf::from("/tmp/rules.json"),
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
            hysteresis_ms: Arc::new(Mutex::new(0)),
            last_trigger: Arc::new(Mutex::new(HashMap::new())),
            cooldown_until: Arc::new(Mutex::new(HashMap::new())),
        };

        let mut params = HashMap::new();
        params.insert("step_kbps".to_string(), serde_json::json!(500));

        let rule = Rule {
            id: "test_reduce".to_string(),
            description: "Test bitrate reduce".to_string(),
            condition: "frame_drop_pct > 10".to_string(),
            action: "bitrate_reduce".to_string(),
            params,
            modes: vec!["auto-pilot".to_string()],
        };

        let action = engine
            .create_action(&rule, &RuleMetrics::default())
            .unwrap();

        assert_eq!(action.rule_id, "test_reduce");
        assert_eq!(action.action_type, ActionType::BitrateReduce);
        assert_eq!(action.param1, 500);
        assert!(action.is_critical);
    }

    #[test]
    fn test_reject_cooldown_suppresses_rule() {
        // V8/I33b: apply_cooldown sonrası kural, cooldown süresince evaluate'te atlanır.
        let mut params = HashMap::new();
        params.insert("step_kbps".to_string(), serde_json::json!(500));
        let rule = Rule {
            id: "cd_rule".to_string(),
            description: String::new(),
            condition: "frame_drop_pct > 10".to_string(),
            action: "bitrate_reduce".to_string(),
            params,
            modes: vec!["auto-pilot".to_string()],
        };
        // hysteresis 0 → tek bastırıcı cooldown olsun.
        let engine = RuleEngine::new_test(vec![rule], 0);
        let mut m = RuleMetrics::default();
        m.frame_drop_pct = 12;

        assert_eq!(
            engine.evaluate(&m, "auto-pilot").unwrap().len(),
            1,
            "ilk değerlendirme aksiyon üretmeli"
        );
        engine.apply_cooldown("cd_rule", Duration::from_secs(60));
        assert_eq!(
            engine.evaluate(&m, "auto-pilot").unwrap().len(),
            0,
            "cooldown süresince kural bastırılmalı"
        );
    }

    #[test]
    fn test_scale_factor_read_for_resolution() {
        // HP2: resolution kuralları scale_factor'ı param1 = scale×1000 olarak
        // okumalı (eskiden step_kbps okunuyordu → param1=0 → set_resolution no-op).
        let engine = RuleEngine::new_test(vec![], 0);

        let mut scale_params = HashMap::new();
        scale_params.insert("scale_factor".to_string(), serde_json::json!(0.25));
        let scale_rule = Rule {
            id: "mem_pressure".to_string(),
            description: String::new(),
            condition: "memory_usage_pct > 85".to_string(),
            action: "scale_resolution".to_string(),
            params: scale_params,
            modes: vec!["auto-pilot".to_string()],
        };
        let a = engine.create_action(&scale_rule, &RuleMetrics::default()).unwrap();
        assert_eq!(a.action_type, ActionType::ScaleResolution);
        assert_eq!(a.param1, 250, "scale_factor 0.25 → param1 250 (×1000)");

        // restore_resolution: boş params → varsayılan 1.0 → param1 1000 (tam çözünürlük).
        let restore_rule = Rule {
            id: "gpu_thermal_restore".to_string(),
            description: String::new(),
            condition: "gpu_temp_c < 70".to_string(),
            action: "restore_resolution".to_string(),
            params: HashMap::new(),
            modes: vec!["auto-pilot".to_string()],
        };
        let r = engine.create_action(&restore_rule, &RuleMetrics::default()).unwrap();
        assert_eq!(r.action_type, ActionType::RestoreResolution);
        assert_eq!(r.param1, 1000, "boş params → scale 1.0 → param1 1000");
    }

    #[test]
    fn test_gpu_thermal_rule_skipped_when_metric_stub() {
        // HP: gpu_temp_c metriği stub (0) iken gpu_temp_c'ye dayanan kurallar
        // atlanmalı — aksi halde `gpu_temp_c < 70` hep-true olup sahte restore üretir.
        let restore_rule = Rule {
            id: "gpu_thermal_restore".to_string(),
            description: String::new(),
            condition: "gpu_temp_c < 70".to_string(),
            action: "restore_resolution".to_string(),
            params: HashMap::new(),
            modes: vec!["auto-pilot".to_string()],
        };
        let engine = RuleEngine::new_test(vec![restore_rule], 0);

        // gpu_temp_c == 0 (stub) → kural atlanır, hiç aksiyon üretilmez.
        let stub = RuleMetrics { gpu_temp_c: 0, ..Default::default() };
        assert_eq!(
            engine.evaluate(&stub, "auto-pilot").unwrap().len(),
            0,
            "stub metrikte (0) termal kural atlanmalı"
        );

        // gpu_temp_c gerçek (0-dışı) ve < 70 → guard kalkar, kural tetiklenir.
        let real = RuleMetrics { gpu_temp_c: 60, ..Default::default() };
        assert_eq!(
            engine.evaluate(&real, "auto-pilot").unwrap().len(),
            1,
            "gerçek metrikte (60<70) termal kural tetiklenmeli"
        );
    }
}
