//! FFI exports — C++ tarafına sunulan extern "C" fonksiyonlar.
//!
//! Kurallar:
//! - Bu fonksiyonlar blocking çağrı yapamaz.
//! - C++ → Rust: metric_ring (256-slot, lock-free ArrayQueue).
//! - Rust → C++: command_queue (64-slot, lock-free ArrayQueue).

use std::collections::HashMap;
use std::ffi::CStr;
use std::os::raw::c_char;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;
use std::sync::{Arc, OnceLock, Mutex};
use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use crossbeam::queue::ArrayQueue;
use tokio::sync::broadcast;
use tokio::runtime::Runtime;
use tokio::time::MissedTickBehavior;
use tracing::{warn, debug, info};
use crate::constants;

use crate::event_bus::{EventBus, HealingEvent, MediaEvent, SystemEvent};
use crate::healing::{HealingMonitor, HealingThresholds};
use crate::metrics::{MetricSample, MetricState};
use crate::rules::RuleEngine;
use crate::ws_server::{self, WsState};


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

/// WS komut kuyruğu kodu: SetScene. (1=stream_start, 2=stream_stop, 3=scene_cut,
/// 4=scene_fade zaten kullanımda — bkz. ffi_bridge.h.) ws_server'ın
/// SetCurrentProgramScene handler'ı (Aşama 5) tarafından push edilir.
pub(crate) const RJ_WS_CMD_SET_SCENE: i32 = 5;

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

/// V8/I33 (I11): UI bildirim event'i — aktüatör kuyruğundan (`RjAction`) AYRI
/// bir kuyrukta akar. Aktüatör kuyruğu yalnız uygulanmaya HAZIR aksiyonları
/// taşır; bu event UI'ı bilgilendirir (banner / CoPilot onayı / geçersizleşme).
/// Böylece "uygula" ile "göster" tek kuyruğu paylaşmaz — I11 yarışı yapısal
/// olarak ortadan kalkar. `#[repr(C)]`, `ffi_auto.h`'deki RjActionEvent ile bire bir.
#[repr(C)]
#[derive(Copy, Clone, Debug)]
pub struct RjActionEvent {
    pub id: u32,
    pub action_type: RjActionType,
    pub param1: i32,
    pub param2: i32,
    /// 0 = bilgi (otomatik uygulanıyor/uygulandı), 1 = kullanıcı onayı gerekiyor.
    pub require_approval: u32,
    /// `RJ_ACTION_EVENT_*`: 0 = New, 1 = Invalidated (TTL dolumu / mod değişimi).
    pub kind: u32,
}
const _: () = assert!(core::mem::size_of::<RjActionEvent>() == 24);

/// UI event türleri — `RjActionEvent::kind`. C++ tarafı `ffi_bridge.h`'de eşler.
pub(crate) const RJ_ACTION_EVENT_NEW: u32 = 0;
pub(crate) const RJ_ACTION_EVENT_INVALIDATED: u32 = 1;

impl RjActionEvent {
    /// Info event: otomatik uygulanan aksiyon için (require_approval=false, New).
    pub(crate) fn info_event(a: &RjAction) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 0,
            kind: RJ_ACTION_EVENT_NEW,
        }
    }

    /// Approval event: CoPilot'ta onay bekleyen aksiyon (require_approval=true, New).
    pub(crate) fn approval_event(a: &RjAction) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 1,
            kind: RJ_ACTION_EVENT_NEW,
        }
    }

    /// Invalidated event: pending aksiyon TTL doldu ya da mod değişti — UI bunu
    /// (checkbox/banner) temizlesin. `require_approval` anlamsız (0).
    pub(crate) fn invalidated_event(a: &RjAction) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 0,
            kind: RJ_ACTION_EVENT_INVALIDATED,
        }
    }
}

/// V8/I33a: CoPilot'ta onay bekleyen bir aksiyonun deposu kaydı. `RjAction`
/// (uygulanacak veri) + `rule_id` (reject → RuleEngine cooldown eşlemesi,
/// commit 5) + `created` (TTL). Pending deposu kaynak-of-truth'tur; UI event'i
/// yalnız bildirimdir (kaybolursa TTL/re-üretim telafi eder).
struct PendingEntry {
    action: RjAction,
    rule_id: String,
    created: Instant,
}

/// V8/I33a: Onay bekleyen aksiyonun geçerlilik süresi. Dolunca UI'a
/// Invalidated bildirimi gider ve entry düşer (metrik düzeldiyse bir sonraki
/// tick koşul hâlâ varsa zaten yeniden üretir). Şimdilik sabit — kullanıcı
/// gözlemine göre ayarlanabilir; konfigürasyon yüzeyine YAGNI gereği çıkarılmadı.
const PENDING_TTL: Duration = Duration::from_secs(30);

/// V8/I33b: Reddedilen aksiyonun kuralına uygulanan cooldown süresi. Bu süre
/// boyunca kural yeniden tetiklenmez — aksi halde ~1s tick'te yeniden üretilip
/// kullanıcıyı spamler. Hysteresis'ten (tipik <5s) belirgin uzun, healing'i
/// büsbütün öldürmeyecek kadar kısa. Sabit — kullanıcı gözlemine göre
/// ayarlanabilir; config yüzeyine YAGNI gereği çıkarılmadı.
const REJECT_COOLDOWN: Duration = Duration::from_secs(120);

struct FfiState {
    metric_ring:       Arc<ArrayQueue<MetricSample>>,
    command_queue:     Arc<ArrayQueue<RjCommand>>,
    action_queue:      Arc<ArrayQueue<RjAction>>,      // v0.4+ Runtime Adaptation (AKTÜATÖR — uygulanmaya hazır)
    ui_event_queue:    Arc<ArrayQueue<RjActionEvent>>, // V8/I33 (I11): UI bildirimi — aktüatörden ayrı
    pending_actions:   Mutex<HashMap<u32, PendingEntry>>, // V8/I33a: CoPilot onay bekleyen aksiyonlar (id → entry)
    ws_command_queue:  Arc<ArrayQueue<(i32, i32)>>,    // (cmd_type, param) — WS → C++ kuyruk
    _metric_state:     Arc<MetricState>,
    _runtime:          Runtime,
    media_tx:          broadcast::Sender<MediaEvent>,
    ws_evt_tx:         broadcast::Sender<String>,      // Metrik eventleri → WS istemcileri
    rule_engine:       Arc<Mutex<Option<RuleEngine>>>, // v0.4+ Hot-reload
    _ws_state:         Arc<WsState>,                   // WebSocket sunucu durumu
    ws_json_buf:       Mutex<String>,                  // Tekrarlanan format! yerine reuse buffer
}

static FFI_STATE: OnceLock<FfiState> = OnceLock::new();
pub(crate) static HEALING_MODE: AtomicU32 = AtomicU32::new(0);

/// V8/I33: Süreç-global, monoton aksiyon ID sayacı. Eskiden ID'ler
/// `RuleEngine::evaluate()` içinde her tick `1`'den başlıyordu (tick-yerel) —
/// pending-onay deposu ID ile anahtarlanacağından tick'ler arası çakışma
/// üretirdi. Bu sayaç tek kaynak: `next_action_id()` her aksiyona benzersiz
/// bir ID verir, RuleEngine hot-reload'undan bağımsız (global static, motorda
/// değil). `0` "geçersiz/yok" sentinel'i olarak ayrılır — asla dağıtılmaz.
static NEXT_ACTION_ID: AtomicU32 = AtomicU32::new(1);

/// Sıradaki benzersiz aksiyon ID'sini döndürür (monoton, atomik). `0` atlanır
/// (sentinel). Wrap ~4.29 milyar aksiyonda; gerçekçi hızda (10/sn) ~13 yıl
/// kesintisiz çalışma gerektirir — pratikte ulaşılamaz.
pub(crate) fn next_action_id() -> u32 {
    let id = NEXT_ACTION_ID.fetch_add(1, Ordering::Relaxed);
    if id == 0 {
        // Wrap sonrası 0'a denk geldik — bir kez daha ilerle, 0'ı atla.
        NEXT_ACTION_ID.fetch_add(1, Ordering::Relaxed)
    } else {
        id
    }
}

/// V8/I33c: Aksiyon kategorileri — UI'ın 3 auto-onay grubuyla (bitrate/
/// resolution/fps) eşleşir. (`chk_source_auto` UI'da var ama karşılık gelen
/// source-switch aksiyon tipi yok — inert; V8 Sprint 4 temizlik maddesi.)
pub(crate) const RJ_ACTION_CAT_BITRATE: u32 = 0;
pub(crate) const RJ_ACTION_CAT_RESOLUTION: u32 = 1;
pub(crate) const RJ_ACTION_CAT_FPS: u32 = 2;

/// V8/I33c: CoPilot per-kategori otomatik-onay bit maskesi
/// (bit0=bitrate, bit1=resolution, bit2=fps). Kapı artık MOTORDA (UI-yerel
/// değil) — tek görünür karar noktası. Varsayılan `0` = tüm kategoriler onay
/// bekler (güvenli); UI startup senkronu + değişiklikte gerçek değerleri
/// `rj_set_action_auto_approve` ile iter (I19 startup deseni).
static ACTION_AUTO_APPROVE: AtomicU32 = AtomicU32::new(0);

/// RjActionType → auto-onay kategorisi. `LogOnly` bir kategoriye ait değil
/// (her zaman otomatik, onay gerektirmez → `None`).
fn action_category(t: RjActionType) -> Option<u32> {
    match t {
        RjActionType::BitrateReduce | RjActionType::BitrateRecover => Some(RJ_ACTION_CAT_BITRATE),
        RjActionType::ScaleResolution | RjActionType::RestoreResolution => Some(RJ_ACTION_CAT_RESOLUTION),
        RjActionType::CapFps | RjActionType::RestoreFps => Some(RJ_ACTION_CAT_FPS),
        RjActionType::LogOnly => None,
    }
}

/// V8/I33c: CoPilot'ta bu aksiyon tipinin kategorisi otomatik-onaylı mı?
/// `LogOnly` her zaman `true` (onay gerektirmez).
pub(crate) fn category_auto_approve(t: RjActionType) -> bool {
    match action_category(t) {
        None => true,
        Some(cat) => (ACTION_AUTO_APPROVE.load(Ordering::Relaxed) & (1 << cat)) != 0,
    }
}

/// Kaç action_queue mesajının kapasitede doluluk nedeniyle düşürüldüğünü sayar.
pub(crate) static DROPPED_ACTIONS_COUNT: AtomicU64 = AtomicU64::new(0);
/// Kaç ws_command_queue mesajının kapasitede doluluk nedeniyle düşürüldüğünü sayar.
pub(crate) static DROPPED_WS_CMDS_COUNT: AtomicU64 = AtomicU64::new(0);
/// V8/I33 (I11): UI event kuyruğunda ring-drop ile düşürülen event sayısı.
pub(crate) static DROPPED_UI_EVENTS_COUNT: AtomicU64 = AtomicU64::new(0);

fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0)
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

        let metric_ring       = Arc::new(ArrayQueue::<MetricSample>::new(256));
        let command_queue     = Arc::new(ArrayQueue::<RjCommand>::new(64));
        let action_queue      = Arc::new(ArrayQueue::<RjAction>::new(64));  // v0.4+ Runtime Adaptation (aktüatör)
        let ui_event_queue    = Arc::new(ArrayQueue::<RjActionEvent>::new(64)); // V8/I33 (I11): UI bildirim kuyruğu
        let ws_command_queue  = Arc::new(ArrayQueue::<(i32, i32)>::new(32)); // WS → C++ kuyruk
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

        // V8/I1: RuleEngine'ı HealingMonitor'dan ÖNCE kur — monitör onu paylaşır.
        // Default path: ~/.reji/rules.json. Yüklenemezse None (monitör kural
        // katmanını atlar, hardcoded katman çalışmaya devam eder).
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

        // HealingMonitor: event bus olaylarını okuyup healing aksiyonları üretir.
        {
            let system_rx  = event_bus.system.subscribe();
            let media_rx   = event_bus.media.subscribe();
            let healing_tx = event_bus.healing.clone();
            let monitor = HealingMonitor::subscribe(
                system_rx,
                media_rx,
                healing_tx,
                // V8/I20: mode parametresi kaldırıldı — canlı HEALING_MODE okunuyor
                HealingThresholds::new(),
                metric_state.clone(),
                rule_engine.clone(),  // V8/I1: kural motorunu paylaş (hot-reload aynı Arc)
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

        // WebSocket kontrol sunucusu — 7070 öncelikli, meşgulse 7071/7072/7073'e düşer.
        let ws_state = Arc::new(WsState {
            cmd_tx: broadcast::channel(64).0,
            evt_rx: broadcast::channel(64).0,
            streaming_active: Arc::new(std::sync::atomic::AtomicBool::new(false)),
            // FfiState._metric_state ile AYNI Arc — tek doğruluk kaynağı, iki instance yok.
            metric_state: metric_state.clone(),
            stream_started_at_ms: Arc::new(std::sync::atomic::AtomicU64::new(0)),
            scene_names: Arc::new(Mutex::new(Vec::new())),
            current_scene_idx: Arc::new(AtomicU32::new(0)),
            // FfiState.ws_command_queue ile AYNI Arc — iki ayrı kuyruk yok (metric_state deseni).
            ws_command_queue: ws_command_queue.clone(),
            // V8/I8: WS kontrol parolası — başlangıçta None (auth kapalı). rj_set_ws_password
            // (commit 5) bu Arc'ı günceller; her bağlantı açılışında taze okunur.
            password: Arc::new(std::sync::RwLock::new(None)),
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
                ws_server::serve(vec![7070, 7071, 7072, 7073], "127.0.0.1", ws_state_clone).await;
            });

            // WS komutu → ws_command_queue (lock-free, C++ run_frame() tarafından drain edilir).
            // streaming_active bayrağı process_stream_cmd içinde (TEK yazma noktası) güncellenir.
            let mut cmd_rx = ws_state.cmd_tx.subscribe();
            let ws_cmd_q = ws_command_queue.clone();
            let streaming_active = ws_state.streaming_active.clone();
            let stream_started_at_ms = ws_state.stream_started_at_ms.clone();
            runtime.spawn(async move {
                while let Ok(cmd) = cmd_rx.recv().await {
                    let Some(code) =
                        ws_server::process_stream_cmd(&cmd, &streaming_active, &stream_started_at_ms)
                    else {
                        continue;
                    };
                    match ws_cmd_q.push((code, 0)) {
                        Ok(_) => {}
                        Err(_) => {
                            let total = DROPPED_WS_CMDS_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
                            eprintln!("[WsCmdQueue] FULL — ws command dropped: code={code} (total dropped: {total})");
                        }
                    }
                }
            });
        }

        FfiState {
            metric_ring,
            command_queue,
            action_queue,       // v0.4+ Runtime Adaptation (aktüatör)
            ui_event_queue,     // V8/I33 (I11): UI bildirim kuyruğu
            pending_actions: Mutex::new(HashMap::new()), // V8/I33a: CoPilot pending deposu
            ws_command_queue,   // WS → C++ kuyruk
            _metric_state: metric_state,
            _runtime: runtime,
            media_tx: event_bus.media.clone(),
            ws_evt_tx: ws_state.evt_rx.clone(),
            rule_engine,
            _ws_state: ws_state,
            ws_json_buf: Mutex::new(String::with_capacity(128)),
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
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_metrics_push ignored");
            return;
        };
        let _ = state.metric_ring.push(s);
        {
            let mut buf = state.ws_json_buf.lock().unwrap();
            buf.clear();
            use std::fmt::Write as _;
            let _ = write!(buf,
                r#"{{"fps":{:.1},"kbps":{},"drop":{},"cpu":{},"gpu":{},"mem":{}}}"#,
                s.fps_actual, s.bitrate_kbps, s.frame_drops,
                s.cpu_load_pct, s.gpu_load_pct, s.memory_usage_pct
            );
            let _ = state.ws_evt_tx.send(buf.clone());
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
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_command_drain ignored");
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
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_connection_lost ignored");
            return;
        };
        let _ = state.media_tx.send(MediaEvent::SourceDisconnected { source_id: 0 });
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
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_action_dequeue ignored");
            return 0;
        };
        if let Some(action) = state.action_queue.pop() {
            unsafe { *out = action; }
            return 1;
        }
        0
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_dequeue caught panic");
        0
    })
}

/// V8/I33 (I11): UI event kuyruğundan sıradaki event'i çeker. `1` = event var
/// (`*out` dolduruldu), `0` = boş/null/init değil. Bu, `rj_action_dequeue`
/// (aktüatör) ile AYRI kuyruktur — UI artık aktüatör kuyruğunu POP etmez,
/// böylece "her aksiyon rastgele tek tüketiciye gider" yarışı yok.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_action_event_dequeue(out: *mut RjActionEvent) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if out.is_null() {
            return 0;
        }
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_action_event_dequeue ignored");
            return 0;
        };
        if let Some(event) = state.ui_event_queue.pop() {
            unsafe { *out = event; }
            return 1;
        }
        0
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_event_dequeue caught panic");
        0
    })
}

/// Gerçek WS port'unu döndürür (sunucu henüz bind olmadıysa 0).
/// C++ pipeline init sonrası çağrılmalı — run_frame() ilk iterasyonunda hazır olur.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_get_ws_port() -> u16 {
    catch_unwind(AssertUnwindSafe(|| {
        crate::ws_server::actual_port()
    }))
    .unwrap_or(0)
}

/// WS komutunu ws_command_queue'ya yazar (non-blocking).
/// C++ run_frame() tarafından rj_ws_command_dequeue ile drain edilir.
/// Handle gerektirmez — PipelineRegistry bağımlılığı yoktur.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_ws_command_v2(cmd: i32, param: i32) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_ws_command_v2 ignored");
            return false;
        };
        match state.ws_command_queue.push((cmd, param)) {
            Ok(_) => true,
            Err(_) => {
                let total = DROPPED_WS_CMDS_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
                eprintln!("[WsCmdQueue] FULL — rj_ws_command_v2 dropped: cmd={cmd} param={param} (total dropped: {total})");
                false
            }
        }
    }))
    .unwrap_or(false)
}

/// ws_command_queue'dan bir komut çıkarır.
/// Döndürür: 1 = komut var (cmd/param yazıldı), 0 = kuyruk boş veya hata.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_ws_command_dequeue(cmd: *mut i32, param: *mut i32) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        if cmd.is_null() || param.is_null() {
            return 0;
        }
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_ws_command_dequeue ignored");
            return 0;
        };
        if let Some((c, p)) = state.ws_command_queue.pop() {
            unsafe {
                *cmd   = c;
                *param = p;
            }
            return 1;
        }
        0
    }))
    .unwrap_or(0)
}

/// v0.4+: Enqueue an action from Rust rule engine to C++
pub fn enqueue_action(action: RjAction) -> bool {
    let Some(state) = FFI_STATE.get() else {
        eprintln!("[FFI] WARNING: FFI_STATE not initialized — enqueue_action ignored");
        return false;
    };
    match state.action_queue.push(action) {
        Ok(_) => true,
        Err(_) => {
            let total = DROPPED_ACTIONS_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
            eprintln!("[ActionQueue] FULL — action dropped: {:?}  (total dropped: {})", action, total);
            false
        }
    }
}

/// V8/I33 (I11): UI event kuyruğuna it. **Ring semantiği**: kuyruk doluysa
/// (UI kapalı/donmuş) EN ESKİ event `force_push` ile düşürülür + sayaç/warn.
/// Gerekçe: info-event kaybı zararsız; approval-event kaybı da pending deposu +
/// TTL ile telafi edilir (pending kaynak-of-truth, event yalnız bildirim).
/// Bu yüzden aktüatör kuyruğunun aksine (push-fail = drop-newest) burada
/// drop-oldest tercih edilir — en güncel UI durumu korunur.
pub fn enqueue_ui_event(event: RjActionEvent) {
    let Some(state) = FFI_STATE.get() else {
        eprintln!("[FFI] WARNING: FFI_STATE not initialized — enqueue_ui_event ignored");
        return;
    };
    if let Some(evicted) = state.ui_event_queue.force_push(event) {
        let total = DROPPED_UI_EVENTS_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
        eprintln!(
            "[UiEventQueue] FULL — oldest event dropped: {:?}  (total dropped: {})",
            evicted, total
        );
    }
}

/// V8/I33a: CoPilot'ta onay bekleyen aksiyonu depola + UI'a approval event
/// gönder. Pending deposu kaynak-of-truth; UI event'i yalnız bildirim (kaybı
/// TTL/re-üretim ile telafi edilir).
pub fn enqueue_pending(action: RjAction, rule_id: String) {
    let Some(state) = FFI_STATE.get() else {
        eprintln!("[FFI] WARNING: FFI_STATE not initialized — enqueue_pending ignored");
        return;
    };
    let event = RjActionEvent::approval_event(&action);
    {
        let mut pending = state.pending_actions.lock().unwrap();
        pending.insert(
            action.id,
            PendingEntry { action, rule_id, created: Instant::now() },
        );
    }
    enqueue_ui_event(event);
}

/// V8/I33a: TTL dolmuş pending aksiyonları süpür — her biri için UI'a
/// Invalidated event gönderir ve entry'yi düşürür. HealingMonitor tick'inden
/// (moddan bağımsız) periyodik çağrılır.
pub fn sweep_expired_pending() {
    let Some(state) = FFI_STATE.get() else { return; };
    let now = Instant::now();
    let expired: Vec<RjAction> = {
        let mut pending = state.pending_actions.lock().unwrap();
        let ids: Vec<u32> = pending
            .iter()
            .filter(|(_, e)| now.duration_since(e.created) >= PENDING_TTL)
            .map(|(id, _)| *id)
            .collect();
        ids.into_iter()
            .filter_map(|id| pending.remove(&id).map(|e| e.action))
            .collect()
    };
    for action in expired {
        eprintln!("[Pending] TTL doldu, aksiyon iptal edildi: id={}", action.id);
        enqueue_ui_event(RjActionEvent::invalidated_event(&action));
    }
}

/// V8/I33a: Mod değişiminde tüm pending aksiyonları temizle (otomatik
/// uygulanmaz) + UI'a Invalidated bildir. Gerekçe: bayat bir pending'i mod
/// değişimi anında patlatmak sürpriz yan etki; koşul hâlâ geçerliyse bir
/// sonraki tick zaten yeniden üretir.
fn clear_pending_on_mode_change() {
    let Some(state) = FFI_STATE.get() else { return; };
    let drained: Vec<RjAction> = {
        let mut pending = state.pending_actions.lock().unwrap();
        pending.drain().map(|(_, e)| e.action).collect()
    };
    for action in drained {
        enqueue_ui_event(RjActionEvent::invalidated_event(&action));
    }
}

/// v0.4+: Approve pending action (Co-Pilot mode)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_action_approve(action_id: u32) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_action_approve ignored");
            return 0;
        };
        let mut pending = state.pending_actions.lock().unwrap();
        let Some(entry) = pending.remove(&action_id) else {
            // yok / süresi dolmuş / zaten işlenmiş — UI `0`'da checkbox'ı temizler.
            return 0;
        };
        match state.action_queue.push(entry.action) {
            Ok(_) => 1,
            Err(returned) => {
                // Aktüatör kuyruğu dolu (cap 64'te pratikte olmaz): sessiz drop
                // YOK — aksiyonu pending'de TUT, `0` dön. Bir sonraki approve/TTL
                // yeniden dener.
                eprintln!(
                    "[ActionQueue] FULL on approve — aksiyon pending'de tutuldu: id={}",
                    action_id
                );
                pending.insert(
                    action_id,
                    PendingEntry { action: returned, rule_id: entry.rule_id, created: entry.created },
                );
                0
            }
        }
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_approve caught panic");
        0
    })
}

/// V8/I33a: CoPilot reddi — pending deposundan `id`'li aksiyonu SİL (uygulanmaz).
/// `1` = bulundu+silindi; `0` = yok/süresi dolmuş/zaten işlenmiş (UI `0`'da
/// checkbox'ı sessizce temizler). Reddedilen aksiyonun kuralına cooldown
/// **commit 5**'te (RuleEngine) eklenecek — aksi halde bir sonraki tick yeniden
/// üretilip kullanıcıyı spamler.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_action_reject(action_id: u32) -> i32 {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_action_reject ignored");
            return 0;
        };
        let removed = state.pending_actions.lock().unwrap().remove(&action_id);
        match removed {
            Some(entry) => {
                // V8/I33b: reddedilen aksiyonun kuralına cooldown uygula — bir
                // sonraki tick yeniden üretilip kullanıcıyı spamlemesin.
                if let Ok(guard) = state.rule_engine.lock() {
                    if let Some(engine) = guard.as_ref() {
                        engine.apply_cooldown(&entry.rule_id, REJECT_COOLDOWN);
                    }
                }
                1
            }
            None => 0,
        }
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_action_reject caught panic");
        0
    })
}

/// v0.4+: Set healing mode (0=AutoPilot, 1=CoPilot, 2=Assist, 3=Manual)
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_set_healing_mode(mode: u32) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if mode > 3 { return false; }
        // V8/I33a: mod GERÇEKTEN değiştiyse pending'leri temizle (aynı değere
        // set — örn. startup senkronu — pending'i patlatmasın).
        let prev = HEALING_MODE.swap(mode, Ordering::SeqCst);
        if prev != mode {
            clear_pending_on_mode_change();
        }
        true
    }))
    .unwrap_or(false)
}

/// V8/I33c: CoPilot per-kategori otomatik-onay ayarını Rust motoruna set eder.
/// `category`: 0=bitrate, 1=resolution, 2=fps. `enabled=true` → o kategori
/// CoPilot'ta onay beklemeden otomatik uygulanır; `false` → onay bekler.
/// Geçersiz kategori → `false`. UI (SettingsDialog) startup'ta ve checkbox
/// değişince çağırır — kapı UI-yerel değil MOTORDA (I19 startup senkronu deseni).
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_set_action_auto_approve(category: u32, enabled: bool) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if category > RJ_ACTION_CAT_FPS { return false; }
        let bit = 1u32 << category;
        if enabled {
            ACTION_AUTO_APPROVE.fetch_or(bit, Ordering::Relaxed);
        } else {
            ACTION_AUTO_APPROVE.fetch_and(!bit, Ordering::Relaxed);
        }
        true
    }))
    .unwrap_or(false)
}

/// V8/I8: WebSocket kontrol parolasını ayarlar. `null` VEYA boş string → auth
/// KAPALI (`None`, bugünkü toleranslı davranış). Parola loglanmaz. Her BAĞLANTI
/// açılışında taze okunur → çalışırken değişim yalnız yeni bağlantılara uygulanır,
/// mevcut doğrulanmış oturumlar sürer. UI (SettingsDialog) startup'ta ve OK'te
/// çağırır (I19 startup senkronu deseni).
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_set_ws_password(password: *const c_char) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_set_ws_password ignored");
            return false;
        };
        // null → None (auth kapalı). CStr trusted C++ Settings'ten; boş → None.
        let pw: Option<String> = if password.is_null() {
            None
        } else {
            let s = unsafe { CStr::from_ptr(password) }.to_string_lossy().into_owned();
            if s.is_empty() { None } else { Some(s) }
        };
        match state._ws_state.password.write() {
            Ok(mut guard) => {
                *guard = pw;   // parola değeri LOGLANMAZ (yalnız set gerçeği)
                eprintln!("[FFI] WS auth {}", if guard.is_some() { "etkin" } else { "kapalı" });
                true
            }
            Err(_) => {
                eprintln!("[FFI] WS password kilidi poisoned — set başarısız");
                false
            }
        }
    }))
    .unwrap_or(false)
}

/// v0.4+: Get current healing mode (0=AutoPilot, 1=CoPilot, 2=Assist, 3=Manual)
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
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_reload_rules ignored");
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

/// C++ UI → Rust: mevcut sahne isimlerini WsState'e yazar (GetSceneList için).
///
/// Ownership: C++ pointer'ları verir, Rust HEMEN kopyalar (`into_owned`); fonksiyon
/// dönünce hiçbir ham pointer saklanmaz — C++ hemen sonra belleği serbest bırakabilir.
/// Bloklamaz (kısa Mutex kilidi, hot-path değil). Panik sınırı geçmez (catch_unwind).
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_push_scene_names(names: *const *const c_char, count: u32) {
    let _ = catch_unwind(AssertUnwindSafe(move || {
        if names.is_null() {
            return;
        }
        const MAX_SCENES: u32 = 256; // sınırsız okuma riski yok (I24 dersi)
        const MAX_NAME_LEN: usize = 256;
        let count = count.min(MAX_SCENES);
        let mut result = Vec::with_capacity(count as usize);
        for i in 0..count {
            // SAFETY: names[0..count] geçerli pointer dizisi (C++ sözleşmesi); count clamp'lendi.
            let ptr = unsafe { *names.add(i as usize) };
            if ptr.is_null() {
                continue;
            }
            // SAFETY: ptr geçerli, NUL-sonlu C string (C++ QByteArray::constData).
            let s = unsafe { CStr::from_ptr(ptr) }.to_string_lossy().into_owned();
            // char-sınırında güvenli kes: ham byte slice UTF-8 ortasında panik yapabilir.
            let s = if s.len() > MAX_NAME_LEN {
                let mut end = MAX_NAME_LEN;
                while end > 0 && !s.is_char_boundary(end) {
                    end -= 1;
                }
                s[..end].to_string()
            } else {
                s
            };
            result.push(s);
        }
        let Some(state) = FFI_STATE.get() else {
            return;
        };
        if let Ok(mut guard) = state._ws_state.scene_names.lock() {
            *guard = result;
        }
    }));
}

/// C++ UI → Rust: aktif sahne değiştiğinde çağrılır (UI tıklaması / legacy cut /
/// SetCurrentProgramScene'in gerçek geçişi). WsState.current_scene_idx'i günceller —
/// tek gerçek kaynak: Rust tahmin etmez, C++'ın gerçekte yaptığını dinler.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_user_event_scene_switch(scene_id: u32) {
    let _ = catch_unwind(AssertUnwindSafe(move || {
        let Some(state) = FFI_STATE.get() else {
            return;
        };
        state._ws_state.current_scene_idx.store(scene_id, Ordering::Relaxed);
    }));
}

#[cfg(test)]
mod tests {
    use super::*;

    // V8/I33a: pending deposu + healing-mode testleri süreç-global FfiState'i
    // paylaşır; paralel çalışınca mod değişimi bir testin pending'ini drain
    // edebilir. Bu guard onları serileştirir (poison yok sayılır — bir testin
    // paniği diğerlerini kilitlemesin).
    static PENDING_TEST_GUARD: Mutex<()> = Mutex::new(());
    fn pending_guard() -> std::sync::MutexGuard<'static, ()> {
        PENDING_TEST_GUARD.lock().unwrap_or_else(|e| e.into_inner())
    }

    fn mk_action(id: u32) -> RjAction {
        RjAction { id, action_type: RjActionType::BitrateReduce, param1: 3500, param2: 0, canary: 0 }
    }

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
            bitrate_kbps:     constants::DEFAULT_BITRATE_KBPS,
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
        let _g = pending_guard(); // V8/I33a: mod değişimi pending'i drain eder — serileştir
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
        let _g = pending_guard(); // V8/I33a: mod değişimi pending'i drain eder — serileştir
        // mode > 3 reddedilmeli, mevcut değer değişmemeli
        assert!(rj_set_healing_mode(0));
        assert!(!rj_set_healing_mode(4));
        assert_eq!(rj_get_healing_mode(), 0, "geçersiz mod önceki değeri ezememeli");
        assert!(!rj_set_healing_mode(u32::MAX));
        assert_eq!(rj_get_healing_mode(), 0);
    }

    #[test]
    fn test_next_action_id_monotonic_and_nonzero() {
        // V8/I33: global sayaç monoton artmalı ve asla 0 (sentinel) dönmemeli.
        // fetch_add atomik olduğundan paralel testler yalnız aralığı büyütür,
        // b > a bağıntısını bozmaz (wrap testte imkânsız).
        let a = next_action_id();
        let b = next_action_id();
        let c = next_action_id();
        assert!(a != 0 && b != 0 && c != 0, "ID 0 sentinel'i dağıtılmamalı");
        assert!(b > a, "ID monoton artmalı (b > a)");
        assert!(c > b, "ID monoton artmalı (c > b)");
    }

    #[test]
    fn test_action_event_dequeue_roundtrip_and_null_safe() {
        // V8/I33 (I11): UI event kuyruğu aktüatörden ayrı; enqueue → dequeue
        // aynı event'i döndürmeli, boşta 0, null'da 0.
        rj_start_monitor();
        let src = RjAction { id: 4242, action_type: RjActionType::BitrateReduce, param1: 3500, param2: 0, canary: 0 };
        enqueue_ui_event(RjActionEvent::info_event(&src));

        let mut out = RjActionEvent { id: 0, action_type: RjActionType::LogOnly, param1: 0, param2: 0, require_approval: 9, kind: 9 };
        // Kuyrukta başka testlerin event'i de olabilir (paralel/global state);
        // bizim event'imizi bulana kadar drenaj yap.
        let mut found = false;
        while rj_action_event_dequeue(&mut out as *mut _) == 1 {
            if out.id == 4242 {
                assert_eq!(out.require_approval, 0, "info event onay gerektirmemeli");
                assert_eq!(out.kind, RJ_ACTION_EVENT_NEW);
                assert_eq!(out.param1, 3500);
                found = true;
                break;
            }
        }
        assert!(found, "enqueue edilen UI event'i dequeue ile bulunmalı");

        // Null çıkış güvenli
        assert_eq!(rj_action_event_dequeue(std::ptr::null_mut()), 0, "null out → 0");
    }

    #[test]
    fn test_pending_approve_moves_to_actuator_queue() {
        let _g = pending_guard();
        rj_start_monitor();
        let id = 900_001;
        enqueue_pending(mk_action(id), "rule_x".to_string());

        // Onay: pending'den aktüatör kuyruğuna taşınmalı, 1 dönmeli.
        assert_eq!(rj_action_approve(id), 1, "geçerli pending onaylanınca 1 dönmeli");

        // Aktüatör kuyruğunda görünmeli (paralel testlerin aksiyonları da olabilir).
        let mut out = mk_action(0);
        let mut found = false;
        while rj_action_dequeue(&mut out as *mut _) == 1 {
            if out.id == id { found = true; break; }
        }
        assert!(found, "onaylanan aksiyon aktüatör kuyruğunda olmalı");

        // İkinci onay: artık pending'de yok → 0.
        assert_eq!(rj_action_approve(id), 0, "zaten işlenmiş id yeniden onaylanınca 0");
    }

    #[test]
    fn test_pending_approve_invalid_id_returns_zero() {
        let _g = pending_guard();
        rj_start_monitor();
        // Hiç enqueue edilmemiş id → 0 (yok/süresi dolmuş/zaten işlenmiş).
        assert_eq!(rj_action_approve(900_999), 0);
    }

    #[test]
    fn test_pending_reject_removes_entry() {
        let _g = pending_guard();
        rj_start_monitor();
        let id = 900_002;
        enqueue_pending(mk_action(id), "rule_y".to_string());

        assert_eq!(rj_action_reject(id), 1, "bulunan pending reddedilince 1");
        assert_eq!(rj_action_reject(id), 0, "ikinci reddet → yok → 0");
        // Reddedilen aksiyon uygulanmamalı: approve da 0 dönmeli.
        assert_eq!(rj_action_approve(id), 0, "reddedilen aksiyon onaylanamaz");
    }

    #[test]
    fn test_pending_sweep_expires_and_invalidates() {
        let _g = pending_guard();
        rj_start_monitor();
        let id = 900_003;
        // Doğrudan depoya TTL'i aşmış (backdated) bir entry koy — 30s beklemeden
        // sweep davranışını test etmek için (test aynı modülde, private erişim OK).
        {
            let state = FFI_STATE.get().expect("monitor started");
            let backdated = Instant::now()
                .checked_sub(PENDING_TTL + Duration::from_secs(5))
                .expect("backdate");
            state.pending_actions.lock().unwrap().insert(
                id,
                PendingEntry { action: mk_action(id), rule_id: "rule_z".to_string(), created: backdated },
            );
        }

        sweep_expired_pending();

        // Entry düşmüş olmalı.
        assert!(
            !FFI_STATE.get().unwrap().pending_actions.lock().unwrap().contains_key(&id),
            "TTL dolmuş entry sweep sonrası kalmamalı"
        );
        // UI'a Invalidated event gitmiş olmalı (kuyruğu tarayıp bul).
        let mut out = RjActionEvent { id: 0, action_type: RjActionType::LogOnly, param1: 0, param2: 0, require_approval: 0, kind: 0 };
        let mut found = false;
        while rj_action_event_dequeue(&mut out as *mut _) == 1 {
            if out.id == id && out.kind == RJ_ACTION_EVENT_INVALIDATED { found = true; break; }
        }
        assert!(found, "sweep, TTL dolan aksiyon için Invalidated event üretmeli");
    }

    #[test]
    fn test_mode_change_clears_pending() {
        let _g = pending_guard();
        rj_start_monitor();
        let id = 900_004;
        // CoPilot'a al, pending ekle, sonra farklı moda geç → pending temizlenmeli.
        assert!(rj_set_healing_mode(1)); // CoPilot
        enqueue_pending(mk_action(id), "rule_w".to_string());
        assert!(
            FFI_STATE.get().unwrap().pending_actions.lock().unwrap().contains_key(&id),
            "pending eklendi"
        );

        assert!(rj_set_healing_mode(0)); // AutoPilot — mod değişti → clear
        assert!(
            !FFI_STATE.get().unwrap().pending_actions.lock().unwrap().contains_key(&id),
            "mod değişiminde pending temizlenmeli"
        );
        // Temizlenen aksiyon onaylanamaz.
        assert_eq!(rj_action_approve(id), 0);
    }

    #[test]
    fn test_action_auto_approve_set_and_query() {
        let _g = pending_guard(); // ACTION_AUTO_APPROVE global — serileştir + geri temizle
        assert!(rj_set_action_auto_approve(RJ_ACTION_CAT_BITRATE, true));
        assert!(category_auto_approve(RjActionType::BitrateReduce), "bitrate auto açıldı");
        assert!(category_auto_approve(RjActionType::BitrateRecover), "aynı kategori");
        assert!(!category_auto_approve(RjActionType::ScaleResolution), "resolution hâlâ manuel");

        assert!(rj_set_action_auto_approve(RJ_ACTION_CAT_BITRATE, false));
        assert!(!category_auto_approve(RjActionType::BitrateReduce), "bitrate auto kapandı");

        // LogOnly her zaman otomatik
        assert!(category_auto_approve(RjActionType::LogOnly));
        // Geçersiz kategori reddedilir
        assert!(!rj_set_action_auto_approve(3, true));
        assert!(!rj_set_action_auto_approve(99, true));

        // Temizle — diğer testlerin routing'ini etkilemesin.
        for cat in [RJ_ACTION_CAT_BITRATE, RJ_ACTION_CAT_RESOLUTION, RJ_ACTION_CAT_FPS] {
            let _ = rj_set_action_auto_approve(cat, false);
        }
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

    // ===== Aşama 4: sahne senkronizasyonu =====

    #[test]
    fn test_scene_switch_event_updates_current_idx() {
        rj_start_monitor();
        let state = FFI_STATE.get().expect("FFI_STATE init");

        // Bu testten başka hiçbir yer current_scene_idx'i yazmaz → başlangıç 0.
        assert_eq!(
            state._ws_state.current_scene_idx.load(Ordering::Relaxed),
            0,
            "current_scene_idx başlangıçta 0 olmalı"
        );

        rj_user_event_scene_switch(3);
        assert_eq!(
            state._ws_state.current_scene_idx.load(Ordering::Relaxed),
            3,
            "scene_switch(3) sonrası 3 olmalı"
        );

        rj_user_event_scene_switch(7);
        assert_eq!(
            state._ws_state.current_scene_idx.load(Ordering::Relaxed),
            7,
            "scene_switch(7) sonrası 7 olmalı"
        );
    }

    #[test]
    fn test_push_scene_names_null_does_not_crash() {
        // SECURITY: null pointer → sessizce dön (panik/crash yok)
        rj_start_monitor();
        rj_push_scene_names(std::ptr::null(), 0);
        rj_push_scene_names(std::ptr::null(), 5); // count > 0 ama null ptr → yine güvenli
    }
}
