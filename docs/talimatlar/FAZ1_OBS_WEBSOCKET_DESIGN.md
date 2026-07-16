# Faz 1 — OBS-WebSocket Protokol Uyumluluk Katmanı — Tasarım

**Durum:** Taslak — implementasyon öncesi inceleme için
**Kapsam:** `docs/ROADMAP.md` Faz 1 maddeleri

---

## 1. Protokol Alt Kümesi (obs-websocket v5, RPC v1)

Kaynak: `github.com/obsproject/obs-websocket` protokol spesifikasyonu.

Tüm mesajlar aynı zarfı kullanır:
```json
{ "op": <int>, "d": { ... } }
```

**Uygulanacak OpCode'lar:**

| OpCode | Ad | Yön | Zorunlu mu? |
|---|---|---|---|
| 0 | Hello | server→client | Evet — bağlantı açılışında |
| 1 | Identify | client→server | Evet — auth yoksa da gönderilir |
| 2 | Identified | server→client | Evet |
| 6 | Request | client→server | Evet |
| 7 | RequestResponse | server→client | Evet |
| 5 | Event | server→client | Faz 1'de opsiyonel (StreamStateChanged vb. Faz 1.1'e ertelenebilir) |

**Kapsam dışı (Faz 1'de yok):** Batch request (op 8/9), Reidentify (op 3), authentication
(challenge/salt SHA256 akışı) — ilk sürüm sadece `localhost`/LAN üzerinde auth'suz çalışacak
(mevcut `ws_server.rs` zaten auth yapmıyor, bu tutarlı).

### Handshake akışı
1. Client bağlanır → sunucu `Hello` (op 0) gönderir: `{"obsWebSocketVersion":"5.x-reji-compat","rpcVersion":1}`
2. Client `Identify` (op 1) gönderir: `{"rpcVersion":1,"eventSubscriptions":0}`
3. Sunucu `Identified` (op 2) gönderir: `{"negotiatedRpcVersion":1}`
4. Bundan sonra client `Request` (op 6) gönderebilir, sunucu `RequestResponse` (op 7) döner.

### Request/Response formatı
```json
// İstek
{"op":6,"d":{"requestType":"GetSceneList","requestId":"uuid","requestData":{}}}
// Yanıt
{"op":7,"d":{"requestType":"GetSceneList","requestId":"uuid",
  "requestStatus":{"result":true,"code":100},"responseData":{...}}}
```
`requestStatus.code`: 100 = Success, 703 = ResourceNotFound (bilinmeyen requestType için kullanılabilir).

---

## 2. Uygulanacak Komutlar ve Mevcut Kod ile Eşleşme

| obs-websocket Request | Mevcut karşılığı | Durum |
|---|---|---|
| `GetVersion` | yok | Yeni — sabit yanıt (feature-detection için Stream Deck/Companion bunu ilk çağırır) |
| `GetStreamStatus` | yok | **Yeni state gerekiyor** (bkz. §3) |
| `StartStream` | `stream_start` cmd zaten var | Adaptör: requestType → mevcut cmd_tx.send("stream_start") |
| `StopStream` | `stream_stop` cmd zaten var | Adaptör: requestType → cmd_tx.send("stream_stop") |
| `GetSceneList` | **yok** | **Yeni state gerekiyor** (bkz. §3) |
| `SetCurrentProgramScene` | `scene_cut`/`scene_fade` var ama isim değil **index** alıyor | Adaptör + isim→index çözümlemesi gerekiyor |

---

## 3. Mimari Boşluk: Sahne Listesi ve Yayın Durumu

Kod taramasında iki önemli eksik bulundu:

### 3.1 Sahne listesi yalnızca C++ UI'da yaşıyor
`src/ui/main_window.cpp`/`.h` içinde `scene_list_` bir `QListWidget` — sahne adları
yalnızca Qt tarafında tutuluyor. Rust orchestrator (`ffi.rs`, `event_bus.rs`) sadece
`SceneSwitch { scene_id: u32 }` biliyor — **isim yok, sadece index**.

`GetSceneList` obs-websocket'te sahne adlarını döndürmek zorunda. Bunun için:
- **Seçenek A (önerilen, düşük risk):** Yeni bir FFI fonksiyonu `rj_push_scene_names(names: *const *const c_char, count: u32)`
  — C++ UI, sahne listesi her değiştiğinde (`addScene`/`removeScene`/başlangıç) bunu çağırır.
  Rust tarafında `Arc<Mutex<Vec<String>>>` olarak `WsState`'e eklenir.
- **Seçenek B:** Rust hiçbir şey bilmesin, `GetSceneList` isteğini FFI üzerinden C++'a
  senkron sorup cevap beklesin — mevcut "FFI'dan sadece veri geçer" prensibiyle çelişir
  (senkron round-trip, pipeline thread'i bloklama riski). **Önerilmiyor.**

### 3.2 "Yayın aktif mi" durumu hiçbir yerde tutulmuyor
`metrics.rs`'teki `MetricSample.bitrate_kbps` yayın sırasında da 0 olabilir (henüz encode
başlamamışsa), güvenilir bir "isStreaming" sinyali değil.

**Çözüm:** `WsState`'e `AtomicBool streaming_active` eklenir. `stream_start`/`stream_stop`
komutları işlenirken (mevcut `cmd_tx.send()` noktasında) bu flag güncellenir.
`GetStreamStatus` bu flag'i ve varsa son `MetricSample`'dan `bitrate_kbps`'i döner.

---

## 4. `ws_server.rs` Değişiklik Planı

```rust
// Yeni: protokol katmanı, mevcut handle_socket() akışını SARMALAR (replace etmez)
mod obs_protocol {
    // OpCode enum, envelope struct, Hello/Identify/Identified handling
    // dispatch_request(request_type, request_data) -> (bool success, Value response_data)
}
```

- Mevcut basit `{"cmd": "..."}` formatı **korunur** (geriye uyumluluk — control.html hâlâ bunu kullanıyor).
- Gelen mesaj `op` alanı içeriyorsa obs-websocket yolu, içermiyorsa eski `cmd` yolu işletilir
  (aynı `/ws` endpoint'inde ayrım `serde_json::Value` üzerinden yapılır).
- `WsState`'e eklenecek alanlar: `scene_names: Arc<Mutex<Vec<String>>>`, `streaming_active: Arc<AtomicBool>`,
  `current_scene_idx: Arc<AtomicU32>`.

---

## 5. Uygulama Adımları (Claude Code için)

1. `src/orchestrator/src/ws_server.rs`: obs-websocket envelope + Hello/Identify/Identified handshake ekle.
2. `WsState`'e `scene_names`, `streaming_active`, `current_scene_idx` alanlarını ekle; `stream_start`/`stream_stop`
   işlenirken `streaming_active` güncelle.
3. `GetVersion`, `GetStreamStatus`, `StartStream`, `StopStream`, `SetCurrentProgramScene` request handler'larını yaz.
4. `GetSceneList` için: yeni FFI fonksiyonu `rj_push_scene_names` (Rust `ffi.rs` + C header + C++ tarafı).
5. `src/ui/main_window.cpp`: `addScene`/`removeScene`/init noktalarında `rj_push_scene_names` çağrısı ekle.
6. `cbindgen.toml`/`ffi_auto.h` otomatik üretimin yeni fonksiyonu kapsadığını doğrula.
7. Test: `tests/` altına `ws_obs_protocol_test.rs` — Hello/Identify/Identified + GetVersion/GetStreamStatus
   round-trip testi (gerçek TCP soket ile, `tokio::test`).
8. Manuel test: obs-cmd veya `simpleobsws` (Python) ile bağlanıp `GetSceneList`/`SetCurrentProgramScene` dene.
9. Stream Deck / Companion ile gerçek bağlantı doğrulaması (ROADMAP.md son maddesi).

**Sıra önerisi:** 1-3 (temel handshake + var olan komutlar) önce, düşük risk. 4-5 (FFI + C++ scene push)
ayrı bir alt-adım, çünkü C++ tarafına dokunuyor ve derleme/test döngüsü daha ağır.

---

## 6. Riskler

- `scene_id` (u32 index) ile obs-websocket'in `sceneName` (string) eşleşmesi: iki sahne aynı adı
  taşıyabilir mi? Şu an C++ UI'da isim benzersizliği zorunlu değil — bu netleştirilmeli.
- `eventSubscriptions` (Event akışı) Faz 1'de yok; Stream Deck/Companion bazı özellikleri için
  event akışı bekleyebilir — ilk testte bu görülürse Faz 1.1 olarak `StreamStateChanged`/
  `CurrentProgramSceneChanged` event'leri eklenmeli.
