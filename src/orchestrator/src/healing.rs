//! Self-healing monitör bileşenleri.
//!
//! 1. Katman: Reaktif olaylara hızlı yanıt.
//! 2. Katman: Prediktif trend algılama.
//! 3. Katman: Adaptif eşik kalibrasyonu.

use std::sync::atomic::Ordering;
use std::time::{Duration, Instant};

use crossbeam::atomic::AtomicCell;

use tokio::sync::broadcast::{error::RecvError, Receiver, Sender};
use tokio::time::{interval, MissedTickBehavior};
use tracing::{debug, error, info, warn};

use std::sync::{Arc, Mutex};
use crate::constants;

use crate::event_bus::{HealingEvent, MediaEvent, SystemEvent};
use crate::ffi::{enqueue_action, next_action_id, RjAction, RjActionType};
use crate::metrics::MetricState;
use crate::rules::{ActionType, Explanation, RuleEngine, RuleMetrics};

/// Self-healing katmanları.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealingLayer {
    Reactive,
    Predictive,
    Adaptive,
}

/// Kullanıcı seviyesine göre self-healing modu.
///
/// V8/I19: UI 4 anlamlı seçenek sunuyor (settings_dialog.cpp / healing_overlay.h),
/// eskiden Rust yalnız 3 varyanta sahipti ve `Assist` + `Manual`'ı tek varyanta
/// (`ManualAssist`) çöküyordu — `Manual` seçen kullanıcı sessizce `Assist`
/// davranışı (kritik aksiyonlar hâlâ otomatik) alıyordu. Artık dört varyant da
/// ayrı; sıralama FFI `HEALING_MODE` u32 kodlamasıyla (0..=3) birebir eşleşir.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealingMode {
    AutoPilot,
    CoPilot,
    /// Kritik aksiyonlar otomatik uygulanır, diğerleri yalnız loglanır.
    Assist,
    /// Hiçbir otomatik aksiyon yok — tüm adaptasyon kapalı.
    Manual,
}

impl HealingMode {
    /// V8/I20: FFI global `HEALING_MODE` (AtomicU32, 0..=3) → enum tek dönüşüm
    /// noktası. `rj_set_healing_mode` zaten >3'ü reddediyor; `_` savunmacı
    /// güvenli varsayılan (onay gerektiren CoPilot).
    fn from_raw(v: u32) -> Self {
        match v {
            0 => HealingMode::AutoPilot,
            1 => HealingMode::CoPilot,
            2 => HealingMode::Assist,
            3 => HealingMode::Manual,
            _ => HealingMode::CoPilot,
        }
    }

    /// FFI global'i tek satırda enum'a çevirir (canlı, `self.mode` değil).
    fn current() -> Self {
        Self::from_raw(crate::ffi::HEALING_MODE.load(Ordering::Relaxed))
    }
}

/// V8/I33c: kuraldan üretilmiş, FFI'ya hazır aksiyon + onu üreten kuralın ID'si.
/// `rule_id`, CoPilot pending yolunda reject → RuleEngine cooldown (commit 5)
/// eşlemesi için taşınır.
struct RoutedAction {
    rj: RjAction,
    rule_id: String,
    /// Özellik#1: aksiyonu tetikleyen metrik/değer/eşik — UI event'ine iletilir
    /// (bugün `rule_id`'nin reject-cooldown için taşındığı gibi taşınır).
    explanation: Explanation,
}

/// V8/I33c: CoPilot'ta bir aksiyonun kullanıcı onayı gerektirip gerektirmediği
/// (saf karar — test edilebilir). Yalnız `CoPilot`'ta VE aksiyonun kategorisi
/// otomatik-onaylı DEĞİLSE onay gerekir. `AutoPilot`/`Assist`: onay yok
/// (otomatik). `Manual`: zaten aksiyon üretilmez.
fn requires_approval(mode: HealingMode, category_auto: bool) -> bool {
    mode == HealingMode::CoPilot && !category_auto
}

/// Kalibrasyon eşikleri ve eylem sınırları.
#[derive(Debug, Clone, Copy)]
pub struct HealingThresholds {
    pub cpu_high: f32,
    pub cpu_critical: f32,
    pub bitrate_drop_pct: f32,
    pub frame_drop_rate: u32,
    pub event_count_threshold: u32,
}

impl HealingThresholds {
    pub fn new() -> Self {
        Self {
            cpu_high: 0.75,
            cpu_critical: 0.92,
            bitrate_drop_pct: 0.18,
            frame_drop_rate: 8,
            event_count_threshold: 3,
        }
    }

    pub fn reactive() -> Self {
        Self {
            cpu_high: 0.95,
            cpu_critical: 0.99,
            bitrate_drop_pct: 0.30,
            frame_drop_rate: 12,
            event_count_threshold: 1,
        }
    }

    pub fn predictive() -> Self {
        Self {
            cpu_high: 0.70,
            cpu_critical: 0.92,
            bitrate_drop_pct: 0.14,
            frame_drop_rate: 6,
            event_count_threshold: 2,
        }
    }

    pub fn adaptive() -> Self {
        Self {
            cpu_high: 0.65,
            cpu_critical: 0.88,
            bitrate_drop_pct: 0.12,
            frame_drop_rate: 4,
            event_count_threshold: 1,
        }
    }

    pub fn adjust_for_session(&self, cpu: f32, _bitrate_kbps: u32) -> Self {
        let cpu_high = self.cpu_high.min(cpu * 0.97).max(0.55);
        let bitrate_drop_pct = (self.bitrate_drop_pct + 0.01).min(0.25);
        Self {
            cpu_high,
            bitrate_drop_pct,
            ..*self
        }
    }
}

impl Default for HealingThresholds {
    fn default() -> Self {
        Self::new()
    }
}

/// Histeresis cooldown tracker.
#[derive(Debug)]
pub struct CooldownTracker {
    last_reactive: AtomicCell<Option<Instant>>,
    last_predictive: AtomicCell<Option<Instant>>,
    last_adaptive: AtomicCell<Option<Instant>>,
    reactive_cooldown: Duration,
    predictive_cooldown: Duration,
    adaptive_cooldown: Duration,
}

impl CooldownTracker {
    pub fn new() -> Self {
        Self {
            last_reactive: AtomicCell::new(None),
            last_predictive: AtomicCell::new(None),
            last_adaptive: AtomicCell::new(None),
            reactive_cooldown: Duration::from_secs(20),
            predictive_cooldown: Duration::from_secs(60),
            adaptive_cooldown: Duration::from_secs(120),
        }
    }

    fn now() -> Instant {
        Instant::now()
    }

    pub fn can_fire(&self, layer: HealingLayer) -> bool {
        let now = Self::now();
        let deadline = match layer {
            HealingLayer::Reactive => self.last_reactive.load().map(|t| t + self.reactive_cooldown),
            HealingLayer::Predictive => self.last_predictive.load().map(|t| t + self.predictive_cooldown),
            HealingLayer::Adaptive => self.last_adaptive.load().map(|t| t + self.adaptive_cooldown),
        };

        match deadline {
            Some(deadline) if now < deadline => false,
            _ => true,
        }
    }

    pub fn record(&self, layer: HealingLayer) {
        let when = Some(Self::now());
        match layer {
            HealingLayer::Reactive => self.last_reactive.store(when),
            HealingLayer::Predictive => self.last_predictive.store(when),
            HealingLayer::Adaptive => self.last_adaptive.store(when),
        }
    }
}

#[derive(Debug)]
struct TrendState {
    cpu_high_since: Option<Instant>,
    recent_frame_drops: u32,
    last_cpu: f32,
}

impl TrendState {
    fn new() -> Self {
        Self {
            cpu_high_since: None,
            recent_frame_drops: 0,
            last_cpu: 0.0,
        }
    }
}

/// Self-healing monitör.
#[derive(Debug)]
pub struct HealingMonitor {
    system_rx: Receiver<SystemEvent>,
    media_rx: Receiver<MediaEvent>,
    healing_tx: Sender<HealingEvent>,
    thresholds: AtomicCell<HealingThresholds>,
    cooldown: CooldownTracker,
    trend: TrendState,
    metric_state: Arc<MetricState>,
    current_metrics: RuleMetrics,
    /// V8/I1: kullanıcı-yapılandırılabilir kural motoru. FfiState ile AYNI Arc
    /// (hot-reload `rj_reload_rules` monitörün gördüğü motoru da günceller).
    rule_engine: Arc<Mutex<Option<RuleEngine>>>,
}

impl HealingMonitor {
    /// V8/I20: `mode` parametresi kaldırıldı — mod artık yalnız canlı FFI global
    /// `HEALING_MODE`'dan okunur (`HealingMode::current()`). Eski donmuş `self.mode`
    /// alanı constructor'da bir kez set edilip UI değişikliklerini asla görmüyordu.
    pub fn subscribe(
        system_rx: Receiver<SystemEvent>,
        media_rx: Receiver<MediaEvent>,
        healing_tx: Sender<HealingEvent>,
        thresholds: HealingThresholds,
        metric_state: Arc<MetricState>,
        rule_engine: Arc<Mutex<Option<RuleEngine>>>,
    ) -> Self {
        Self {
            system_rx,
            media_rx,
            healing_tx,
            thresholds: AtomicCell::new(thresholds),
            cooldown: CooldownTracker::new(),
            trend: TrendState::new(),
            metric_state,
            current_metrics: RuleMetrics::default(),
            rule_engine,
        }
    }

    pub async fn run(mut self) {
        let mut ticker = interval(Duration::from_secs(1));
        ticker.set_missed_tick_behavior(MissedTickBehavior::Delay);

        info!("HealingMonitor started");

        loop {
            tokio::select! {
                result = self.system_rx.recv() => {
                    match result {
                        Ok(system) => { self.handle_system(system); }
                        Err(RecvError::Lagged(n)) => {
                            eprintln!("[EventBus] {} mesaj atlandı, devam ediliyor", n);
                            // devam et — canlı yayında eski metrik kaybı kabul edilebilir
                        }
                        Err(RecvError::Closed) => break,
                    }
                }
                result = self.media_rx.recv() => {
                    match result {
                        Ok(media) => { self.handle_media(media); }
                        Err(RecvError::Lagged(n)) => {
                            eprintln!("[EventBus] {} mesaj atlandı, devam ediliyor", n);
                            // devam et — canlı yayında eski metrik kaybı kabul edilebilir
                        }
                        Err(RecvError::Closed) => break,
                    }
                }
                _ = ticker.tick() => {
                    // V8/I33a: pending TTL süpürme moddan bağımsız (Manual'da bile
                    // önceden birikmiş pending'ler süresi dolunca UI'dan düşmeli).
                    crate::ffi::sweep_expired_pending();
                    let mode = HealingMode::current();
                    if mode == HealingMode::Manual { continue; } // Manual — komut üretme
                    self.on_periodic(mode);
                }
            }
        }
    }

    fn handle_system(&mut self, event: SystemEvent) {
        if HealingMode::current() == HealingMode::Manual { return; } // Manual — reactive kanal bastırılıyor
        let thresholds = self.thresholds.load();

        match event {
            SystemEvent::CpuUsage { ratio } => {
                debug!(cpu = ratio, "SystemEvent::CpuUsage");
                self.trend.last_cpu = ratio;
                self.current_metrics.cpu_load_pct = (ratio * 100.0) as u32;
                if ratio >= thresholds.cpu_critical {
                    if self.cooldown.can_fire(HealingLayer::Reactive) {
                        self.emit(HealingEvent::LightenCodec, HealingLayer::Reactive);
                        self.cooldown.record(HealingLayer::Reactive);
                    } else {
                        debug!("CPU critical action suppressed by cooldown");
                    }
                } else if ratio >= thresholds.cpu_high {
                    self.track_cpu_trend(ratio);
                } else {
                    self.trend.cpu_high_since = None;
                }
            }
            SystemEvent::GpuUsage { ratio } => {
                debug!(gpu = ratio, "SystemEvent::GpuUsage");
                self.current_metrics.gpu_load_pct = (ratio * 100.0) as u32;
                if ratio >= 0.98 && self.cooldown.can_fire(HealingLayer::Predictive) {
                    self.emit(HealingEvent::ReduceBitrate { target_kbps: 4000 }, HealingLayer::Predictive);
                    self.cooldown.record(HealingLayer::Predictive);
                }
            }
            SystemEvent::MemUsage { ratio } => {
                debug!(mem = ratio, "SystemEvent::MemUsage");
                self.current_metrics.memory_usage_pct = (ratio * 100.0) as u32;
                if ratio >= 0.96 && self.cooldown.can_fire(HealingLayer::Predictive) {
                    self.emit(HealingEvent::ReducePreviewFps { target_fps: 24 }, HealingLayer::Predictive);
                    self.cooldown.record(HealingLayer::Predictive);
                }
            }
            SystemEvent::DiskWarning { free_mb } => {
                warn!(free_mb, "Disk warning received");
                if self.cooldown.can_fire(HealingLayer::Adaptive) {
                    self.emit(HealingEvent::ActivateFallback { reason: "Disk pressure".into() }, HealingLayer::Adaptive);
                    self.cooldown.record(HealingLayer::Adaptive);
                }
            }
            SystemEvent::NetworkStats { rtt_ms, loss_pct } => {
                debug!(rtt_ms, loss_pct, "SystemEvent::NetworkStats");
                self.current_metrics.network_rtt_ms = rtt_ms.min(u16::MAX as u32) as u16;
                self.current_metrics.network_loss_pct = loss_pct.clamp(0.0, 100.0) as u8;
            }
        }
    }

    fn handle_media(&mut self, event: MediaEvent) {
        if HealingMode::current() == HealingMode::Manual { return; } // Manual — reactive kanal bastırılıyor
        let thresholds = self.thresholds.load();

        match event {
            MediaEvent::SourceDisconnected { source_id } => {
                warn!(source_id, "source disconnected");
                if self.cooldown.can_fire(HealingLayer::Reactive) {
                    self.emit(HealingEvent::ActivateFallback { reason: format!("Source {} disconnected", source_id) }, HealingLayer::Reactive);
                    self.cooldown.record(HealingLayer::Reactive);
                }
            }
            MediaEvent::SourceReconnected { source_id } => {
                info!(source_id, "source reconnected");
                if self.cooldown.can_fire(HealingLayer::Adaptive) {
                    self.emit(HealingEvent::RestoreNormal, HealingLayer::Adaptive);
                    self.cooldown.record(HealingLayer::Adaptive);
                }
            }
            MediaEvent::FrameDropped { count } => {
                debug!(count, "frame drop event");
                self.trend.recent_frame_drops = self.trend.recent_frame_drops.saturating_add(count);
                if count >= thresholds.frame_drop_rate && self.cooldown.can_fire(HealingLayer::Reactive) {
                    self.emit(HealingEvent::ReducePreviewFps { target_fps: 20 }, HealingLayer::Reactive);
                    self.cooldown.record(HealingLayer::Reactive);
                }
            }
            MediaEvent::EncodeError { code } => {
                error!(code, "encode error reported");
                if self.cooldown.can_fire(HealingLayer::Reactive) {
                    self.emit(HealingEvent::BypassPlugin { plugin_id: 0 }, HealingLayer::Reactive);
                    self.cooldown.record(HealingLayer::Reactive);
                }
            }
        }
    }

    /// V8/I20: mod bir kez `run()` ticker'ında canlı okunup buraya geçilir;
    /// tek periyot içinde tutarlı bir görünüm sağlar ve alt fonksiyonları
    /// (evaluate_*) global'e bağımlı olmadan test edilebilir kılar.
    fn on_periodic(&mut self, mode: HealingMode) {
        self.evaluate_predictive();
        self.evaluate_adaptive(mode);
        self.evaluate_rule_engine(mode);
    }

    /// V8/I1: kural motorunu değerlendirip üretilen aksiyonları RjAction'a
    /// çevirir — kuyruğa YAZMAZ (saf, test edilebilir). `evaluate_rule_engine`
    /// bunu çağırıp sonuçları `enqueue_action` ile kuyruğa iter.
    ///
    /// V8/I19: mod artık parametre (donmuş `self.mode` değil). `Manual` modda
    /// RuleEngine HİÇ değerlendirilmez ("tüm adaptasyon kapalı" sözü — kuralları
    /// değerlendirip sonra atmak yerine hiç çağırmamak doğrusu; şablonda zaten
    /// "manual" mode'lu kural yok). `Assist` modda yalnız `is_critical` aksiyonlar
    /// otomatik uygulanır; kritik olmayanlar loglanır ("kritik otomatik,
    /// diğerleri log" — UI sözü).
    fn collect_rule_actions(&self, mode: HealingMode) -> Vec<RoutedAction> {
        // Manual: adaptasyon tamamen kapalı — motoru hiç çağırma.
        if mode == HealingMode::Manual {
            return Vec::new();
        }

        let guard = match self.rule_engine.lock() {
            Ok(g) => g,
            Err(_) => {
                warn!("rule_engine mutex poisoned, skipping evaluation");
                return Vec::new();
            }
        };
        let Some(engine) = guard.as_ref() else { return Vec::new(); };

        // mode_str: gerçek rules.json şablonu tireli değerler kullanıyor
        // ("auto-pilot"/"co-pilot"/"assist" — bkz. docs/config/rules.json.template),
        // enum'un CamelCase adları DEĞİL.
        let mode_str = match mode {
            HealingMode::AutoPilot => "auto-pilot",
            HealingMode::CoPilot   => "co-pilot",
            HealingMode::Assist    => "assist",
            HealingMode::Manual    => unreachable!("Manual yukarıda erken dönüyor"),
        };

        let actions = match engine.evaluate(&self.current_metrics, mode_str) {
            Ok(actions) => actions,
            Err(e) => {
                warn!(error = %e, "rule engine evaluate() failed");
                return Vec::new();
            }
        };

        actions
            .into_iter()
            .filter_map(|a| {
                // Assist: yalnız kritik aksiyonlar otomatik; gerisi loglanıp bırakılır.
                if mode == HealingMode::Assist && !a.is_critical {
                    info!(
                        action = ?a.action_type,
                        rule_id = %a.rule_id,
                        "Assist modu: kritik olmayan aksiyon loglandı, uygulanmadı"
                    );
                    return None;
                }
                Some(RoutedAction {
                    rj: RjAction {
                        // V8/I33: FFI-facing benzersiz ID global sayaçtan (tick-yerel
                        // `a.id` kaldırıldı — pending deposu ID çakışması olmasın diye).
                        id: next_action_id(),
                        action_type: convert_action_type(a.action_type),
                        param1: a.param1,
                        param2: a.param2,
                        // RjAction'ın canary'si doğrulanmıyor (MetricSample'ın aksine —
                        // apply_action yalnız action_type'a bakar); mevcut kalıp 0.
                        canary: 0,
                    },
                    rule_id: a.rule_id,
                    explanation: a.explanation, // Özellik#1: üçlüyü UI event'ine taşı
                })
            })
            .collect()
    }

    /// V8/I1: kural motoru aksiyonlarını action_queue'ya (FFI) iter.
    ///
    /// V8/I33 (I11): İki-kuyruk — aksiyonlar aktüatör kuyruğuna (uygula) veya
    /// pending deposuna (CoPilot onayı) gider; her ikisinde de UI event kuyruğuna
    /// (bildir) bir event düşer. Aynı kuyruğu paylaşmadıkları için "aksiyon
    /// rastgele tek tüketiciye gider" yarışı yok.
    ///
    /// V8/I33c: Routing kararı ENQUEUE anında verilir (`requires_approval`):
    /// - Otomatik (AutoPilot / Assist / CoPilot-auto-kategori / LogOnly)
    ///   → aktüatör kuyruğu + info-event.
    /// - CoPilot-manuel-kategori → pending deposu + approval-event (aktüatöre
    ///   GİTMEZ; yalnız `rj_action_approve` sonrası taşınır).
    fn evaluate_rule_engine(&self, mode: HealingMode) {
        let mode_code = mode as u32; // Özellik#3: healing-log `mode` sütunu (0..3)
        for routed in self.collect_rule_actions(mode) {
            let auto = crate::ffi::category_auto_approve(routed.rj.action_type);
            if requires_approval(mode, auto) {
                // CoPilot manuel-kategori: pending + approval event.
                // Özellik#3: pending outcome enqueue_pending içinde loglanır.
                crate::ffi::enqueue_pending(routed.rj, routed.rule_id, routed.explanation);
            } else {
                // Otomatik: aktüatör + info event.
                let id = routed.rj.id;
                crate::ffi::enqueue_ui_event(crate::ffi::RjActionEvent::info_event(&routed.rj, &routed.explanation));
                if !enqueue_action(routed.rj) {
                    warn!(action_id = id, "rule action enqueue failed, kuyruk dolu");
                }
                // Özellik#3: otomatik uygulanan aksiyonu healing-log'a yaz (üçüncü
                // fan-out). Tam üçlü (metric/value/threshold) üretim anında mevcut.
                crate::healing_log::log_healing(crate::healing_log::HealingLogRecord::now(
                    id,
                    routed.rule_id,
                    routed.explanation.metric_id,
                    routed.explanation.current_value,
                    routed.explanation.threshold_value,
                    routed.rj.action_type as u32,
                    crate::healing_log::OUTCOME_APPLIED,
                    mode_code,
                ));
            }
        }
    }

    fn track_cpu_trend(&mut self, _cpu: f32) {
        let now = Instant::now();
        if self.trend.cpu_high_since.is_none() {
            self.trend.cpu_high_since = Some(now);
        }
    }

    fn evaluate_predictive(&mut self) {
        let thresholds = self.thresholds.load();
        if let Some(start) = self.trend.cpu_high_since {
            let elapsed = start.elapsed();
            if elapsed >= Duration::from_secs(4) && self.cooldown.can_fire(HealingLayer::Predictive) {
                info!(elapsed = ?elapsed, "predictive CPU trend exceeded threshold");
                self.emit(HealingEvent::LightenCodec, HealingLayer::Predictive);
                self.cooldown.record(HealingLayer::Predictive);
                self.trend.cpu_high_since = None;
            }
        }

        if self.trend.recent_frame_drops >= thresholds.event_count_threshold {
            if self.cooldown.can_fire(HealingLayer::Predictive) {
                info!(drops = self.trend.recent_frame_drops, "predictive frame drop trend detected");
                self.emit(HealingEvent::ReduceBitrate { target_kbps: constants::REDUCED_BITRATE_KBPS }, HealingLayer::Predictive);
                self.cooldown.record(HealingLayer::Predictive);
            }
            self.trend.recent_frame_drops = 0;
        }
    }

    /// V8/I19+I20: mod parametre (donmuş `self.mode` değil). Adaptif eşik
    /// kalibrasyonu `AutoPilot` ve `Assist`'te çalışır (Assist'te reaktif kanal
    /// zaten aktif olduğundan eşikleri sıkılaştırmak tutarlı); `CoPilot` ve
    /// `Manual`'da arka plan yeniden ayarı yapılmaz. Not: kalibrasyon tekil bir
    /// arka plan davranışıdır, RuleEngine aksiyonları gibi per-aksiyon
    /// `is_critical` ayrımı yoktur — o granüler filtre `collect_rule_actions`'ta.
    fn evaluate_adaptive(&self, mode: HealingMode) {
        match mode {
            HealingMode::AutoPilot | HealingMode::Assist => {}
            HealingMode::CoPilot | HealingMode::Manual => return,
        }

        let current_cpu = self.trend.last_cpu;
        let current_bitrate = self.metric_state.bitrate();
        let thresholds = self.thresholds.load();
        let adjusted = thresholds.adjust_for_session(current_cpu, current_bitrate);
        if adjusted.cpu_high < thresholds.cpu_high {
            let log_event = HealingEvent::RestoreNormal;
            if self.cooldown.can_fire(HealingLayer::Adaptive) {
                info!(old_cpu = thresholds.cpu_high, new_cpu = adjusted.cpu_high, "adaptive threshold calibrated");
                self.emit(log_event, HealingLayer::Adaptive);
                self.cooldown.record(HealingLayer::Adaptive);
            }
            self.thresholds.store(adjusted);
        }
    }

    fn emit(&self, event: HealingEvent, layer: HealingLayer) {
        debug!(?event, ?layer, "emitting healing event");
        let _ = self.healing_tx.send(event);
    }
}

/// V8/I1: rules::ActionType → ffi::RjActionType mekanik dönüşümü.
/// Varyantlar birebir eşleşir (ikisi de aynı 7 adaptasyon aksiyonu); mantık yok.
fn convert_action_type(a: ActionType) -> RjActionType {
    match a {
        ActionType::BitrateReduce     => RjActionType::BitrateReduce,
        ActionType::BitrateRecover    => RjActionType::BitrateRecover,
        ActionType::ScaleResolution   => RjActionType::ScaleResolution,
        ActionType::RestoreResolution => RjActionType::RestoreResolution,
        ActionType::CapFps            => RjActionType::CapFps,
        ActionType::RestoreFps        => RjActionType::RestoreFps,
        ActionType::LogOnly           => RjActionType::LogOnly,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::event_bus::EventBus;
    use crate::metrics::MetricState;

    #[test]
    fn test_healing_thresholds_default() {
        let thresholds = HealingThresholds::default();
        assert!(thresholds.cpu_high > 0.0);
        assert!(thresholds.cpu_critical > thresholds.cpu_high);
        assert!(thresholds.frame_drop_rate > 0);
    }

    #[test]
    fn test_cooldown_tracker() {
        let tracker = CooldownTracker::new();
        assert!(tracker.can_fire(HealingLayer::Reactive));
        tracker.record(HealingLayer::Reactive);
        assert!(!tracker.can_fire(HealingLayer::Reactive));
    }

    #[tokio::test]
    async fn test_healing_monitor_reactive_send() {
        let bus = EventBus::new();
        let system_rx = bus.system.subscribe();
        let media_rx = bus.media.subscribe();
        let mut healing_rx = bus.healing.subscribe();

        let monitor = HealingMonitor::subscribe(system_rx, media_rx, bus.healing.clone(), HealingThresholds::new(), MetricState::new(), Arc::new(Mutex::new(None)));
        let handle = tokio::spawn(async move {
            tokio::select! {
                _ = monitor.run() => {}
            }
        });

        bus.send_media(MediaEvent::SourceDisconnected { source_id: 12 });
        let event = healing_rx.recv().await.unwrap();

        match event {
            HealingEvent::ActivateFallback { reason } => assert!(reason.contains("Source 12")),
            _ => panic!("Yanlış healing event"),
        }

        handle.abort();
    }

    // ===== V8/I1: RuleEngine → HealingMonitor bağlantısı =====
    //
    // Testler saf `collect_rule_actions()`'ı hedefliyor (RjAction listesi döner,
    // kuyruğa yazmaz) — böylece paylaşılan global FFI_STATE/action_queue üzerinden
    // paralel-test yarışı olmadan deterministik doğrulama yapılır. `enqueue_action`
    // sarmalayıcısı (evaluate_rule_engine) yalnız bu listeyi kuyruğa iten üç satır.

    use crate::rules::{Rule, RuleEngine};
    use std::collections::HashMap;
    use std::sync::{Arc, Mutex};

    fn make_rule(id: &str, condition: &str, action: &str, step_kbps: i64, modes: &[&str]) -> Rule {
        let mut params = HashMap::new();
        params.insert("step_kbps".to_string(), serde_json::json!(step_kbps));
        Rule {
            id: id.to_string(),
            description: String::new(),
            condition: condition.to_string(),
            action: action.to_string(),
            params,
            modes: modes.iter().map(|s| s.to_string()).collect(),
        }
    }

    // V8/I20: `make_monitor` artık mode almıyor (constructor'dan kaldırıldı). Mod
    // testte doğrudan `collect_rule_actions(mode)`'a geçilir — böylece global
    // `HEALING_MODE`'a dokunulmaz, paralel-test yarışı olmadan deterministik kalır.
    fn make_monitor(rule_engine: Arc<Mutex<Option<RuleEngine>>>) -> HealingMonitor {
        let bus = EventBus::new();
        HealingMonitor::subscribe(
            bus.system.subscribe(),
            bus.media.subscribe(),
            bus.healing.clone(),
            HealingThresholds::new(),
            MetricState::new(),
            rule_engine,
        )
    }

    #[test]
    fn test_rule_engine_collects_matching_action() {
        let rule = make_rule("fd_high", "frame_drop_pct > 10", "bitrate_reduce", 500, &["auto-pilot"]);
        let engine = RuleEngine::new_test(vec![rule], 0);
        let mut monitor = make_monitor(Arc::new(Mutex::new(Some(engine))));
        monitor.current_metrics.frame_drop_pct = 12; // eşiği aşar

        let actions = monitor.collect_rule_actions(HealingMode::AutoPilot);
        assert_eq!(actions.len(), 1, "eşiği aşan tek kural bir aksiyon üretmeli");
        assert!(matches!(actions[0].rj.action_type, RjActionType::BitrateReduce));
        assert_eq!(actions[0].rj.param1, 500);
    }

    #[test]
    fn test_rule_engine_skips_wrong_mode() {
        // co-pilot-only kural, monitör AutoPilot → mode filtresi atlamalı
        let rule = make_rule("fd_high", "frame_drop_pct > 10", "bitrate_reduce", 500, &["co-pilot"]);
        let engine = RuleEngine::new_test(vec![rule], 0);
        let mut monitor = make_monitor(Arc::new(Mutex::new(Some(engine))));
        monitor.current_metrics.frame_drop_pct = 12;

        assert!(
            monitor.collect_rule_actions(HealingMode::AutoPilot).is_empty(),
            "co-pilot kuralı auto-pilot modunda atlanmalı"
        );
    }

    #[test]
    fn test_rule_engine_hysteresis_suppresses_repeat() {
        let rule = make_rule("fd_high", "frame_drop_pct > 10", "bitrate_reduce", 500, &["auto-pilot"]);
        let engine = RuleEngine::new_test(vec![rule], 60_000); // 60s histeresis
        let mut monitor = make_monitor(Arc::new(Mutex::new(Some(engine))));
        monitor.current_metrics.frame_drop_pct = 12;

        let first = monitor.collect_rule_actions(HealingMode::AutoPilot);
        assert_eq!(first.len(), 1, "ilk değerlendirme aksiyon üretmeli");
        let second = monitor.collect_rule_actions(HealingMode::AutoPilot);
        assert!(second.is_empty(), "histeresis içinde aynı kural tekrar tetiklenmemeli");
    }

    #[test]
    fn test_rule_engine_none_yields_empty() {
        // Motor yüklenmemiş (rules.json yok/parse hatası) → panik yok, boş liste
        let monitor = make_monitor(Arc::new(Mutex::new(None)));
        assert!(monitor.collect_rule_actions(HealingMode::AutoPilot).is_empty());
    }

    // ===== V8/I19+I20: 4-varyant mod + Assist/Manual semantiği =====

    #[test]
    fn test_healing_mode_from_raw_round_trip() {
        assert_eq!(HealingMode::from_raw(0), HealingMode::AutoPilot);
        assert_eq!(HealingMode::from_raw(1), HealingMode::CoPilot);
        assert_eq!(HealingMode::from_raw(2), HealingMode::Assist);
        assert_eq!(HealingMode::from_raw(3), HealingMode::Manual);
        // Aralık dışı → güvenli varsayılan CoPilot (onay gerektirir)
        assert_eq!(HealingMode::from_raw(4), HealingMode::CoPilot);
        assert_eq!(HealingMode::from_raw(u32::MAX), HealingMode::CoPilot);
    }

    #[test]
    fn test_assist_runs_only_critical_actions() {
        // İki assist kuralı: biri kritik (bitrate_reduce), biri değil (bitrate_recover).
        let critical = make_rule("fd_reduce", "frame_drop_pct > 10", "bitrate_reduce", 500, &["assist"]);
        let non_critical = make_rule("fd_recover", "frame_drop_pct > 10", "bitrate_recover", 250, &["assist"]);
        let engine = RuleEngine::new_test(vec![critical, non_critical], 0);
        let mut monitor = make_monitor(Arc::new(Mutex::new(Some(engine))));
        monitor.current_metrics.frame_drop_pct = 12;

        let actions = monitor.collect_rule_actions(HealingMode::Assist);
        assert_eq!(actions.len(), 1, "Assist modunda yalnız kritik aksiyon uygulanmalı");
        assert!(
            matches!(actions[0].rj.action_type, RjActionType::BitrateReduce),
            "kalan aksiyon kritik olan (bitrate_reduce) olmalı"
        );
    }

    #[test]
    fn test_manual_mode_evaluates_no_rules() {
        // AutoPilot'ta eşleşen bir kural bile olsa, Manual modda motor hiç çağrılmaz.
        let rule = make_rule("fd_high", "frame_drop_pct > 10", "bitrate_reduce", 500,
            &["auto-pilot", "co-pilot", "assist"]);
        let engine = RuleEngine::new_test(vec![rule], 0);
        let mut monitor = make_monitor(Arc::new(Mutex::new(Some(engine))));
        monitor.current_metrics.frame_drop_pct = 12;

        assert!(
            monitor.collect_rule_actions(HealingMode::Manual).is_empty(),
            "Manual modda hiçbir aksiyon üretilmemeli (tüm adaptasyon kapalı)"
        );
    }

    #[test]
    fn test_requires_approval_only_copilot_manual_category() {
        // V8/I33c: onay yalnız CoPilot + kategori-otomatik-DEĞİL durumunda.
        assert!(requires_approval(HealingMode::CoPilot, false), "CoPilot + manuel kategori → onay");
        assert!(!requires_approval(HealingMode::CoPilot, true), "CoPilot + auto kategori → onay yok");
        assert!(!requires_approval(HealingMode::AutoPilot, false), "AutoPilot → onay yok");
        assert!(!requires_approval(HealingMode::Assist, false), "Assist → onay yok");
        assert!(!requires_approval(HealingMode::Manual, false), "Manual → onay yok");
    }

    #[test]
    fn test_convert_action_type_mapping() {
        assert!(matches!(convert_action_type(ActionType::BitrateReduce), RjActionType::BitrateReduce));
        assert!(matches!(convert_action_type(ActionType::ScaleResolution), RjActionType::ScaleResolution));
        assert!(matches!(convert_action_type(ActionType::LogOnly), RjActionType::LogOnly));
    }
}
