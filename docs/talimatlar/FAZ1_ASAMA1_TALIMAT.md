# Claude Code Talimatı — Faz 1 / Aşama 1: obs-websocket Handshake İskeleti

Referans: `docs/FAZ1_OBS_WEBSOCKET_DESIGN.md` ve `docs/FAZ1_CLAUDE_CODE_TALIMAT.md` (repoda,
önce bunları oku). Bu talimat yalnızca **Aşama 1**'i kapsar — sonraki aşamalara geçme,
bu aşama bitince özet + test sonucu raporla ve onay bekle.

## Hedef

`src/orchestrator/src/ws_server.rs` içine obs-websocket v5 protokolünün handshake
iskeletini ekle: `Hello` (op 0) → `Identify` (op 1) → `Identified` (op 2).
Bu aşamada henüz gerçek komut (`Request`/`RequestResponse`, op 6/7) işlenmiyor —
sadece bağlantı açılışı obs-websocket uyumlu hale geliyor.

## Yapılacaklar

1. **Envelope tipi ekle** (`ws_server.rs` içine veya yeni `obs_protocol.rs` modülüne):
   ```rust
   #[derive(serde::Serialize, serde::Deserialize)]
   struct WsEnvelope {
       op: u8,
       d: serde_json::Value,
   }
   ```

2. **Bağlantı açılışında `Hello` gönder** — `handle_socket()` fonksiyonunun en başında,
   `evt_rx`/komut döngüsüne girmeden önce:
   ```json
   {"op":0,"d":{"obsWebSocketVersion":"5.0.0-reji-compat","rpcVersion":1}}
   ```
   (Not: `rpcVersion` alanı `Hello`'da server'ın *desteklediği* versiyonu bildirir, auth
   gerektirmiyoruz, o yüzden `authentication` alanı yok.)

3. **`Identify` mesajını bekle ve işle** — client'tan gelen ilk mesaj `op:1` değilse
   bağlantıyı kapat (obs-websocket spesifikasyonundaki davranış: hatalı sıralama →
   close). `op:1` geldiğinde `d.rpcVersion` oku; 1 değilse yine de 1 olarak
   negotiate et (tek versiyonu destekliyoruz, esnek ol — reddetme).

4. **`Identified` yanıtı gönder**:
   ```json
   {"op":2,"d":{"negotiatedRpcVersion":1}}
   ```

5. **Mevcut akışla birleştir, KIRMA:**
   - Identify tamamlandıktan sonra mevcut `tokio::select!` döngüsüne (cmd recv + evt_rx recv) gir.
   - Gelen mesajlarda ayrım şu şekilde olsun: JSON parse edilebiliyor ve `op` alanı
     içeriyorsa yeni obs-websocket yolu (bu aşamada sadece log'la, henüz işlenmiyor);
     `op` alanı yoksa ve `cmd` alanı varsa **mevcut eski yol** (`stream_start` vb.)
     aynen çalışmaya devam etsin. `control.html`'in gönderdiği mesajlar bu eski yoldan geçmeye
     devam etmeli — davranış değişmemeli.

6. **Hata durumları:**
   - `Identify` yerine başka bir `op` gelirse veya JSON parse hatası olursa bağlantıyı
     `close` et (obs-websocket'in `WebSocketCloseCode` mantığına birebir uymak zorunda
     değilsin, sade bir close yeterli — sadece bağlantıyı sonsuz beklemede bırakma).
   - Identify için timeout ekle (ör. 5 saniye) — client hiç Identify göndermezse bağlantı
     kendiliğinden kapansın (kaynak sızıntısını önler).

## Test

Yeni dosya: `src/orchestrator/tests/ws_obs_protocol_test.rs`

- `tokio::test` ile gerçek bir `TcpListener`/`ws_server::serve()` başlat (test için ayrı porta bind et,
  port çakışmasını önlemek için 0 portu kullanıp OS'in verdiği portu al ya da mevcut `try_bind` mantığını kullan).
- Test istemcisi (`tokio-tungstenite` zaten bağımlılıklarda var mı kontrol et, yoksa dev-dependency
  olarak ekle) ile bağlan:
  1. İlk mesaj olarak `Hello` (op 0) geldiğini doğrula.
  2. `Identify` (op 1, `rpcVersion:1`) gönder.
  3. `Identified` (op 2, `negotiatedRpcVersion:1`) geldiğini doğrula.
- Regresyon testi (aynı dosyada veya ayrı test): Identify sonrası eski `{"cmd":"stream_start"}`
  formatını gönder, sunucunun hâlâ bunu kabul ettiğini (crash/close olmadığını) doğrula.

## Doğrulama Checklist (raporunda bunları belirt)

- [ ] `cargo build` (orchestrator) temiz derlendi
- [ ] Yeni handshake testi PASS
- [ ] Eski `{"cmd":...}` regresyon testi PASS
- [ ] Identify timeout davranışı test edildi (opsiyonel ama tercih edilir)
- [ ] `docs/SESSION_NOTES.md`'ye kısa özet eklendi (mevcut dosyadaki format/dille tutarlı,
      Türkçe, "### Faz 1 — Aşama 1" başlığıyla)
- [ ] Commit: `feat(ws): Faz1 Aşama 1 — obs-websocket Hello/Identify/Identified handshake`
- [ ] Push yapma — commit sonrası bana özet + test çıktısı raporla, push için onay bekle

## Sınır

Bu aşamada `GetSceneList`, `StartStream`, `GetVersion` gibi hiçbir `requestType` işlenmiyor —
sadece handshake. Aşama 2 bunları ele alacak, şimdi ona geçme.
