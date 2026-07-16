# benchmark_compare.py — Reji vs OBS Karşılaştırmalı Benchmark

## Ne Yapar?

Her iki uygulamayı **aynı donanımda aynı sahne/oyunla** çalıştırırken bağlanır;
kare-düşürme, FPS ve bitrate verilerini aynı formatta CSV olarak kaydeder.

Script uygulamaları **başlatmaz veya yönetmez** — her ikisi de elle açık ve yayın yapıyor olmalı.

## Kurulum

```
pip install websockets
```

## Ortamı Hazırlama

1. **Reji'yi başlat** — yayın aktif olacak şekilde aynı sahneyi yükle.
2. **OBS'i başlat** — aynı sahnede Tools → WebSocket Server Settings → Enable (port 4455).
3. Her iki uygulamayı **aynı oyun/kaynakla** eş zamanlı yayın yapacak şekilde ayarla.
4. Script'i **iki uygulama da çalışırken** başlat.

## Kullanım

```bash
# Her ikisini ölç, 60 saniye, 1s aralık
python scripts/benchmark_compare.py

# Yalnızca Reji (OBS çalışmıyor)
python scripts/benchmark_compare.py --target reji

# Yalnızca OBS (Reji çalışmıyor)
python scripts/benchmark_compare.py --target obs

# Süre ve çıktı dosyasını özelleştir
python scripts/benchmark_compare.py --duration 120 --interval 2 --output karsilastirma.csv

# WS şifresi ayarlıysa
python scripts/benchmark_compare.py --reji-pass gizli --obs-pass obs_sifre
```

## Tüm Parametreler

| Parametre | Varsayılan | Açıklama |
|---|---|---|
| `--reji-port` | 0 (otomatik) | Reji WS portu; 0 = 7070-7073 otomatik keşif |
| `--obs-port` | 4455 | OBS WS portu |
| `--reji-pass` | boş | Reji WS şifresi |
| `--obs-pass` | boş | OBS WS şifresi |
| `--duration` | 60 | Örnekleme süresi (saniye) |
| `--interval` | 1.0 | OBS poll aralığı (saniye) |
| `--output` | otomatik | CSV dosya adı |
| `--target` | both | `reji`, `obs` veya `both` |

## Çıktı Formatı

**CSV sütunları:**

| Sütun | Reji kaynağı | OBS kaynağı |
|---|---|---|
| `source` | `reji` | `obs` |
| `timestamp` | UTC ISO-8601 | UTC ISO-8601 |
| `fps` | Legacy metric event `.fps` | `GetStats.activeFps` |
| `bitrate_kbps` | Legacy metric event `.kbps` | `ΔoutputBytes / interval / 125` (yaklaşık) |
| `dropped_frames` | Legacy metric event `.drop` (delta) | `ΔoutputSkippedFrames` (kümülatif→delta) |
| `output_active` | — (event'te yok) | `GetStreamStatus.outputActive` |

Her satır bağımsız bir ölçüm noktasıdır; Reji ve OBS satırları zaman damgasına göre sıralıdır
fakat tam hizalı değildir (Reji event-driven ~1s, OBS `--interval`'a göre).

## Dürüstlük Notları

- OBS için `bitrate_kbps` **yaklaşık** değerdir (obs-websocket v5 doğrudan kbps vermez).
- Reji için `output_active` verisi yoktur (legacy metric event bu alanı içermez).
- Gerçek OBS'e bağlantı testi kullanıcıda kalır — script OBS kurulu olmayan ortamda bunu test edemez.
