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

// NOT: Bu sabitler `pub(crate)` — obs-websocket protokolünün İÇ değerleri, FFI ABI'si DEĞİL.
// `pub` olsalardı cbindgen bunları ffi_auto.h'a export ederdi ve `RPC_VERSION`/`SUCCESS` gibi
// jenerik isimler Windows SDK başlıklarıyla çakışırdı (rpcdcep.h: RPC_VERSION → C2378).
// Crate içinde (ws_server) kullanım için pub(crate) yeterli; sınırın dışına çıkmazlar.

/// obs-websocket opcode'ları (bu aşamada yalnızca handshake kısmı kullanılıyor).
pub(crate) mod op {
    pub(crate) const HELLO: u8 = 0;
    pub(crate) const IDENTIFY: u8 = 1;
    pub(crate) const IDENTIFIED: u8 = 2;
    pub(crate) const REQUEST: u8 = 6;
    pub(crate) const REQUEST_RESPONSE: u8 = 7;
}

/// Desteklenen tek RPC versiyonu.
pub(crate) const RPC_VERSION: u8 = 1;

/// Server tanıtım stringi — auth gerektirmediğimiz için `-reji-compat` soneki.
pub(crate) const OBS_WS_VERSION: &str = "5.0.0-reji-compat";

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
/// `pub(crate)`: iç protokol değerleri, FFI ABI'si değil (bkz. üstteki not — `SUCCESS`
/// gibi isimler C++ global namespace'inde çakışabilir).
pub(crate) mod request_status {
    /// İstek başarıyla işlendi.
    pub(crate) const SUCCESS: u32 = 100;
    /// requestType tanınmadı.
    pub(crate) const UNKNOWN_REQUEST_TYPE: u32 = 204;
    /// İstenen kaynak (ör. sahne) bulunamadı — obs-websocket spec: ResourceNotFound.
    pub(crate) const RESOURCE_NOT_FOUND: u32 = 600;
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

/// İsimden deterministik "pseudo-UUID" üretir: aynı isim → aynı 8-4-4-4-12 hex dizi,
/// farklı çağrılarda kararlı. Reji'de gerçek UUID kavramı yok; obs-websocket istemcileri
/// sahne için bir `sceneUuid` alanı beklediğinden isim başına KARARLI bir tanımlayıcı sunulur.
/// Bu, kriptografik olarak çakışmasız gerçek bir UUID DEĞİLDİR — yalnızca isim-başına
/// kararlı bir kimliktir (dürüstlük ilkesi; bkz. SESSION_NOTES Aşama 5).
pub(crate) fn pseudo_uuid(name: &str) -> String {
    use std::collections::hash_map::DefaultHasher;
    use std::hash::{Hash, Hasher};
    let mut h = DefaultHasher::new();
    name.hash(&mut h);
    let v = h.finish();
    format!(
        "{:08x}-{:04x}-{:04x}-{:04x}-{:012x}",
        (v >> 32) as u32,
        (v >> 16) as u16 & 0xffff,
        v as u16,
        (v >> 48) as u16,
        v & 0xffff_ffff_ffff
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pseudo_uuid_kararli() {
        // Aynı isim iki kez çağrılınca aynı UUID döner (deterministik).
        let a = pseudo_uuid("Sahne A");
        let b = pseudo_uuid("Sahne A");
        assert_eq!(a, b, "aynı isim aynı UUID üretmeli");
        // 8-4-4-4-12 hex biçimi: 32 hex + 4 tire = 36 karakter.
        assert_eq!(a.len(), 36, "UUID uzunluğu 36 olmalı");
        assert_eq!(a.matches('-').count(), 4, "4 tire olmalı");
        // Farklı isim farklı UUID üretmeli (çakışma beklenmez).
        assert_ne!(pseudo_uuid("Sahne A"), pseudo_uuid("Sahne B"));
    }
}
