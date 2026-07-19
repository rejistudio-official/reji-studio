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
use crate::rules::{RuleEngine, Explanation};
use crate::ws_server::{self, WsState};


/// I24: FFI'dan gelen C string okumaları için üst sınırlar (strnlen semantiği).
/// `CStr::from_ptr` NUL'a kadar SINIRSIZ tarar; bu sabitler `cstr_bounded` ile
/// birlikte bozuk/NUL'suz girdide OOB read'i sınırlar.
const MAX_FFI_PATH_LEN: usize = 32 * 1024; // Windows uzun-path (\\?\) tavanı
const MAX_FFI_STR_LEN: usize = 4096;       // reason / parola gibi kısa alanlar

/// I24: Sınırlı uzunlukta C string okuma. `CStr::from_ptr` NUL'a kadar SINIRSIZ
/// tarar → bozuk/NUL'suz pointer OOB read'e yol açabilir. Bu yardımcı en fazla
/// `max_len` byte tarar: NUL bulunursa lossy-UTF8 `String` döner; `max_len`
/// içinde NUL yoksa `None` (güvenli reddet, panik yok). `ptr` null → `None`.
///
/// # Safety
/// Çağıran `ptr`'ın ya null ya da bir C string bölgesinin başına işaret ettiğini
/// garanti etmeli. Tarama `max_len` ile sınırlı olduğundan NUL'suz bir string
/// bile en fazla `max_len` byte okutur (sınırsız tarama yok).
unsafe fn cstr_bounded(ptr: *const c_char, max_len: usize) -> Option<String> {
    if ptr.is_null() {
        return None;
    }
    let bytes = ptr as *const u8;
    let mut len = 0usize;
    while len < max_len {
        if *bytes.add(len) == 0 {
            let slice = std::slice::from_raw_parts(bytes, len);
            return Some(String::from_utf8_lossy(slice).into_owned());
        }
        len += 1;
    }
    None // max_len içinde NUL bulunamadı → güvenli reddet
}

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

/// Özellik#1: Aksiyonu tetikleyen metrik kimliği — `rules::metric_id` sabitleriyle
/// AYNI sayısal kodlama (tek doğruluk kaynağı `rules.rs`; bu enum onu FFI'ya
/// yansıtır). C++ tarafı bu id'yi insan-okunur ada (`tr("GPU Sıcaklığı")`) çevirir.
/// `MetricNone` = açıklanamayan aksiyon (LogOnly / koşul parse edilemedi) → UI
/// açıklama satırını atlar. `#[repr(u32)]` (E1: repr(C) enum yasak). Sentinel adı
/// `MetricNone` — unscoped C++ enum'da (`enum_class=false`) çıplak `None` olası
/// makro çakışmasına karşı.
#[repr(u32)]
#[derive(Copy, Clone, Debug, PartialEq, Eq)]
pub enum RjMetricId {
    FrameDropPct = 0,
    GpuTempC = 1,
    CpuTempC = 2,
    MemoryUsagePct = 3,
    CpuLoadPct = 4,
    GpuLoadPct = 5,
    NetworkRttMs = 6,
    NetworkLossPct = 7,
    MetricNone = 8,
}

impl RjMetricId {
    /// `rules::metric_id` u32 kodunu FFI enum'una çevirir. Değerler birebir
    /// eşleştiğinden bilinmeyen/aralık-dışı → `MetricNone` (savunmacı).
    fn from_raw(v: u32) -> Self {
        use crate::rules::metric_id as m;
        match v {
            m::FRAME_DROP_PCT   => RjMetricId::FrameDropPct,
            m::GPU_TEMP_C       => RjMetricId::GpuTempC,
            m::CPU_TEMP_C       => RjMetricId::CpuTempC,
            m::MEMORY_USAGE_PCT => RjMetricId::MemoryUsagePct,
            m::CPU_LOAD_PCT     => RjMetricId::CpuLoadPct,
            m::GPU_LOAD_PCT     => RjMetricId::GpuLoadPct,
            m::NETWORK_RTT_MS   => RjMetricId::NetworkRttMs,
            m::NETWORK_LOSS_PCT => RjMetricId::NetworkLossPct,
            _                   => RjMetricId::MetricNone,
        }
    }
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
    /// Özellik#1: aksiyonu tetikleyen metrik/değer/eşik açıklaması. Struct'ın
    /// SONUNA eklendi (ABI: 24B→36B, `RJ_FFI_VERSION` yükseltildi). `metric_id ==
    /// MetricNone` iken `current_value`/`threshold_value` anlamsız (0) — UI açıklama
    /// satırını atlar. Invalidated event'lerde her zaman `MetricNone` (UI yalnız
    /// `id` ile temizlik yapar, gösterilecek yeni bilgi yok).
    pub metric_id: RjMetricId,
    pub current_value: i32,
    pub threshold_value: i32,
    /// Özellik#5: `threshold_value` çalışma-zamanı kalibrasyonundan mı geliyor?
    /// 0 = statik (`rules.json`), 1 = kalibre. UI "[kalibre]" etiketi gösterir —
    /// kullanıcı statik `rules.json` değeriyle dinamik eşiği karıştırmasın
    /// (Özellik#1 güven amacı). Struct'ın SONUNA eklendi (ABI: 36B→40B,
    /// `RJ_FFI_VERSION` 2.0→3.0). `metric_id == MetricNone` iken anlamsız (0).
    pub calibrated: u32,
}
const _: () = assert!(core::mem::size_of::<RjActionEvent>() == 40);

/// UI event türleri — `RjActionEvent::kind`. C++ tarafı `ffi_bridge.h`'de eşler.
pub(crate) const RJ_ACTION_EVENT_NEW: u32 = 0;
pub(crate) const RJ_ACTION_EVENT_INVALIDATED: u32 = 1;

impl RjActionEvent {
    /// Info event: otomatik uygulanan aksiyon için (require_approval=false, New).
    /// Özellik#1: `expl` üçlüsü UI açıklaması için taşınır.
    pub(crate) fn info_event(a: &RjAction, expl: &Explanation) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 0,
            kind: RJ_ACTION_EVENT_NEW,
            metric_id: RjMetricId::from_raw(expl.metric_id),
            current_value: expl.current_value,
            threshold_value: expl.threshold_value,
            calibrated: expl.calibrated as u32,
        }
    }

    /// Approval event: CoPilot'ta onay bekleyen aksiyon (require_approval=true, New).
    /// Özellik#1: `expl` üçlüsü UI açıklaması için taşınır.
    pub(crate) fn approval_event(a: &RjAction, expl: &Explanation) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 1,
            kind: RJ_ACTION_EVENT_NEW,
            metric_id: RjMetricId::from_raw(expl.metric_id),
            current_value: expl.current_value,
            threshold_value: expl.threshold_value,
            calibrated: expl.calibrated as u32,
        }
    }

    /// Invalidated event: pending aksiyon TTL doldu ya da mod değişti — UI bunu
    /// (checkbox/banner) temizlesin. `require_approval` anlamsız (0). Özellik#1:
    /// açıklama taşınmaz (`MetricNone`) — UI yalnız `id` ile temizlik yapar,
    /// gösterilecek yeni bilgi yok (PendingEntry'ye üçlü koymaya gerek kalmadı).
    pub(crate) fn invalidated_event(a: &RjAction) -> Self {
        RjActionEvent {
            id: a.id,
            action_type: a.action_type,
            param1: a.param1,
            param2: a.param2,
            require_approval: 0,
            kind: RJ_ACTION_EVENT_INVALIDATED,
            metric_id: RjMetricId::MetricNone,
            current_value: 0,
            threshold_value: 0,
            calibrated: 0,
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
    rule_engine:       Arc<Mutex<Option<RuleEngine>>>, // v0.4+ Hot-reload
    _ws_state:         Arc<WsState>,                   // WebSocket sunucu durumu
    // V8/I15: metrik JSON formatlama/broadcast hot-path'ten (rj_metrics_push) 16ms
    // drainer task'ına taşındı; Sender drainer + WsState.evt_rx içinde yaşar.
    // Özellik#2: healing VendorEvent'lerini AYNI broadcast kanalına yaymak için
    // Sender'ın bir clone'u burada tutulur (enqueue_ui_event / rj_action_reject /
    // rj_set_healing_mode kullanır). Metrik ile aynı kanal — yeni kanal icat edilmez.
    ws_evt_tx:         broadcast::Sender<String>,
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
/// resolution/fps) eşleşir. (V8/I34: eski inert `chk_source_auto` UI'dan
/// kaldırıldı — karşılık gelen source-switch aksiyon tipi yok.)
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

/// Bir video `MetricSample`'ından türetilen sistem-metrik event'leri (saf, test
/// edilebilir). Metric drainer bu listeyi `event_bus.system`'e iletir; ayrıca
/// kare-kaybı ayrı bir `MediaEvent` olduğundan drainer'da inline kalır.
///
/// Özellik#5 (plumbing): `MemUsage` eskiden HİÇ yayılmıyordu — `memory_usage_pct`
/// C++ tarafında gerçek (`GlobalMemoryStatusEx`) doldurulmasına rağmen drainer onu
/// SystemEvent'e çevirmiyordu, dolayısıyla `RuleMetrics.memory_usage_pct` hep 0
/// kalıp `memory_pressure` kuralı ölü kalıyordu. Artık yayılıyor.
///
/// "0 = veri yok" konvansiyonu (Healing Plumbing dersi): `gpu_load`/`memory`/
/// `network` yalnız 0-dışıyken yayılır (0, sahte "her şey yolunda" sinyali
/// üretmesin). `CpuUsage` tarihsel olarak koşulsuz yayılır — mevcut davranış korunur.
fn system_events_for_sample(sample: &MetricSample) -> Vec<SystemEvent> {
    let mut events = Vec::new();

    // cpu_load_pct: C++ PDH sistem yükü (offset +44); cpu_percent (offset +24)
    // burada OKUNMAZ — o alan MetricState/UI snapshot yoluna (rj_metrics_poll)
    // aittir. RuleEngine cpu_load_high kuralını cpu_load_pct üzerinden çalıştırır.
    events.push(SystemEvent::CpuUsage {
        ratio: sample.cpu_load_pct as f32 / 100.0,
    });
    if sample.gpu_load_pct > 0 {
        events.push(SystemEvent::GpuUsage {
            ratio: sample.gpu_load_pct as f32 / 100.0,
        });
    }
    if sample.memory_usage_pct > 0 {
        events.push(SystemEvent::MemUsage {
            ratio: sample.memory_usage_pct as f32 / 100.0,
        });
    }
    if sample.network_rtt_ms > 0 || sample.network_loss_pct > 0 {
        events.push(SystemEvent::NetworkStats {
            rtt_ms: sample.network_rtt_ms as u32,
            loss_pct: sample.network_loss_pct as f32,
        });
    }

    events
}

/// TALIMAT_FRAME_DROP_PLUMBING: `MetricSample`'dan medya event'lerini türetir
/// (saf/test edilebilir — `system_events_for_sample`'ın mirror'ı).
///
/// KRİTİK FARK (memory'nin `> 0` guard'ından): `frame_drop_pct` KOŞULSUZ
/// yayılır — %0 dahil. `frame_drop_pct` `u32` ve C++ kontratınca `[0,100]`
/// (canary `is_valid()` bütünlüğü drainer'da zaten doğrulanmış), dolayısıyla
/// filtrelenecek "geçersiz" değer yok; %0 gerçek/arzu edilen durum ve
/// `frame_drop_recovery` kuralı (`< 5`) 0'da tetiklenmeli. Clamp tüketici
/// sınırında (`handle_media`) yapılır.
///
/// Ham `frame_drops` sayacı ise yalnız `> 0` iken yayılır (mevcut davranış korunur):
/// `FrameDropped` ile `FrameDropPct` AYRI event'ler — gate paylaşmazlar.
fn media_events_for_sample(sample: &MetricSample) -> Vec<MediaEvent> {
    let mut events = Vec::new();
    events.push(MediaEvent::FrameDropPct {
        pct: sample.frame_drop_pct,
    });
    if sample.frame_drops > 0 {
        events.push(MediaEvent::FrameDropped {
            count: sample.frame_drops,
        });
    }
    events
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

        // Özellik#3: healing-log yazma thread'ini başlat (ayrı std::thread, DB
        // bağlantısını sahiplenir). Fan-out noktaları `healing_log::log_healing`
        // ile yalnız lock-free kuyruğa push eder — hot-path/frame-thread bloklanmaz.
        crate::healing_log::start_writer(crate::paths::db_path("healing_log.sqlite"));

        // V8/I15: WS metrik broadcast kanalı — drainer'a taşınan JSON/broadcast
        // işi için drainer spawn'ından ÖNCE oluşturulur. Eskiden WsState kendi
        // kanalını kurup FfiState.ws_evt_tx onu klonluyordu; artık TEK Sender
        // hem drainer (üretici) hem WsState.evt_rx (tüketici abonelikleri) tarafından
        // paylaşılır — iki ayrı kanal yok.
        let ws_evt_tx = broadcast::channel::<String>(64).0;

        // Metric drainer: 16ms periyotla ring buffer'ı boşaltıp EventBus'a iletir
        // ve (V8/I15) WS istemcilerine metrik JSON'unu broadcast eder.
        {
            let ring       = metric_ring.clone();
            let bus_system = event_bus.system.clone();
            let bus_media  = event_bus.media.clone();
            let state      = metric_state.clone();
            let evt_tx     = ws_evt_tx.clone();  // V8/I15: broadcast üretici ucu
            runtime.spawn(async move {
                let mut ticker = tokio::time::interval(Duration::from_millis(16));
                ticker.set_missed_tick_behavior(MissedTickBehavior::Skip);
                // V8/I15: task-yerel reuse buffer — hot-path'teki Mutex<String>
                // yerine (drainer tek task olduğu için kilit gerekmez).
                let mut json_buf = String::with_capacity(128);
                loop {
                    ticker.tick().await;
                    while let Some(sample) = ring.pop() {
                        if !sample.is_valid() {
                            warn!("MetricSample canary hatasi, atlanıyor");
                            continue;
                        }
                        state.update(&sample);
                        // V8/I15: eskiden rj_metrics_push içinde inline yapılan JSON
                        // formatlama + broadcast — çıktı formatı birebir korundu.
                        {
                            json_buf.clear();
                            use std::fmt::Write as _;
                            let _ = write!(json_buf,
                                r#"{{"fps":{:.1},"kbps":{},"drop":{},"cpu":{},"gpu":{},"mem":{}}}"#,
                                sample.fps_actual, sample.bitrate_kbps, sample.frame_drops,
                                sample.cpu_load_pct, sample.gpu_load_pct, sample.memory_usage_pct
                            );
                            let _ = evt_tx.send(json_buf.clone());
                        }
                        // Özellik#5: sistem-metrik event türetimi saf fonksiyonda
                        // (test edilebilir); `MemUsage` artık burada yayılıyor.
                        for evt in system_events_for_sample(&sample) {
                            let _ = bus_system.send(evt);
                        }
                        // TALIMAT_FRAME_DROP_PLUMBING: medya event türetimi saf
                        // fonksiyonda (test edilebilir). `FrameDropPct` artık burada
                        // KOŞULSUZ yayılıyor (eskiden hiç yayılmıyordu → üç
                        // frame_drop kuralı current_metrics'e erişemiyordu).
                        for evt in media_events_for_sample(&sample) {
                            let _ = bus_media.send(evt);
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
            evt_rx: ws_evt_tx.clone(),  // V8/I15: drainer ile AYNI broadcast kanalı
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
            // Spawn öncesi log — I21: hardcoded path yerine ws_server::log_to_file
            // (taşınabilir %LOCALAPPDATA%\reji-studio\ws_debug.log, DRY).
            ws_server::log_to_file("[FFI] rj_start_monitor_impl: spawning WS server task");
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
            rule_engine,
            _ws_state: ws_state,
            // Özellik#2: drainer/WsState ile AYNI broadcast kanalı (metrik ile ortak).
            ws_evt_tx,
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
        // SAFETY: C++ tarafı geçerli RjMetricSample* geçirir; layout MetricSample
        // ile özdeş. V8/I15: `read_unaligned` — hizasız pointer'da da UB'siz okuma
        // (hizalıyken `*sample` ile bit-aynı değeri üretir, davranış değişmez).
        let s = unsafe { core::ptr::read_unaligned(sample) };
        if !s.is_valid() {
            return;
        }
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_metrics_push ignored");
            return;
        };
        // V8/I15: hot-path yalnız non-blocking ring push. JSON formatlama +
        // broadcast (eskiden burada inline: Mutex + write! + clone + send) 16ms
        // drainer task'ına taşındı — hot-path'te heap alloc/kilit kalmadı.
        let _ = state.metric_ring.push(s);
    }))
    .map_err(|_| {
        eprintln!("[PANIC] rj_metrics_push caught panic");
    });
}

/// V8/I14: UI'ın anlık metrik çekmesi (pull) için — push'un beslediği AYNI
/// `MetricState` snapshot'ını `out`'a yazar. Bloklamaz (atomik okuma), WMI
/// tetiklemez (AGENTS.md hot-path kuralı). `rj_metrics_push`/drainer ile
/// yarışmaz: geçici `metric_ring`'ten değil, agregeli `MetricState`'ten okur.
///
/// # Safety
/// `out`, geçerli tek `RjMetricSample` (== `MetricSample`) için yazılabilir
/// bellek göstermeli. Null güvenli (0 döner).
///
/// # Return
/// 1 → `out` dolduruldu (snapshot yazıldı). 0 → yazılmadı (null `out` veya
/// FFI_STATE henüz init değil). C++ tarafı 0'da UI güncellemesini atlar.
#[no_mangle]
pub extern "C" fn rj_metrics_poll(out: *mut MetricSample) -> i32 {
    catch_unwind(AssertUnwindSafe(move || {
        if out.is_null() {
            return 0;
        }
        let Some(state) = FFI_STATE.get() else {
            return 0;
        };
        let sample = state._metric_state.snapshot();
        // SAFETY: caller `out`'un geçerli/yazılabilir RjMetricSample* olduğunu
        // garanti eder; layout MetricSample ile birebir (#[repr(C)], 64 byte).
        unsafe { *out = sample; }
        1
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_metrics_poll caught panic");
        0
    })
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
        // I24: sınırlı okuma — NUL'suz/aşırı uzun reason OOB read yerine güvenli
        // reddedilir. UTF-8 geçersiz baytlar U+FFFD'ye map'lenir (lossy).
        let msg = unsafe { cstr_bounded(reason, MAX_FFI_STR_LEN) }
            .unwrap_or_else(|| "<null-veya-gecersiz>".to_owned());
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

/// O anki aktif WebSocket bağlantı sayısını döndürür (anlık durum; kalıcı değil).
/// C++ Settings dialog salt-okunur gösterim için çağırır. Sayaç FFI_STATE'ten
/// bağımsızdır (ws_server statik atomic'i) — init'ten önce de güvenle 0 döner.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_get_ws_connection_count() -> u32 {
    catch_unwind(AssertUnwindSafe(|| {
        crate::ws_server::active_connections()
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

/// Özellik#2: RjActionType → obs vendor eventData'da taşınan kararlı ad.
/// `healing.rs::convert_action_type` deseniyle mekanik; WS teli için stabil
/// string kimlik (Stream Deck/Companion tarafı bu ada göre ayrım yapabilir).
fn action_type_name(t: RjActionType) -> &'static str {
    match t {
        RjActionType::BitrateReduce     => "BitrateReduce",
        RjActionType::BitrateRecover    => "BitrateRecover",
        RjActionType::ScaleResolution   => "ScaleResolution",
        RjActionType::RestoreResolution => "RestoreResolution",
        RjActionType::CapFps            => "CapFps",
        RjActionType::RestoreFps        => "RestoreFps",
        RjActionType::LogOnly           => "LogOnly",
    }
}

/// Özellik#2: RjMetricId → obs vendor eventData ad. `MetricNone` → `None`
/// (açıklama taşınmaz; metric/value/threshold alanları atlanır).
fn metric_id_name(m: RjMetricId) -> Option<&'static str> {
    match m {
        RjMetricId::FrameDropPct   => Some("FrameDropPct"),
        RjMetricId::GpuTempC       => Some("GpuTempC"),
        RjMetricId::CpuTempC       => Some("CpuTempC"),
        RjMetricId::MemoryUsagePct => Some("MemoryUsagePct"),
        RjMetricId::CpuLoadPct     => Some("CpuLoadPct"),
        RjMetricId::GpuLoadPct     => Some("GpuLoadPct"),
        RjMetricId::NetworkRttMs   => Some("NetworkRttMs"),
        RjMetricId::NetworkLossPct => Some("NetworkLossPct"),
        RjMetricId::MetricNone     => None,
    }
}

/// Özellik#2: 0..=3 mod kodunu rules.json şablonundaki tireli ada çevirir
/// (`healing.rs`'deki `mode_str` ile birebir). Aralık dışı → "co-pilot"
/// (savunmacı; `rj_set_healing_mode` zaten >3'ü reddeder).
fn healing_mode_name(mode: u32) -> &'static str {
    match mode {
        0 => "auto-pilot",
        1 => "co-pilot",
        2 => "assist",
        3 => "manual",
        _ => "co-pilot",
    }
}

/// Özellik#2: Bir `RjActionEvent`'i obs-websocket VendorEvent olarak WS broadcast
/// kanalına yayar (JSON + msgpack teli — op 5 taşıdığından ikisi de alır).
/// Aynı `ws_evt_tx` kanalı metrikle ortak. Abonesi yoksa `send` Err döner, yutulur.
///
/// eventType eşlemesi (mevcut kind/require_approval alanlarından türetilir, yeni
/// alan/mimari yok): Invalidated → `HealingActionInvalidated`; New+onay →
/// `HealingActionPending`; New+otomatik → `HealingActionApplied`. Böylece
/// enqueue_ui_event'ten geçen üç event tipi (info/approval/invalidated) tek
/// noktadan yayınlanır. `rj_action_reject` de bu yolu (Invalidated) kullanır.
fn emit_healing_event(ev: &RjActionEvent) {
    let Some(state) = FFI_STATE.get() else { return; };
    send_vendor_event(state, &healing_vendor_event(ev), "healing VendorEvent");
}

/// Özellik#2 (saf, test edilebilir): `RjActionEvent`'i obs-websocket VendorEvent
/// zarfına eşler — global state / kanal dokunmaz. `emit_healing_event` bunu kurup
/// yayar. eventType eşlemesi: Invalidated → `HealingActionInvalidated`; New+onay →
/// `HealingActionPending`; New+otomatik → `HealingActionApplied`.
fn healing_vendor_event(ev: &RjActionEvent) -> crate::obs_protocol::WsEnvelope {
    if ev.kind == RJ_ACTION_EVENT_INVALIDATED {
        // Invalidated: dinleyicinin tek ihtiyacı "bu id'yi UI'dan kaldır".
        return crate::obs_protocol::vendor_event(
            "HealingActionInvalidated",
            serde_json::json!({
                "id": ev.id,
                "action": action_type_name(ev.action_type),
            }),
        );
    }
    let vendor_type = if ev.require_approval == 1 {
        "HealingActionPending"
    } else {
        "HealingActionApplied"
    };
    let mut data = serde_json::json!({
        "id": ev.id,
        "action": action_type_name(ev.action_type),
        "param1": ev.param1,
        "param2": ev.param2,
        "requireApproval": ev.require_approval == 1,
    });
    // Özellik#1 açıklaması yalnız anlamlıysa (MetricNone değil) taşınır.
    if let Some(metric) = metric_id_name(ev.metric_id) {
        data["metric"] = serde_json::json!(metric);
        data["value"] = serde_json::json!(ev.current_value);
        data["threshold"] = serde_json::json!(ev.threshold_value);
        // Özellik#5: eşiğin kalibre mi statik mi olduğunu WS istemcisi de bilsin
        // (web UI "[kalibre]" gösterebilsin — native UI ile tutarlı).
        data["calibrated"] = serde_json::json!(ev.calibrated == 1);
    }
    crate::obs_protocol::vendor_event(vendor_type, data)
}

/// Özellik#2: Healing mod değişimini obs VendorEvent (`HealingModeChanged`) olarak
/// WS broadcast kanalına yayar. `rj_set_healing_mode` yalnız GERÇEK değişimde çağırır.
fn emit_mode_changed(mode: u32) {
    let Some(state) = FFI_STATE.get() else { return; };
    send_vendor_event(state, &mode_changed_vendor_event(mode), "HealingModeChanged");
}

/// Özellik#2 (saf, test edilebilir): mod değişimi VendorEvent zarfını kurar.
fn mode_changed_vendor_event(mode: u32) -> crate::obs_protocol::WsEnvelope {
    crate::obs_protocol::vendor_event(
        "HealingModeChanged",
        serde_json::json!({ "mode": mode, "modeName": healing_mode_name(mode) }),
    )
}

/// Özellik#2: Zarfı JSON'a kodlayıp WS broadcast kanalına (metrikle ortak
/// `ws_evt_tx`) gönderir. Abonesi yoksa `send` Err döner, yutulur (metrik deseni).
/// op 5 taşıdığından hem JSON hem msgpack teline ulaşır.
fn send_vendor_event(state: &FfiState, env: &crate::obs_protocol::WsEnvelope, what: &str) {
    match serde_json::to_string(env) {
        Ok(s) => { let _ = state.ws_evt_tx.send(s); }
        Err(e) => eprintln!("[WS] {} kodlanamadı: {}", what, e),
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
    // Özellik#2: C++ UI kuyruğuyla AYNI event'i WS'e de yay (VendorEvent). Fan-out;
    // force_push event'i taşıdığından ondan ÖNCE (referansla) yayınlanır.
    emit_healing_event(&event);
    if let Some(evicted) = state.ui_event_queue.force_push(event) {
        let total = DROPPED_UI_EVENTS_COUNT.fetch_add(1, Ordering::Relaxed) + 1;
        eprintln!(
            "[UiEventQueue] FULL — oldest event dropped: {:?}  (total dropped: {})",
            evicted, total
        );
    }
}

/// Özellik#3: Açıklamasız outcome'ları (approve/reject/invalidated anları — o an
/// yalnız `RjAction` bilinir, üçlü açıklama yok) healing-log'a yazar. `metric_id`
/// None(8), değerler 0; `rule_id`/`mode` çağrandan. Yalnız lock-free kuyruğa
/// non-blocking push — disk/kilit yok, çağıran thread (tokio tick veya C++ frame
/// thread) asla bloklanmaz.
fn log_action_outcome(a: &RjAction, rule_id: &str, outcome: u32) {
    crate::healing_log::log_healing(crate::healing_log::HealingLogRecord::now(
        a.id,
        rule_id.to_string(),
        crate::rules::metric_id::NONE,
        0,
        0,
        a.action_type as u32,
        outcome,
        HEALING_MODE.load(Ordering::Relaxed),
    ));
}

/// V8/I33a: CoPilot'ta onay bekleyen aksiyonu depola + UI'a approval event
/// gönder. Pending deposu kaynak-of-truth; UI event'i yalnız bildirim (kaybı
/// TTL/re-üretim ile telafi edilir).
pub fn enqueue_pending(action: RjAction, rule_id: String, explanation: Explanation) {
    let Some(state) = FFI_STATE.get() else {
        eprintln!("[FFI] WARNING: FFI_STATE not initialized — enqueue_pending ignored");
        return;
    };
    let event = RjActionEvent::approval_event(&action, &explanation);
    // Özellik#3: pending outcome — tam üçlü (metric/value/threshold) mevcut.
    crate::healing_log::log_healing(crate::healing_log::HealingLogRecord::now(
        action.id,
        rule_id.clone(),
        explanation.metric_id,
        explanation.current_value,
        explanation.threshold_value,
        action.action_type as u32,
        crate::healing_log::OUTCOME_PENDING,
        HEALING_MODE.load(Ordering::Relaxed),
    ));
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
    let expired: Vec<(RjAction, String)> = {
        let mut pending = state.pending_actions.lock().unwrap();
        let ids: Vec<u32> = pending
            .iter()
            .filter(|(_, e)| now.duration_since(e.created) >= PENDING_TTL)
            .map(|(id, _)| *id)
            .collect();
        ids.into_iter()
            .filter_map(|id| pending.remove(&id).map(|e| (e.action, e.rule_id)))
            .collect()
    };
    for (action, rule_id) in expired {
        eprintln!("[Pending] TTL doldu, aksiyon iptal edildi: id={}", action.id);
        enqueue_ui_event(RjActionEvent::invalidated_event(&action));
        // Özellik#3: TTL geçersizleşmesini healing-log'a yaz.
        log_action_outcome(&action, &rule_id, crate::healing_log::OUTCOME_INVALIDATED);
    }
}

/// V8/I33a: Mod değişiminde tüm pending aksiyonları temizle (otomatik
/// uygulanmaz) + UI'a Invalidated bildir. Gerekçe: bayat bir pending'i mod
/// değişimi anında patlatmak sürpriz yan etki; koşul hâlâ geçerliyse bir
/// sonraki tick zaten yeniden üretir.
fn clear_pending_on_mode_change() {
    let Some(state) = FFI_STATE.get() else { return; };
    let drained: Vec<(RjAction, String)> = {
        let mut pending = state.pending_actions.lock().unwrap();
        pending.drain().map(|(_, e)| (e.action, e.rule_id)).collect()
    };
    for (action, rule_id) in drained {
        enqueue_ui_event(RjActionEvent::invalidated_event(&action));
        // Özellik#3: mod değişimiyle geçersizleşmeyi healing-log'a yaz.
        log_action_outcome(&action, &rule_id, crate::healing_log::OUTCOME_INVALIDATED);
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
            Ok(_) => {
                // Özellik#3: onaylanıp aktüatöre giden aksiyonu applied olarak yaz.
                log_action_outcome(&entry.action, &entry.rule_id, crate::healing_log::OUTCOME_APPLIED);
                1
            }
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
                // Özellik#2: WS dinleyicisine (Stream Deck/Companion) bu pending'in
                // artık geçerli olmadığını bildir. Reddetme ile TTL/mod-değişimi
                // sebebi farklı olsa da dinleyicinin tepkisi aynı ("UI'dan kaldır"),
                // bu yüzden ayrı `HealingActionRejected` tipi İCAT EDİLMEZ — mevcut
                // Invalidated yeniden kullanılır. Yalnız WS'e yayılır; C++ UI zaten
                // reddi kendi başlattı, ui_event_queue'ya itilmez (çift işleme yok).
                emit_healing_event(&RjActionEvent::invalidated_event(&entry.action));
                // Özellik#3: reddedilen aksiyonu healing-log'a yaz.
                log_action_outcome(&entry.action, &entry.rule_id, crate::healing_log::OUTCOME_REJECTED);
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
            // Özellik#2: mod değişimini WS'e yay (VendorEvent HealingModeChanged).
            // Yalnız gerçek değişimde — startup senkronu / no-op set spam üretmez.
            emit_mode_changed(mode);
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
        // null/boş → None (auth kapalı). I24: sınırlı okuma; aşırı uzun
        // (>MAX_FFI_STR_LEN) veya NUL'suz girdi de None döner (auth kapalı kalır,
        // panik yok). Parola trusted C++ Settings'ten gelir; bu bir savunma katmanı.
        let pw: Option<String> = unsafe { cstr_bounded(password, MAX_FFI_STR_LEN) }
            .filter(|s| !s.is_empty());
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
            // I24: sınırlı okuma — aşırı uzun (>MAX_FFI_PATH_LEN) veya NUL'suz
            // path OOB read yerine güvenli reddedilir (reload başarısız = 0).
            // Dizin-kısıtı EKLENMEDİ: rj_reload_rules'ın üretimde C++ çağrısı yok,
            // kısıt spekülatif olurdu (bkz. TALIMAT_SPRINT3_GRUPB I24-b kararı).
            match unsafe { cstr_bounded(path, MAX_FFI_PATH_LEN) } {
                Some(s) => PathBuf::from(s),
                None => {
                    warn!("rj_reload_rules: geçersiz/aşırı uzun path (>{}B) — reddedildi", MAX_FFI_PATH_LEN);
                    return 0;
                }
            }
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

/// Görünürlük (salt-okunur): motorun BELLEK-İÇİ kural listesini JSON dizisi olarak
/// caller'ın verdiği `buf`'a (kapasite `cap` byte) NUL-terminated yazar. GUI "Kurallar"
/// sekmesi bunu okuyup `QJsonDocument` ile parse eder — dosyayı değil aktif kuralları
/// gösterir (hot_reload rollback'inde disk ≠ bellek olabilir; "kör kutu" derdi motorun
/// gerçeğini görmek).
///
/// Dönüş:
///   `>= 0` — yazılan JSON gövdesinin byte uzunluğu (NUL hariç).
///   `-1`   — `buf` null / `cap <= 0` / FFI_STATE init değil / `cap` yetersiz.
///
/// String EGRESS (Rust üretici, C++ parse eder): `ffi-safety-review`'in "string-sınırda-
/// reddet" ilkesi güvenilmez INGRESS içindi; burada ABI'ye yeni struct/enum EKLENMEZ
/// (`metrics.rs` dokunulmaz, `static_assert`/sizeof gerekmez). `cap` yetersizse KIRPMAZ
/// (kırpılmış JSON C++'ta parse edilemezdi) — güvenli `-1` döner. `rj_reload_rules`'un
/// panik/poison desenini izler.
/// SECURITY: Wrapped in catch_unwind to prevent panic unwind into C++
#[no_mangle]
pub extern "C" fn rj_rules_snapshot_json(buf: *mut c_char, cap: i32) -> i32 {
    catch_unwind(AssertUnwindSafe(move || {
        if buf.is_null() || cap <= 0 {
            return -1;
        }
        let Some(state) = FFI_STATE.get() else {
            eprintln!("[FFI] WARNING: FFI_STATE not initialized — rj_rules_snapshot_json ignored");
            return -1;
        };

        // Kısa kilit: JSON'u üret, kilidi bırak, sonra kopyala.
        let json = {
            let engine_lock = state.rule_engine
                .lock()
                .unwrap_or_else(|poisoned| {
                    warn!("rule_engine mutex poison — recovering");
                    poisoned.into_inner()
                });
            match engine_lock.as_ref() {
                Some(engine) => engine.snapshot_json(),
                None => "[]".to_string(),
            }
        };

        let bytes = json.as_bytes();
        let cap = cap as usize;
        // NUL için 1 byte ayrılır; gövde `cap - 1`'e sığmalı.
        if bytes.len() + 1 > cap {
            warn!(needed = bytes.len() + 1, cap, "rj_rules_snapshot_json: buffer küçük — reddedildi");
            return -1;
        }
        // SAFETY: `buf` en az `cap` byte (C++ sözleşmesi); `bytes.len()+1 <= cap` doğrulandı.
        unsafe {
            std::ptr::copy_nonoverlapping(bytes.as_ptr(), buf as *mut u8, bytes.len());
            *buf.add(bytes.len()) = 0; // NUL terminator
        }
        bytes.len() as i32
    }))
    .unwrap_or_else(|_| {
        eprintln!("[PANIC] rj_rules_snapshot_json caught panic");
        -1
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
            // J1/I24: `CStr::from_ptr` NUL'a kadar SINIRSIZ tarar → bozuk/NUL'suz
            // C++ girdisinde OOB read. `cstr_bounded` en fazla MAX_NAME_LEN byte
            // tarar; NUL bulamazsa None döner (güvenli reddet). Eski kod önce
            // sınırsız tarayıp sonucu kırpıyordu — kırpma taramayı sınırlamıyordu.
            // from_utf8_lossy kullanıldığından char-sınırı paniği riski de yok.
            // SAFETY: ptr geçerli C string bölgesinin başına işaret ediyor (C++
            // QByteArray::constData); tarama MAX_NAME_LEN ile sınırlı.
            let s = match unsafe { cstr_bounded(ptr, MAX_NAME_LEN) } {
                Some(s) => s,
                None => continue, // NUL 256 byte içinde yok → güvenli atla
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

    // Görünürlük: rj_rules_snapshot_json guard'ları — null buf / cap<=0, FFI_STATE'e
    // dokunmadan kısa devre eder (süreç-global state'e bağımlı değil, deterministik).
    // Snapshot içeriği rules::snapshot_json birim testlerinde doğrulanır.
    #[test]
    fn snapshot_json_null_buffer_returns_error() {
        assert_eq!(rj_rules_snapshot_json(std::ptr::null_mut(), 4096), -1);
    }

    #[test]
    fn snapshot_json_nonpositive_cap_returns_error() {
        let mut buf = [0i8; 8];
        assert_eq!(rj_rules_snapshot_json(buf.as_mut_ptr(), 0), -1);
        assert_eq!(rj_rules_snapshot_json(buf.as_mut_ptr(), -1), -1);
    }

    #[test]
    fn test_push_null_does_not_crash() {
        rj_metrics_push(std::ptr::null());
    }

    // --- Özellik#5: sistem-metrik event türetimi (plumbing) ---

    fn sample_for_events() -> MetricSample {
        MetricSample {
            magic_head:       MetricSample::MAGIC,
            timestamp_us:     0,
            bitrate_kbps:     6000,
            fps_actual:       60.0,
            cpu_percent:      40.0,
            frame_drops:      0,
            frame_drop_pct:   0,
            gpu_temp_c:       0,
            cpu_temp_c:       0,
            memory_usage_pct: 0,
            cpu_load_pct:     0,
            gpu_load_pct:     0,
            network_rtt_ms:   0,
            network_loss_pct: 0,
            source_id:        0,
            magic_tail:       MetricSample::MAGIC,
        }
    }

    #[test]
    fn mem_usage_event_emitted_when_real() {
        // Özellik#5 plumbing: gerçek memory_usage_pct → MemUsage yayılmalı
        // (eskiden hiç yayılmıyordu → memory_pressure kuralı ölüydü).
        let mut s = sample_for_events();
        s.memory_usage_pct = 82;
        let events = system_events_for_sample(&s);
        let ratio = events.iter().find_map(|e| match e {
            SystemEvent::MemUsage { ratio } => Some(*ratio),
            _ => None,
        });
        assert!(ratio.is_some(), "gerçek bellek metriğinde MemUsage yayılmalı");
        assert!((ratio.unwrap() - 0.82).abs() < 0.001, "ratio = pct/100");
    }

    #[test]
    fn mem_usage_event_skipped_when_zero() {
        // "0 = veri yok" (Healing Plumbing dersi) → MemUsage yayılmaz.
        let mut s = sample_for_events();
        s.memory_usage_pct = 0;
        let events = system_events_for_sample(&s);
        assert!(
            !events.iter().any(|e| matches!(e, SystemEvent::MemUsage { .. })),
            "sıfır bellek metriği sahte MemUsage üretmemeli"
        );
    }

    #[test]
    fn cpu_usage_always_emitted_others_guarded() {
        // Regresyon: CpuUsage koşulsuz; gpu/mem/network 0 iken yayılmaz.
        let mut s = sample_for_events();
        s.cpu_load_pct = 55;
        let events = system_events_for_sample(&s);
        assert_eq!(events.len(), 1, "yalnız CpuUsage kalmalı (diğerleri 0)");
        match &events[0] {
            SystemEvent::CpuUsage { ratio } => assert!((ratio - 0.55).abs() < 0.001),
            other => panic!("ilk event CpuUsage olmalı, alınan: {:?}", other),
        }
    }

    #[test]
    fn cpu_usage_reads_cpu_load_pct_not_cpu_percent() {
        // Pozitif kontrol: cpu_load_pct (PDH sistem yükü) CpuUsage ratio'sunu besler.
        let mut s = sample_for_events();
        s.cpu_load_pct = 91;
        let events = system_events_for_sample(&s);
        let ratio = events.iter().find_map(|e| match e {
            SystemEvent::CpuUsage { ratio } => Some(*ratio),
            _ => None,
        });
        assert!(ratio.is_some(), "CpuUsage her zaman yayılmalı");
        assert!((ratio.unwrap() - 0.91).abs() < 0.001, "cpu_load_pct/100 = 0.91");
    }

    #[test]
    fn cpu_usage_ignores_cpu_percent_field() {
        // Negatif kontrol: cpu_percent farklı değerde olsa bile CpuUsage,
        // cpu_load_pct'yi yansıtır. "Yanlışlıkla eski alanı okuyor" regresyonunu
        // doğrudan yakalar — cpu_percent=10.0 ise ratio 0.10 çıkar, 0.91 değil.
        let mut s = sample_for_events();
        s.cpu_load_pct = 91;
        s.cpu_percent  = 10.0;  // farklı kaynak — burası okunmamalı
        let events = system_events_for_sample(&s);
        let ratio = events.iter().find_map(|e| match e {
            SystemEvent::CpuUsage { ratio } => Some(*ratio),
            _ => None,
        });
        assert!(ratio.is_some(), "CpuUsage her zaman yayılmalı");
        assert!(
            (ratio.unwrap() - 0.91).abs() < 0.001,
            "cpu_load_pct=91 → ratio=0.91 beklendi, alınan: {:?}",
            ratio.unwrap()
        );
    }

    #[test]
    fn all_system_events_emitted_when_present() {
        // gpu/mem/network hepsi 0-dışı → dördü de yayılır (Cpu + Gpu + Mem + Net).
        let mut s = sample_for_events();
        s.gpu_load_pct = 70;
        s.memory_usage_pct = 60;
        s.network_rtt_ms = 15;
        let events = system_events_for_sample(&s);
        assert!(events.iter().any(|e| matches!(e, SystemEvent::CpuUsage { .. })));
        assert!(events.iter().any(|e| matches!(e, SystemEvent::GpuUsage { .. })));
        assert!(events.iter().any(|e| matches!(e, SystemEvent::MemUsage { .. })));
        assert!(events.iter().any(|e| matches!(e, SystemEvent::NetworkStats { .. })));
    }

    // --- TALIMAT_FRAME_DROP_PLUMBING: medya-metrik event türetimi ---

    #[test]
    fn frame_drop_pct_emitted_unconditionally_including_zero() {
        // KRİTİK FARK (memory'den): frame_drop_pct KOŞULSUZ yayılır — %0 dahil.
        // %0 geçerli/arzu edilen durum; `> 0` guard'ı BURAYA kopyalanmamalı
        // (recovery kuralı `< 5` 0'da tetiklenmeli).
        let mut s = sample_for_events();
        s.frame_drop_pct = 0;
        s.frame_drops = 0;
        let events = media_events_for_sample(&s);
        assert!(
            events.iter().any(|e| matches!(e, MediaEvent::FrameDropPct { pct: 0 })),
            "sıfır kare-kaybı yüzdesi de yayılmalı (filtrelenmemeli)"
        );
    }

    #[test]
    fn frame_drop_pct_carries_real_value() {
        let mut s = sample_for_events();
        s.frame_drop_pct = 12;
        let events = media_events_for_sample(&s);
        let pct = events.iter().find_map(|e| match e {
            MediaEvent::FrameDropPct { pct } => Some(*pct),
            _ => None,
        });
        assert_eq!(pct, Some(12), "gerçek frame_drop_pct birebir taşınmalı");
    }

    #[test]
    fn frame_dropped_still_gated_on_count() {
        // Regresyon: ham FrameDropped sayacı yalnız > 0 iken yayılır (mevcut davranış);
        // ama FrameDropPct her hâlükârda yayılır → iki event ayrı, gate paylaşmaz.
        let mut s = sample_for_events();
        s.frame_drops = 0;
        s.frame_drop_pct = 3;
        let events = media_events_for_sample(&s);
        assert!(
            !events.iter().any(|e| matches!(e, MediaEvent::FrameDropped { .. })),
            "frame_drops == 0 iken FrameDropped yayılmamalı"
        );
        assert!(
            events.iter().any(|e| matches!(e, MediaEvent::FrameDropPct { .. })),
            "FrameDropped gate'li olsa da FrameDropPct yayılmalı"
        );

        s.frame_drops = 7;
        let events = media_events_for_sample(&s);
        assert!(
            events.iter().any(|e| matches!(e, MediaEvent::FrameDropped { count: 7 })),
            "frame_drops > 0 iken FrameDropped yayılmalı"
        );
    }

    // --- I24: cstr_bounded sınırlı okuma ---

    #[test]
    fn cstr_bounded_null_returns_none() {
        assert_eq!(unsafe { cstr_bounded(std::ptr::null(), 256) }, None);
    }

    #[test]
    fn cstr_bounded_reads_normal_nul_terminated() {
        let c = std::ffi::CString::new("rules.json").unwrap();
        let s = unsafe { cstr_bounded(c.as_ptr(), MAX_FFI_PATH_LEN) };
        assert_eq!(s.as_deref(), Some("rules.json"));
    }

    #[test]
    fn cstr_bounded_empty_string_is_some_empty() {
        let c = std::ffi::CString::new("").unwrap();
        assert_eq!(unsafe { cstr_bounded(c.as_ptr(), 16) }.as_deref(), Some(""));
    }

    #[test]
    fn cstr_bounded_rejects_when_nul_beyond_limit() {
        // 100 baytlık string, sınır 10 → NUL sınır içinde yok → None (güvenli reddet).
        let long = std::ffi::CString::new("a".repeat(100)).unwrap();
        assert_eq!(unsafe { cstr_bounded(long.as_ptr(), 10) }, None);
    }

    #[test]
    fn cstr_bounded_accepts_exactly_at_limit() {
        // 5 bayt + NUL; sınır tam olarak NUL'un okunmasına izin verecek kadar (>=5).
        let c = std::ffi::CString::new("abcde").unwrap();
        assert_eq!(unsafe { cstr_bounded(c.as_ptr(), 5) }, None); // 5 içinde NUL[idx5] okunmaz
        assert_eq!(unsafe { cstr_bounded(c.as_ptr(), 6) }.as_deref(), Some("abcde"));
    }

    #[test]
    fn cstr_bounded_invalid_utf8_is_lossy() {
        // 0xFF geçersiz UTF-8 → U+FFFD; panik yok.
        let bytes = [0xFFu8, b'a', 0x00];
        let s = unsafe { cstr_bounded(bytes.as_ptr() as *const c_char, 16) };
        assert_eq!(s.as_deref(), Some("\u{FFFD}a"));
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
    fn test_metrics_poll_null_returns_zero() {
        // V8/I14: null out → 0 (yazma yok), çökme yok.
        assert_eq!(rj_metrics_poll(std::ptr::null_mut()), 0);
    }

    #[test]
    fn test_metrics_poll_after_start_fills_valid_sample() {
        // V8/I14: FFI_STATE init sonrası poll 1 döner ve GEÇERLI (canary) snapshot yazar.
        rj_start_monitor();
        let mut sample = MetricSample {
            magic_head: 0, timestamp_us: 0, bitrate_kbps: 0, fps_actual: 0.0,
            cpu_percent: 0.0, frame_drops: 0, frame_drop_pct: 0, gpu_temp_c: 0,
            cpu_temp_c: 0, memory_usage_pct: 0, cpu_load_pct: 0, gpu_load_pct: 0,
            network_rtt_ms: 0, network_loss_pct: 0, source_id: 0, magic_tail: 0,
        };
        let rc = rj_metrics_poll(&mut sample as *mut _);
        assert_eq!(rc, 1, "init sonrası poll 1 dönmeli");
        assert!(sample.is_valid(), "yazılan snapshot canary'si geçerli olmalı");
        assert_eq!(sample.source_id, 0, "snapshot video örneği olmalı");
    }

    #[test]
    fn test_panic_safety_rj_metrics_poll() {
        // SECURITY: rj_metrics_poll C++'a panic unwind etmemeli (catch_unwind).
        rj_start_monitor();
        assert_eq!(rj_metrics_poll(std::ptr::null_mut()), 0);
        let mut s = MetricSample {
            magic_head: 0, timestamp_us: 0, bitrate_kbps: 0, fps_actual: 0.0,
            cpu_percent: 0.0, frame_drops: 0, frame_drop_pct: 0, gpu_temp_c: 0,
            cpu_temp_c: 0, memory_usage_pct: 0, cpu_load_pct: 0, gpu_load_pct: 0,
            network_rtt_ms: 0, network_loss_pct: 0, source_id: 0, magic_tail: 0,
        };
        let _ = rj_metrics_poll(&mut s as *mut _); // tekrar çağrı güvenli
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
        // Özellik#1: gerçek açıklama üçlüsüyle enqueue et — FFI roundtrip'te
        // metric_id/current_value/threshold_value'ın bozulmadan geçtiğini doğrula.
        let expl = Explanation {
            metric_id: crate::rules::metric_id::GPU_TEMP_C,
            current_value: 87,
            threshold_value: 85,
            calibrated: true, // Özellik#5: kalibre bayrağının roundtrip'te korunduğunu doğrula
        };
        enqueue_ui_event(RjActionEvent::info_event(&src, &expl));

        let mut out = RjActionEvent {
            id: 0, action_type: RjActionType::LogOnly, param1: 0, param2: 0,
            require_approval: 9, kind: 9,
            metric_id: RjMetricId::MetricNone, current_value: 0, threshold_value: 0,
            calibrated: 9,
        };
        // Kuyrukta başka testlerin event'i de olabilir (paralel/global state);
        // bizim event'imizi bulana kadar drenaj yap.
        let mut found = false;
        while rj_action_event_dequeue(&mut out as *mut _) == 1 {
            if out.id == 4242 {
                assert_eq!(out.require_approval, 0, "info event onay gerektirmemeli");
                assert_eq!(out.kind, RJ_ACTION_EVENT_NEW);
                assert_eq!(out.param1, 3500);
                // Özellik#1: açıklama üçlüsü roundtrip'te korunmalı.
                assert_eq!(out.metric_id, RjMetricId::GpuTempC, "metric_id roundtrip");
                assert_eq!(out.current_value, 87, "current_value roundtrip");
                assert_eq!(out.threshold_value, 85, "threshold_value roundtrip");
                assert_eq!(out.calibrated, 1, "Özellik#5: calibrated bayrağı roundtrip'te korunmalı");
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
        enqueue_pending(mk_action(id), "rule_x".to_string(), Explanation::none());

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
        enqueue_pending(mk_action(id), "rule_y".to_string(), Explanation::none());

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
        let mut out = RjActionEvent {
            id: 0, action_type: RjActionType::LogOnly, param1: 0, param2: 0,
            require_approval: 0, kind: 0,
            metric_id: RjMetricId::MetricNone, current_value: 0, threshold_value: 0,
            calibrated: 0,
        };
        let mut found = false;
        while rj_action_event_dequeue(&mut out as *mut _) == 1 {
            if out.id == id && out.kind == RJ_ACTION_EVENT_INVALIDATED {
                // Özellik#1: invalidated event açıklama taşımaz.
                assert_eq!(out.metric_id, RjMetricId::MetricNone, "invalidated → MetricNone");
                found = true;
                break;
            }
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
        enqueue_pending(mk_action(id), "rule_w".to_string(), Explanation::none());
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

    // ===== Özellik#2: healing → obs VendorEvent eşlemesi (saf, global-state'siz) =====

    #[test]
    fn healing_vendor_event_applied_aciklama_tasir() {
        // Info event (New + require_approval=0 + gerçek metrik) → HealingActionApplied,
        // metric/value/threshold dahil.
        let a = RjAction { id: 7, action_type: RjActionType::BitrateReduce, param1: 3500, param2: 0, canary: 0 };
        let expl = Explanation { metric_id: crate::rules::metric_id::GPU_TEMP_C, current_value: 87, threshold_value: 85, calibrated: false };
        let env = healing_vendor_event(&RjActionEvent::info_event(&a, &expl));

        assert_eq!(env.op, 5);
        let inner = &env.d["eventData"];
        assert_eq!(inner["vendorName"], "reji-studio");
        assert_eq!(inner["eventType"], "HealingActionApplied");
        let d = &inner["eventData"];
        assert_eq!(d["id"], 7);
        assert_eq!(d["action"], "BitrateReduce");
        assert_eq!(d["param1"], 3500);
        assert_eq!(d["requireApproval"], false);
        assert_eq!(d["metric"], "GpuTempC");
        assert_eq!(d["value"], 87);
        assert_eq!(d["threshold"], 85);
        assert_eq!(d["calibrated"], false, "Özellik#5: statik eşik → calibrated=false");
    }

    #[test]
    fn healing_vendor_event_calibrated_flag_in_json() {
        // Özellik#5: kalibre eşikte WS JSON calibrated:true taşımalı (web UI etiketi).
        let a = RjAction { id: 8, action_type: RjActionType::ScaleResolution, param1: 250, param2: 0, canary: 0 };
        let expl = Explanation { metric_id: crate::rules::metric_id::MEMORY_USAGE_PCT, current_value: 88, threshold_value: 83, calibrated: true };
        let env = healing_vendor_event(&RjActionEvent::info_event(&a, &expl));
        let d = &env.d["eventData"]["eventData"];
        assert_eq!(d["metric"], "MemoryUsagePct");
        assert_eq!(d["threshold"], 83, "kalibre eşik (83) taşınmalı");
        assert_eq!(d["calibrated"], true, "kalibre eşik → calibrated=true");
    }

    #[test]
    fn healing_vendor_event_pending_onay_bayragi() {
        // Approval event (New + require_approval=1) → HealingActionPending, requireApproval:true.
        let a = mk_action(11);
        let expl = Explanation { metric_id: crate::rules::metric_id::FRAME_DROP_PCT, current_value: 12, threshold_value: 10, calibrated: false };
        let env = healing_vendor_event(&RjActionEvent::approval_event(&a, &expl));
        let inner = &env.d["eventData"];
        assert_eq!(inner["eventType"], "HealingActionPending");
        assert_eq!(inner["eventData"]["requireApproval"], true);
        assert_eq!(inner["eventData"]["metric"], "FrameDropPct");
    }

    #[test]
    fn healing_vendor_event_invalidated_sade() {
        // Invalidated → HealingActionInvalidated; yalnız id+action, açıklama YOK.
        let a = mk_action(13);
        let env = healing_vendor_event(&RjActionEvent::invalidated_event(&a));
        let inner = &env.d["eventData"];
        assert_eq!(inner["eventType"], "HealingActionInvalidated");
        let d = &inner["eventData"];
        assert_eq!(d["id"], 13);
        assert_eq!(d["action"], "BitrateReduce");
        assert!(d.get("metric").is_none(), "invalidated açıklama taşımamalı");
        assert!(d.get("requireApproval").is_none(), "invalidated onay alanı taşımamalı");
    }

    #[test]
    fn healing_vendor_event_metric_none_alan_atlar() {
        // metric_id == MetricNone → metric/value/threshold alanları atlanır (Özellik#1).
        let a = mk_action(15);
        let env = healing_vendor_event(&RjActionEvent::info_event(&a, &Explanation::none()));
        let inner = &env.d["eventData"];
        assert_eq!(inner["eventType"], "HealingActionApplied");
        let d = &inner["eventData"];
        assert_eq!(d["id"], 15, "id yine taşınır");
        assert!(d.get("metric").is_none(), "MetricNone → metric alanı olmamalı");
        assert!(d.get("value").is_none());
        assert!(d.get("threshold").is_none());
    }

    #[test]
    fn mode_changed_vendor_event_ad_ve_kod() {
        // Mod değişimi → HealingModeChanged, mode kodu + tireli ad.
        let env = mode_changed_vendor_event(1);
        let inner = &env.d["eventData"];
        assert_eq!(inner["eventType"], "HealingModeChanged");
        assert_eq!(inner["eventData"]["mode"], 1);
        assert_eq!(inner["eventData"]["modeName"], "co-pilot");
        // Diğer kodlar
        assert_eq!(mode_changed_vendor_event(0).d["eventData"]["eventData"]["modeName"], "auto-pilot");
        assert_eq!(mode_changed_vendor_event(3).d["eventData"]["eventData"]["modeName"], "manual");
    }

    #[test]
    fn test_push_scene_names_null_does_not_crash() {
        // SECURITY: null pointer → sessizce dön (panik/crash yok)
        rj_start_monitor();
        rj_push_scene_names(std::ptr::null(), 0);
        rj_push_scene_names(std::ptr::null(), 5); // count > 0 ama null ptr → yine güvenli
    }

    #[test]
    fn test_push_scene_names_bounds_scan_and_skips_unterminated() {
        // J1/I24: geçerli NUL-sonlu isim saklanır; NUL'u MAX_NAME_LEN (256) byte
        // içinde olmayan bozuk isim SINIRSIZ taranmadan güvenle atlanır.
        rj_start_monitor();
        let state = FFI_STATE.get().expect("FFI_STATE init");

        let good = std::ffi::CString::new("Sahne A").unwrap();
        // 300 byte, NUL yok → cstr_bounded 256'da None döner → atlanır (OOB read yok:
        // tampon 300 byte, tarama 256 ile sınırlı).
        let no_nul = vec![b'x'; 300];
        let ptrs: [*const c_char; 2] =
            [good.as_ptr(), no_nul.as_ptr() as *const c_char];

        rj_push_scene_names(ptrs.as_ptr(), 2);

        let guard = state._ws_state.scene_names.lock().unwrap();
        assert_eq!(
            *guard,
            vec!["Sahne A".to_string()],
            "geçerli isim saklanmalı, NUL'suz (>256 byte) isim güvenle atlanmalı"
        );
    }
}
