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
use reji_orchestrator::metrics::MetricState;
use reji_orchestrator::ws_server::{self, WsState};
use serde_json::{json, Value};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
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
    let state = Arc::new(WsState {
        cmd_tx,
        evt_rx: evt_tx.clone(),
        streaming_active: Arc::new(AtomicBool::new(false)),
        metric_state: MetricState::new(),
        stream_started_at_ms: Arc::new(AtomicU64::new(0)),
        scene_names: Arc::new(std::sync::Mutex::new(Vec::new())),
        current_scene_idx: Arc::new(std::sync::atomic::AtomicU32::new(0)),
        ws_command_queue: Arc::new(crossbeam::queue::ArrayQueue::new(32)),
    });
    (state, cmd_rx, evt_tx)
}

/// Üretimdeki ffi.rs cmd_rx döngüsünün test karşılığı: cmd_tx'i tüketip streaming_active +
/// stream_started_at_ms'i TEK yazma noktası `process_stream_cmd` üzerinden günceller (C++ kuyruğu yok).
fn spawn_cmd_consumer(state: Arc<WsState>) {
    let mut rx = state.cmd_tx.subscribe();
    tokio::spawn(async move {
        while let Ok(cmd) = rx.recv().await {
            let _ = ws_server::process_stream_cmd(
                &cmd,
                &state.streaming_active,
                &state.stream_started_at_ms,
            );
        }
    });
}

/// obs-websocket Request (op 6) gönderir.
async fn send_request(ws: &mut Client, request_type: &str, request_id: &str) {
    send_text(
        ws,
        json!({"op": 6, "d": {"requestType": request_type, "requestId": request_id}}),
    )
    .await;
}

/// GetStreamStatus isteyip yanıttaki outputActive'i okur.
async fn get_output_active(ws: &mut Client) -> bool {
    send_request(ws, "GetStreamStatus", "q").await;
    let r = next_json(ws).await;
    r["d"]["responseData"]["outputActive"].as_bool().unwrap_or(false)
}

/// outputActive istenen değere ulaşana kadar (async tüketici yarışını aşmak için) pollar.
async fn poll_output_active(ws: &mut Client, want: bool) -> bool {
    for _ in 0..50 {
        if get_output_active(ws).await == want {
            return true;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    false
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

// ── Faz 1 Aşama 2: Request (op 6) / RequestResponse (op 7) ─────────────────────

#[tokio::test]
async fn get_version_doner_dogru_alanlar() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    send_request(&mut ws, "GetVersion", "v1").await;
    let resp = next_json(&mut ws).await;

    assert_eq!(resp["op"], 7, "RequestResponse (op 7) beklenir");
    assert_eq!(resp["d"]["requestType"], "GetVersion");
    assert_eq!(resp["d"]["requestId"], "v1", "requestId bire bir geri dönmeli");
    assert_eq!(resp["d"]["requestStatus"]["result"], true);
    assert_eq!(resp["d"]["requestStatus"]["code"], 100, "Success = 100");
    assert_eq!(resp["d"]["responseData"]["rpcVersion"], 1);
    assert!(
        resp["d"]["responseData"]["obsWebSocketVersion"].is_string(),
        "obsWebSocketVersion alanı olmalı"
    );
}

#[tokio::test]
async fn start_stop_stream_streaming_active_gunceller() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    // Tek yazma noktası tüketicisi (üretimde ffi.rs cmd_rx döngüsü)
    spawn_cmd_consumer(state.clone());
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    // Başlangıçta yayın kapalı
    assert!(!get_output_active(&mut ws).await, "başta outputActive false olmalı");

    // StartStream → outputActive true olmalı
    send_request(&mut ws, "StartStream", "s1").await;
    let start_resp = next_json(&mut ws).await;
    assert_eq!(start_resp["d"]["requestStatus"]["code"], 100);
    assert!(
        poll_output_active(&mut ws, true).await,
        "StartStream sonrası outputActive true olmalı"
    );

    // StopStream → outputActive false olmalı
    send_request(&mut ws, "StopStream", "s2").await;
    let stop_resp = next_json(&mut ws).await;
    assert_eq!(stop_resp["d"]["requestStatus"]["code"], 100);
    assert!(
        poll_output_active(&mut ws, false).await,
        "StopStream sonrası outputActive false olmalı"
    );
}

#[tokio::test]
async fn bilinmeyen_request_type_204_doner_baglanti_kapanmaz() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    // Bilinmeyen requestType → code 204, result false
    send_request(&mut ws, "SomeBogusRequest", "b1").await;
    let err = next_json(&mut ws).await;
    assert_eq!(err["op"], 7);
    assert_eq!(err["d"]["requestStatus"]["result"], false);
    assert_eq!(err["d"]["requestStatus"]["code"], 204, "UnknownRequestType = 204");
    assert_eq!(err["d"]["requestId"], "b1");

    // Bağlantı kapanmadı kanıtı: AYNI soketten geçerli GetVersion hâlâ çalışmalı
    send_request(&mut ws, "GetVersion", "b2").await;
    let ok = next_json(&mut ws).await;
    assert_eq!(ok["d"]["requestStatus"]["code"], 100, "hatadan sonra bağlantı sürmeli");
    assert_eq!(ok["d"]["requestId"], "b2");
}

#[tokio::test]
async fn identify_olmadan_request_yine_islenir() {
    // Tasarım kararı 1: Identify GÖNDERİLMEDEN doğrudan Request → yine de işlenir.
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    // Identify YOK — doğrudan GetVersion
    send_request(&mut ws, "GetVersion", "n1").await;
    let resp = next_json(&mut ws).await;

    assert_eq!(resp["op"], 7);
    assert_eq!(resp["d"]["requestStatus"]["code"], 100, "Identify'sız Request de işlenmeli");
    assert_eq!(resp["d"]["responseData"]["rpcVersion"], 1);
    assert_eq!(resp["d"]["requestId"], "n1");
}

// ── Faz 1 Aşama 3: GetStreamStatus tam alan seti + gerçek metrikler ────────────

/// GetStreamStatus'un responseData'sını döndürür (handshake tamamlanmış istemcide).
async fn get_stream_status(ws: &mut Client) -> Value {
    send_request(ws, "GetStreamStatus", "st").await;
    let r = next_json(ws).await;
    r["d"]["responseData"].clone()
}

#[tokio::test]
async fn get_stream_status_tam_alan_seti() {
    // Regresyon guard'ı: obs-websocket v5 spec'inin 8 alanı da (isim + tip) yanıtta olmalı.
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    let data = get_stream_status(&mut ws).await;

    assert!(data["outputActive"].is_boolean(), "outputActive bool olmalı");
    assert!(data["outputReconnecting"].is_boolean(), "outputReconnecting bool olmalı");
    assert!(data["outputTimecode"].is_string(), "outputTimecode string olmalı");
    assert!(data["outputDuration"].is_u64(), "outputDuration number olmalı");
    assert!(data["outputCongestion"].is_number(), "outputCongestion number olmalı");
    assert!(data["outputBytes"].is_u64(), "outputBytes number olmalı");
    assert!(data["outputSkippedFrames"].is_u64(), "outputSkippedFrames number olmalı");
    assert!(data["outputTotalFrames"].is_u64(), "outputTotalFrames number olmalı");

    // Yayın kapalıyken timecode başlangıç değeri
    assert_eq!(data["outputActive"], false);
    assert_eq!(data["outputTimecode"], "00:00:00.000");
    assert_eq!(data["outputDuration"], 0);
}

#[tokio::test]
async fn stream_start_sonrasi_duration_artar() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    spawn_cmd_consumer(state.clone());
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    // StartStream
    send_request(&mut ws, "StartStream", "s1").await;
    let _ = next_json(&mut ws).await;

    // outputDuration > 0 olana kadar polla (async tüketici + geçen süre)
    let mut duration = 0u64;
    for _ in 0..50 {
        tokio::time::sleep(Duration::from_millis(50)).await;
        let data = get_stream_status(&mut ws).await;
        duration = data["outputDuration"].as_u64().unwrap_or(0);
        if duration > 0 {
            // timecode da duration ile tutarlı gerçek olmalı (sıfır değil)
            assert_ne!(data["outputTimecode"], "00:00:00.000", "timecode duration ile ilerlemeli");
            assert_eq!(data["outputActive"], true);
            break;
        }
    }
    assert!(duration > 0, "StartStream sonrası outputDuration > 0 olmalı");
}

#[tokio::test]
async fn stream_stop_sonrasi_duration_sifirlanir() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    spawn_cmd_consumer(state.clone());
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    // Başlat, biraz beklet, durdur
    send_request(&mut ws, "StartStream", "s1").await;
    let _ = next_json(&mut ws).await;
    tokio::time::sleep(Duration::from_millis(80)).await;
    send_request(&mut ws, "StopStream", "s2").await;
    let _ = next_json(&mut ws).await;

    // Stop sonrası duration 0'a dönmeli (tüketici işleyene kadar polla)
    let mut ok = false;
    for _ in 0..50 {
        let data = get_stream_status(&mut ws).await;
        if data["outputDuration"].as_u64() == Some(0) && data["outputActive"] == false {
            assert_eq!(data["outputTimecode"], "00:00:00.000");
            ok = true;
            break;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    assert!(ok, "StopStream sonrası outputDuration 0 olmalı");
}

#[tokio::test]
async fn frame_drops_metric_state_uzerinden_yansir() {
    // MetricState'e doğrudan frame_drops yaz; GetStreamStatus.outputSkippedFrames yansıtmalı.
    let (state, _cmd_rx, _evt_tx) = make_state();
    let observer = state.clone(); // aynı Arc — sunucunun okuduğu MetricState ile birebir
    observer.metric_state.frame_drops.store(42, Ordering::Relaxed);

    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    let data = get_stream_status(&mut ws).await;
    assert_eq!(
        data["outputSkippedFrames"], 42,
        "outputSkippedFrames MetricState.frame_drops'u yansıtmalı"
    );
}

// ── Faz 1 Aşama 5: GetSceneList / SetCurrentProgramScene ───────────────────────

/// GetSceneList isteyip responseData'yı döndürür (handshake tamamlanmış istemcide).
async fn get_scene_list(ws: &mut Client) -> Value {
    send_request(ws, "GetSceneList", "sl").await;
    let r = next_json(ws).await;
    r["d"]["responseData"].clone()
}

/// requestData taşıyan bir Request (op 6) gönderir (SetCurrentProgramScene için).
async fn send_request_data(ws: &mut Client, request_type: &str, request_id: &str, data: Value) {
    send_text(
        ws,
        json!({"op": 6, "d": {"requestType": request_type, "requestId": request_id, "requestData": data}}),
    )
    .await;
}

#[tokio::test]
async fn get_scene_list_bos_liste() {
    // Hiç rj_push_scene_names çağrılmadan (scene_names boş) GetSceneList → boş scenes dizisi,
    // boş currentProgramSceneName; crash yok, bağlantı sürer.
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    let data = get_scene_list(&mut ws).await;
    assert_eq!(
        data["scenes"].as_array().map(|a| a.len()),
        Some(0),
        "boş scenes dizisi olmalı"
    );
    assert_eq!(data["currentProgramSceneName"], "", "boş currentProgramSceneName");
    assert!(data["currentPreviewSceneName"].is_null(), "preview null olmalı");
}

#[tokio::test]
async fn get_scene_list_isimler_dogru() {
    // scene_names'e 3 isim yaz; GetSceneList yanıtında sceneIndex/sceneName eşleşmesini doğrula.
    let (state, _cmd_rx, _evt_tx) = make_state();
    let seeder = state.clone(); // aynı Arc — sunucunun okuduğu scene_names ile birebir
    *seeder.scene_names.lock().unwrap() =
        vec!["Sahne A".to_string(), "Sahne B".to_string(), "Sahne C".to_string()];

    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    let data = get_scene_list(&mut ws).await;
    let scenes = data["scenes"].as_array().expect("scenes dizisi");
    assert_eq!(scenes.len(), 3);
    assert_eq!(scenes[0]["sceneIndex"], 0);
    assert_eq!(scenes[0]["sceneName"], "Sahne A");
    assert_eq!(scenes[1]["sceneIndex"], 1);
    assert_eq!(scenes[1]["sceneName"], "Sahne B");
    assert_eq!(scenes[2]["sceneIndex"], 2);
    assert_eq!(scenes[2]["sceneName"], "Sahne C");
    assert!(scenes[0]["sceneUuid"].is_string(), "sceneUuid alanı olmalı");
    // current_scene_idx varsayılan 0 → currentProgramSceneName ilk sahne
    assert_eq!(data["currentProgramSceneName"], "Sahne A");
}

#[tokio::test]
async fn set_current_program_scene_basarili() {
    // scene_names'e isim yaz, SetCurrentProgramScene gönder → code 100 +
    // ws_command_queue'da (RJ_WS_CMD_SET_SCENE=5, doğru idx).
    let (state, _cmd_rx, _evt_tx) = make_state();
    let observer = state.clone(); // aynı Arc — sunucunun push ettiği kuyruk ile birebir
    *observer.scene_names.lock().unwrap() = vec!["Intro".to_string(), "Live".to_string()];

    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    send_request_data(&mut ws, "SetCurrentProgramScene", "sc1", json!({"sceneName": "Live"})).await;
    let resp = next_json(&mut ws).await;
    assert_eq!(resp["d"]["requestStatus"]["result"], true);
    assert_eq!(resp["d"]["requestStatus"]["code"], 100);
    assert_eq!(resp["d"]["requestId"], "sc1");

    // SetScene komutu C++ kuyruğuna girmeli: (5, idx=1) — "Live" ikinci sahne.
    let mut popped = None;
    for _ in 0..50 {
        if let Some(cmd) = observer.ws_command_queue.pop() {
            popped = Some(cmd);
            break;
        }
        tokio::time::sleep(Duration::from_millis(10)).await;
    }
    assert_eq!(popped, Some((5, 1)), "ws_command_queue'da (SET_SCENE=5, idx=1) olmalı");
}

#[tokio::test]
async fn set_current_program_scene_bulunamadi() {
    // Var olmayan isim → code 600 (ResourceNotFound); bağlantı kapanmaz (sonraki istek çalışır).
    let (state, _cmd_rx, _evt_tx) = make_state();
    let seeder = state.clone();
    *seeder.scene_names.lock().unwrap() = vec!["Intro".to_string()];

    let port = free_port();
    spawn_server(port, state);

    let mut ws = connect(port).await;
    let _hello = next_json(&mut ws).await;
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let _identified = next_json(&mut ws).await;

    send_request_data(
        &mut ws,
        "SetCurrentProgramScene",
        "sc2",
        json!({"sceneName": "YokBoyleSahne"}),
    )
    .await;
    let err = next_json(&mut ws).await;
    assert_eq!(err["d"]["requestStatus"]["result"], false);
    assert_eq!(err["d"]["requestStatus"]["code"], 600, "ResourceNotFound = 600");
    assert_eq!(err["d"]["requestId"], "sc2");

    // Bağlantı kapanmadı kanıtı: AYNI soketten geçerli GetVersion hâlâ çalışmalı.
    send_request(&mut ws, "GetVersion", "after").await;
    let ok = next_json(&mut ws).await;
    assert_eq!(ok["d"]["requestStatus"]["code"], 100, "hatadan sonra bağlantı sürmeli");
    assert_eq!(ok["d"]["requestId"], "after");
}

// ── Faz 1 Aşama 6: Sec-WebSocket-Protocol alt-protokol müzakeresi ──────────────
//
// Gerçek obs-websocket istemcileri (obs-websocket-js/Companion, Touch Portal) handshake'te
// bir alt-protokol teklif eder ve sunucunun onu SEÇMESİNİ bekler; seçilmezse "Server sent no
// subprotocol" ile koparlar. Reji JSON konuşur → yalnızca `obswebsocket.json` echo'lanmalı;
// `obswebsocket.msgpack` (henüz desteklenmiyor) veya teklif yoksa hiçbir şey seçilmemeli.

/// Verilen alt-protokolü teklif ederek bağlanır; (soket, sunucunun SEÇTİĞİ alt-protokol) döndürür.
async fn connect_with_subprotocol(port: u16, offered: &str) -> (Client, Option<String>) {
    use tokio_tungstenite::tungstenite::client::IntoClientRequest;
    use tokio_tungstenite::tungstenite::http::header::SEC_WEBSOCKET_PROTOCOL;
    let url = format!("ws://127.0.0.1:{port}/ws");
    for _ in 0..50 {
        let mut req = url.clone().into_client_request().unwrap();
        req.headers_mut()
            .insert(SEC_WEBSOCKET_PROTOCOL, offered.parse().unwrap());
        if let Ok((ws, resp)) = tokio_tungstenite::connect_async(req).await {
            let selected = resp
                .headers()
                .get(SEC_WEBSOCKET_PROTOCOL)
                .and_then(|v| v.to_str().ok())
                .map(|s| s.to_string());
            return (ws, selected);
        }
        tokio::time::sleep(Duration::from_millis(50)).await;
    }
    panic!("sunucuya bağlanılamadı");
}

#[tokio::test]
async fn subprotocol_json_teklif_edilirse_secilir_ve_handshake_calisir() {
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    let (mut ws, selected) = connect_with_subprotocol(port, "obswebsocket.json").await;
    assert_eq!(
        selected.as_deref(),
        Some("obswebsocket.json"),
        "sunucu obswebsocket.json alt-protokolünü echo'lamalı (obs-websocket-js aksi halde kopar)"
    );

    // Alt-protokol seçilse de akış normal JSON obs-ws handshake'i: Hello → Identify → Identified.
    let hello = next_json(&mut ws).await;
    assert_eq!(hello["op"], 0, "Hello (op 0) gelmeli");
    send_text(&mut ws, json!({"op": 1, "d": {"rpcVersion": 1}})).await;
    let identified = next_json(&mut ws).await;
    assert_eq!(identified["op"], 2, "Identified (op 2) gelmeli");
}

#[tokio::test]
async fn subprotocol_msgpack_teklif_edilirse_secilmez_ve_istemci_koparir() {
    // Reji msgpack konuşmadığından, msgpack-only teklifte HİÇBİR alt-protokol seçilmemeli
    // (yanlışlıkla msgpack seçip JSON frame göndermek istemciyi bozardı). Spec'e uyan istemci
    // (tungstenite, obs-websocket-js) teklif ettiği alt-protokol seçilmezse handshake'i reddeder
    // → msgpack-only istemciler DÜRÜSTÇE bağlanamaz (Aşama 7'de msgpack eklenene dek beklenen).
    use tokio_tungstenite::tungstenite::client::IntoClientRequest;
    use tokio_tungstenite::tungstenite::http::header::SEC_WEBSOCKET_PROTOCOL;
    let (state, _cmd_rx, _evt_tx) = make_state();
    let port = free_port();
    spawn_server(port, state);

    // Önce sunucunun hazır olduğunu (ve alt-protokolsüz legacy yolun çalıştığını) doğrula.
    let _ = connect(port).await;

    // msgpack-only teklif → axum hiçbir şey seçmez → tungstenite handshake'i Err ile reddeder.
    let url = format!("ws://127.0.0.1:{port}/ws");
    let mut req = url.into_client_request().unwrap();
    req.headers_mut()
        .insert(SEC_WEBSOCKET_PROTOCOL, "obswebsocket.msgpack".parse().unwrap());
    let result = tokio_tungstenite::connect_async(req).await;
    assert!(
        result.is_err(),
        "msgpack-only teklifte sunucu alt-protokol seçmemeli → istemci koparmalı (bağlanmamalı)"
    );
}
