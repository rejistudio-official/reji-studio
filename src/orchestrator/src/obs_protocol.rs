// obs_protocol.rs — obs-websocket v5 protokol tipleri (Faz 1)
//
// Bu modül obs-websocket v5 mesaj zarfını, handshake ve request/response yardımcılarını sağlar.
// Aşama 1: handshake — Hello (op 0) → Identify (op 1) → Identified (op 2).
// Aşama 2: Request (op 6) / RequestResponse (op 7) — GetVersion, StartStream, StopStream,
//          GetStreamStatus. Bilinmeyen requestType → code 204 (bağlantı kapatılmaz).
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

/// obs-websocket v5 mesaj zarfı: her mesaj `{op, d}` biçimindedir.
#[derive(Debug, Serialize, Deserialize)]
pub struct WsEnvelope {
    pub op: u8,
    pub d: Value,
}

/// obs-websocket opcode'ları (bu aşamada yalnızca handshake kısmı kullanılıyor).
pub mod op {
    pub const HELLO: u8 = 0;
    pub const IDENTIFY: u8 = 1;
    pub const IDENTIFIED: u8 = 2;
    pub const REQUEST: u8 = 6;
    pub const REQUEST_RESPONSE: u8 = 7;
}

/// Desteklenen tek RPC versiyonu.
pub const RPC_VERSION: u8 = 1;

/// Server tanıtım stringi — auth gerektirmediğimiz için `-reji-compat` soneki.
pub const OBS_WS_VERSION: &str = "5.0.0-reji-compat";

/// Bağlantı açılışında gönderilen Hello (op 0) zarfı.
/// `rpcVersion` server'ın desteklediği versiyonu bildirir; `authentication` alanı yok.
pub fn hello() -> WsEnvelope {
    WsEnvelope {
        op: op::HELLO,
        d: json!({
            "obsWebSocketVersion": OBS_WS_VERSION,
            "rpcVersion": RPC_VERSION,
        }),
    }
}

/// Identify başarıyla işlendiğinde gönderilen Identified (op 2) zarfı.
pub fn identified() -> WsEnvelope {
    WsEnvelope {
        op: op::IDENTIFIED,
        d: json!({
            "negotiatedRpcVersion": RPC_VERSION,
        }),
    }
}

/// obs-websocket RequestStatus kodları (spec: obsproject/obs-websocket).
pub mod request_status {
    /// İstek başarıyla işlendi.
    pub const SUCCESS: u32 = 100;
    /// requestType tanınmadı.
    pub const UNKNOWN_REQUEST_TYPE: u32 = 204;
}

/// Başarılı RequestResponse (op 7) zarfı. `data`, spec'teki `responseData` alanına yazılır.
pub fn request_response_ok(request_type: &str, request_id: &str, data: Value) -> WsEnvelope {
    WsEnvelope {
        op: op::REQUEST_RESPONSE,
        d: json!({
            "requestType": request_type,
            "requestId": request_id,
            "requestStatus": { "result": true, "code": request_status::SUCCESS },
            "responseData": data,
        }),
    }
}

/// Hatalı RequestResponse (op 7) zarfı. `code`/`comment` spec'e uygun verilir;
/// bağlantı kapatılmaz — istemci aynı soket üzerinden yeni istek gönderebilir.
pub fn request_response_err(request_type: &str, request_id: &str, code: u32, comment: &str) -> WsEnvelope {
    WsEnvelope {
        op: op::REQUEST_RESPONSE,
        d: json!({
            "requestType": request_type,
            "requestId": request_id,
            "requestStatus": { "result": false, "code": code, "comment": comment },
        }),
    }
}

/// Milisaniyeyi obs-websocket timecode formatına çevirir: `"HH:MM:SS.mmm"`.
/// Spec örneğiyle aynı: 0 ms → `"00:00:00.000"`.
pub fn format_timecode(ms: u64) -> String {
    let millis = ms % 1000;
    let total_secs = ms / 1000;
    let secs = total_secs % 60;
    let mins = (total_secs / 60) % 60;
    let hours = total_secs / 3600;
    format!("{:02}:{:02}:{:02}.{:03}", hours, mins, secs, millis)
}
