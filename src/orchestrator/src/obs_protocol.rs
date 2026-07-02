// obs_protocol.rs — obs-websocket v5 protokol tipleri (Faz 1)
//
// Bu modül obs-websocket v5 mesaj zarfını ve handshake yardımcılarını sağlar.
// Aşama 1 kapsamı yalnızca handshake'tir: Hello (op 0) → Identify (op 1) → Identified (op 2).
// Request/RequestResponse (op 6/7) sonraki aşamada eklenecek.
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
