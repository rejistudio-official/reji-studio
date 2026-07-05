# librtmp (RTMPDump çekirdeği — OBS Studio kopyası)

## Kaynak

- Depo: https://github.com/obsproject/obs-studio
- Dizin: `plugins/obs-outputs/librtmp/` (YALNIZCA bu alt dizin)
- Commit: `30d3b89b3bd02c40ca55d071b2fd038a1c40caa6` (2 Temmuz 2026)
- Alınma tarihi: 5 Temmuz 2026

## Lisans

Bu dizindeki kod **LGPL 2.1** lisanslıdır (bkz. `COPYING` ve dosya başlıkları).
RTMPDump projesinin librtmp çekirdeğinin OBS Studio tarafından bakımı yapılan
kopyasıdır: el sıkışma (handshake), chunk'lama ve AMF0 kodlama/çözme.

OBS'e özel GPL v2 dosyaları (`rtmp-stream.c`, `rtmp-helpers.h` vb.
`plugins/obs-outputs/` üst dizinindekiler) **bilinçli olarak ALINMADI** —
yalnızca LGPL 2.1 çekirdek kopyalandı.

## Değişiklikler

Kaynak dosyalarda değişiklik yapılmadı; orijinal lisans başlıkları korundu.
Yerel değişiklik gerekirse bu bölümde listelenmelidir.
