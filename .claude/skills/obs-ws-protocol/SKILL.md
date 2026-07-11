---
name: obs-ws-protocol
description: Reji Studio'nun obs-websocket v5 protokol implementasyonu (Faz 1) üzerinde çalışma rehberi. src/orchestrator/src/obs_protocol.rs veya ws_server.rs'e dokunan, yeni obs-websocket request/event ekleyen, Hello/Identify/Identified handshake'i değiştiren, WebSocket bağlantı sorunlarını ayıklayan veya OBS uyumlu istemcilerle (obs-websocket-js, Streamdeck, Touch Portal) test yapan HER görevde bu skill'i kullan. Kullanıcı "obs-websocket", "ws", "op code", "Identify", "protokol", "Faz 1" veya "remote control" dediğinde de tetiklenir.
---

# obs-websocket Protokolü — Reji Studio Faz 1

Hedef: Rust orchestrator'ın obs-websocket **v5** uyumlu sunucu olması,
mevcut legacy (control.html) istemcileri kırmadan.

## Dosya haritası — request eklerken dokunulacaklar

| Dosya | Rol |
|---|---|
| `src/orchestrator/src/obs_protocol.rs` | Envelope (`WsEnvelope`), op sabitleri, `hello()`, `identified()` üreticileri |
| `src/orchestrator/src/ws_server.rs` | Bağlantı döngüsü, `ClientMsg` sınıflandırma, Identify soft-timeout |
| `src/orchestrator/tests/ws_obs_protocol_test.rs` | Protokol testleri — her yeni op/request için test şart |
| `src/orchestrator/src/event_bus.rs` | Event'leri obs event'lerine haritalarken kaynak |
| `src/orchestrator/src/control.html` | Legacy istemci — davranışı BOZULMAMALI |

## Projenin mimari kararları (spec'ten sapmalar — bilerek)

1. **Toleranslı handshake:** obs-websocket spec'i Identify gelmezse bağlantıyı
   kapatmayı öngörür. Reji Studio kapatmaz: soft-timeout dolunca istemci
   *legacy* sayılır, event akışı kesintisiz sürer. Bu davranışı "spec'e uydurmak"
   için değiştirme — control.html buna dayanır.
2. **Sınıflandırma `op` alanıyla:** JSON'da `op` varsa obs mesajı, yoksa legacy.
   `ClientMsg` enum'u bu ayrımın tek noktası; yeni op'lar buraya eklenir.
3. **Tekrar Identify yok sayılır** (log'lanır, bağlantı kapatılmaz).

## obs-websocket v5 hızlı referans

- Op kodları: 0 Hello, 1 Identify, 2 Identified, 3 Reidentify, 5 Event,
  6 Request, 7 RequestResponse, 8 RequestBatch, 9 RequestBatchResponse
- Zarf: `{"op": <int>, "d": {...}}`
- Request `d`: `requestType`, `requestId`, `requestData`
- RequestResponse `d`: `requestType`, `requestId`,
  `requestStatus: {result: bool, code: int, comment?}`
- Event `d`: `eventType`, `eventIntent`, `eventData`
- Handshake sonrası her mesaj Identify'daki `rpcVersion`'a (1) tabidir.
- Auth (V8/I8, IMPLEMENTE): Parola AYARLIYKEN Hello'da `authentication`
  {challenge, salt}; istemci `base64(sha256(base64(sha256(pw+salt))+challenge))`
  hesaplayıp Identify'da gönderir. `obs_protocol::{compute_auth, verify_auth,
  gen_salt_challenge}` (saf, sabit-zamanlı). Parola YOKken auth kapalı (bugünkü
  toleranslı davranış). Detay: aşağıdaki "Auth (I8)" bölümü.

Detay gerekirse resmi spec: obsproject/obs-websocket → docs/generated/protocol.md
(değişiklikte web'den güncel halini doğrula).

## Yeni request tipi ekleme prosedürü

1. `obs_protocol.rs`: request/response `d` gövdesi için serde struct'ları ekle;
   mevcut `hello()`/`identified()` üretici desenini izle.
2. `ws_server.rs`: `ClientMsg` sınıflandırmasına op 6 dalını genişlet;
   `requestType` eşleşmesini tek `match`'te topla. Bilinmeyen requestType'a
   spec'e uygun `requestStatus.code` (örn. 204 UnknownRequestType) döndür.
3. Orchestrator içi işi **event_bus/rules üzerinden** yap — ws_server'a
   iş mantığı gömme; o sadece protokol katmanı.
4. Test: `ws_obs_protocol_test.rs`'e (a) mutlu yol, (b) bozuk `d` gövdesi,
   (c) Identify'sız istemcinin request atması senaryolarını ekle.
5. Çalıştır: `just test`
6. Elle doğrulama (opsiyonel ama önerilir):
   ```bash
   # wscat ile
   wscat -c ws://127.0.0.1:<port>
   > {"op":1,"d":{"rpcVersion":1}}
   > {"op":6,"d":{"requestType":"GetVersion","requestId":"1"}}
   ```
   veya obs-websocket-js istemcisiyle gerçek uyumluluk testi.

## Gözden geçirme kontrol listesi

- [ ] Legacy yol (op'suz mesaj) hâlâ çalışıyor mu? control.html elle test edildi mi?
- [ ] Soft-timeout davranışı korunuyor mu (bağlantı kapatma EKLENMEDİ)?
- [ ] `requestId` yanıtta bire bir geri dönüyor mu?
- [ ] Hata yanıtları spec code'larıyla mı (kendi uydurma kodlarınla değil)?
- [ ] Yeni event, Identify'daki eventSubscriptions bitmask'ine saygılı mı?
- [ ] Commit mesajı formatı: `feat(ws): Faz1 Aşama N — <özet>`

## Faz 1 kapsam takibi

Aşama 1 (Hello/Identify/Identified) ✅ — commit 61c8057.
Aşama 2 (Request op 6 / RequestResponse op 7) ✅ — dispatch mekanizması + 4 requestType:
  - `GetVersion` — obsWebSocketVersion, rpcVersion=1, platform, rejiStudioVersion
  - `StartStream` — cmd_tx→"stream_start"e delege (legacy ile aynı kanal)
  - `StopStream` — cmd_tx→"stream_stop"e delege
  - `GetStreamStatus` — Aşama 3'te tam 8 alana tamamlandı (aşağı bak)
  Bilinmeyen requestType → 204, bağlantı kapatılmaz. Identify zorunlu değil (Request için de).
  `streaming_active` tek yazma noktası: `ws_server::process_stream_cmd()` (cmd tüketici tarafı).
Aşama 3 (GetStreamStatus tam alan seti + MetricState) ✅ — obs-websocket v5 spec'inin 8 alanı:
  - Gerçek: `outputActive`, `outputDuration` (stream_started_at_ms'ten), `outputTimecode`
    (`obs_protocol::format_timecode`), `outputSkippedFrames` (`MetricState.frame_drops()`)
  - 0/false (bilinçli, dürüstlük ilkesi — stub/sayaç yok): `outputBytes` (SRT stub),
    `outputReconnecting`, `outputCongestion`, `outputTotalFrames`
  - `WsState.metric_state` = FfiState._metric_state ile AYNI Arc; `stream_started_at_ms` de
    `process_stream_cmd` içinde (tek yazma noktası) güncellenir.
Aşama 4 (FFI/C++ scene altyapısı) ✅ — `rj_push_scene_names` (C++ UI → `scene_names`),
  `rj_user_event_scene_switch` (C++ → `current_scene_idx`, tek gerçek kaynak),
  `RJ_WS_CMD_SET_SCENE=5` komut kodu + C++ drain. Handler'lar bu aşamada YOK (Aşama 5).
Aşama 5 (GetSceneList / SetCurrentProgramScene) ✅ — iki sahne handler'ı:
  - `GetSceneList` — `scene_names` + `current_scene_idx`'ten yanıt; C++'ın DOĞRULADIĞI son
    durum (iyimser değil). `scenes[]`: sceneIndex/sceneName/sceneUuid. currentPreviewScene* = null.
  - `SetCurrentProgramScene` — isim `scene_names`'te bulunursa `ws_command_queue`'ya (5, idx)
    push + code 100; bulunamazsa code **600** (ResourceNotFound). `current_scene_idx`'i BURADA
    güncellemez (gerçek geçiş C++'tan `rj_user_event_scene_switch` ile geri döner — tek gerçek kaynak).
  - **pseudo-UUID:** `obs_protocol::pseudo_uuid(name)` — isimden deterministik 8-4-4-4-12 hex.
    Gerçek/çakışmasız UUID DEĞİL, isim-başına kararlı tanımlayıcı (dürüstlük ilkesi).
    (Aşama 6'da KESİNLEŞTİ: hiçbir istemci akışı UUID ile seçim yapmıyor → zararsız.)
  - **Sahne sırası:** Aşama 6'da KESİNLEŞTİ — obs-ws v5 konvansiyonu `sceneIndex 0`'ı UI'nın
    EN ALTINA koyar, diziyi alttan üste verir. `scene_names` (üstten alta) `.rev().enumerate()`
    ile ters çevrilir. Artık "belirsiz" DEĞİL; ters çevirme GEREKLİ ve uygulandı (commit 3f6e2cf).
  - Kapsam dışı: `CreateScene`/`RemoveScene`/`SetSceneName` (sahne CRUD'u yalnızca UI'dan).

Aşama 6 (gerçek istemci doğrulaması) ✅ — obs-websocket-js 5.0.8 / simpleobsws / ham JSON ile test:
  - **Alt-protokol seçimi eklendi (commit 1fa47d5):** `ws_handler` `obswebsocket.json` teklif
    edilirse echo'lar. ÖNCESİNDE hiçbir alt-protokol seçilmiyordu → TÜM obs-websocket-js istemcileri
    *"Server sent no subprotocol"* ile kopuyordu (kök blokör). Legacy istemciler (alt-protokol teklif
    etmez) etkilenmez. (`obswebsocket.msgpack` o aşamada desteklenmiyordu → Aşama 7'de eklendi, aşağı bak.)
  - Sahne sırası ters çevrildi (yukarı bak). pseudo-UUID zararsız doğrulandı. Paralel Identify'lı +
    legacy bağlantı çalışıyor, legacy soft-timeout'ta kapatılmıyor. StartStream/StopStream
    obs-ws seviyesinde çalışıyor (`outputActive`/süre/timecode gerçek) ama `outputBytes=0` (SRT stub).
  - Doğrulama araçları: `scripts/test_obs_json.py`, `test_obs_parallel.py`, `test_obs_websocket_js.js`.
  - Fiziksel Stream Deck/Companion donanımı test edilmedi (yok) — istemci kütüphaneleriyle doğrulandı.

Aşama 7 (msgpack serileştirme) ✅ — `obswebsocket.msgpack` alt-protokolü tam destekli:
  - **Tek mantık, iki kodlama:** `ws_server::WireMode { Json, Msgpack }` (bağlantı bazlı yerel,
    WsState'e KONMAZ) + `encode()` — aynı `{op, d}` zarfı Text+JSON veya Binary+MessagePack
    (`rmp_serde::to_vec_named`) olarak yazılır. Gelen tarafta `classify_value` iki telin ortak
    sınıflandırıcısı. Alt-protokol seçimini axum upgrade sonrası `WebSocket::protocol()` raporlar;
    teklif yoksa Json varsayılan (control.html/legacy birebir korunur).
  - **Yanlış çerçeve = protokol ihlali → KAPAT** (spec MessageDecodeError): msgpack telinde Text
    frame veya çözülemeyen binary gövde bağlantıyı kapatır. Bu, Aşama 1'in toleranslı handshake'iyle
    ÇELİŞMEZ — orada belirsizlik (Identify gelmedi), burada net ihlal (istemci kodlamayı zaten seçti).
  - **op'suz legacy metrik eventi msgpack teline YAZILMAZ** (canlı bulgu: simpleobsws'in recv
    döngüsü KeyError('op') ile ölüyor, sonraki request'ler timeout). JSON telinde event davranışı
    değişmedi. Regresyon testi: `msgpack_modunda_legacy_event_iletilmez`.
  - Canlı doğrulama: obs-websocket-js Node-varsayılan (msgpack) modu artık PASS (Aşama 6'da FAIL),
    simpleobsws (msgpack-only) uçtan uca çalışıyor. Fiziksel Stream Deck/Companion kurulumu yine
    test edilmedi (yok) — kütüphane seviyesinde doğrulandı.
  - Bağımlılık: `rmp-serde = "1"` (workspace). Testler: ws suite 19 → 23 (5 yeni msgpack/json-guard
    testi; Aşama 6'nın geçici "msgpack seçilmez" testi kaldırıldı — davranış bilinçli tersine döndü).

Auth (V8/I8) ✅ — obs-websocket v5 kimlik doğrulaması (11.07, 7 commit):
  - **Oturum-düzeyi, tek bayrak:** `Session { identified, authenticated, auth }`.
    Parola AYARLIYKEN doğrulanmamış oturumdan Identify DIŞI HER mesaj (obs Request
    VE legacy `{cmd}`) → **4007**; yanlış/eksik Identify authentication → **4009**.
    Parola YOKken `authenticated` vacuously true → bugünkü davranış BİT-AYNI.
  - **Kripto:** `obs_protocol::{compute_auth, verify_auth (subtle sabit-zamanlı),
    gen_salt_challenge (getrandom)}`; `hello_with_auth`. Deps: sha2/base64/getrandom/subtle.
  - **Kritik bulgu (Faz 0):** I8 açığı legacy `{cmd}` yolundaydı (obs handshake'ini
    baypas ediyordu); tek bayrak kuralı onu da kapatır. Origin kontrolü çürütüldü
    (saldırgan sekme = control.html, aynı origin). control.html obs-auth'a
    minimal yükseltildi (crypto.subtle, localhost secure context).
  - **Parola kaynağı:** `rj_set_ws_password` FFI + SettingsDialog + startup senkronu
    (I19 deseni). Boş = kapalı. Her bağlantıda taze okunur (çalışırken değişim →
    yalnız yeni bağlantılar). Parola LOGLANMAZ.
  - **close_with(code, reason):** obs close-code altyapısı (Message::Close, WireMode
    bağımsız). `obs_protocol::close_code::{NOT_IDENTIFIED=4007, AUTHENTICATION_FAILED=4009}`.
  - **Spec sapmaları:** ikinci Identify → yoksay+log (4008 değil); parola ayarlıyken
    de soft-timeout yalnız log. **Kapsam dışı:** rate-limit/brute-force, TLS/wss.
  - Testler: `test_close_with_sends_obs_close_codes` (lib) + auth çekirdeği (obs_protocol)
    + 7 handshake/4007/4009 entegrasyon (ws_obs_protocol_test). Client-side auth
    testte BAĞIMSIZ hesaplanır (sha2/base64 dev-dep).

Sonraki aşamaları TASK dosyası/CONTEXT.md'den doğrula; bu skill'i her aşama
tamamlandığında güncelle (tamamlanan requestType'ların listesini buraya ekle).
