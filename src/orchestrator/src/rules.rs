//! Kural motoru — JSON/TOML config dosyaları, hot-reload, condition evaluation.
//!
//! Adaptive bitrate, resolution, FPS controls kuralları burada tanımlanır.
//! Hot-reload: Windows NamedEvent veya file watching ile trigger edilir.

use std::collections::HashMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};
use std::time::Instant;

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
    pub id: u32,
    pub action_type: ActionType,
    pub param1: i32,
    pub param2: i32,
    pub timestamp: Instant,
    pub require_approval: bool,
    pub log_only: bool,
    pub is_critical: bool,
}

/// Kural — condition + action + modes.
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Rule {
    pub id: String,
    pub description: String,
    pub condition: String,  // e.g., "frame_drop_pct > 10"
    pub action: String,
    pub params: HashMap<String, serde_json::Value>,
    pub modes: Vec<String>,
}

/// Kural motoru — JSON/TOML kuralları yükler, hot-reload desteği.
pub struct RuleEngine {
    rules: Arc<Mutex<Vec<Rule>>>,
    file_path: PathBuf,
    last_reload: Arc<Mutex<Instant>>,
    last_file_mtime: Arc<Mutex<Option<std::time::SystemTime>>>,
}

impl RuleEngine {
    /// Yeni RuleEngine'ı kurur, dosyadan kuralları yükler.
    pub fn new(file_path: impl AsRef<Path>) -> Result<Self, Box<dyn std::error::Error>> {
        let path = file_path.as_ref().to_path_buf();
        let engine = Self {
            rules: Arc::new(Mutex::new(Vec::new())),
            file_path: path,
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
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
        let new_rules = match serde_json::from_str::<RuleFileJson>(&content) {
            Ok(rf) => rf.rules,
            Err(_) => {
                let toml_data = toml::from_str::<RuleFileTOML>(&content)
                    .map_err(|e| format!("Cannot parse rules as JSON or TOML: {}", e))?;
                toml_data.rules
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
        let mut actions = Vec::new();
        let mut action_id = 1u32;

        for rule in rules.iter() {
            // Mode check
            if !rule.modes.contains(&mode.to_string()) {
                continue;
            }

            // Condition evaluation
            if self.eval_condition(&rule.condition, metrics)? {
                let action = self.create_action(&rule, action_id, metrics)?;
                actions.push(action);
                action_id = action_id.wrapping_add(1);
            }
        }

        Ok(actions)
    }

    /// Condition string'i basit kural motoru ile değerlendir.
    /// Örn: "frame_drop_pct > 10", "gpu_temp_c > 85"
    fn eval_condition(
        &self,
        condition: &str,
        metrics: &RuleMetrics,
    ) -> Result<bool, Box<dyn std::error::Error>> {
        // Simple condition parser — limited, secure
        // Supports: metric_name > value, metric_name < value, metric_name >= value
        //
        // Examples:
        //   "frame_drop_pct > 10"
        //   "gpu_temp_c > 85"
        //   "memory_usage_pct > 85 && cpu_load_pct > 90"

        let condition = condition.trim();

        // Split by && (AND)
        let and_parts: Vec<&str> = condition.split("&&").map(|s| s.trim()).collect();

        for part in and_parts {
            if !self.eval_simple_condition(part, metrics)? {
                return Ok(false);
            }
        }

        Ok(true)
    }

    fn eval_simple_condition(
        &self,
        cond: &str,
        metrics: &RuleMetrics,
    ) -> Result<bool, Box<dyn std::error::Error>> {
        // Parse: metric_name operator value
        // e.g., "frame_drop_pct > 10"

        for op in &[">=", "<=", ">", "<", "=="] {
            if let Some(idx) = cond.find(op) {
                let metric_name = cond[..idx].trim();
                let threshold_str = cond[idx + op.len()..].trim();
                let threshold = threshold_str.parse::<i32>()
                    .map_err(|_| format!("Invalid threshold: {}", threshold_str))?;

                let metric_value = match metric_name {
                    "frame_drop_pct" => metrics.frame_drop_pct as i32,
                    "gpu_temp_c" => metrics.gpu_temp_c as i32,
                    "cpu_temp_c" => metrics.cpu_temp_c as i32,
                    "memory_usage_pct" => metrics.memory_usage_pct as i32,
                    "cpu_load_pct" => metrics.cpu_load_pct as i32,
                    "network_rtt_ms" => metrics.network_rtt_ms as i32,
                    "network_loss_pct" => metrics.network_loss_pct as i32,
                    _ => return Err(format!("Unknown metric: {}", metric_name).into()),
                };

                let result = match *op {
                    ">" => metric_value > threshold,
                    "<" => metric_value < threshold,
                    ">=" => metric_value >= threshold,
                    "<=" => metric_value <= threshold,
                    "==" => metric_value == threshold,
                    _ => false,
                };

                return Ok(result);
            }
        }

        Err(format!("Cannot parse condition: {}", cond).into())
    }

    fn create_action(
        &self,
        rule: &Rule,
        action_id: u32,
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

        let param1 = rule
            .params
            .get("step_kbps")
            .and_then(|v| v.as_i64())
            .unwrap_or(0) as i32;

        let param2 = rule
            .params
            .get("fps_limit")
            .and_then(|v| v.as_i64())
            .unwrap_or(0) as i32;

        let is_critical = rule.action == "bitrate_reduce" || rule.action == "scale_resolution";
        let require_approval = !is_critical;

        Ok(Action {
            id: action_id,
            action_type,
            param1,
            param2,
            timestamp: Instant::now(),
            require_approval,
            log_only: false,
            is_critical,
        })
    }
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_condition_parse_simple() {
        let engine = RuleEngine {
            rules: Arc::new(Mutex::new(vec![])),
            file_path: PathBuf::from("/tmp/rules.json"),
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
        };

        let metrics = RuleMetrics {
            frame_drop_pct: 12,
            ..Default::default()
        };

        assert!(engine.eval_simple_condition("frame_drop_pct > 10", &metrics).unwrap());
        assert!(!engine.eval_simple_condition("frame_drop_pct > 15", &metrics).unwrap());
    }

    #[test]
    fn test_condition_thermal() {
        let engine = RuleEngine {
            rules: Arc::new(Mutex::new(vec![])),
            file_path: PathBuf::from("/tmp/rules.json"),
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
        };

        let metrics = RuleMetrics {
            gpu_temp_c: 87,
            ..Default::default()
        };

        assert!(engine.eval_simple_condition("gpu_temp_c > 85", &metrics).unwrap());
        assert!(!engine.eval_simple_condition("gpu_temp_c < 85", &metrics).unwrap());
    }

    #[test]
    fn test_action_creation() {
        let engine = RuleEngine {
            rules: Arc::new(Mutex::new(vec![])),
            file_path: PathBuf::from("/tmp/rules.json"),
            last_reload: Arc::new(Mutex::new(Instant::now())),
            last_file_mtime: Arc::new(Mutex::new(None)),
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
            .create_action(&rule, 1, &RuleMetrics::default())
            .unwrap();

        assert_eq!(action.id, 1);
        assert_eq!(action.action_type, ActionType::BitrateReduce);
        assert_eq!(action.param1, 500);
        assert!(action.is_critical);
    }
}
