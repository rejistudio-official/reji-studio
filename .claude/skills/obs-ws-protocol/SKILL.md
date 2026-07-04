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
- Auth: Hello'da `authentication` alanı varsa istemci challenge çözer.
  Reji Studio şimdilik auth'suz — eklerken spec'in SHA256(base64) akışını kullan.

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
  - **Sahne sırası:** ters çevrilmeden `scene_names` sırasıyla — gerçek istemciyle (Aşama 6)
    doğrulanacak, ters çıkarsa düzeltilecek.
  - Kapsam dışı: `CreateScene`/`RemoveScene`/`SetSceneName` (sahne CRUD'u yalnızca UI'dan).

Sonraki aşamaları TASK dosyası/CONTEXT.md'den doğrula; bu skill'i her aşama
tamamlandığında güncelle (tamamlanan requestType'ların listesini buraya ekle).
