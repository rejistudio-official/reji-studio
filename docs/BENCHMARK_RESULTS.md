# BENCHMARK_RESULTS — Reji vs OBS Ölçüm Kaydı (2026-07-31)

Bu belge bir **mühendislik kaydıdır, pazarlama materyali değildir.** Amaç:
"bu tarihte, şu koşullarda şunu ölçtük" diye geri dönülebilecek tek referans.
Dışa dönük herhangi bir iddia (README, forum) bu belge okunup "burada gerçekten
anlatmaya değer bir şey var mı" sorusu cevaplandıktan sonra, ayrı bir karar
olarak düşünülecek.

**Dürüstlük kuralı:** Ölçüm sonuçları Reji'nin lehine değil. Bu aynen
yazılmıştır. Sonucu cilalamak veya "koşullar uygun değildi" diye rafa
kaldırmak, tüm ölçümün güvenilirliğini yok eder.

Ham CSV'ler veri artefaktıdır ve commit edilmemiştir; tüm sayılar bu belgede
metin olarak yer alır. Ölçüm aracının kullanımı ve çıktı formatı için:
[`scripts/README_benchmark.md`](../scripts/README_benchmark.md).

---

## 1. Test Kurulumu (tekrarlanabilirlik için)

- **Donanım:** hibrit-GPU laptop — AMD Radeon 780M iGPU + NVIDIA RTX 4070
  Laptop dGPU, Windows 11. Tek makine.
- **Eşleştirilmiş ayarlar (her iki yazılım):** 1920x1080, 60 FPS, 12000 kbps
  CBR, NVENC H.264, preset P4/Balanced, tuning Ultra Low Latency.
- **Hedef:** yerel RTMP dinleyicisi (`ffmpeg -listen 1`, loopback) — ağ
  değişkeni yok.
- **İçerik:** hareketli video. **Not: test videosu 25 FPS'ti** — bu, RTMP
  tarafında gözlenen `null` oranını doğrudan etkiledi (bkz. §6, VFR/CFR).
- **Ölçüm aracı:** `scripts/benchmark_compare.py`, 60 saniye, 1Hz
  toplulaştırılmış veriler üzerinden.

## 2. Sonuçlar — Baseline (12 Mbps)

| | OBS | Reji |
|---|---|---|
| FPS ort | 60.0 | 58.7 |
| FPS min | 60.0 | 48.0 |
| Bitrate | 12173 kbps* | 12000 kbps |
| Dropped frames | 0 | 0 |

\* OBS bitrate'i `ΔoutputBytes`'tan türetilmiş yaklaşık değerdir
(obs-websocket v5 doğrudan kbps vermez).

**Dürüst okuma:** OBS bu koşuda daha kararlı — FPS'i hiç dalgalanmadan 60.0'da
tutuyor; Reji ortalamada 58.7'ye, dipte 48'e iniyor. İkisi de kare düşürmüyor —
izleyici açısından asıl kritik metrik bu ve orada eşitler. Ancak FPS
kararlılığında OBS önde; bunun bir kısmı ölçüm semantiği farkından geliyor
olabilir (bkz. §4), ama fark gerçek.

### Reji koşumları arası değişkenlik

Üç Reji koşumunun 1Hz özeti (aynı ayarlar):

| Koşum | FPS ort / min | Not |
|---|---|---|
| 1 (ilk ölçüm) | 58.8 / 45 | `:38` dibi burada görüldü (aşağıda) |
| 2 (preview AÇIK) | 60.1 / 58 | dip yok |
| 3 (preview KAPALI) | 60.1 / 58 | dip yok |

- **`:38` olayı (çözülmedi):** Koşum 1'de tek bir saniyede fps ~45'e düştü.
  Ham 60Hz veri incelendi: tek bir uzun hang değil, saniyeye yayılmış
  yavaşlama (Δt medyanı 30ms ≈ 33Hz, ~410ms birikmiş fazla süre), `drop=0`
  (encoder kare düşüşü değil). Tekrar üretilemedi; aralıklı bir olay olarak
  değerlendirildi (muhtemel WGC teslim cadence'i veya sistem-seviyesi geçici
  kontensiyon). İleride tekrar görülürse per-frame wall-clock Δt +
  alt-sistem kırılımı gerekir — ancak FrameProfiler WGC yolunda beslenmiyor
  (`pipeline.cpp:450` yalnız DXGI'ye bağlıyor; teknik borç kaydı mevcut).
- **Preview A/B testi (koşum 2 vs 3, `REJI_DISABLE_PREVIEW` geçici geçidiyle)
  fark yaratmadı** → hibrit-GPU preview çapraz-kopyası suçlu **değil**.
- Tabloda rapor edilen baseline, tam 60 örnekli koşudur (58.7 / 48).

## 3. Stres Testi — Seçenek A (50 Mbps)

| | OBS | Reji |
|---|---|---|
| FPS ort | 60.0 | 59.4 |
| FPS min | 60.0 | 53.0 |
| Dropped frames | 0 | 0 |

Her ikisi de sorunsuz. Bu donanımda 50 Mbps yeterli stres yaratmadı —
NVENC'in darboğazı bitrate değil. **Seçenek B (gerçek GPU yükü altında ölçüm)
yapılmadı** — açık kalem.

## 4. Metodolojik Sınırlar (gizlenmeyecek)

- **Örnekleme oranı farkı:** Reji ~60Hz (video karesi başına bir örnek), OBS
  1Hz. Adil karşılaştırma için Reji verisi 1Hz'e toplulaştırıldı.
- **fps agregasyonu:** Reji'nin anlık `1/Δt` değerlerini aritmetik ortalamak
  Jensen biası üretiyordu (63.9 vs gerçek 58.7) — agregasyon pencere içi
  **kare sayısına** (= kare/sn, harmonik ortalamaya eşit) çevrildi; bu, OBS
  `activeFps` ile doğrudan denktir.
- **fps semantiği birebir denk değil:** Reji `fps_actual` = `run_frame` döngü
  cadence'i (anlık `1/Δt`, `metrics_subsystem.cpp:104`, 240'a clamp); OBS
  `activeFps` = compositor render oranı (yumuşatılmış). İkisi de "pipeline
  hedef FPS'i tutturuyor mu" sinyali verir ama aynı şeyi ölçmez.
- **drop tanımları denk olmayabilir:** Reji per-sample delta, OBS
  `Δ(outputSkippedFrames)`.
- **GOP/B-frame gibi ikincil encoder parametreleri eşleştirilmedi** — ana
  parametreler (kodek, çözünürlük, FPS, bitrate, rate control, preset,
  tuning) eşleştirildi.
- **Tek makine:** Sonuçlar genellenemez.

## 5. Ölçüm Sürecinin Bulduğu Gerçek Bug'lar (bu turun asıl değeri)

Benchmark'ın asıl getirisi rakip karşılaştırması değil, kendi kodda görünmez
olanı görünür kılmak oldu:

- **`benchmark_compare.py` imza uyuşmazlığı** (`76a594b`) —
  `_recv_skip_to_op` imzası bozuktu; script daha önce hiç uçtan uca
  çalıştırılmamıştı.
- **WS Ping/Pong bug'ı** (`2a8f83d`) — `ws_server.rs`'in `_ => break`'i
  RFC 6455 Ping/Pong kontrol çerçevelerini protokol ihlali sayıyordu;
  **her standart WS istemcisi 20 saniyede sessizce düşüyordu** (Stream
  Deck / Companion dahil). Faz 1'in temel vaadini zedeliyordu.
- **Preview `Map(READ)` darboğazı** (`5a816e5`) — `emit_wgc_preview` kare
  başına 10-14ms yiyordu (16.7ms bütçenin %60-84'ü). Düzeltme sonrası
  `tot` 13-19ms → 6.7-8.8ms.
- **Üç yanlış hipotez kanıtla çürütüldü:** librtmp/chunk-size
  (`sendV=0.3ms`), WGC teslimi (non-blocking poll), encode (bütçe içinde).

## 6. Açık Tasarım Sorusu — VFR/CFR

Reji fiilen VFR üretiyor (WGC yalnız değişen kareyi teslim eder); OBS son
kareyi tekrarlayarak CFR üretir. Test videosu 25 FPS olduğundan `null≈15`
gözlendi — bu doğru davranış, Reji'de bir sorun değil. Ama canlı yayın
platformları genelde CFR bekler; VFR ingest'te stutter / süre kayması /
"low FPS" uyarısı riski var. **Karar verilmedi** (Todoist P2 kaydı mevcut).

## 7. Sonuç ve Sonraki Adımlar

- Baseline kaydedildi; gelecekteki iyileştirmeler bu referansa karşı
  ölçülebilir. Mevcut durumda **OBS FPS kararlılığında önde, kare düşürmede
  eşitlik** var — sonuç Reji'nin lehine değil ve bu belge bunu olduğu gibi
  kaydediyor.
- Açık kalemler:
  - Seçenek B stres testi (gerçek GPU yükü altında).
  - VFR/CFR kararı (§6).
  - Çok-makineli tekrar (tek makine sonucu genellenemez).

## 8. Ek Kayıt — NVENC VBV Düzeltmesi, Canlı Twitch Doğrulaması (2026-09-01)

**Bağlam:** `8743a4f` öncesi VBV tek kareydi (`bitrate/fps`; 6000 kbps
@ 60 fps → 100.000 bit ≈ 12,5 KB/kare tavanı). Canlı Twitch teşhisinde
encoder hedefin ~%12'sini üretiyordu; `run.log` maks paket 13.342 B bu
tavanın kanıtıydı. Düzeltme: VBV = 1 saniyelik bitrate (OBS/FFmpeg
`bufsize = bitrate` eşdeğeri, yayın standardı).

**Canlı Twitch testi (düzeltme sonrası, kullanıcı koşumu):**

| Metrik | Önce | Sonra |
|---|---|---|
| Maks keyframe paketi | 13.342 B (VBV tavanı) | 46.656 B (tavan kalktı) |
| Twitch Inspector bitrate | 741 kbps | 2.471 kbps (**3,3×**) |
| Inspector Yapılandırma Kontrolü | — | **Excellent** |

**Dürüst okuma:** Düzeltmenin hedeflediği mekanizma doğrulandı —
keyframe'ler artık tek-kare bütçesine ezilmiyor ve ingest bitrate'i
3,3× arttı. Ancak 2.471 kbps, yapılandırılan hedefin hâlâ altında
olabilir; bu koşuda hedef-tutturma ayrıca ölçülmedi (içerik
karmaşıklığı / VFR etkisi olası — §6 ile bağlantılı). "Excellent"
Inspector'ın yapılandırma değerlendirmesidir, hedef doğrulaması değil.

**Neden §2 bunu görmedi:** Reji'nin bitrate metriği yapılandırılan
hedefi raporlar (`pipeline.cpp:970`, `s.bitrate_kbps.load()`) — §2
tablosundaki düz "12000 kbps" ölçüm değil, ayar yansımasıydı. İçerideki
ölçüm bug'la aynı varsayımı paylaştığı için dış gözlemci (Inspector)
gerekti. Metriğin ölçülen çıkışa çevrilmesi iyileştirme adayı.

**Bug yaşam süresi (B1 defteri):** formül `3fe1fbb` (2026-05-21,
`encode_nvenc.cpp`'nin ilk commit'i) → `8743a4f` (2026-09-01) =
**~3,4 ay**. Desen: ultra-low-latency reçetesi yanlış bağlamda
kopyalanmış. Ayrıntı: `SESSION_NOTES.md` B1 tablosu.

**Kapsam notu:** tuning ULTRA_LOW_LATENCY ve preset P4 bilinçli olarak
değişmedi; geri-adım noktası VBV 0,5 sn.
