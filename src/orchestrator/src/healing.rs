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

use std::sync::Arc;
use crate::constants;

use crate::event_bus::{HealingEvent, MediaEvent, SystemEvent};
use crate::metrics::MetricState;
use crate::rules::RuleMetrics;

/// Self-healing katmanları.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealingLayer {
    Reactive,
    Predictive,
    Adaptive,
}

/// Kullanıcı seviyesine göre self-healing modu.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealingMode {
    AutoPilot,
    CoPilot,
    ManualAssist,
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
    mode: AtomicCell<HealingMode>,
    trend: TrendState,
    metric_state: Arc<MetricState>,
    current_metrics: RuleMetrics,
}

impl HealingMonitor {
    pub fn subscribe(
        system_rx: Receiver<SystemEvent>,
        media_rx: Receiver<MediaEvent>,
        healing_tx: Sender<HealingEvent>,
        mode: HealingMode,
        thresholds: HealingThresholds,
        metric_state: Arc<MetricState>,
    ) -> Self {
        Self {
            system_rx,
            media_rx,
            healing_tx,
            thresholds: AtomicCell::new(thresholds),
            cooldown: CooldownTracker::new(),
            mode: AtomicCell::new(mode),
            trend: TrendState::new(),
            metric_state,
            current_metrics: RuleMetrics::default(),
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
                    let mode = crate::ffi::HEALING_MODE.load(Ordering::Relaxed);
                    // 0=AutoPilot, 1=CoPilot, 2=Assist, 3=Manual
                    if mode == 3 { continue; } // Manual — komut üretme
                    self.on_periodic();
                }
            }
        }
    }

    fn handle_system(&mut self, event: SystemEvent) {
        let mode = crate::ffi::HEALING_MODE.load(Ordering::Relaxed);
        if mode == 3 { return; } // Manual — reactive kanal bastırılıyor
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
        let mode = crate::ffi::HEALING_MODE.load(Ordering::Relaxed);
        if mode == 3 { return; } // Manual — reactive kanal bastırılıyor
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

    fn on_periodic(&mut self) {
        self.evaluate_predictive();
        self.evaluate_adaptive();
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

    fn evaluate_adaptive(&self) {
        if self.mode.load() != HealingMode::AutoPilot {
            return;
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

        let monitor = HealingMonitor::subscribe(system_rx, media_rx, bus.healing.clone(), HealingMode::AutoPilot, HealingThresholds::new(), MetricState::new());
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
}
