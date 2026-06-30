// ws_server.rs — WebSocket kontrol API
use axum::{
    extract::ws::{Message, WebSocket, WebSocketUpgrade},
    extract::State,
    response::IntoResponse,
    routing::get,
    Router,
};
use serde_json::Value;
use std::sync::Arc;
use std::sync::atomic::{AtomicU16, Ordering};
use tokio::sync::broadcast;

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
}

pub async fn ws_handler(
    ws: WebSocketUpgrade,
    State(state): State<Arc<WsState>>,
) -> impl IntoResponse {
    ws.on_upgrade(move |socket| handle_socket(socket, state))
}

async fn handle_socket(mut socket: WebSocket, state: Arc<WsState>) {
    let mut evt_rx = state.evt_rx.subscribe();
    loop {
        tokio::select! {
            // Gelen komutlar → cmd_tx'e
            msg = socket.recv() => {
                match msg {
                    Some(Ok(Message::Text(text))) => {
                        eprintln!("[WS] cmd: {}", text);
                        if let Ok(v) = serde_json::from_str::<Value>(&text) {
                            let cmd = v["cmd"].as_str().unwrap_or("").to_string();
                            match cmd.as_str() {
                                "stream_start" | "stream_stop" |
                                "scene_cut"    | "scene_fade" => {
                                    let _ = state.cmd_tx.send(cmd);
                                }
                                _ => eprintln!("[WS] unknown cmd: {}", cmd),
                            }
                        }
                    }
                    _ => break,
                }
            }
            // Metrik eventleri → istemciye
            evt = evt_rx.recv() => {
                if let Ok(data) = evt {
                    if socket.send(Message::Text(data.into())).await.is_err() {
                        break;
                    }
                }
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
