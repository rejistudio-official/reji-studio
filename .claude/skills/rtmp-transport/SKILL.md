---
name: rtmp-transport
description: Reji Studio RTMP çıkış katmanında (src/pipeline/rtmp/*.zig, output/rtmp_transport.*, third_party/librtmp) güvenli değişiklik ve hata ayıklama prosedürü. RTMP/FLV/AVCC muxing, librtmp, happy_eyeballs, "yayın platformda görünmüyor", "RTMP bağlantı hatası", ingest testi, stream key veya RTMPS/TLS konularına dokunan HER görevde bu skill'i kullan. Kullanıcı "RTMP", "Twitch", "YouTube yayını", "FLV", "ingest" veya "librtmp" dediğinde de tetiklenir.
---

# RTMP Transport — Reji Studio

Veri yolu: NVENC Annex-B H.264 → `src/pipeline/rtmp/rtmp_transport.zig`
(NAL parse → AVCC/FLV mux → librtmp `RTMP_Write`) → `rj_rtmp_*` C ABI →
`output/rtmp_transport.cpp` (`RtmpTransport : ITransport`) → OutputSubsystem.
Zig lib: `zig build rtmp` → `zig-out/lib/rtmp_transport_zig.lib` (MinGW ABI —
MSVC hedefi @cImport'ta winsock çeviriminden geçemez; x86-64 C ABI aynı).

## Değişmez kurallar

1. **third_party/librtmp'e dokunma** — vendorlanmış LGPL 2.1 kaynak
   (obs-studio 30d3b89b), yerel yama gerekiyorsa README-REJI.md'ye yaz.
2. **NO_CRYPTO korunur** (TLS kararı A). RTMPS gerekirse ayrı aşama (mbedTLS
   yolu açık, rtmp.h USE_MBEDTLS destekli) — OpenSSL'i sessizce ekleme.
3. **URL modeli:** OBS librtmp'te app = URL path'inin TAMAMI, stream key AYRI
   `RTMP_AddStream(key)` ile (RTMP_ParseURL "just.. whatever", parseurl.c:138).
   URL+key'i birleştirip tek string gönderme — sunucu app eşleşmesi bozulur.
4. **Soket blocking kalır:** rtmp.c SO_RCVTIMEO/SNDTIMEO kullanır (yalnız
   blocking sokette çalışır), hiç FIONBIO çağırmaz. happy_eyeballs.zig bu
   yüzden sıralı blocking connect yapar — non-blocking'e çevirme.
5. **Export sınırında panik yok:** Zig'de catch_unwind yok; panik = abort.
   Ayırmalar `catch`'li, sınır kontrolleri açık. `rj_rtmp_shutdown`
   SEH-leaf'ten çağrılır — exception fırlatamaz.
6. **Yayın taze IDR ile başlar:** `Pipeline::start_stream()` →
   `EncodeSubsystem::request_idr()` + NVENC `repeatSPSPPS=1`. Bunu kaldırma —
   muxer SPS'siz kareyi (doğru olarak) düşürür ve platformda hiç video görünmez.

## Hata ayıklama prosedürü

1. `$env:REJI_RTMP_LOG = "C:\yol\rtmp.log"` set et, uygulamayı başlat —
   librtmp'in kendi RTMP_LOGDEBUG akışı + muxer'ın ilk 10 send NAL dökümü
   dosyaya yazılır (GUI'de stderr kaybolur; tek görünürlük yolu budur).
2. Log okuma anahtarları:
   - `NetConnection.Connect.Success` yok → TCP/handshake/app sorunu (URL'i
     ve kural 3'ü kontrol et).
   - `NetStream.Publish.Start` yok → key/izin reddi (Publish.Rejected/BadName).
   - `SPS yok, kare DROP` → IDR talebi akmıyor (kural 6) veya codec H.264 değil
     (HEVC FLV'de desteklenmez — enhanced-RTMP ayrı iş).
3. Yerel uçtan uca test (platform anahtarı gerektirmez):
   ```
   ffmpeg -y -listen 1 -i rtmp://127.0.0.1:1935/live/test -c copy out.flv
   # SettingsDialog: RTMP, URL=rtmp://127.0.0.1:1935/live, key=test
   # yayını başlat/durdur; sonra: ffprobe -count_frames out.flv
   ```
   Beklenen: h264, doğru çözünürlük, kareler tam decode.
4. Zig birim testleri: `zig build rtmp-test` (9 test: TCP connect + NAL/AVCC).

## Build tuzakları (yaşandı, tekrarlama)

- Zig Debug, C kaynaklarına UBSan ekler → librtmp C bayraklarında
  `-fno-sanitize=undefined` kalmalı (MSVC linkinde __ubsan_* yok).
- MinGW obj'lerinin `sscanf/_vsnprintf` başvuruları → CMake'te
  `legacy_stdio_definitions.lib` (reji_pipeline'a ekli, kaldırma).
- `gai_strerrorA` → rtmp_transport.zig'de weak export (MSVC ws2_32.lib'de yok).
- `zig build rtmp` CMake'ten ÖNCE koşulmalı (vulkan_init_zig/ext_bridge kalıbı).
- ws2_32 artık koşulsuz PUBLIC (SRT stub build'inde de librtmp winsock ister).

## Kapsam sınırları (v1 — bilinçli)

Yalnız H.264 video (AAC ses yolu yok), onMetaData yok, RTMPS yok,
happy_eyeballs RFC 8305 paralel yarışı değil. Bunlardan birini eklemeden önce
SESSION_NOTES "Faz 2 Aşama 2" bölümünü oku.
