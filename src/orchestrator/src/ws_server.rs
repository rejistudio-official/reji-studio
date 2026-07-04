// ws_server.rs — WebSocket kontrol API
use axum::{
    extract::ws::{Message, WebSocket, WebSocketUpgrade},
    extract::State,
    response::IntoResponse,
    routing::get,
    Router,
};
use serde_json::{json, Value};
use std::sync::{Arc, Mutex};
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
            // deterministik üretilir (bkz. obs_protocol::pseudo_uuid). Sahne SIRASI ters
            // çevrilmeden scene_names sırasıyla verilir (gerçek istemciyle doğrulanacak —
            // bkz. SESSION_NOTES Aşama 5). currentPreviewScene* studio mode olmadığından null.
            let names = state.scene_names.lock().unwrap().clone();
            let cur_idx = state.current_scene_idx.load(Ordering::Relaxed) as usize;
            let cur_name = names.get(cur_idx).cloned().unwrap_or_default();
            let scenes: Vec<Value> = names
                .iter()
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
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

/// Gelen bir istemci mesajının türü.
enum ClientMsg {
    /// obs-websocket Identify (op 1), istenen rpcVersion ile.
    Identify { rpc: Option<u64> },
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

/// Text mesajını sınıflandırır. `op` alanına göre obs/legacy ayrımı yapılır.
fn classify(text: &str) -> ClientMsg {
    match serde_json::from_str::<Value>(text) {
        Ok(v) => match v.get("op").and_then(Value::as_u64) {
            Some(op) if op as u8 == obs_op::IDENTIFY => ClientMsg::Identify {
                rpc: v.get("d").and_then(|d| d.get("rpcVersion")).and_then(Value::as_u64),
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
        },
        // JSON değil → eski davranışla uyumlu şekilde legacy yolundan geçir
        Err(_) => ClientMsg::Legacy,
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

async fn handle_socket(mut socket: WebSocket, state: Arc<WsState>) {
    // Hello (op 0) gönder — hemen ardından normal select! döngüsüne girilir.
    // AYRI BLOKLAYAN "Identify bekleniyor" adımı YOK: Identify ve soft-timeout
    // döngü içinde ele alınır; evt_rx metrik akışı ilk andan itibaren kesintisiz iletilir.
    match serde_json::to_string(&obs_protocol::hello()) {
        Ok(hello) => {
            if socket.send(Message::Text(hello.into())).await.is_err() {
                return;
            }
        }
        Err(_) => return,
    }

    let mut evt_rx = state.evt_rx.subscribe();
    let mut identified = false;

    // Identify için soft-timeout: süre dolarsa YALNIZCA log'lanır — bağlantı kapatılmaz,
    // event akışı hiç kesilmez. Legacy istemci (obs handshake yapmayan) böyle sürdürülür.
    let identify_deadline = tokio::time::sleep(IDENTIFY_TIMEOUT);
    tokio::pin!(identify_deadline);
    let mut deadline_fired = false;

    loop {
        tokio::select! {
            // Metrik eventleri → istemciye (handshake beklenirken DE paralel çalışır)
            evt = evt_rx.recv() => {
                if let Ok(data) = evt {
                    if socket.send(Message::Text(data.into())).await.is_err() {
                        break;
                    }
                }
            }
            // Gelen mesajlar
            msg = socket.recv() => {
                match msg {
                    Some(Ok(Message::Text(text))) => {
                        match classify(&text) {
                            ClientMsg::Identify { rpc } => {
                                if identified {
                                    eprintln!("[WS] tekrar Identify geldi (yok sayıldı)");
                                } else {
                                    // rpcVersion 1 değilse yine de 1 olarak negotiate et (esnek)
                                    if rpc != Some(obs_protocol::RPC_VERSION as u64) {
                                        eprintln!(
                                            "[WS] Identify rpcVersion={:?}, {} olarak negotiate ediliyor",
                                            rpc, obs_protocol::RPC_VERSION
                                        );
                                    }
                                    match serde_json::to_string(&obs_protocol::identified()) {
                                        Ok(id) => {
                                            if socket.send(Message::Text(id.into())).await.is_err() {
                                                break;
                                            }
                                            identified = true;
                                            eprintln!("[WS] obs-websocket istemcisi identified");
                                        }
                                        Err(_) => break,
                                    }
                                }
                            }
                            // op 6 Request → dispatch. Identify zorunlu değil (toleranslı mod):
                            // Identify gelmeden gelen Request de işlenir, reddedilmez.
                            ClientMsg::Request { request_type, request_id, request_data } => {
                                if !identified {
                                    eprintln!(
                                        "[WS] Identify'sız Request işleniyor (toleranslı mod): {}",
                                        request_type
                                    );
                                }
                                let resp = dispatch_request(&request_type, &request_id, &request_data, &state);
                                match serde_json::to_string(&resp) {
                                    Ok(s) => {
                                        if socket.send(Message::Text(s.into())).await.is_err() {
                                            break;
                                        }
                                    }
                                    Err(_) => break,
                                }
                            }
                            // Identify/Request dışı obs mesajları yalnızca log'lanır
                            ClientMsg::Obs => {
                                eprintln!("[WS] obs mesajı (bu aşamada işlenmiyor): {}", text);
                            }
                            // Legacy komut yolu — davranış değişmez
                            ClientMsg::Legacy => handle_legacy_cmd(&text, &state),
                        }
                    }
                    _ => break,
                }
            }
            // Identify soft-timeout: tek sefer, yalnızca log; akışı bloklamaz/kesmez
            _ = &mut identify_deadline, if !identified && !deadline_fired => {
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

fn log_to_file(msg: &str) {
    use std::io::Write;
    if let Ok(mut f) = std::fs::OpenOptions::new()
        .create(true).append(true)
        .open("C:\\reji-studio\\ws_debug.log")
    {
        let _ = writeln!(f, "{}", msg);
    }
    eprintln!("{}", msg);
}
