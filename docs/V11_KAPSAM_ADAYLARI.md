# V11 Kapsam Adayları

**Statü:** Durum kaydı — her tur kapanışında güncellenir.

## Amaç

V11 taraması başladığında kapsam listesini `git diff` ile sıfırdan
çıkarmamak. V10'da bu iş zaman aldı ve bir grup (orchestrator
healing/telemetri zinciri) ilk taslakta atlanmıştı — `git diff`
teyidiyle yakalandı. Bu belge, V10 kapanışından beri değişen alanları
tur bittikçe biriktirir; V11 tarama prompt'u buradan beslenir.

## Başlangıç Noktası

V10 NİHAİ mührü: commit `4f55118` (2026-07-30). Aday gruplar
`4f55118..HEAD` aralığından `git log` ile çıkarılıp doğrulanmıştır
(son işlenen commit: `a9d6cd2`, 2026-09-08).

## Tetikleyici Koşul

Takvim değil, koşul — şunlardan biri gerçekleşince V11 başlatılır:

- **(a)** Faz 3 kompozisyon tamamlandığında, **veya**
- **(b)** CI C++ build onarımı bitip `build.yml` yeşile döndüğünde.

## Aday Gruplar

### 1. VFR/CFR kare tekrarı (Faz 2 serisi)

- **Dosyalar:** `src/pipeline/include/frame_repeat_policy.h` (yeni),
  `src/pipeline/pipeline.cpp` (tekrar mantığı), `existing_desktop_source.cpp/.h`
  (WGC kalıcı kopya), `send_diag.h` (dup, copy, idrF alanları),
  her iki encode backend'i (kopya yolu)
- **Ne değişti:** null tick'te kare tekrarı (K2 CFR üretimi), WGC encode
  girdisinin kalıcı kopyaya taşınması (K1), zaman bazlı yedek IDR (K3,
  eşik 3sn'e çekildi), saf `frame_repeat_policy` çekirdeği
- **Commit'ler:** `1dc68d6`, `17401bb`, `1bbb7d4`, `7aa08dd`,
  `72a4c83`, `7f67242`, `1cb7c2f`

### 2. Preview darboğaz düzeltmesi

- **Dosyalar:** `existing_desktop_source.cpp/.h`, `pipeline.cpp`
- **Ne değişti:** 2-slot staging ring, `DO_NOT_WAIT` try-map,
  30Hz seyreltme
- **Commit:** `5a816e5`

### 3. NVENC VBV

- **Dosyalar:** `src/pipeline/encode/encode_nvenc.cpp/.h`
- **Ne değişti:** `vbv_bits()` (1sn pencere), `init_encoder`,
  `set_bitrate` reconfig yolu + healing reconfig bayatlığı düzeltmesi;
  canlı Twitch doğrulaması yapıldı
- **Commit'ler:** `8743a4f` (fix), `a9d6cd2` (doğrulama kaydı)

### 4. WS Ping/Pong

- **Dosyalar:** `src/orchestrator/src/ws_server.rs`
- **Ne değişti:** RFC 6455 Ping/Pong kontrol çerçeveleri bağlantıyı
  koparmıyor + regresyon testleri
- **Commit:** `2a8f83d`

### 5. SendDiag enstrümantasyonu

- **Dosyalar:** `src/pipeline/include/send_diag.h` (yeni),
  `pipeline.cpp` ölçüm noktaları
- **Ne değişti:** gönderim teşhis agregatörü (Faz 1), ölçüm boşluğu
  kapatma (Faz 2), enc mikro-split (Faz 3)
- **Commit'ler:** `717c9c5`, `5a816e5`

### 6. Metrik zinciri

- **Dosyalar:** `src/pipeline/include/metrics_subsystem.h`,
  `metrics_subsystem.cpp`, `src/orchestrator/src/ffi.rs`
- **Ne değişti:** `gpu_load_pct` build_sample'da kopyalanmıyordu —
  `apply_collector_metrics()` saf seam'ine çıkarıldı (8 alan, tek
  doğruluk kaynağı); Rust drainer GpuUsage testleri eklendi
- **Commit:** `d89f2b7`

### 7. Benchmark araçları *(git log doğrulamasında eklendi — ilk taslakta yoktu)*

- **Dosyalar:** `scripts/benchmark_compare.py`
- **Ne değişti:** 1Hz toplulaştırma (adil min-FPS), fps agregasyonunun
  kare-sayımına çevrilmesi + kısmi uç pencereler,
  `_recv_skip_to_op` imza düzeltmesi
- **Commit'ler:** `3530329`, `76a594b`, `f6882f8`
- **Not:** Ölçüm aracı — üretim yolu değil; tarama önceliği düşük ama
  benchmark sonuçlarının geçerliliği buna dayanıyor.

### 8. Hijyen turları

- **Ne değişti:** Clippy `-D warnings` temizliği (7 dosya) + regresyon
  testleri, C4005/D9025/C4324 derleme uyarıları (davranış-nötr),
  `sceneActivated` ölü sinyalinin kaldırılması + Fade geçişine
  `sendSceneSwitchEvent`, FrameProfiler örnek kaydının `markAcquireEnd`'e
  taşınması + ShaderCache saf FNV-1a, HealingOverlayTest PATH düzeltmesi
- **Commit'ler:** `cc0f29d`, `dc0864e`, `5b14c89`, `598520a`, `21a648f`

### 9. Altyapı / CI

- **Ne değişti:** `Cargo.lock` izlemeye alındı, crossbeam-epoch
  0.9.20 (RUSTSEC-2026-0204), `quality.yml`'e clippy adımı + C++
  derleme adımlarının çıkarılması, `scripts/daily.ps1` + `weekly.ps1`
  + `lint.ps1` + justfile hedefleri, PostToolUse clippy hook'u
- **Commit'ler:** `7337762`, `f5792a4`, `21cc80e`, `51583de`,
  `6dbfad2`, `6e93eed`, `65c6dc5`, `79ef519`
- **Not:** CI/script değişiklikleri kod tarama kapsamına girmez ama
  tarama araçlarının (clippy, audit) artık neyi gördüğünü değiştirir.

### Grup doğrulama notu (2026-09-08)

Beklenen listedeki **"Kural motoru (L21-L23)"** grubu `git log`
doğrulamasında aralık DIŞI çıktı: ilgili commit'ler (`ecf9b99`,
`ec24d2a`, `67d9258`, merge `612a357`) V10 Sprint 2'ye aittir ve
`4f55118` mührünün öncesindedir — V10 kapsamında zaten değerlendirildi,
V11 adayı değildir. Aralıktaki kalan commit'ler yalnız belge
değişiklikleridir (SESSION_NOTES, ROADMAP, B1 defteri, sağlık
raporları vb.) ve tarama adayı değildir.

## Risk Notları

- **`run_frame` hot-path'ine bu dönemde ÇOK dokunuldu** (preview
  darboğazı, kare tekrarı, SendDiag ölçüm noktaları) —
  eşzamanlılık/bütçe regresyonu riski yüksek; V11'de öncelikli alan.
- **Yeni bir eşzamanlılık deseni girdi:** 2-slot staging ring
  (preview). Üretici/tüketici sınırları ve ordering V11'de
  incelenmeli.
- **Encoder yapılandırması değişti (VBV):** ABI'ye dokunmadı ama
  davranışı değiştirdi — reconfig yolu (healing dahil) ve bitrate
  hedef tutturma etkileşimleri taranmalı.

## Kapsam Dışı

V8/V9/V10'un zaten sertleştirdiği ve `4f55118`'den beri değişmemiş
alanlar:

- Capture çekirdeği
- Keyed-mutex senkronizasyonu
- NVENC çekirdeği (**dikkat:** VBV/reconfig yolu değişti — o kısım
  Grup 3 ile kapsam İÇİNDE; çekirdek oturum/encode akışı dışında)

## Bilinçli Kararlar — Tarayıcılar "Bulgu" Sanmasın

V10 listesinden taşınan kalemler (bayatlayanlar işaretli):

1. **SRT'de ses yok** — MPEG-TS muxer bekliyor; ses şimdilik yalnız RTMP.
2. **Çözünürlük kontrolü yok** — encode-time downscale bekliyor
   (capture-authoritative mimari).
3. ~~`gpu_load_pct` daima 0 — YAGNI kararı~~ **GÜNCELLENDİ:**
   `d89f2b7` ile alan artık gerçek PDH ölçümünü taşıyor;
   `gpu_load_high > 80` kuralı fiilen canlandı. Artık bilinçli
   eksiklik DEĞİL — V11 bu yolu normal kod olarak taramalı.
4. **Termal metrikler stub** (`gpu_temp_c`) — kurallar bunları atlıyor.
5. **Resampling yok** — 48kHz/2ch dışındaki cihaz formatları
   desteklenmiyor (bilinçli sınır).
6. **MF encoder cross-thread shutdown** — belgeli kabul edilebilir risk.
7. **RTMPS yok** — platform ingest testi sonrası değerlendirilecek.
8. **Kural GUI-düzenleme yok** — harici editör bilinçli tercih.
9. ~~CaptureSubsystem'in kaderi Faz 3 wiring'e ertelendi~~
   **ÇÖZÜLDÜ:** V10 içinde silindi (`5afdaaa`, ISource wiring
   tamamlandı) — madde düştü.
10. **Kimlik bilgileri (WS parola, RTMP key) registry'de düz metin** —
    V9/J14'te gözden geçirilip kabul edilmiş karar (DPAPI ertelendi).
11. **`default_mode`** rules.json'da parse ediliyor ama kullanılmıyor —
    bilinen borç.
12. **Zig modül-global state** (`external_memory_bridge.zig`,
    `vulkan_initializer.zig`) — Faz 5'e ertelendi (double-init
    uyarısı mevcut).

Bu dönemde eklenen kalemler:

13. **ULTRA_LOW_LATENCY tuning bilinçli korundu** — VBV tek başına
    ölçüldü, tuning kararı ertelendi.
14. **`set_fps_limit()` VBV'yi güncellemiyor** — yeni formül
    fps-bağımsız olduğu için gerek yok (bilinçli).
15. **Metrik `bitrate_kbps` yapılandırılan hedefi raporluyor, ölçülen
    çıkışı değil** — bilinen eksiklik, iyileştirme adayı. (Bu, VBV
    bug'ının ~3,4 ay görünmemesinin sebebiydi.)
16. **PipelineCharacterization NVENC'i egzersiz etmiyor (mock yol)** —
    encoder davranışını doğrulayamaz; VBV benzeri bug'ları testler
    yakalayamaz.

## Bakım Kuralı

> **Her tur kapanışında:** SESSION_NOTES mühürlemesine ek olarak,
> o turda değişen dosyalar bu listeye eklenir. Tur "tamamlandı"
> sayılmadan önce bu adım yapılmalı — V10'da kapsam listesini
> sonradan çıkarmak zaman almıştı.
