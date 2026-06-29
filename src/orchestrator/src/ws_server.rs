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
use tokio::sync::broadcast;

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

pub async fn serve(port: u16, state: Arc<WsState>) {
    let app = Router::new()
        .route("/ws", get(ws_handler))
        .route("/", get(|| async { axum::response::Html(include_str!("control.html")) }))
        .with_state(state);

    let addr = format!("0.0.0.0:{}", port);

    let listener = match tokio::net::TcpListener::bind(&addr).await {
        Ok(l) => {
            log_to_file(&format!("[WS] Listening on ws://{}/ws", addr));
            l
        }
        Err(e) => {
            log_to_file(&format!("[WS] BIND FAILED {}: {}", addr, e));
            return;
        }
    };

    if let Err(e) = axum::serve(listener, app).await {
        log_to_file(&format!("[WS] serve error: {}", e));
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
