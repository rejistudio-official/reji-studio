//! FFI exports — C++ tarafına sunulan extern "C" fonksiyonlar.
//!
//! Kurallar:
//! - Bu fonksiyonlar blocking çağrı yapamaz.
//! - C++ → Rust: metric_ring (256-slot, lock-free ArrayQueue).
//! - Rust → C++: command_queue (64-slot, lock-free ArrayQueue).

use std::ffi::CStr;
use std::os::raw::c_char;
use std::sync::{Arc, OnceLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crossbeam::queue::ArrayQueue;
use tokio::sync::broadcast;
use tokio::runtime::Runtime;
use tokio::time::MissedTickBehavior;
use tracing::warn;

use crate::event_bus::{EventBus, HealingEvent, MediaEvent, SystemEvent};
use crate::healing::{HealingMode, HealingMonitor, HealingThresholds};
use crate::metrics::{MetricSample, MetricState};

/// Rust tarafı RjCommand — `ffi_bridge.h`'daki RjCommand ile #[repr(C)] eşleşmeli.
#[repr(C)]
#[derive(Copy, Clone)]
pub struct RjCommand {
    pub cmd_type:     u32,
    pub timestamp_us: u64,
    pub param_u32:    u32,
    pub param_f32:    f32,
}

const RJ_CMD_SCENE_SWITCH: u32 = 0;
const RJ_CMD_BITRATE_SET:  u32 = 1;
const RJ_CMD_PREVIEW_FPS:  u32 = 2;

struct FfiState {
    metric_ring:   Arc<ArrayQueue<MetricSample>>,
    command_queue: Arc<ArrayQueue<RjCommand>>,
    _metric_state: Arc<MetricState>,
    _runtime:      Runtime,
    media_tx:      broadcast::Sender<MediaEvent>,
}

static FFI_STATE: OnceLock<FfiState> = OnceLock::new();

fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0)
}

/// Tokio runtime, ring buffer drainer ve HealingMonitor'u başlatır.
/// Yalnızca bir kez etkili olur; tekrar çağrı güvenlidir (no-op).
#[no_mangle]
pub extern "C" fn rj_start_monitor() {
    FFI_STATE.get_or_init(|| {
        let runtime = Runtime::new().expect("Tokio runtime olusturulamadi");

        let metric_ring   = Arc::new(ArrayQueue::<MetricSample>::new(256));
        let command_queue = Arc::new(ArrayQueue::<RjCommand>::new(64));
        let event_bus     = EventBus::new();
        let metric_state  = MetricState::new();

        // Metric drainer: 16ms periyotla ring buffer'ı boşaltıp EventBus'a iletir.
        {
            let ring       = metric_ring.clone();
            let bus_system = event_bus.system.clone();
            let bus_media  = event_bus.media.clone();
            let state      = metric_state.clone();
            runtime.spawn(async move {
                let mut ticker = tokio::time::interval(Duration::from_millis(16));
                ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);
                loop {
                    ticker.tick().await;
                    while let Some(sample) = ring.pop() {
                        if !sample.is_valid() {
                            warn!("MetricSample canary hatasi, atlanıyor");
                            continue;
                        }
                        state.update(&sample);
                        let _ = bus_system.send(SystemEvent::CpuUsage {
                            ratio: sample.cpu_percent / 100.0,
                        });
                        if sample.frame_drops > 0 {
                            let _ = bus_media.send(MediaEvent::FrameDropped {
                                count: sample.frame_drops,
                            });
                        }
                    }
                }
            });
        }

        // HealingMonitor: event bus olaylarını okuyup healing aksiyonları üretir.
        {
            let system_rx  = event_bus.system.subscribe();
            let media_rx   = event_bus.media.subscribe();
            let healing_tx = event_bus.healing.clone();
            let monitor = HealingMonitor::subscribe(
                system_rx,
                media_rx,
                healing_tx,
                HealingMode::AutoPilot,
                HealingThresholds::new(),
            );
            runtime.spawn(monitor.run());
        }

        // Command writer: HealingEvent → RjCommand dönüşümü, command_queue'ya yazar.
        {
            let mut healing_rx = event_bus.healing.subscribe();
            let cmd_q = command_queue.clone();
            runtime.spawn(async move {
                loop {
                    let event = match healing_rx.recv().await {
                        Ok(e)  => e,
                        Err(_) => break,
                    };
                    let cmd = match event {
                        HealingEvent::ReduceBitrate { target_kbps } => RjCommand {
                            cmd_type:     RJ_CMD_BITRATE_SET,
                            timestamp_us: now_us(),
                            param_u32:    target_kbps,
                            param_f32:    0.0,
                        },
                        HealingEvent::ReducePreviewFps { target_fps } => RjCommand {
                            cmd_type:     RJ_CMD_PREVIEW_FPS,
                            timestamp_us: now_us(),
                            param_u32:    target_fps,
                            param_f32:    target_fps as f32,
                        },
                        HealingEvent::LightenCodec | HealingEvent::ActivateFallback { .. } => {
                            RjCommand {
                                cmd_type:     RJ_CMD_SCENE_SWITCH,
                                timestamp_us: now_us(),
                                param_u32:    0,
                                param_f32:    0.0,
                            }
                        }
                        _ => continue,
                    };
                    // Kuyruk doluysa düş — hot-path'de blocking yasak.
                    let _ = cmd_q.push(cmd);
                }
            });
        }

        FfiState {
            metric_ring,
            command_queue,
            _metric_state: metric_state,
            _runtime: runtime,
            media_tx: event_bus.media.clone(),
        }
    });
}

/// C++ pipeline'dan MetricSample alır ve ring buffer'a yazar (non-blocking).
/// Null pointer veya geçersiz canary varsa sessizce atlar.
#[no_mangle]
pub extern "C" fn rj_metrics_push(sample: *const MetricSample) {
    if sample.is_null() {
        return;
    }
    // SAFETY: C++ tarafı geçerli RjMetricSample* geçirir; layout MetricSample ile özdeş.
    let s = unsafe { *sample };
    if !s.is_valid() {
        return;
    }
    if let Some(state) = FFI_STATE.get() {
        // Ring buffer doluysa en eski sample'ı ezme yerine düş (backpressure).
        let _ = state.metric_ring.push(s);
    }
}

/// Rust komut kuyruğunu boşaltır; en fazla `max` adet RjCommand yazar.
/// Döndürülen değer: yazılan komut sayısı. Null/geçersiz argümanda -1.
#[no_mangle]
pub extern "C" fn rj_command_drain(out: *mut RjCommand, max: i32) -> i32 {
    if out.is_null() || max <= 0 {
        return -1;
    }
    let Some(state) = FFI_STATE.get() else {
        return 0;
    };
    let mut count = 0i32;
    while count < max {
        match state.command_queue.pop() {
            Some(cmd) => {
                // SAFETY: out + count < out + max, C++ tarafı yeterli alan ayırmış olmalı.
                unsafe { out.add(count as usize).write(cmd) };
                count += 1;
            }
            None => break,
        }
    }
    count
}

/// SRT bağlantı kopuşunu event bus'a iletir; reason null-safe, UTF-8 beklenir.
#[no_mangle]
pub extern "C" fn rj_connection_lost(reason: *const c_char) {
    let msg = if reason.is_null() {
        "<null>".to_owned()
    } else {
        // to_string_lossy replaces invalid UTF-8 bytes with U+FFFD — safe for untrusted C strings
        unsafe { CStr::from_ptr(reason) }
            .to_string_lossy()
            .into_owned()
    };
    warn!(target: "rj_srt", "connection_lost reason={}", msg);
    if let Some(state) = FFI_STATE.get() {
        let _ = state.media_tx.send(MediaEvent::SourceDisconnected { source_id: 0 });
    }
}

/// Pipeline durumu: 0 = hazır (monitor başlatılmış), -1 = başlatılmamış.
#[no_mangle]
pub extern "C" fn rj_pipeline_status() -> i32 {
    if FFI_STATE.get().is_some() { 0 } else { -1 }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_push_null_does_not_crash() {
        rj_metrics_push(std::ptr::null());
    }

    #[test]
    fn test_drain_null_returns_minus_one() {
        assert_eq!(rj_command_drain(std::ptr::null_mut(), 10), -1);
    }

    #[test]
    fn test_drain_zero_max_returns_minus_one() {
        assert_eq!(rj_command_drain(std::ptr::null_mut(), 0), -1);
    }

    #[test]
    fn test_start_monitor_idempotent() {
        rj_start_monitor();
        rj_start_monitor(); // ikinci çağrı no-op olmalı
        assert_eq!(rj_pipeline_status(), 0);
    }

    #[test]
    fn test_push_valid_sample_does_not_crash() {
        rj_start_monitor();
        let sample = MetricSample {
            magic_head:   MetricSample::MAGIC,
            timestamp_us: 1_000_000,
            bitrate_kbps: 6000,
            fps_actual:   60.0,
            cpu_percent:  40.0,
            frame_drops:  0,
            magic_tail:   MetricSample::MAGIC,
        };
        rj_metrics_push(&sample as *const _);
    }

    #[test]
    fn test_drain_after_start_is_nonnegative() {
        rj_start_monitor();
        let mut cmd = RjCommand { cmd_type: 0, timestamp_us: 0, param_u32: 0, param_f32: 0.0 };
        let n = rj_command_drain(&mut cmd as *mut _, 1);
        assert!(n >= 0);
    }
}
