// obs_protocol.rs — obs-websocket v5 protokol tipleri (Faz 1)
//
// Bu modül obs-websocket v5 mesaj zarfını, handshake ve request/response yardımcılarını sağlar.
// Aşama 1: handshake — Hello (op 0) → Identify (op 1) → Identified (op 2).
// Aşama 2: Request (op 6) / RequestResponse (op 7) — GetVersion, StartStream, StopStream,
//          GetStreamStatus. Bilinmeyen requestType → code 204 (bağlantı kapatılmaz).
use serde::{Deserialize, Serialize};
use serde_json::{json, Value};

use base64::Engine as _;
use sha2::{Digest, Sha256};

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

/// obs-websocket v5 WebSocketCloseCode (spec: obsproject/obs-websocket) — I8 auth
/// için gereken alt küme. `pub(crate)`: iç protokol değerleri, FFI ABI'si değil.
pub(crate) mod close_code {
    /// Doğrulanmamış oturumdan Identify dışı mesaj (Request veya legacy `{cmd}`).
    pub(crate) const NOT_IDENTIFIED: u16 = 4007;
    /// Identify'daki `authentication` yanlış veya (parola gerekliyken) eksik.
    pub(crate) const AUTHENTICATION_FAILED: u16 = 4009;
}

/// Desteklenen tek RPC versiyonu.
pub(crate) const RPC_VERSION: u8 = 1;

/// Server tanıtım stringi — auth gerektirmediğimiz için `-reji-compat` soneki.
pub(crate) const OBS_WS_VERSION: &str = "5.0.0-reji-compat";

/// obs-websocket v5 WebSocket alt-protokol adı (JSON serileştirme, Text frame).
/// Gerçek obs-websocket istemcileri (obs-websocket-js, Touch Portal) handshake'te
/// `Sec-WebSocket-Protocol` ile bir alt-protokol teklif eder ve sunucunun onu SEÇMESİNİ
/// bekler; seçilmezse "Server sent no subprotocol" ile koparlar (Faz 1 Aşama 6 bulgusu).
pub(crate) const SUBPROTOCOL_JSON: &str = "obswebsocket.json";

/// obs-websocket v5 alt-protokol adı (MessagePack serileştirme, Binary frame) — Aşama 7.
/// Node-varsayılan obs-websocket-js (Companion'ın bağımlılığı) ve simpleobsws bunu teklif
/// eder. Seçildiğinde TÜM trafik binary + msgpack'tir; aynı mantıksal {op, d} zarfının
/// ikinci "teli" (bkz. ws_server::WireMode).
pub(crate) const SUBPROTOCOL_MSGPACK: &str = "obswebsocket.msgpack";

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

/// V8/I8: Parola AYARLIYKEN gönderilen Hello (op 0) — `authentication` alanı
/// istemciye challenge+salt taşır. Parola yokken `hello()` kullanılır (alan yok,
/// bugünkü davranış birebir korunur).
pub fn hello_with_auth(challenge: &str, salt: &str) -> WsEnvelope {
    WsEnvelope {
        op: op::HELLO,
        d: json!({
            "obsWebSocketVersion": OBS_WS_VERSION,
            "rpcVersion": RPC_VERSION,
            "authentication": { "challenge": challenge, "salt": salt },
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

// ===== V8/I8: obs-websocket v5 auth çekirdeği (saf, test edilebilir) =====
//
// Spec (obsproject/obs-websocket protocol.md):
//   secret = base64( sha256( password + salt ) )
//   auth   = base64( sha256( secret + challenge ) )
// Base64 standart (padding'li); sha256 ham 32 byte çıktısı base64'lenir.

// NOT: aşağıdaki auth çekirdeği commit 3'te (handshake entegrasyonu) çağrılacak;
// o zamana dek yalnız birim testlerden kullanılıyor → geçici allow(dead_code).

/// `input`'un SHA-256 özetini standart base64 (padding'li) olarak döndürür.
#[allow(dead_code)]
fn sha256_b64(input: &str) -> String {
    let mut hasher = Sha256::new();
    hasher.update(input.as_bytes());
    base64::engine::general_purpose::STANDARD.encode(hasher.finalize())
}

/// obs-websocket v5 authentication string'ini üretir (spec zinciri).
#[allow(dead_code)]
pub(crate) fn compute_auth(password: &str, salt: &str, challenge: &str) -> String {
    let secret = sha256_b64(&format!("{}{}", password, salt));
    sha256_b64(&format!("{}{}", secret, challenge))
}

/// İstemcinin gönderdiği `client_response`'u beklenen değerle **sabit-zamanlı**
/// karşılaştırır. Uzunluk farkında erken `false` (beklenen değer daima 44 karakter
/// = sha256→base64 sabit uzunluk, dolayısıyla bu dal uzunluk sızdırmaz).
#[allow(dead_code)]
pub(crate) fn verify_auth(password: &str, salt: &str, challenge: &str, client_response: &str) -> bool {
    use subtle::ConstantTimeEq;
    let expected = compute_auth(password, salt, challenge);
    if expected.len() != client_response.len() {
        return false;
    }
    expected.as_bytes().ct_eq(client_response.as_bytes()).into()
}

/// Oturum başına yeni (salt, challenge) üretir — OS CSPRNG'den 32'şer bayt,
/// standart base64. İki değer birbirinden ve bağlantılar arasında bağımsız.
#[allow(dead_code)]
pub(crate) fn gen_salt_challenge() -> (String, String) {
    (random_b64_32(), random_b64_32())
}

#[allow(dead_code)]
fn random_b64_32() -> String {
    let mut buf = [0u8; 32];
    getrandom::getrandom(&mut buf).expect("OS CSPRNG başarısız");
    base64::engine::general_purpose::STANDARD.encode(buf)
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
    fn test_verify_auth_correct_wrong_and_corrupt() {
        // V8/I8: round-trip — compute_auth ile üretilen yanıt verify_auth'tan geçer.
        let (salt, challenge) = ("Zm9vc2FsdA==", "YmFyY2hhbGxlbmdl");
        let good = compute_auth("hunter2", salt, challenge);
        assert!(verify_auth("hunter2", salt, challenge, &good), "doğru parola geçmeli");
        assert!(!verify_auth("wrong", salt, challenge, &good), "yanlış parola reddedilmeli");
        // Bozuk / base64 olmayan / boş istemci yanıtı → false (panik yok).
        assert!(!verify_auth("hunter2", salt, challenge, "!!! not base64 !!!"));
        assert!(!verify_auth("hunter2", salt, challenge, ""));
    }

    #[test]
    fn test_compute_auth_deterministic_and_fixed_length() {
        // Aynı girdi aynı çıktı; sha256→base64 daima 44 karakter (padding'li).
        let a = compute_auth("p", "s", "c");
        let b = compute_auth("p", "s", "c");
        assert_eq!(a, b);
        assert_eq!(a.len(), 44, "sha256(32B)→base64 = 44 karakter");
        // Farklı parola farklı sonuç.
        assert_ne!(compute_auth("p1", "s", "c"), compute_auth("p2", "s", "c"));
    }

    #[test]
    fn test_gen_salt_challenge_unique() {
        let (s1, c1) = gen_salt_challenge();
        let (s2, c2) = gen_salt_challenge();
        assert_ne!(s1, c1, "aynı çağrıda salt≠challenge");
        assert_ne!(s1, s2, "çağrılar arası salt farklı");
        assert_ne!(c1, c2, "çağrılar arası challenge farklı");
        assert_eq!(s1.len(), 44, "32 rastgele bayt → base64 = 44 karakter");
    }

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
