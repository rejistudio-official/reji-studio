// ws_server.rs — WebSocket kontrol API
use axum::{
    extract::ws::{CloseFrame, Message, WebSocket, WebSocketUpgrade},
    extract::State,
    response::IntoResponse,
    routing::get,
    Router,
};
use serde_json::{json, Value};
use std::sync::{Arc, Mutex, RwLock};
use std::sync::atomic::{AtomicBool, AtomicU16, AtomicU32, AtomicU64, Ordering};
use std::time::Duration;
use crossbeam::queue::ArrayQueue;
use tokio::sync::broadcast;

use crate::ffi::RJ_WS_CMD_SET_SCENE;
use crate::metrics::MetricState;
use crate::obs_protocol::{self, op as obs_op};

/// Şu anki Unix epoch zamanı, milisaniye. Stream süre hesapları için;
/// saat geriye giderse `unwrap_or(0)` ile 0 döner.
pub fn now_epoch_ms() -> u64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_millis() as u64)
        .unwrap_or(0)
}

/// Identify için bekleme süresi. Toleranslı mod: süre dolarsa bağlantı KAPATILMAZ,
/// istemci legacy (obs handshake yapmayan) olarak kabul edilir ve akış sürer.
const IDENTIFY_TIMEOUT: Duration = Duration::from_secs(5);

/// Gerçek bağlanan port — `rj_get_ws_port()` FFI ile C++ tarafına sunulur.
static ACTUAL_PORT: AtomicU16 = AtomicU16::new(0);

/// Bağlanan portu döndürür; sunucu henüz bind olmadıysa 0.
pub fn actual_port() -> u16 {
    ACTUAL_PORT.load(Ordering::Acquire)
}

/// Port listesini sırayla dener; başarılı bağlanmayı döndürür.
pub async fn try_bind(bind_addr: &str, ports: &[u16]) -> Option<(tokio::net::TcpListener, u16)> {
    for &port in ports {
        let addr = format!("{}:{}", bind_addr, port);
        if let Ok(listener) = tokio::net::TcpListener::bind(&addr).await {
            return Some((listener, port));
        }
        eprintln!("[WS] Port {} kullanımda, deneniyor: sonraki", port);
    }
    None
}

#[derive(Clone)]
pub struct WsState {
    pub cmd_tx: broadcast::Sender<String>,
    pub evt_rx: broadcast::Sender<String>,  // clone → subscribe
    /// Yayının aktif olup olmadığı. TEK yazma noktası `process_stream_cmd` (cmd tüketici
    /// tarafı); StartStream/StopStream handler'ı burayı YAZMAZ, yalnızca cmd_tx'e delege eder.
    pub streaming_active: Arc<AtomicBool>,
    /// Metrik durumu — FfiState'teki `_metric_state` ile AYNI Arc (iki instance değil).
    /// GetStreamStatus gerçek `frame_drops` (outputSkippedFrames) buradan okur.
    pub metric_state: Arc<MetricState>,
    /// Yayının başladığı epoch ms; `0` = akış aktif değil. `process_stream_cmd` günceller,
    /// GetStreamStatus outputDuration/outputTimecode'u bundan hesaplar.
    pub stream_started_at_ms: Arc<AtomicU64>,
    /// Sahne isimleri — C++ UI'dan `rj_push_scene_names` ile beslenir. Düşük frekans,
    /// kısa kilit (hot-path değil). GetSceneList (Aşama 5) buradan okur.
    pub scene_names: Arc<Mutex<Vec<String>>>,
    /// C++'ın DOĞRULADIĞI aktif sahne indeksi. `rj_user_event_scene_switch` yazar
    /// (UI tıklaması / legacy cut / SetCurrentProgramScene'in gerçek geçişi). Tek gerçek kaynak.
    pub current_scene_idx: Arc<AtomicU32>,
    /// WS→C++ komut kuyruğu — FfiState'teki ile AYNI Arc (metric_state deseni). SetScene (5)
    /// komutu (Aşama 5) buraya push edilir; iki ayrı kuyruk oluşturulmaz.
    pub ws_command_queue: Arc<ArrayQueue<(i32, i32)>>,
    /// V8/I8: WebSocket kontrol parolası. `None`/boş = auth KAPALI (bugünkü
    /// toleranslı davranış birebir). `Some(pw)` = obs-websocket auth zorunlu.
    /// `rj_set_ws_password` (C++ Settings) yazar; her BAĞLANTI açılışında (Hello)
    /// taze okunur → çalışırken değişim yalnız yeni bağlantılara uygulanır,
    /// mevcut doğrulanmış oturumlar sürer. FfiState._ws_state ile AYNI Arc.
    pub password: Arc<RwLock<Option<String>>>,
}

/// Legacy `{cmd:...}` ve obs-websocket StartStream/StopStream yollarının ORTAK stream-komut
/// işleyicisi. `streaming_active` bayrağının TEK yazma noktasıdır: `stream_start` ⇒ true,
/// `stream_stop` ⇒ false. Dönüş, C++ `ws_command_queue`'ya push edilecek komut kodu
/// (bilinmeyen komutta `None`). C++'a iletim çağıranın (ffi.rs) sorumluluğundadır; burada
/// yalnızca protokol-seviyesi durum senkronize edilir.
///
/// Not: Flag "komut gönderildi" anlamında iyimser güncellenir — encode/output tarafının
/// gerçekten başladığını doğrulayan bir onay mekanizması henüz yok (bkz. SESSION_NOTES Aşama 2).
/// `stream_started_at_ms` de burada güncellenir (stream_start ⇒ now, stream_stop ⇒ 0);
/// böylece süre/timecode hesabı tek doğruluk kaynağından beslenir.
pub fn process_stream_cmd(
    cmd: &str,
    streaming_active: &AtomicBool,
    stream_started_at_ms: &AtomicU64,
) -> Option<i32> {
    match cmd {
        "stream_start" => {
            streaming_active.store(true, Ordering::Relaxed);
            stream_started_at_ms.store(now_epoch_ms(), Ordering::Relaxed);
            Some(1)
        }
        "stream_stop" => {
            streaming_active.store(false, Ordering::Relaxed);
            stream_started_at_ms.store(0, Ordering::Relaxed);
            Some(2)
        }
        "scene_cut" => Some(3),
        "scene_fade" => Some(4),
        _ => None,
    }
}

/// op 6 (Request) dispatch: requestType'ı gerçek işleve bağlar. Bilinmeyen tip → 204,
/// bağlantı kapatılmaz. StartStream/StopStream mevcut `cmd_tx` yoluna delege edilir
/// (legacy `{cmd:...}` ile aynı kanal) — böylece `streaming_active` tek noktadan güncellenir.
fn dispatch_request(
    request_type: &str,
    request_id: &str,
    request_data: &Value,
    state: &WsState,
    ws_incoming: u64,
    ws_outgoing: u64,
) -> obs_protocol::WsEnvelope {
    use obs_protocol::{request_response_err, request_response_ok, request_status};
    match request_type {
        "GetVersion" => request_response_ok(
            request_type,
            request_id,
            json!({
                "obsWebSocketVersion": obs_protocol::OBS_WS_VERSION,
                "rpcVersion": obs_protocol::RPC_VERSION,
                "platform": "windows",
                "rejiStudioVersion": env!("CARGO_PKG_VERSION"),
            }),
        ),
        "GetStats" => {
            // obs-websocket v5 GetStats — tam 11 alan. Dürüstlük ilkesi (GetStreamStatus ile
            // aynı): gerçek kaynak varsa doldurulur, yoksa 0/0.0 (tahmini değer üretilmez).
            //   MetricState (zaten var):
            //     cpuUsage           ← cpu() = cpu_percent (UI metrik barıyla AYNI kaynak;
            //                          PDH sistem yükü cpu_load_pct RuleEngine'e özel, MetricState'te
            //                          tutulmaz — bkz. commit 224c4af).
            //     activeFps          ← fps()
            //     outputSkippedFrames← frame_drops() (GetStreamStatus ile aynı)
            //   Anlık OS sorgusu (sys_stats — streaming telemetri değil, sorgu-anı gerçeği):
            //     memoryUsage        ← process working-set MB
            //     availableDiskSpace ← çalışma dizini sürücüsü boş alan MB
            //   Oturum sayaçları (bağlantı-yerel):
            //     webSocketSessionIncoming/OutgoingMessages ← ws_incoming/ws_outgoing
            //   Bilinçli 0 (Reji mimarisinde karşılığı yok / FFI ABI gerektirir — MVP sınırı,
            //   Faz 0 onayı): averageFrameRenderTime, renderSkippedFrames, renderTotalFrames
            //   (WGC zero-copy'de render-thread kavramı yok), outputTotalFrames (C++ total_frames
            //   FFI'da taşınmıyor → ertelendi).
            request_response_ok(
                request_type,
                request_id,
                json!({
                    "cpuUsage": state.metric_state.cpu() as f64,
                    "memoryUsage": crate::sys_stats::process_memory_mb(),
                    "availableDiskSpace": crate::sys_stats::available_disk_mb(),
                    "activeFps": state.metric_state.fps() as f64,
                    "averageFrameRenderTime": 0.0,
                    "renderSkippedFrames": 0,
                    "renderTotalFrames": 0,
                    "outputSkippedFrames": state.metric_state.frame_drops(),
                    "outputTotalFrames": 0,
                    "webSocketSessionIncomingMessages": ws_incoming,
                    "webSocketSessionOutgoingMessages": ws_outgoing,
                }),
            )
        }
        "StartStream" => {
            // Legacy yolla aynı kanal; streaming_active tüketici tarafında güncellenir.
            let _ = state.cmd_tx.send("stream_start".to_string());
            request_response_ok(request_type, request_id, json!({}))
        }
        "StopStream" => {
            let _ = state.cmd_tx.send("stream_stop".to_string());
            request_response_ok(request_type, request_id, json!({}))
        }
        "GetStreamStatus" => {
            // Tam obs-websocket v5 alan seti (8 alan). Dürüstlük ilkesi: gerçek veri varsa
            // doldurulur, yoksa 0/false — tahmini/sahte değer üretilmez (bkz. SESSION_NOTES Aşama 3).
            let active = state.streaming_active.load(Ordering::Relaxed);
            let duration_ms = if active {
                now_epoch_ms().saturating_sub(state.stream_started_at_ms.load(Ordering::Relaxed))
            } else {
                0
            };
            request_response_ok(
                request_type,
                request_id,
                json!({
                    "outputActive": active,                                    // gerçek
                    "outputReconnecting": false,                              // reconnect mantığı yok
                    "outputTimecode": obs_protocol::format_timecode(duration_ms), // gerçek (duration'dan)
                    "outputDuration": duration_ms,                            // gerçek (started_at'ten)
                    "outputCongestion": 0.0,                                  // congestion sinyali yok
                    "outputBytes": 0,                                         // SRT output stub
                    "outputSkippedFrames": state.metric_state.frame_drops(),  // gerçek (MetricState)
                    "outputTotalFrames": 0,                                   // toplam kare sayacı yok
                }),
            )
        }
        "GetSceneList" => {
            // scene_names + current_scene_idx yalnızca C++'ın DOĞRULADIĞI son durumu yansıtır
            // (tek gerçek kaynak): GetStreamStatus gibi iyimser değildir. sceneUuid isimden
            // deterministik üretilir (bkz. obs_protocol::pseudo_uuid).
            //
            // Sahne SIRASI: obs-websocket v5 konvansiyonu sceneIndex 0'ı UI'nın EN ALTINA koyar
            // ve diziyi alttan üste verir (OBS kaynağı Obs_ArrayHelper.cpp — azalan indeks +
            // std::reverse; Aşama 6'da obs-websocket-js/simpleobsws ile doğrulandı). scene_names
            // C++'tan UI sırasıyla (üstten alta, row 0 = ilk) gelir → `.rev()` ile alttan üste
            // çevrilip enumerate edilir: sceneIndex 0 = en ALT sahne. Örn. [S1,S2,S3] (üstten
            // alta) → sceneIndex 0=S3, 1=S2, 2=S1. current_scene_idx ve SetCurrentProgramScene
            // bu SUNUM sırasından ETKİLENMEZ (isimle / iç index ile çalışırlar — tek gerçek
            // kaynak). currentPreviewScene* studio mode olmadığından null.
            let names = state.scene_names.lock().unwrap().clone();
            let cur_idx = state.current_scene_idx.load(Ordering::Relaxed) as usize;
            let cur_name = names.get(cur_idx).cloned().unwrap_or_default();
            let scenes: Vec<Value> = names
                .iter()
                .rev()
                .enumerate()
                .map(|(i, n)| {
                    json!({
                        "sceneIndex": i,
                        "sceneName": n,
                        "sceneUuid": obs_protocol::pseudo_uuid(n),
                    })
                })
                .collect();
            request_response_ok(
                request_type,
                request_id,
                json!({
                    "currentProgramSceneName": cur_name,
                    "currentProgramSceneUuid": obs_protocol::pseudo_uuid(&cur_name),
                    "currentPreviewSceneName": null,
                    "currentPreviewSceneUuid": null,
                    "scenes": scenes,
                }),
            )
        }
        "SetCurrentProgramScene" => {
            // Not: current_scene_idx BURADA güncellenmez — gerçek geçiş C++'ta olup
            // rj_user_event_scene_switch üzerinden geri bildirilene kadar beklenir (tek gerçek
            // kaynak). Yalnızca SetScene komutu C++ kuyruğuna push edilir. Bulunamayan sahne
            // ResourceNotFound (600) döner; bağlantı kapatılmaz.
            let scene_name = request_data
                .get("sceneName")
                .and_then(Value::as_str)
                .unwrap_or("");
            let position = {
                let names = state.scene_names.lock().unwrap();
                names.iter().position(|n| n == scene_name)
            };
            match position {
                Some(idx) => {
                    let _ = state.ws_command_queue.push((RJ_WS_CMD_SET_SCENE, idx as i32));
                    request_response_ok(request_type, request_id, json!({}))
                }
                None => request_response_err(
                    request_type,
                    request_id,
                    request_status::RESOURCE_NOT_FOUND,
                    "Scene not found",
                ),
            }
        }
        _ => request_response_err(
            request_type,
            request_id,
            request_status::UNKNOWN_REQUEST_TYPE,
            "Unknown request type",
        ),
    }
}

pub async fn ws_handler(
    ws: WebSocketUpgrade,
    State(state): State<Arc<WsState>>,
) -> impl IntoResponse {
    // obs-websocket istemcileri (obs-websocket-js, Touch Portal) Sec-WebSocket-Protocol ile
    // bir alt-protokol teklif eder ve sunucunun onu SEÇMESİNİ bekler; seçilmezse istemci
    // "Server sent no subprotocol" ile kopar (Faz 1 Aşama 6 bulgusu). `.protocols` istemcinin
    // teklif ettiklerinden sunucu listesindeki İLK eşleşeni seçer (axum 0.7 `find` sırası) —
    // ikisini de teklif eden istemcide JSON kazanır. Hiç teklif yoksa hiçbir şey seçilmez —
    // legacy istemciler (control.html, alt-protokol teklif etmez) etkilenmez, geriye tam uyumlu.
    ws.protocols([obs_protocol::SUBPROTOCOL_JSON, obs_protocol::SUBPROTOCOL_MSGPACK])
        .on_upgrade(move |socket| {
            // axum, seçtiği alt-protokolü hem yanıtın Sec-WebSocket-Protocol header'ına yazar
            // hem de upgrade sonrası `WebSocket::protocol()` ile raporlar (axum 0.7.9
            // extract/ws.rs — kaynak koddan doğrulandı). WireMode bağlantı ömrü boyunca sabit
            // yerel bir özelliktir; WsState'e KONMAZ (global değil, bağlantı bazlı).
            let wire_mode = match socket.protocol().and_then(|p| p.to_str().ok()) {
                Some(p) if p == obs_protocol::SUBPROTOCOL_MSGPACK => WireMode::Msgpack,
                // json seçildi VEYA hiç alt-protokol yok (legacy) → JSON varsayılan
                _ => WireMode::Json,
            };
            handle_socket(socket, state, wire_mode)
        })
}

/// Bağlantının tel kodlaması — seçilen alt-protokole göre bağlantı ömrü boyunca sabittir.
/// Aynı mantıksal `{op, d}` zarfının iki farklı "teli": JSON/Text veya MessagePack/Binary.
/// Tüm iş mantığı (dispatch_request, hello, identified) WsEnvelope üzerinde moddan bağımsız
/// çalışır; yalnızca tele yazış/okuyuş biçimi değişir.
#[derive(Clone, Copy, PartialEq, Eq)]
enum WireMode {
    Json,
    Msgpack,
}

/// Zarfı seçili tel kodlamasıyla WebSocket mesajına çevirir. Serileştirme hatası pratikte
/// beklenmez (zarflar kendi ürettiğimiz Value'lar); hata çağıran tarafta log'lanıp bağlantı
/// kapatılır.
fn encode(mode: WireMode, env: &obs_protocol::WsEnvelope) -> Result<Message, String> {
    match mode {
        WireMode::Json => serde_json::to_string(env)
            .map(|s| Message::Text(s.into()))
            .map_err(|e| e.to_string()),
        WireMode::Msgpack => rmp_serde::to_vec_named(env)
            .map(|b| Message::Binary(b.into()))
            .map_err(|e| e.to_string()),
    }
}

/// Zarfı kodlayıp gönderir; `false` dönerse bağlantı sonlandırılmalıdır.
async fn send_env(
    socket: &mut WebSocket,
    mode: WireMode,
    env: &obs_protocol::WsEnvelope,
) -> bool {
    match encode(mode, env) {
        Ok(msg) => socket.send(msg).await.is_ok(),
        Err(e) => {
            eprintln!("[WS] zarf kodlanamadı: {}", e);
            false
        }
    }
}

/// V8/I8: Bağlantıyı obs-websocket close-code'uyla kapatır (ör. 4007 NotIdentified,
/// 4009 AuthenticationFailed). Mevcut implicit `break` (kod'suz drop) yalnızca
/// koparma; obs istemcileri (obs-websocket-js vb.) auth reddini KAPATMA KODUNDAN
/// okur — bu yüzden auth reddi kod'lu Close frame ile bildirilmeli. Close bir
/// kontrol frame'idir → WireMode (JSON/msgpack) bağımsız çalışır. Gönderim
/// başarısızsa yutulur (bağlantı zaten kapanıyor).
async fn close_with(socket: &mut WebSocket, code: u16, reason: &str) {
    let frame = CloseFrame { code, reason: reason.to_owned().into() };
    let _ = socket.send(Message::Close(Some(frame))).await;
}

/// Gelen bir istemci mesajının türü.
enum ClientMsg {
    /// obs-websocket Identify (op 1), istenen rpcVersion + (varsa) authentication.
    Identify { rpc: Option<u64>, authentication: Option<String> },
    /// obs-websocket Request (op 6). requestId yanıtta bire bir geri döner.
    Request {
        request_type: String,
        request_id: String,
        request_data: Value,
    },
    /// Identify/Request dışında `op` içeren obs mesajı (bu aşamada yalnızca log'lanır).
    Obs,
    /// `op` içermeyen legacy mesaj (ör. `{cmd:...}`) veya JSON olmayan metin.
    Legacy,
}

/// Text mesajını sınıflandırır (JSON teli). JSON olmayan metin legacy yoluna düşer.
fn classify(text: &str) -> ClientMsg {
    match serde_json::from_str::<Value>(text) {
        Ok(v) => classify_value(&v),
        // JSON değil → eski davranışla uyumlu şekilde legacy yolundan geçir
        Err(_) => ClientMsg::Legacy,
    }
}

/// Çözülmüş değeri `op` alanına göre sınıflandırır — JSON ve msgpack tellerinin ortak noktası.
fn classify_value(v: &Value) -> ClientMsg {
    match v.get("op").and_then(Value::as_u64) {
        Some(op) if op as u8 == obs_op::IDENTIFY => ClientMsg::Identify {
            rpc: v.get("d").and_then(|d| d.get("rpcVersion")).and_then(Value::as_u64),
            authentication: v
                .get("d")
                .and_then(|d| d.get("authentication"))
                .and_then(Value::as_str)
                .map(String::from),
        },
        Some(op) if op as u8 == obs_op::REQUEST => {
            let d = v.get("d");
            ClientMsg::Request {
                request_type: d
                    .and_then(|d| d.get("requestType"))
                    .and_then(Value::as_str)
                    .unwrap_or("")
                    .to_string(),
                request_id: d
                    .and_then(|d| d.get("requestId"))
                    .and_then(Value::as_str)
                    .unwrap_or("")
                    .to_string(),
                request_data: d
                    .and_then(|d| d.get("requestData"))
                    .cloned()
                    .unwrap_or(Value::Null),
            }
        }
        Some(_) => ClientMsg::Obs,
        None => ClientMsg::Legacy, // op yok → legacy (cmd dahil)
    }
}

/// Legacy `{cmd:...}` kontrol mesajını eski yoldan işler — davranış değişmez.
fn handle_legacy_cmd(text: &str, state: &WsState) {
    let Ok(v) = serde_json::from_str::<Value>(text) else {
        eprintln!("[WS] JSON parse edilemedi: {}", text);
        return;
    };
    let cmd = v["cmd"].as_str().unwrap_or("");
    match cmd {
        "stream_start" | "stream_stop" | "scene_cut" | "scene_fade" => {
            let _ = state.cmd_tx.send(cmd.to_string());
        }
        "" => eprintln!("[WS] bilinmeyen mesaj (op/cmd yok): {}", text),
        _ => eprintln!("[WS] unknown cmd: {}", cmd),
    }
}

/// V8/I8: Bağlantı başına oturum durumu. `identified` obs handshake'i tamamlandı mı;
/// `authenticated` parola gereğinin karşılanıp karşılanmadığı (parola YOKken vacuously
/// true → bugünkü toleranslı davranış). `auth` yalnız parola ayarlıyken Some — Identify
/// doğrulaması için gereken bağlamı (bağlantı açılışında snapshot'lanan parola + o oturuma
/// özel salt/challenge) taşır; snapshot sayesinde parola çalışırken değişse bile bu oturum
/// açılıştaki parolaya göre doğrulanır.
struct Session {
    identified: bool,
    /// Parola gereği karşılandı mı. Parola YOKken vacuously true (gating kapalı).
    /// Parola ayarlıyken yalnız geçerli Identify+auth sonrası true; false iken
    /// Identify DIŞI her mesaj 4007 ile reddedilir (aşağıdaki guard).
    authenticated: bool,
    auth: Option<PendingAuth>,
    /// GetStats `webSocketSessionIncomingMessages`: bu bağlantıda sınıflandırılan istemci
    /// protokol mesajı sayısı. Bağlantı-yerel (paylaşılmaz). Her `process_client_msg`
    /// girişinde +1 (reddedilen mesajlar da sayılır — gerçekten ALINDILAR).
    incoming_messages: u64,
    /// GetStats `webSocketSessionOutgoingMessages`: bu bağlantıya gönderilen protokol
    /// mesajı sayısı (Hello + Identified + RequestResponse + event). Kurucularda 1 ile
    /// başlar (açılışta gönderilen Hello). Close kontrol-frame'leri sayılmaz (terminal).
    outgoing_messages: u64,
}

struct PendingAuth {
    salt: String,
    challenge: String,
    password: String,
}

/// Sınıflandırılmış istemci mesajını işler; `false` dönerse bağlantı kapatılmalıdır.
/// `raw_text`: yalnızca JSON telinde ham metin (legacy `{cmd}` yolu + log için). msgpack
/// telinde `None` — legacy yol msgpack'te anlamsızdır (control.html alt-protokol teklif
/// etmez, dolayısıyla hiç tetiklenmez) ama kırılmaz: op'suz mesaj yalnızca log'lanır.
async fn process_client_msg(
    socket: &mut WebSocket,
    wire_mode: WireMode,
    msg: ClientMsg,
    session: &mut Session,
    state: &WsState,
    raw_text: Option<&str>,
) -> bool {
    // GetStats sayacı: her sınıflandırılan istemci mesajı bir "incoming"dir — ret
    // (4007 guard) öncesinde sayılır çünkü mesaj gerçekten alındı.
    session.incoming_messages += 1;
    // V8/I8: Parola ayarlıyken (authenticated=false) doğrulanmamış oturumdan
    // Identify DIŞI HER mesaj (obs Request VE legacy `{cmd}` VE diğer obs op'ları)
    // 4007 ile reddedilir — I8'in asıl açığı legacy `{cmd}` yoluydu ve o da bu tek
    // oturum-bayrağı kuralıyla kapanır. Parolasızken authenticated vacuously true →
    // bu dal HİÇ girilmez, bugünkü davranış birebir korunur.
    if !session.authenticated && !matches!(msg, ClientMsg::Identify { .. }) {
        // Saldırı görünürlüğü (parola/içerik loglanmaz — yalnız ret gerçeği).
        eprintln!("[WS] WARN: doğrulanmamış oturumdan Identify-dışı mesaj → 4007 ile reddedildi");
        close_with(socket, obs_protocol::close_code::NOT_IDENTIFIED, "Not identified").await;
        return false;
    }
    match msg {
        ClientMsg::Identify { rpc, authentication } => {
            if session.identified {
                // Spec 4008 (AlreadyIdentified) ile kapatmayı öngörür; toleranslı ruhla
                // yoksay+log seçildi (bugünkü davranış korunur) — spec sapması, belgelenmiş.
                eprintln!("[WS] tekrar Identify geldi (yok sayıldı)");
                return true;
            }
            // V8/I8: parola ayarlıysa authentication'ı DOĞRULA; yanlış/eksik → 4009 kapat.
            if let Some(pending) = &session.auth {
                let ok = authentication
                    .as_deref()
                    .map(|resp| obs_protocol::verify_auth(
                        &pending.password, &pending.salt, &pending.challenge, resp,
                    ))
                    .unwrap_or(false);
                if !ok {
                    // Parola/deneme değeri ASLA loglanmaz — yalnız ret gerçeği.
                    eprintln!("[WS] Identify auth başarısız → 4009 ile kapatılıyor");
                    close_with(
                        socket,
                        obs_protocol::close_code::AUTHENTICATION_FAILED,
                        "Authentication failed",
                    ).await;
                    return false;
                }
                session.authenticated = true;
            }
            // rpcVersion 1 değilse yine de 1 olarak negotiate et (esnek)
            if rpc != Some(obs_protocol::RPC_VERSION as u64) {
                eprintln!(
                    "[WS] Identify rpcVersion={:?}, {} olarak negotiate ediliyor",
                    rpc, obs_protocol::RPC_VERSION
                );
            }
            if !send_env(socket, wire_mode, &obs_protocol::identified()).await {
                return false;
            }
            session.outgoing_messages += 1; // Identified gönderildi
            session.identified = true;
            eprintln!("[WS] obs-websocket istemcisi identified");
            true
        }
        // op 6 Request → dispatch. Parolasızken Identify zorunlu değil (toleranslı mod).
        // Parola ayarlıyken doğrulanmamış Request reddi commit 4'te (4007).
        ClientMsg::Request { request_type, request_id, request_data } => {
            if !session.identified {
                eprintln!(
                    "[WS] Identify'sız Request işleniyor (toleranslı mod): {}",
                    request_type
                );
            }
            // Sayaç snapshot'ı: incoming bu isteği DE içerir (üstte +1 edildi); outgoing
            // henüz bu yanıtı içermez (aşağıda gönderilecek) — GetStats için doğru anlık.
            let resp = dispatch_request(
                &request_type,
                &request_id,
                &request_data,
                state,
                session.incoming_messages,
                session.outgoing_messages,
            );
            let sent = send_env(socket, wire_mode, &resp).await;
            if sent {
                session.outgoing_messages += 1; // RequestResponse gönderildi
            }
            sent
        }
        // Identify/Request dışı obs mesajları yalnızca log'lanır
        ClientMsg::Obs => {
            eprintln!(
                "[WS] obs mesajı (bu aşamada işlenmiyor): {}",
                raw_text.unwrap_or("<binary msgpack>")
            );
            true
        }
        // Legacy komut yolu — JSON telinde davranış değişmez; msgpack telinde tetiklenmez.
        ClientMsg::Legacy => {
            match raw_text {
                Some(text) => handle_legacy_cmd(text, state),
                None => eprintln!("[WS] op'suz msgpack mesajı yok sayıldı (legacy yol yalnızca JSON telinde)"),
            }
            true
        }
    }
}

async fn handle_socket(mut socket: WebSocket, state: Arc<WsState>, wire_mode: WireMode) {
    // V8/I8: Parolayı bağlantı açılışında SNAPSHOT'la — çalışırken değişse bile bu
    // oturum açılıştaki parolaya göre doğrulanır (mevcut oturumlar sürer). Boş string
    // = auth kapalı (None gibi).
    let password_snapshot = state
        .password
        .read()
        .ok()
        .and_then(|g| g.clone())
        .filter(|p| !p.is_empty());

    // Hello (op 0) gönder — parola ayarlıysa authentication (challenge+salt) ile.
    // AYRI BLOKLAYAN "Identify bekleniyor" adımı YOK: Identify ve soft-timeout
    // döngü içinde ele alınır; evt_rx metrik akışı ilk andan itibaren kesintisiz iletilir.
    let mut session = match &password_snapshot {
        Some(pw) => {
            let (salt, challenge) = obs_protocol::gen_salt_challenge();
            let hello = obs_protocol::hello_with_auth(&challenge, &salt);
            if !send_env(&mut socket, wire_mode, &hello).await {
                return;
            }
            Session {
                identified: false,
                authenticated: false,
                auth: Some(PendingAuth { salt, challenge, password: pw.clone() }),
                incoming_messages: 0,
                outgoing_messages: 1, // az önce gönderilen Hello (auth'lu)
            }
        }
        None => {
            if !send_env(&mut socket, wire_mode, &obs_protocol::hello()).await {
                return;
            }
            // Parola yok → authenticated vacuously true (gating kapalı, bugünkü davranış).
            Session {
                identified: false,
                authenticated: true,
                auth: None,
                incoming_messages: 0,
                outgoing_messages: 1, // az önce gönderilen Hello
            }
        }
    };

    let mut evt_rx = state.evt_rx.subscribe();

    // Identify için soft-timeout: süre dolarsa YALNIZCA log'lanır — bağlantı kapatılmaz,
    // event akışı hiç kesilmez. Legacy istemci (obs handshake yapmayan) böyle sürdürülür.
    let identify_deadline = tokio::time::sleep(IDENTIFY_TIMEOUT);
    tokio::pin!(identify_deadline);
    let mut deadline_fired = false;

    loop {
        tokio::select! {
            // Metrik eventleri → istemciye (handshake beklenirken DE paralel çalışır).
            // JSON telinde davranış değişmez: event_bus'un ürettiği string aynen gider
            // (control.html + Aşama 6'da canlı doğrulanan obs-websocket-js/json buna dayanır).
            // msgpack telinde YALNIZCA {op, d} zarfı olan eventler kodlanıp gönderilir;
            // op'suz legacy metrik eventi ({"fps":..}) bu tele HİÇ yazılmaz — CANLI BULGU
            // (Aşama 7, simpleobsws): zarf dışı gövde strict istemcinin recv döngüsünü
            // KeyError('op') ile öldürüyor, sonraki tüm request'ler timeout oluyor.
            // (Log yok: metrik akışı ~saniyelik, stderr'i boğardı.)
            evt = evt_rx.recv() => {
                // Özellik#2: Yayın (metrik + healing VendorEvent) yalnız DOĞRULANMIŞ
                // oturuma gider. Parola YOKken `authenticated` vacuously true →
                // bugünkü toleranslı davranış birebir korunur. Parola AYARLIYKEN bu
                // guard, healing event'lerinin sızmasını engellediği gibi ESKİDEN
                // beri var olan metrik sızıntısını da kapatır: gelen mesajlar
                // (process_client_msg) auth-kapılıydı ama giden yayın değildi —
                // sessiz doğrulanmamış bir bağlantı metrikleri almaya devam ediyordu.
                // Tek düzeltme, iki kazanım (I8'in "parola ayarlıysa kilitli" ruhu).
                if !session.authenticated {
                    continue;
                }
                if let Ok(data) = evt {
                    let msg = match wire_mode {
                        WireMode::Json => Some(Message::Text(data.into())),
                        WireMode::Msgpack => serde_json::from_str::<Value>(&data)
                            .ok()
                            .filter(|v| v.get("op").is_some())
                            .and_then(|v| rmp_serde::to_vec_named(&v).ok())
                            .map(|b| Message::Binary(b.into())),
                    };
                    if let Some(m) = msg {
                        if socket.send(m).await.is_err() {
                            break;
                        }
                        session.outgoing_messages += 1; // event (metrik/VendorEvent) gönderildi
                    }
                }
            }
            // Gelen mesajlar — çerçeve tipi tel moduna uymalı. Yanlış çerçeve tipi bir
            // PROTOKOL İHLALİDİR (istemci kodlamayı alt-protokolle ZATEN seçti) → spec'in
            // MessageDecodeError davranışıyla kapatılır. Bu, Aşama 1'in toleranslı
            // handshake'i ile KARIŞTIRILMAMALI: orada "Identify hiç gelmedi" belirsizliği
            // vardı (legacy istemci olabilir); burada net bir kural ihlali var.
            msg = socket.recv() => {
                match msg {
                    Some(Ok(Message::Text(text))) => {
                        if wire_mode == WireMode::Msgpack {
                            eprintln!("[WS] msgpack telinde Text frame — protokol ihlali, bağlantı kapatılıyor");
                            break;
                        }
                        let m = classify(&text);
                        if !process_client_msg(&mut socket, wire_mode, m, &mut session, &state, Some(&text)).await {
                            break;
                        }
                    }
                    Some(Ok(Message::Binary(bin))) => {
                        if wire_mode != WireMode::Msgpack {
                            // JSON telinde binary frame beklenmez — önceki davranış (kapat) korunur.
                            break;
                        }
                        match rmp_serde::from_slice::<Value>(&bin) {
                            Ok(v) => {
                                let m = classify_value(&v);
                                if !process_client_msg(&mut socket, wire_mode, m, &mut session, &state, None).await {
                                    break;
                                }
                            }
                            Err(e) => {
                                // Seçili kodlamayla çözülemeyen gövde — spec: MessageDecodeError → kapat.
                                eprintln!("[WS] msgpack çözülemedi, bağlantı kapatılıyor: {}", e);
                                break;
                            }
                        }
                    }
                    _ => break,
                }
            }
            // Identify soft-timeout: tek sefer, yalnızca log; akışı bloklamaz/kesmez.
            // V8/I8: parola ayarlıyken de yalnız log — doğrulanmamış oturum zaten hiçbir
            // şey yapamaz (commit 4: her Request/{cmd} → 4007), zamanla kapatmak şart değil
            // (spec sapması, bugünkü toleranslı davranışla tutarlı, belgelenmiş).
            _ = &mut identify_deadline, if !session.identified && !deadline_fired => {
                deadline_fired = true;
                eprintln!(
                    "[WS] Identify {}s içinde gelmedi → legacy istemci (bağlantı sürüyor)",
                    IDENTIFY_TIMEOUT.as_secs()
                );
            }
        }
    }
}

/// `ports` sırasıyla denenir; ilk başarılı adrese bağlanılır.
/// Gerçek port `actual_port()` ile okunabilir.
pub async fn serve(ports: Vec<u16>, bind_addr: &str, state: Arc<WsState>) {
    let app = Router::new()
        .route("/ws", get(ws_handler))
        .route("/", get(|| async { axum::response::Html(include_str!("control.html")) }))
        .with_state(state);

    match try_bind(bind_addr, &ports).await {
        Some((listener, port)) => {
            ACTUAL_PORT.store(port, Ordering::Release);
            log_to_file(&format!("[WS] Listening on ws://{}:{}/ws", bind_addr, port));
            if let Err(e) = axum::serve(listener, app).await {
                log_to_file(&format!("[WS] serve error: {}", e));
            }
        }
        None => {
            log_to_file(&format!(
                "[WS] Tüm portlar meşgul ({:?}), WS sunucusu başlatılamadı",
                ports
            ));
        }
    }
}

// I21: Hardcoded path yerine taşınabilir %LOCALAPPDATA%\reji-studio\ws_debug.log.
// pub(crate): ffi.rs de spawn logu için aynı yola yazmak üzere çağırır (DRY).
pub(crate) fn log_to_file(msg: &str) {
    use std::io::Write;
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true).append(true)
        .open(crate::paths::log_path("ws_debug.log"))
    {
        let _ = writeln!(f, "{}", msg);
    }
    eprintln!("{}", msg);
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures_util::StreamExt;

    /// Minimal sunucu: bağlanınca verilen close-code ile kapatır (private
    /// `close_with`'i gerçek bir WebSocket üzerinde çalıştırır). Dinlenen portu döndürür.
    async fn spawn_closer(code: u16) -> u16 {
        let listener = tokio::net::TcpListener::bind("127.0.0.1:0").await.unwrap();
        let port = listener.local_addr().unwrap().port();
        let app = Router::new().route(
            "/ws",
            get(move |ws: WebSocketUpgrade| async move {
                ws.on_upgrade(move |mut socket| async move {
                    close_with(&mut socket, code, "test").await;
                })
            }),
        );
        tokio::spawn(async move {
            let _ = axum::serve(listener, app).await;
        });
        port
    }

    /// Verilen URL'e bağlanıp ilk Close frame'inin taşıdığı kodu döndürür.
    async fn recv_close_code(url: &str) -> Option<u16> {
        use tokio_tungstenite::tungstenite::Message as TMsg;
        let (mut ws, _) = tokio_tungstenite::connect_async(url).await.unwrap();
        while let Some(Ok(msg)) = ws.next().await {
            if let TMsg::Close(Some(cf)) = msg {
                return Some(u16::from(cf.code));
            }
        }
        None
    }

    #[tokio::test]
    async fn test_close_with_sends_obs_close_codes() {
        // V8/I8 (commit 1): close_with, obs close-code'larını (4007/4009) istemciye
        // doğru iletir — auth reddinin kod'lu Close frame ile bildirilmesinin temeli.
        for code in [
            obs_protocol::close_code::NOT_IDENTIFIED,
            obs_protocol::close_code::AUTHENTICATION_FAILED,
        ] {
            let port = spawn_closer(code).await;
            let got = recv_close_code(&format!("ws://127.0.0.1:{}/ws", port)).await;
            assert_eq!(got, Some(code), "close frame {} taşımalı", code);
        }
    }
}
