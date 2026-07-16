# Claude Code Talimatı — Faz 1: OBS-WebSocket Uyumluluk Katmanı (Kontrollü, Aşamalı)

Referans tasarım: `docs/FAZ1_OBS_WEBSOCKET_DESIGN.md` (repoya ekli, önce oku).

**Genel kural:** Her aşama bağımsız commit'tir. Bir aşama build+test geçmeden
bir sonrakine geçme. Her aşama sonunda:
1. `cargo build` (orchestrator) temiz derlensin
2. İlgili test(ler) PASS
3. Kısa bir doğrulama notu `docs/SESSION_NOTES.md`'ye eklensin (SESSION_NOTES.md'deki
   mevcut format ve dil ile tutarlı: neler yapıldı, nasıl doğrulandı)
4. Commit mesajı net ve aşama numarasını içersin, örn:
   `feat(ws): Faz1 Aşama 1 — obs-websocket Hello/Identify/Identified handshake`
5. Bir sonraki aşamaya geçmeden önce bana kısa bir özet ver (ne yapıldı, test sonucu,
   varsa beklenmeyen bulgu) — onay bekleyerek devam et.

---

## Aşama 1 — Handshake iskeleti (Hello/Identify/Identified)

- `ws_server.rs` içine obs-websocket envelope (`{"op":..,"d":{..}}`) parse/serialize eklenir.
- Bağlantı açılışında `Hello` (op 0) gönderilir.
- Gelen `op` alanı varsa yeni yol, yoksa mevcut `{"cmd":...}` yolu (geriye uyumluluk) —
  bunu bozma, regresyon testi olarak mevcut `control.html` davranışını manuel doğrula.
- `Identify` (op 1) → `Identified` (op 2) yanıtı.
- Test: yeni bir `tests/ws_obs_protocol_test.rs` — gerçek TCP soket açıp Hello→Identify→Identified
  round-trip'ini doğrulayan bir `tokio::test`.
- Doğrulama: hem yeni test PASS hem de eski `{"cmd":"stream_start"}` yolu elle (ör. `websocat`
  veya basit bir Python script ile) hâlâ çalışıyor mu kontrol et.

## Aşama 2 — Mevcut komutların adaptasyonu (GetVersion, StartStream, StopStream)

- `Request`(op 6)/`RequestResponse`(op 7) genel dispatch mekanizması.
- `GetVersion`: sabit yanıt (obsWebSocketVersion, rpcVersion, platform vb.).
- `StartStream`/`StopStream`: mevcut `cmd_tx.send("stream_start"/"stream_stop")` çağrısına yönlendir.
- `WsState`'e `streaming_active: Arc<AtomicBool>` ekle, bu iki komutta güncelle.
- Test: `GetVersion` ve `StartStream`→`StopStream` için `requestStatus.code == 100` doğrulayan
  entegrasyon testleri.
- Doğrulama notu: hangi requestType'lar artık destekleniyor, hangileri hâlâ `703 ResourceNotFound`
  dönüyor (ör. GetSceneList henüz yok).

## Aşama 3 — GetStreamStatus

- `streaming_active` + varsa son `MetricSample.bitrate_kbps` kullanarak yanıt oluştur.
- Test: stream_start sonrası `GetStreamStatus.outputActive == true`, stream_stop sonrası `false`.

## Aşama 4 — Scene list FFI (C++ dokunan kısım — daha riskli, ayrı dikkatle)

- Yeni FFI fonksiyonu `rj_push_scene_names` (Rust `ffi.rs` + cbindgen ile üretilen header + C++ tarafı).
- `src/ui/main_window.cpp`: `addScene`/`removeScene`/başlangıçta çağrı eklenir.
- `WsState`'e `scene_names: Arc<Mutex<Vec<String>>>` eklenir.
- Bu aşamada hem Rust hem C++ tarafı derlenmeli — CMake/Cargo build'i tam çalıştır, sadece
  `cargo build` yetmez. Mevcut karakterizasyon test harness'ini (varsa) çalıştırıp regresyon olmadığını doğrula.
- ABI değişikliği olduğu için `sizeof_check`/offsetof assert'lerini kontrol et (yeni struct/pointer
  eklenmiyorsa muhtemelen etkilenmez, ama fonksiyon imzasını FFI_CONTRACT.md'ye ekle).

## Aşama 5 — GetSceneList + SetCurrentProgramScene

- `GetSceneList`: `scene_names`'ten obs-websocket formatında sahne dizisi döner
  (`sceneIndex`, `sceneName`).
- `SetCurrentProgramScene`: `sceneName` → index çözümlemesi (isim eşleşmesi bulunamazsa
  `600 ResourceNotFound` benzeri bir hata dön), sonra mevcut `scene_cut` komutuna yönlendir.
- Test: sahte sahne listesi push edip GetSceneList'in doğru döndüğünü, SetCurrentProgramScene'in
  var olmayan isimde hata döndüğünü doğrulayan testler.
- Tasarım dokümanındaki "sahne adı benzersizliği" riskini burada gözlemleyip bir not düş.

## Aşama 6 — Gerçek istemci testi

- `docs/FAZ1_OBS_WEBSOCKET_DESIGN.md` §5 madde 8-9: obs-cmd veya simpleobsws (Python) ile
  bağlanıp GetSceneList/SetCurrentProgramScene/StartStream/StopStream/GetStreamStatus dene.
- Mümkünse Stream Deck veya Companion ile gerçek bağlantı doğrulaması (ROADMAP.md'nin son maddesi).
- Bu aşama tamamlanınca `docs/ROADMAP.md` Faz 1'in dört checkbox'ını da `[x]` yap ve
  `docs/SESSION_NOTES.md`'ye özet ekle (önceki fazlarda yapıldığı gibi).

---

## Genel Uyarılar

- Mevcut `{"cmd": "..."}` protokolünü hiçbir aşamada kırma — `control.html` buna bağımlı.
- `catch_unwind`/panic-safety prensibini yeni FFI fonksiyonunda da uygula (FFI_CONTRACT.md'deki
  mevcut 13 fonksiyonla aynı standart).
- Her aşamada ilerlemeden önce bana özet + test sonucu raporla, onay bekle.
