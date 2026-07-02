// ws_obs_protocol_test.rs — Faz 1 Aşama 1: obs-websocket handshake testleri
//
// Kapsam:
//  - Hello (op 0) → Identify (op 1) → Identified (op 2) akışı
//  - Legacy {cmd} regresyonu: handshake sonrası eski komut yolu çalışmaya devam ediyor
//  - Toleranslı davranış: Identify göndermeyen legacy istemci koparılmıyor
//  - Identify timeout sonrası bağlantı legacy olarak sürüyor (kapatılmıyor)
use std::sync::Arc;
use std::time::Duration;

use futures_util::{SinkExt, StreamExt};
use reji_orchestrator::ws_server::{self, WsState};
use serde_json::{json, Value};
use tokio::net::TcpStream;
use tokio::sync::broadcast;
use tokio_tungstenite::tungstenite::Message;
use tokio_tungstenite::{MaybeTlsStream, WebSocketStream};

type Client = WebSocketStream<MaybeTlsStream<TcpStream>>;

/// OS'ten boşta bir port alır (bind edip hemen bırakır).
fn free_port() -> u16 {
    let l = std::net::TcpListener::bind("127.0.0.1:0").expect("bind 0");
    l.local_addr().expect("local_addr").port()
}

/// Verilen state ile sunucuyu belirtilen portta ayrı görevde başlatır.
fn spawn_server(port: u16, state: Arc<WsState>) {
    tokio::spawn(async move {
        ws_server::serve(vec![port], "127.0.0.1", state).await;
    });
}

/// Boş kanallarla varsayılan bir WsState üretir; cmd tarafını gözlemlemek için
/// abone edilebilir Receiver ve event yayınlamak için evt Sender döndürür.
fn make_state() -> (Arc<WsState>, broadcast::Receiver<String>, broadcast::Sender<String>) {
    let (cmd_tx, cmd_rx) = broadcast::channel(16);
    let (evt_tx, _evt_rx) = broadcast::channel(16);
    let state = Arc::new(WsState { cmd_tx, evt_rx: evt_tx.clone() });
    (state, cmd_rx, evt_tx)
}

/// Sunucu dinlemeye başlayana kadar tekrar deneyerek bağlanır.
async fn connect(port: u16) -> Client {
    let url = format!("ws://127.0.0.1:{}/ws", port);
    for _ in 0..100 {
        if let Ok((ws, _)) = tokio_tungstenite::connect_async(&url).await {
            return ws;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    panic!("sunucuya bağlanılamadı: {}", url);
}

/// Sıradaki text mesajını JSON olarak okur (ping/pong'ları atlar).
async fn next_json(ws: &mut Client) -> Value {
    loop {
        match ws.next().await {
            Some(Ok(Message::Text(t))) => {
                return serde_json::from_str(&t).expect("geçerli JSON");
            }
            Some(Ok(Message::Ping(_))) | Some(Ok(Message::Pong(_))) => continue,
            other => panic!("beklenmeyen mesaj: {:?}", other),
        }
    }
}

async fn send_text(ws: &mut Client, v: Value) {
    ws.send(Message::Text(v.to_string().into())).await.expect("send");
}

#[tokio::test]
async fn hello_identify_identified_akisi() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;

    // 1) İlk mesaj Hello (op 0) olmalı
    let hello = next_json(&mut ws).await;
    assert_eq!(hello["op"], 0, "ilk mesaj Hello (op 0) olmalı");
    assert_eq!(hello["d"]["rpcVersion"], 1);
    assert!(
        hello["d"]["obsWebSocketVersion"].is_string(),
        "obsWebSocketVersion alanı olmalı"
    );

    // 2) Identify (op 1) gönder
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;

    // 3) Identified (op 2) gelmeli
    let identified = next_json(&mut ws).await;
    assert_eq!(identified["op"], 2, "Identified (op 2) beklenir");
    assert_eq!(identified["d"]["negotiatedRpcVersion"], 1);
}

#[tokio::test]
async fn legacy_cmd_identify_sonrasi_eski_yoldan_gecer() {
    let (state, mut cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;

    // Handshake'i tamamla
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    // Eski {cmd} formatı — regresyon: hâlâ kabul edilmeli, cmd_tx'e düşmeli
    send_text(&mut ws, json!({"cmd": "stream_start"})).await;

    let received = tokio::time::timeout(Duration::from_secs(2), cmd_rx.recv())
        .await
        .expect("cmd zaman aşımı")
        .expect("cmd kanalı kapandı");
    assert_eq!(received, "stream_start", "legacy komut eski yoldan işlenmeli");
}

#[tokio::test]
async fn legacy_istemci_identify_gondermeden_calisir() {
    // Toleranslı mod: Hello alınır, Identify GÖNDERİLMEZ, doğrudan legacy {cmd} gönderilir.
    // Bağlantı koparılmamalı ve komut işlenmelidir (control.html senaryosu).
    let (state, mut cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;

    send_text(&mut ws, json!({"cmd": "stream_start"})).await;

    let received = tokio::time::timeout(Duration::from_secs(2), cmd_rx.recv())
        .await
        .expect("cmd zaman aşımı")
        .expect("cmd kanalı kapandı");
    assert_eq!(received, "stream_start");
}

#[tokio::test]
async fn event_akisi_handshake_beklerken_bloklanmaz() {
    // KRİTİK REGRESYON: Identify GÖNDERİLMEZ (control.html gibi sadece metrik izleyen
    // istemci). Hello alındıktan sonra yayınlanan metrik event'i, 5sn Identify timeout'unu
    // BEKLEMEDEN anında iletilmeli. Bloklayan handshake olsaydı event 5sn gecikirdi.
    let (state, _cmd_rx, evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await; // Hello

    // Sunucunun evt_rx'e subscribe olması Hello gönderiminin hemen ardından olur; broadcast
    // "subscribe'dan önceki mesajı almaz" olduğundan subscribe race'ini aşmak için 100ms
    // aralıkla tekrar yayınla. Event akışı bloklanmıyorsa 2sn (< 5sn timeout) içinde gelmeli.
    let publisher = tokio::spawn(async move {
        for _ in 0..40 {
            let _ = evt_tx.send(r#"{"fps":60.0,"kbps":6000}"#.to_string());
            tokio::time::sleep(Duration::from_millis(100)).await;
        }
    });

    let got = tokio::time::timeout(Duration::from_secs(2), async {
        loop {
            let v = next_json(&mut ws).await;
            if v.get("fps").is_some() {
                return v;
            }
        }
    })
    .await
    .expect("metrik event 2sn içinde gelmeli — handshake bekleyişi akışı bloklamamalı");

    assert_eq!(got["fps"], 60.0);
    publisher.abort();
}

#[tokio::test]
async fn identify_timeout_sonrasi_legacy_olarak_surer() {
    // Identify hiç gönderilmez; IDENTIFY_TIMEOUT (5s) dolduktan sonra bağlantı
    // legacy sayılmalı ve KAPATILMAMALI. Sonrasında gönderilen komut işlenmeli.
    let (state, mut cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;

    // Timeout süresini biraz aşacak kadar bekle (5s + pay)
    tokio::time::sleep(Duration::from_millis(5300)).await;

    // Bağlantı hâlâ açık olmalı — legacy komut işlenmeli
    send_text(&mut ws, json!({"cmd": "stream_stop"})).await;

    let received = tokio::time::timeout(Duration::from_secs(2), cmd_rx.recv())
        .await
        .expect("timeout sonrası bağlantı kapanmış olabilir")
        .expect("cmd kanalı kapandı");
    assert_eq!(received, "stream_stop");
}
