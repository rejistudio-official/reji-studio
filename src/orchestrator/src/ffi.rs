//! FFI exports — C++ tarafına sunulan extern "C" fonksiyonlar.
//!
//! Kurallar:
//! - Bu fonksiyonlar blocking çağrı yapamaz.
//! - C++ → Rust: metric_ring (256-slot, lock-free ArrayQueue).
//! - Rust → C++: command_queue (64-slot, lock-free ArrayQueue).

use std::ffi::CStr;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::{Arc, OnceLock, Mutex};
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use crossbeam::queue::ArrayQueue;
use tokio::sync::broadcast;
use tokio::runtime::Runtime;
use tokio::time::MissedTickBehavior;
use tracing::{warn, debug, info};

use crate::event_bus::{EventBus, HealingEvent, MediaEvent, SystemEvent};
use crate::healing::{HealingMode, HealingMonitor, HealingThresholds};
use crate::metrics::{MetricSample, MetricState};
use crate::rules::RuleEngine;
use crate::ws_server::{self, WsState};

// Reverse FFI: Rust → C++ (implemented in pipeline.cpp, resolved at link time)
#[cfg(not(test))]
extern "C" {
    fn rj_ws_command(handle: u64, cmd: i32);
}

// Test binary'si C++ tarafını link edemez — no-op stub kullan
#[cfg(test)]
unsafe fn rj_ws_command(_handle: u64, _cmd: i32) {}

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

/// v0.4+: Adaptation action — `ffi_bridge.h`'daki RjAction ile #[repr(C)] eşleşmeli.
#[repr(u32)]  // E1: kesin u32 discriminant — repr(C) ABI implementation-defined
#[derive(Copy, Clone, Debug)]
pub enum RjActionType {
    BitrateReduce = 0,
    BitrateRecover = 1,
    ScaleResolution = 2,
    RestoreResolution = 3,
    CapFps = 4,
    RestoreFps = 5,
    LogOnly = 6,
}

#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct RjAction {
    pub id: u32,
    pub action_type: RjActionType,
    pub param1: i32,
    pub param2: i32,
    pub canary: u32,
}

// E1: ABI boyut garantisi — C++ static_assert ile eşleşmeli
const _: () = assert!(core::mem::size_of::<RjAction>() == 20);
const _: () = assert!(core::mem::size_of::<RjCommand>() == 24);

struct FfiState {
    metric_ring:    Arc<ArrayQueue<MetricSample>>,
    command_queue:  Arc<ArrayQueue<RjCommand>>,
    action_queue:   Arc<ArrayQueue<RjAction>>,     // v0.4+ Runtime Adaptation
    _metric_state:  Arc<MetricState>,
    _runtime:       Runtime,
    media_tx:       broadcast::Sender<MediaEvent>,
    ws_evt_tx:      broadcast::Sender<String>,     // Metrik eventleri → WS istemcileri
    rule_engine:    Arc<Mutex<Option<RuleEngine>>>, // v0.4+ Hot-reload
    _ws_state:      Arc<WsState>,                  // WebSocket sunucu durumu
}

static FFI_STATE: OnceLock<FfiState> = OnceLock::new();
pub(crate) static HEALING_MODE: AtomicU32 = AtomicU32::new(0);
static PIPELINE_HANDLE: AtomicU64 = AtomicU64::new(0);

fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0)
}

/// C++ Pipeline::init() tarafından çağrılır — registry handle'ı Rust'a bildirir.
/// rj_ws_command her çağrısında bu handle'ı C++'a geri geçirir.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_register_pipeline_handle(handle: u64) {
    let result = catch_unwind(AssertUnwindSafe(|| {
        PIPELINE_HANDLE.store(handle, Ordering::Release);
    }));
    if let Err(e) = result {
        eprintln!("[FFI] panic caught in rj_register_pipeline_handle: {:?}", e);
    }
}

/// Tokio runtime, ring buffer drainer ve HealingMonitor'u başlatır.
/// Yalnızca bir kez etkili olur; tekrar çağrı güvenlidir (no-op).
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_start_monitor() {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        rj_start_monitor_impl()
    }))
    .map_err(|_| {
        eprintln!("[PANIC] rj_start_monitor caught panic");
    });
}

fn rj_start_monitor_impl() {
    FFI_STATE.get_or_init(|| {
        let runtime = Runtime::new().expect("Tokio runtime olusturulamadi");

        let metric_ring   = Arc::new(ArrayQueue::<MetricSample>::new(256));
        let command_queue = Arc::new(ArrayQueue::<RjCommand>::new(64));
        let action_queue  = Arc::new(ArrayQueue::<RjAction>::new(64));     // v0.4+ Runtime Adaptation
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
                        if sample.gpu_load_pct > 0 {
                            let _ = bus_system.send(SystemEvent::GpuUsage {
                                ratio: sample.gpu_load_pct as f32 / 100.0,
                            });
                        }
                        if sample.frame_drops > 0 {
                            let _ = bus_media.send(MediaEvent::FrameDropped {
                                count: sample.frame_drops,
                            });
                        }
                        if sample.network_rtt_ms > 0 || sample.network_loss_pct > 0 {
                            let _ = bus_system.send(SystemEvent::NetworkStats {
                                rtt_ms: sample.network_rtt_ms as u32,
                                loss_pct: sample.network_loss_pct as f32,
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
                metric_state.clone(),
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

        // WebSocket kontrol sunucusu — port 7070
        let ws_state = Arc::new(WsState {
            cmd_tx: broadcast::channel(64).0,
            evt_rx: broadcast::channel(64).0,
        });
        {
            // Spawn öncesi log
            if let Ok(mut f) = std::fs::OpenOptions::new()
                .create(true).append(true)
                .open("C:\\reji-studio\\ws_debug.log")
            {
                use std::io::Write;
                let _ = writeln!(f, "[FFI] rj_start_monitor_impl: spawning WS server task");
            }
            let ws_state_clone = ws_state.clone();
            runtime.spawn(async move {
                ws_server::serve(7070, "127.0.0.1", ws_state_clone).await;
            });

            // WS komutu → C++ pipeline köprüsü
            let mut cmd_rx = ws_state.cmd_tx.subscribe();
            runtime.spawn(async move {
                while let Ok(cmd) = cmd_rx.recv().await {
                    let code: i32 = match cmd.as_str() {
                        "stream_start" => 1,
                        "stream_stop"  => 2,
                        "scene_cut"    => 3,
                        "scene_fade"   => 4,
                        _              => continue,
                    };
                    let handle = PIPELINE_HANDLE.load(Ordering::Acquire);
                    if handle != 0 {
                        // SAFETY: PipelineRegistry::get(handle) null-check yapar — UAF yok
                        unsafe { rj_ws_command(handle, code) };
                    }
                }
            });
        }

        // Initialize RuleEngine with default rules.json path (~/.reji/rules.json)
        let rules_path = {
            if let Ok(home) = std::env::var("USERPROFILE") {
                PathBuf::from(home).join(".reji").join("rules.json")
            } else {
                PathBuf::from("rules.json")
            }
        };

        let rule_engine = Arc::new(Mutex::new(
            RuleEngine::new(&rules_path)
                .map_err(|e| {
                    warn!("Failed to load rules from {:?}: {}", rules_path, e);
                    e
                })
                .ok()
        ));

        FfiState {
            metric_ring,
            command_queue,
            action_queue,    // v0.4+ Runtime Adaptation
            _metric_state: metric_state,
            _runtime: runtime,
            media_tx: event_bus.media.clone(),
            ws_evt_tx: ws_state.evt_rx.clone(),
            rule_engine,
            _ws_state: ws_state,
        }
    });
}

/// C++ pipeline'dan MetricSample alır ve ring buffer'a yazar (non-blocking).
/// Null pointer veya geçersiz canary varsa sessizce atlar.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_metrics_push(sample: *const MetricSample) {
    let _ = catch_unwind(AssertUnwindSafe(move || {
        if sample.is_null() {
            return;
        }
        // SAFETY: C++ tarafı geçerli RjMetricSample* geçirir; layout MetricSample ile özdeş.
        let s = unsafe { *sample };
        if !s.is_valid() {
            return;
        }
        if let Some(state) = FFI_STATE.get() {
            let _ = state.metric_ring.push(s);
            let json = format!(
                r#"{{"fps":{:.1},"kbps":{},"drop":{},"cpu":{},"gpu":{},"mem":{}}}"#,
                s.fps_actual,
                s.bitrate_kbps,
                s.frame_drops,
                s.cpu_load_pct,
                s.gpu_load_pct,
                s.memory_usage_pct
            );
            let _ = state.ws_evt_tx.send(json);
        }
    }))
    .map_err(|_| {
        eprintln!("[PANIC] rj_metrics_push caught panic");
    });
}

/// Rust komut kuyruğunu boşaltır; en fazla `max` adet RjCommand yazar.
///
/// # Safety
/// Caller MUST ensure:
/// 1. `out` points to valid RjCommand[max] buffer
/// 2. `max` ≤ 64 (enforced, returns 0 if violated)
/// 3. Buffer lifetime extends beyond this call
///
/// # Return
/// Number of commands written (0 if error, null, or max > 64; -1 on init error)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_command_drain(out: *mut RjCommand, max: i32) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if out.is_null() || max <= 0 {
            return -1;
        }
        // SECURITY FIX: Validate max to prevent buffer overflow
        if max > 64 {
            debug!("rj_command_drain: max={} exceeds limit 64, returning 0", max);
            return 0;
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
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_command_drain caught panic, returning -1");
        -1
    })
}

/// SRT bağlantı kopuşunu event bus'a iletir; reason null-safe, UTF-8 beklenir.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_connection_lost(reason: *const c_char) {
    let _ = catch_unwind(AssertUnwindSafe(move || {
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
    }))
    .map_err(|_| {
        eprintln!("[PANIC] rj_connection_lost caught panic");
    });
}

/// Pipeline durumu: 0 = hazır (monitor başlatılmış), -1 = başlatılmamış.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_pipeline_status() -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if FFI_STATE.get().is_some() { 0 } else { -1 }
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_pipeline_status caught panic");
        -1
    })
}

/// v0.4+: Dequeue next adaptation action from Rust to C++
/// Returns 1 if action available, 0 if queue empty
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_action_dequeue(out: *mut RjAction) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if out.is_null() {
            return 0;
        }
        if let Some(state) = FFI_STATE.get() {
            if let Some(action) = state.action_queue.pop() {
                unsafe { *out = action; }
                return 1;
            }
        }
        0
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_dequeue caught panic");
        0
    })
}

/// v0.4+: Enqueue an action from Rust rule engine to C++
pub fn enqueue_action(action: RjAction) -> bool {
    if let Some(state) = FFI_STATE.get() {
        return state.action_queue.push(action).is_ok();
    }
    false
}

/// v0.4+: Approve pending action (Co-Pilot mode)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_action_approve(_action_id: u32) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        1  // TODO: Implement Co-Pilot approval
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_approve caught panic");
        0
    })
}

/// v0.4+: Set healing mode (0=AutoPilot, 1=CoPilot, 2=Manual)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_set_healing_mode(mode: u32) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if mode > 3 { return false; }
        HEALING_MODE.store(mode, Ordering::SeqCst);
        true
    }))
    .unwrap_or(false)
}

/// v0.4+: Get current healing mode (0=AutoPilot, 1=CoPilot, 2=Manual)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_get_healing_mode() -> u32 {
    catch_unwind(AssertUnwindSafe(|| {
        HEALING_MODE.load(Ordering::SeqCst)
    }))
    .unwrap_or(0)
}

/// Reload rules from file (async hot-reload)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_reload_rules(path: *const c_char) -> i32 {
    catch_unwind(AssertUnwindSafe(move || {
        let Some(state) = FFI_STATE.get() else {
            debug!("rj_reload_rules: FFI_STATE not initialized");
            return 0;
        };

        let path_str = if path.is_null() {
            // Default path: ~/.reji/rules.json
            if let Ok(home) = std::env::var("USERPROFILE") {
                PathBuf::from(home).join(".reji").join("rules.json")
            } else {
                PathBuf::from("rules.json")
            }
        } else {
            // SAFETY: C++ geçerli UTF-8 string geçirmeli
            let cstr = unsafe { CStr::from_ptr(path) };
            PathBuf::from(cstr.to_string_lossy().as_ref())
        };

        match RuleEngine::new(&path_str) {
            Ok(new_engine) => {
                let mut engine_lock = state.rule_engine
                    .lock()
                    .unwrap_or_else(|poisoned| {
                        warn!("rule_engine mutex poison — recovering");
                        poisoned.into_inner()
                    });
                *engine_lock = Some(new_engine);
                info!(path = ?path_str, "Rules reloaded successfully");
                1
            }
            Err(e) => {
                warn!(path = ?path_str, error = %e, "Failed to reload rules");
                0
            }
        }
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_reload_rules caught panic");
        0
    })
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
            magic_head:       MetricSample::MAGIC,
            timestamp_us:     1_000_000,
            bitrate_kbps:     6000,
            fps_actual:       60.0,
            cpu_percent:      40.0,
            frame_drops:      0,
            frame_drop_pct:   0,
            gpu_temp_c:       0,
            cpu_temp_c:       0,
            memory_usage_pct: 0,
            cpu_load_pct:     40,
            gpu_load_pct:     0,
            network_rtt_ms:   0,
            network_loss_pct: 0,
            source_id:        0,
            magic_tail:       MetricSample::MAGIC,
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

    // ===== SECURITY FIX TESTS =====

    #[test]
    fn test_drain_max_exceeds_limit_returns_zero() {
        rj_start_monitor();
        let mut cmd = RjCommand { cmd_type: 0, timestamp_us: 0, param_u32: 0, param_f32: 0.0 };
        // SECURITY: max > 64 should return 0, not crash or overflow
        let n = rj_command_drain(&mut cmd as *mut _, 100);
        assert_eq!(n, 0, "max > 64 must return 0 to prevent buffer overflow");
    }

    #[test]
    fn test_drain_max_at_limit_succeeds() {
        rj_start_monitor();
        let mut cmds = [RjCommand { cmd_type: 0, timestamp_us: 0, param_u32: 0, param_f32: 0.0 }; 64];
        // SECURITY: max = 64 (limit) should be allowed
        let n = rj_command_drain(cmds.as_mut_ptr(), 64);
        assert!(n >= 0 && n <= 64, "max = 64 should succeed");
    }

    #[test]
    fn test_panic_safety_rj_metrics_push() {
        // SECURITY: rj_metrics_push should not panic and unwind into C++
        // This test verifies catch_unwind is in place
        rj_start_monitor();
        rj_metrics_push(std::ptr::null()); // Should not crash
        rj_metrics_push(std::ptr::null()); // Repeated null is safe
    }

    #[test]
    fn test_panic_safety_rj_command_drain() {
        // SECURITY: rj_command_drain should not panic and unwind into C++
        rj_start_monitor();
        let mut cmd = RjCommand { cmd_type: 0, timestamp_us: 0, param_u32: 0, param_f32: 0.0 };
        let _ = rj_command_drain(&mut cmd as *mut _, 1);
        let _ = rj_command_drain(&mut cmd as *mut _, 1); // Repeated call safe
    }

    #[test]
    fn test_panic_safety_rj_action_dequeue() {
        // SECURITY: rj_action_dequeue should not panic and unwind into C++
        rj_start_monitor();
        let mut action = RjAction { id: 0, action_type: RjActionType::BitrateReduce, param1: 0, param2: 0, canary: 0 };
        let _ = rj_action_dequeue(&mut action as *mut _);
        let _ = rj_action_dequeue(&mut action as *mut _); // Repeated call safe
    }

    #[test]
    fn test_healing_mode_roundtrip() {
        // AtomicU32 store/load doğruluğu
        assert!(rj_set_healing_mode(0));
        assert_eq!(rj_get_healing_mode(), 0);

        assert!(rj_set_healing_mode(1));
        assert_eq!(rj_get_healing_mode(), 1);

        assert!(rj_set_healing_mode(2));
        assert_eq!(rj_get_healing_mode(), 2);

        // Sıfırlamayı doğrula
        assert!(rj_set_healing_mode(0));
        assert_eq!(rj_get_healing_mode(), 0);
    }

    #[test]
    fn test_healing_mode_invalid_rejected() {
        // mode > 3 reddedilmeli, mevcut değer değişmemeli
        assert!(rj_set_healing_mode(0));
        assert!(!rj_set_healing_mode(4));
        assert_eq!(rj_get_healing_mode(), 0, "geçersiz mod önceki değeri ezememeli");
        assert!(!rj_set_healing_mode(u32::MAX));
        assert_eq!(rj_get_healing_mode(), 0);
    }

    #[test]
    fn test_null_pointer_safety_all_functions() {
        // SECURITY: All FFI functions with pointer args must handle null
        rj_start_monitor();

        // Null metrics push
        rj_metrics_push(std::ptr::null());

        // Null command drain
        let n = rj_command_drain(std::ptr::null_mut(), 10);
        assert_eq!(n, -1, "Null output ptr must return -1");

        // Null action dequeue
        let n = rj_action_dequeue(std::ptr::null_mut());
        assert_eq!(n, 0, "Null output ptr must return 0");
    }
}
