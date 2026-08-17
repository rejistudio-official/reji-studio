# ÖLÇÜM: VFR/CFR Faz 2 — Kare Tekrarı Doğrulaması

**Tarih:** 2026-08-17 · **Tasarım:** `TASARIM_VFR_CFR_FAZ1.md` ·
**Bulgular:** `BULGULAR_VFR_CFR_FAZ0.md`

## Uygulanan commit'ler

1. `feat: SendDiag dup/copy/idrF alanlari` — gözlemlenebilirlik (saf, TDD)
2. `feat: frame_repeat_policy saf cekirdek` — K2 tekrar kararı + K3 IdrCadence (TDD)
3. `feat: WGC encode girdisi kalici kopyaya tasindi` — K1
4. `feat: null tick'te kare tekrari` — K2 (davranışsal çekirdek)
5. `feat: zaman bazli yedek IDR` — K3
6. `fix: yedek IDR esigi 3sn` — ölçümün bulduğu tasarım hatası (aşağıda)

Test durumu: 27/27 ctest yeşil (PipelineCharacterization dahil);
FrameRepeatPolicyTest yeni (9 test).

## Yerel ölçüm (25 sn koşu, WGC + NVENC, yayın kapalı)

Kabul kriterleri → sonuçlar:

| Kriter | Hedef | Ölçülen | Durum |
|---|---|---|---|
| CFR: `frames + dup` | ≈ 60/sn | 60-61 her pencerede | ✅ |
| `copy` (CopyResource submit) | < 1 ms | 0,0-0,1 ms | ✅ |
| `tot` ortalama | < 16,7 ms | 8,7-14,1 ms | ✅ |
| `idrF` | 0 | 0 (düzeltme sonrası) | ✅ |
| Encoder çıkış temposu | 60 paket/sn | pts deltası 60 pakette tam ~1.000.000 µs | ✅ |
| Paket boyutu (statik ağırlıklı) | patlama yok | ~4-5 KB P-frame (≈2,4 Mbps @6000 hedef) | ✅ (kısmi) |

Örnek satır (düzeltme sonrası):

```
[SendDiag] wire_fps=0 frames=44 null=17 dup=17 idrF=0 cap=0.1/0.6ms
capNull=0.2/0.5ms copy=0.0/0.0ms enc=9.4/15.0ms ... tot=13.3/22.3ms
```

### Ölçümün yakaladığı tasarım hatası (6. commit)

İlk koşuda `idrF=1` ~7-9 sn'de bir görünüyordu. Kök neden: doğal GOP
kadansı (120 kare @60 encode/sn) **tam 2,0 sn**; yedek eşik de 2,0 sn —
ikisi jitter'a göre yarışıyordu. Eşik 3 sn'ye çekildi (doğal kadansın
üstünde pay, YouTube 4 sn sınırının altında); test kilidi eklendi
(`DefaultIntervalHasHeadroomAboveNaturalGop`). `idrF=0` doğrulandı.

Tasarımdaki "normal akışta hiç tetiklenmez" beklentisi ancak bu
düzeltmeyle gerçek oldu — `idrF` metriği tam da bu işi yapmak için
eklenmişti (K5 gerekçesi kendini ilk koşuda kanıtladı).

### Faz 0 bulgularının kapanışı

- **A4 (GOP zaman kayması):** Kapandı — encoder artık her tick kare
  aldığından 120 kare ≈ 2 sn; ayrıca 3 sn yedek IDR savunması var.
- **A5 (ses durması):** Mekanizma düzeldi (her tick encode → on_packet →
  drain). Yayınla uçtan uca doğrulama canlı testte (aşağıda).
- **A2 yan bulgu (NVENC kayıt thrash'i):** WGC'de encoder artık tek
  kararlı texture görüyor — kayıt oturum başına bir kez.

## Doğrulanmamış kalanlar (canlı test — kullanıcıda)

Yerel koşu yayınsızdı (`wire_fps=0`, transport yok) — şunlar ancak
gerçek yayında kesinleşir:

1. **Twitch testi (5-10 dk):** ilk yarı hareketli içerik, ikinci yarı
   tam statik BRB ekranı. Twitch Inspector'da: FPS sabit mi, keyframe
   uyarısı var mı, statik bölümde bağlantı/ses kesintisi var mı?
2. **Ses sürekliliği:** statik bölümde `audioDrain(n=…)` sayacının
   `[SendDiag]`'da kesintisiz akması + izleyici tarafında ses.
3. **Statik sahne hat bitrate'i:** tekrar karelerinin gerçek maliyeti
   (yerel paket örnekleri patlama göstermiyor ama saf-tekrar penceresi
   izole ölçülmedi).

## Bilinen sınırlar / notlar

- `tot_max` ara sıra 22-26 ms (keyframe `lock` spike'ı) — Faz 2 öncesi de
  vardı; ortalama bütçenin içinde. Regresyon nöbetçisi: `tot`/`lock`.
- Tekrar artık her tick encode demek: `enc` ortalaması ~9 ms'e çıktı
  (senkron NVENC bekleme her tick ödeniyor). Bütçe içinde; ileride
  gerekirse async encode değerlendirilir (yeni kayıt, taahhüt değil).
- Uygulama sırasında yakalanan tasarım boşluğu: kopya başta yalnız
  WGC'de alınıyordu → DXGI topolojisi VFR kalacaktı. Tasarımın öngördüğü
  fallback'e geçildi (tek kod yolu: kopya İKİ backend'de de alınır;
  ölçülen maliyet ~0,0 ms, keyed-mutex/Vulkan interop'a dokunulmaz —
  kopya kaynağı shared_texture_ değil, encode texture'ı). DXGI
  makinesinde koşu yine de yapılmadı (**doğrulanmadı** işareti sürer;
  bu makine WGC yolunda).
