# BULGULAR: VFR/CFR Faz 0 Araştırması

**Tarih:** 2026-08-17 · **Talimat:** `TALIMAT_VFR_CFR_FAZ0.md` · **Kod değişikliği yok.**

## SONUÇ: Sorun gerçek ve ciddi → Faz 1 gerekir

Gerekçe üç bacaklı:

1. **Keyframe aralığı zaman bazında garanti edilmiyor.** GOP kare-sayısı
   bazlı (120 kare); teslim edilen fps içeriğe bağlı olduğundan statik
   içerikte keyframe aralığı zaman ekseninde sınırsız uzar. Twitch 2 sn
   bekler, YouTube resmî olarak "4 sn'yi aşmayın" der ve aşıldığında
   uyarı/buffering bildirir; Amazon IVS 5 sn üstünde decode hatası ve
   görsel bozulma riskini resmen belgeliyor.
2. **Tam statik ekranda video İLE BİRLİKTE ses de durur.** `audio_bridge_.drain()`
   yalnız video paket callback'i içinden çağrılıyor — WGC kare teslim
   etmezse AAC paketleri de gönderilmez. Boşta kalan bir sahne (BRB
   ekranı, sabit slayt) bir reji ürününde çekirdek kullanım senaryosudur.
3. **OBS bu sorunu bilinçli olarak kare tekrarıyla çözüyor** (kaynak kod
   doğrulandı) — endüstri davranışı CFR üretmek.

---

## Bölüm A — Mevcut Davranış (tümü kod incelemesiyle doğrulandı)

### A1. PTS üretimi
- PTS duvar saatinden türetiliyor: `pts_us = pacer_.pts_us(frame_start)`
  (`pipeline.cpp:741`), QPC tabanlı (`frame_pacer.cpp:37-39`).
- Kare teslim edilmediğinde PTS **durmaz** — bir sonraki geçerli karede
  duvar saati neredeyse, o değer kullanılır. Encoder'a giden PTS dizisi
  monoton artan ama **boşluklu** (ör. 60 fps pacing'de 16,7 ms yerine
  33,3 ms / 50 ms sıçramalar). Sürüklenme (drift) yok; eksik kare var.

### A2. NVENC'e ne gidiyor
- Null iterasyonda `encode_frame()` **hiç çağrılmıyor** — çağrı yalnız
  `if (tex)` dalında (`pipeline.cpp:740-748`); null dalı (`:833-850`)
  yalnız sayaç/reinit işleri yapar. Hiçbir katmanda kare tekrarı yok.
- WGC `next_frame()` null döndürmeden ÖNCE önceki texture'ı bırakıyor
  (`capture_wgc.cpp:171-176`: `last_tex_.Reset()` → `TryGetNextFrame()`).
  Yani null anında "son kare" elimizde bile değil — tekrar için geçerli
  karede kopya almak şart.
- `frameRateNum = config.fps_num` (`encode_nvenc.cpp:249`): rate control
  60 fps varsayar; fiilen daha az kare gelince CBR hedef bitrate'in
  altında kalır → hat üzerinde "strict CBR" değil.

### A3. FLV/RTMP timestamp'leri
- Tag timestamp'i `pts_us`'ten türetiliyor: ilk paketin pts'i t=0 kabul,
  ms cinsinden göreli (`rtmp_transport.zig:16-17, 375-376`). B-frame yok
  (`frameIntervalP=1`) → dts==pts, composition time 0.
- Kare atlandığında timestamp akışında **boşluk oluşuyor** (kaydedilen
  zamanlar doğru, ama seyrek) — klasik VFR imzası.

### A4. SPS/PPS ve GOP
- GOP **kare-sayısı bazlı**: `gopLength = config.gop_size`, varsayılan
  120 (`encode_nvenc.h:40`, "2 s at 60 fps" varsayımıyla). Teslim 45 fps
  ise ~2,7 sn; 5 fps'e düşen statik sahnede **24 sn**; sıfır karede ∞.
- `force_idr` yalnız `start_stream()`'de tetikleniyor (`pipeline.cpp:609`);
  zaman bazlı periyodik IDR mekanizması yok.
- SPS/PPS her IDR'a iliştiriliyor (`repeatSPSPPS`, `encode_nvenc.cpp:218-221`)
  — bu kısım sağlıklı.

### A5. (Ek bulgu) Ses, video kadansına bağlı
- `audio_bridge_.drain(pkt.pts)` yalnız video `on_packet` içinde
  (`pipeline.cpp:355-360`). Video paketi yoksa AAC gönderimi de yok.
  Tam statik ekranda RTMP bağlantısına **hiç veri gitmez**; sunucu
  zaman aşımı davranışı **çıkarım, doğrulanmadı** (yerel testle
  kesinleştirilemez, gerçek yayın testi gerekir).

## Bölüm B — Platform Beklentileri

| Bulgu | Kaynak sınıfı |
|---|---|
| YouTube Live resmî: keyframe her 2 sn, "4 sn'yi aşmayın"; aşılırsa canlı uyarı: "keyframes are not being sent often enough, which can cause buffering" | Resmî doküman (web) |
| YouTube Live resmî: CBR zorunlu, closed GOP tercih | Resmî doküman (web) |
| Amazon IVS (Twitch altyapısı) resmî: keyframe 2 sn; >5 sn'de "segments not beginning with an IDR/keyframe may result in decode errors or visual distortions"; "Always use CBR, not VBR" | Resmî doküman (web) |
| Twitch guidelines: H.264, strict CBR, keyframe 2 sn (help.twitch.tv sayfası doğrudan erişilemedi — CSS hatası; içerik ikincil kaynaklardan) | Web kaynağı, ikincil |
| Hiçbir platform "VFR yasak" demiyor; ancak tüm gereksinimler (sabit keyframe kadansı, CBR, fps beyanı) fiilen CFR varsayar. VFR girişte transcoder/oynatıcı davranışı (stutter, "low fps" uyarısı, süre kayması) topluluk raporlarında var, resmî belgede yok | Çıkarım + forum düzeyi — **doğrulanmadı** |
| OBS CFR üretir: video thread'i sabit tick'te çalışır, geç kalınan tick'lerde `count` hesaplanıp kare **tekrarlanır** (`obs-video.c`: `video_sleep` → `vframe_info.count`; GPU encoder kuyruğunda `duplicate` dalı `tf->count++`). Yani çözüm compositor/çıkış katmanında, encoder'da değil | OBS kaynak kodu (web'den okundu) |

## Bölüm C — Düzeltme Seçenekleri (boyutlandırma, kod yazılmadı)

1. **Son kareyi kopyala + null tick'te yeniden encode et — ÖNERİLEN.**
   WGC texture'ı borrowed ve null çağrıda zaten bırakılıyor (A2) →
   geçerli karede pipeline'a ait bir texture'a `CopyResource` (1080p
   BGRA ~8 MB, GPU'da <0,5 ms), null tick'te o kopyayı paced PTS ile
   `encode_frame()`'e ver. Bitrate maliyeti düşük: birebir aynı kare
   NVENC'te skip-MB P-frame üretir (birkaç yüz bayt/kare — **çıkarım,
   ölçülmedi**). Yan kazanım: her tick'te encode → `on_packet` → ses
   drain kadansı da düzelir (A5 kendiliğinden çözülür), GOP'un 120
   karesi yeniden ~2 sn'ye oturur. Tahmini boyut: pipeline.cpp null
   dalı + küçük texture cache; ~50-100 satır, testleriyle 1-2 gün.
2. **Encoder-seviyesi:** `frameIntervalP` kare tekrarı DEĞİL — I/P/B
   deseni aralığıdır (1 = B-frame yok). NVENC teslim edilen resmi
   encode eder; kendiliğinden çoğaltma özelliği bilinmiyor
   (**doğrulanmadı** — NVENC SDK'da "skipped picture" benzeri bir yol
   Faz 1'de kontrol edilebilir, ama bulunsa da her tick'te bir submit
   yine app'ten gelmek zorunda; kazanç yalnız kopyayı atlamak olur).
3. **PTS-tabanlı çözüm: yetersiz.** Timestamp'ler zaten doğru (A1/A3);
   sorun zaman damgası değil, **eksik kare**. Timestamp'i CFR ızgarasına
   oturtmak kare üretmez; keyframe kadansı ve "no data" sorunlarına
   dokunmaz. Elenir.
4. **Hiçbir şey yapma: savunulamaz.** Bant genişliği avantajı gerçek,
   ama kare-sayısı bazlı GOP + ses-video bağlaşımı yüzünden statik
   içerik (reji ürününde olağan senaryo) resmî platform gereksinimlerini
   ihlal ediyor. "Özellik olarak konumlandırma" ancak 1. seçenek
   uygulandıktan sonra kayıt/az-hareket profillerinde düşünülebilir.

## Doğrulanmayan noktalar (yerel testle kesinleşmez)

- Gerçek Twitch/YouTube transcoder'ının bu akışa somut tepkisi
  (uyarı mı, stutter mı, kopma mı). **Öneri: kullanıcı kısa bir test
  yayını yapmalı** — statik masaüstü ile 5-10 dk Twitch'e yayın,
  Twitch Inspector'da keyframe/fps uyarıları gözlensin.
- Tam statik ekranda RTMP sunucusunun bağlantıyı ne kadar sürede
  düşürdüğü.
- Tekrarlanan karelerin gerçek bitrate maliyeti (Faz 1'de ölçülmeli).

## Kaynaklar

- Kod: `frame_pacer.{h,cpp}`, `pipeline.cpp`, `encode_nvenc.{h,cpp}`,
  `encode_subsystem.cpp`, `rtmp_transport.zig`, `capture_wgc.cpp`
- [YouTube Live encoder ayarları (resmî)](https://support.google.com/youtube/answer/2853702)
- [YouTube canlı yayın hata mesajları (resmî)](https://support.google.com/youtube/answer/3006768)
- [Amazon IVS Streaming Configuration (resmî)](https://docs.aws.amazon.com/ivs/latest/LowLatencyUserGuide/streaming-config.html)
- [OBS `libobs/obs-video.c` (kaynak kod)](https://github.com/obsproject/obs-studio/blob/master/libobs/obs-video.c)
- Twitch Broadcasting Guidelines (help.twitch.tv — doğrudan erişilemedi, ikincil aktarım)
