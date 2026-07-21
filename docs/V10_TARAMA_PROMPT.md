# V10 TARAMA PROMPT'U — Reji Studio V9-Sonrası Yeni Kod Taraması

> Bu metin, üç bağımsız modele (Fable 5, Opus 4.8, GLM 5.2 veya güncel
> eşdeğerleri) **ayrı ayrı ve olduğu gibi** verilecek ortak tarama
> talimatıdır. Kapsam listesi `git diff efe0fec..master` (V9-kapanış →
> 21.07.2026) çıktısından türetilmiş ve tek tek teyit edilmiştir.

---

Sen bağımsız bir kod denetçisisin. Görevin, aşağıda tam yollarıyla
listelenen dosyalarda **gerçek, kanıtlanabilir hataları** bulmak.
Kod değiştirmeyeceksin — yalnızca bulgu raporu üreteceksin.

## 1. Proje Bağlamı (kısa)

**Reji Studio** — C++17/MSVC + Rust/Tokio + Zig + Qt6 6.8.0 hibrit
mimarili, çift-GPU (AMD iGPU display + NVIDIA dGPU encode), Windows 11
hedefli açık kaynak canlı yayın yazılımı. Katmanlar:

- `src/pipeline/` — C++ frame/ses pipeline'ı (capture → encode → output)
- `src/pipeline/rtmp/*.zig`, `src/pipeline/gpu/*.zig` — Zig çekirdekler
  (RTMP/FLV mux, Vulkan interop)
- `src/orchestrator/` — Rust/Tokio: event bus, RuleEngine, self-healing,
  obs-websocket v5 sunucusu
- `src/ui/` — Qt6 (MainWindow, SettingsDialog, PreviewWidget)
- `src/ffi/` — C ABI köprüsü (C++ ↔ Rust)

**Mimari prensipler (ihlalleri bulgu sayılır):**

1. **FFI'dan yalnız veri geçer** — POD struct'lar sabit boyutlu, iki
   uçta `static_assert`/sizeof kontrolü (`check-abi.ps1`). Pointer'lı
   string girişleri `cstr_bounded` ile sınırlanmalı (V9/J1 dersi).
2. **Tek-thread RTMP invariant'ı** — RTMP soketi/librtmp state'ine tek
   thread dokunur; ses ve video aynı gönderim yolundan (OutputSubsystem
   encode-thread drain) akar.
3. **SPSC desenleri** — ring buffer'lar tek-üretici/tek-tüketici
   varsayımıyla yazılır (ör. capture-thread PCM üretir, encode-thread
   AAC tüketir). Üçüncü bir thread'in dokunması bug'dır.
4. **SEH disiplini** — D3D11/DXGI çağrıları `__declspec(noinline)` leaf
   wrapper'da `__try/__except`; `__try` scope'unda destructor'lı C++
   nesnesi yasak.
5. **COM disiplini** — `CoInitializeEx` başarısıysa (`SUCCEEDED` guard)
   ve yalnız o zaman `CoUninitialize`; COM nesneleri oluşturuldukları
   thread/apartment bağlamında kullanılıp orada bırakılır (V8 I9/I10
   desenleri).
6. **Hot-path kuralları** — `paintGL()`, `run_frame()`,
   `get_frame_images()` ve ses callback'lerinde heap allocation,
   blocking bekleme, JSON/string işleme yasak.

## 2. Kapsam — Taranacak Dosyalar (tam yollar)

Tümü V9-kapanışından (12-14.07.2026) sonra eklendi veya büyük değişti
ve **hiç bağımsız taramadan geçmedi**. Son canlı GUI testleri bu bölgede
üç gerçek bug buldu (hot-reload kuruluş-sırası, içe aktarım kopyalama,
dışa aktarım kör-kopyalama) — yüzeyde daha fazlası olabilir.

### Grup 1 — Ses pipeline'ı (EN YÜKSEK ÖNCELİK: eşzamanlılık + COM + FFI/ABI)

- `src/pipeline/audio/aac_config.h` — AAC AudioSpecificConfig saf mantık
- `src/pipeline/audio/flv_audio_tag.h` — FLV audio tag başlığı saf mantık
- `src/pipeline/audio/pcm_convert.h` — PCM dönüşüm saf mantık
- `src/pipeline/audio/av_sync.h` — A-V drift valfi
- `src/pipeline/audio/aac_encoder.h` + `aac_encoder.cpp` — Media
  Foundation AAC-LC encoder (COM yaşam döngüsü; cross-thread shutdown
  **belgeli kabul edilebilir risk** — bunu bulgu olarak raporlama, ama
  onun DIŞINDAKİ yaşam-döngüsü hatalarını ara)
- `src/pipeline/audio/audio_ring.h` — SPSC audio ring
- `src/pipeline/audio/audio_encode_bridge.h` + `audio_encode_bridge.cpp`
  — PCM→AAC köprüsü
- `src/pipeline/audio/audio_device_enum.h` + `audio_device_enum.cpp`
  — WASAPI cihaz enumerasyonu
- `src/pipeline/audio/wasapi_capture.h` + `wasapi_capture.cpp`
  — yalnız V9-sonrası delta: seçili-cihaz desteği
- `src/pipeline/audio_subsystem.cpp` +
  `src/pipeline/include/audio_subsystem.h` — sink bağlama deltası
- `src/pipeline/output_subsystem.cpp` +
  `src/pipeline/include/output_subsystem.h` — `send()` içi audio-ring
  drain entegrasyonu
- `src/pipeline/rtmp/rtmp_transport.zig` — FLV audio genişletmesi +
  yeni ABI yüzeyi (`rj_rtmp_send_audio`)
- `src/pipeline/output/rtmp_transport.h` + `rtmp_transport.cpp`
  — C++ sarmalayıcı tarafı
- `src/pipeline/include/i_transport.h` — `send_audio` kontrat genişletmesi

### Grup 2 — Donanım Profilleme

- `src/ui/profile_advisor.h` + `profile_advisor.cpp` — HwSignals sinyal
  toplama + `suggest_profile` karar mantığı
- `src/ui/main_window.cpp` + `main_window.h` —
  `maybeSuggestProfileOnFirstRun` + `applyProfile` akışı
- `docs/config/profiles/stability.json`, `performance.json`,
  `efficiency.json` + `src/ui/rules_template.qrc` — içerik tutarlılığı:
  eşikler Faz 1 tablosuna uygun mu (batarya→Verimlilik; VRAM<4GB ||
  RAM<8GB→Stabilite; aksi→Performans; preset'ler 12000/60 · 6000/30 ·
  4500/30)
- `src/pipeline/pipeline.cpp` + `src/pipeline/include/pipeline.h`
  — **yalnız** `max_gpu_vram_mb()` accessor'ı ve init-parametre deltası
  (bkz. kapsam-dışı: run_frame/capture-wiring)

### Grup 3 — Kural Yönetimi Zinciri (son üç bug'ın bölgesi — ÖZELLİKLE DİKKAT)

- `src/ui/main_window.cpp` — `writeValidatedRules` /
  `validateRulesFile` / `importRules` / `exportRules` (üç bug-fix'in
  — `449c084`, `c99f1b6`, `e36176e` — SONRASI güncel hali; kalan
  boşlukları ara)
- `src/ui/rules_watch.h` — hot-reload izleyici (QFileSystemWatcher
  yeniden-silahlandırma dahil)
- `src/orchestrator/src/rules.rs` — V9-sonrası delta: JSON snapshot,
  kalibre eşik override, profil doğrulama
- `src/orchestrator/src/ffi.rs` — `rj_rules_snapshot_json` FFI
- `src/ui/settings_dialog.cpp` — salt-okunur "Kurallar" sekmesi doldurma

### Grup 4 — WS/Ayarlar

- `src/orchestrator/src/ws_server.rs` — `ConnectionGuard` RAII
  doğruluğu (tüm çıkış yolları sayacı geri düşürüyor mu)
- `src/orchestrator/src/ffi.rs` — `rj_get_ws_connection_count`
- `src/ui/settings_dialog.h` + `settings_dialog.cpp` — QTabWidget
  yeniden yapılanması + Ses/Video ayar alanları + QSettings persistence
- `src/pipeline/include/bitrate_policy.h` —
  `reduce_floor_for_target` min_bitrate clamp mantığı

### Grup 5 — ISource Katmanı (yeni, henüz wire EDİLMEMİŞ — izole)

- `src/pipeline/include/i_source.h` — arayüz kontratı
- `src/pipeline/include/desktop_source_logic.h` — saf çekirdek
  (alan eşlemesi + `NullStreakTracker`)
- `src/pipeline/include/existing_desktop_source.h` +
  `src/pipeline/existing_desktop_source.cpp` — adapter

### Grup 6 — Orchestrator Healing/Telemetri Zinciri (Özellik#1-5 + GetStats)

*(git-teyit eki: bu grup da V9-sonrası yazıldı ve hiç taranmadı)*

- `src/orchestrator/src/calibration.rs` — online temel-çizgi
  istatistiği (Özellik#5)
- `src/orchestrator/src/healing_log.rs` — SQLite yazma altyapısı +
  retention (Özellik#3)
- `src/orchestrator/src/healing.rs` — V9-sonrası delta: kalibrasyon
  sürücüsü, action explanation, healing-log fan-out, frame_drop_pct
  bağlama
- `src/orchestrator/src/obs_protocol.rs` — obs-websocket v5
  VendorEvent (op 5) zarf yardımcısı
- `src/orchestrator/src/sys_stats.rs` — GetStats: process bellek +
  disk boş alanı
- `src/orchestrator/src/ws_server.rs` — VendorEvent fan-out + auth
  gate + GetStats handler deltası
- `src/orchestrator/src/ffi.rs` — V9-sonrası delta: `RjActionEvent`
  +3 açıklama alanı + `calibrated`, MemUsage/CpuUsage yayını,
  healing-log fan-out noktaları
- `src/orchestrator/src/event_bus.rs`, `src/orchestrator/src/paths.rs`
  — küçük deltalar

**Referans (bulgu bölgesi değil, davranış anlamak için kullanılabilir):**
`tests/test_aac_config.cpp`, `test_aac_encoder.cpp`,
`test_audio_ring.cpp`, `test_audio_wire.cpp`, `test_av_sync.cpp`,
`test_audio_device_enum.cpp`, `test_bitrate_policy.cpp`,
`test_profile_advisor.cpp`, `test_rules_watch.cpp`,
`test_desktop_source_logic.cpp`, `test_existing_desktop_source.cpp`,
`src/orchestrator/tests/ws_obs_protocol_test.rs`.

## 3. Kapsam DIŞI — Bunları Tarama / Bulgu Olarak Raporlama

### 3a. Zaten sertleştirilmiş eski bölgeler (yeniden tarama israf)

- Capture çekirdeği (`capture_dxgi.cpp`, WGC), keyed-mutex zinciri,
  `copy_optimizer.*`, `external_memory_bridge.*`, `preview_widget.cpp`
  — V8/V9/K-serisi (K1-K7 Vulkan/GL interop turu) bu dosyaları taradı
  ve düzeltti. V9-sonrası diff'te görünmeleri K-serisi fix'lerinden.
- NVENC çekirdeği, metrics plumbing — V8/V9'da kapandı.

### 3b. Yakında değişecek bölge (düşük öncelik)

- `src/pipeline/pipeline.cpp` içindeki `run_frame()` / capture-wiring
  bölgesi — Faz 3 wiring'i yakında yeniden yazacak. (pipeline.cpp'nin
  yalnız Grup 2/4'te sayılan kısımları kapsamda.)
- `scripts/` altındaki her şey (benchmark aracı vb.) — geliştirme
  aracı, runtime kodu değil.

### 3c. Bilinen/bilinçli açık kalemler — BUNLARI "BULGU" DİYE RAPORLAMA

Önceki taramalarda en büyük zaman kaybı, bilinçli kararların "bug"
sanılmasıydı (V9'da J10/J11 çürütmeleri). Aşağıdakiler biliniyor:

1. **SRT'de ses yok** — MPEG-TS muxer bekliyor; ses şimdilik yalnız RTMP.
2. **Çözünürlük kontrolü yok** — encode-time downscale bekliyor
   (capture-authoritative mimari).
3. **`gpu_load_pct` daima 0** — YAGNI kararı.
4. **Termal metrikler stub** (`gpu_temp_c`) — kurallar bunları atlıyor.
5. **Resampling yok** — 48kHz/2ch dışındaki cihaz formatları
   desteklenmiyor (bilinçli sınır).
6. **MF encoder cross-thread shutdown** — belgeli kabul edilebilir risk.
7. **RTMPS yok** — platform ingest testi sonrası değerlendirilecek.
8. **Kural GUI-düzenleme yok** — harici editör bilinçli tercih.
9. **CaptureSubsystem'in kaderi** Faz 3 wiring'e ertelendi.
10. **Kimlik bilgileri (WS parola, RTMP key) registry'de düz metin** —
    V9/J14'te gözden geçirilip kabul edilmiş karar (DPAPI ertelendi).
11. **`default_mode`** rules.json'da parse ediliyor ama kullanılmıyor —
    bilinen borç.
12. **Zig modül-global state** (`external_memory_bridge.zig`,
    `vulkan_initializer.zig`) — Faz 5'e ertelendi (double-init uyarısı
    mevcut).

## 4. Özel Dikkat Çağrıları

1. **SPSC ring'in thread sınırları (ses):** üretici yalnız
   capture-thread, tüketici yalnız encode-thread mi? Head/tail memory
   ordering doğru mu? Wrap-around ve kısmî yazım durumları?
2. **MF/COM nesne yaşam döngüsü:** `IMFTransform` ve buffer'ların
   create/release eşleşmesi, hata yollarında sızıntı, V8 I9/I10
   desenlerine (SUCCEEDED-guard'lı CoInitialize/CoUninitialize)
   uygunluk.
3. **Zig FLV mux'ın ABI/bounds güvenliği:** `rj_rtmp_send_audio` yeni
   ABI yüzeyi — uzunluk parametreleri doğrulanıyor mu, J1'in
   `cstr_bounded` dersi (sınırsız pointer okuma yasağı) bu yeni yüzeyde
   uygulanmış mı, tag boyutu hesapları taşma yapabilir mi?
4. **RAII guard'ların tüm çıkış yolları:** `ConnectionGuard`
   (ws_server.rs) ve `NullStreakTracker` (desktop_source_logic.h) —
   erken return, hata, panic/exception yollarında da doğru mu?
5. **Qt dosya-sistemi işlemlerinin hata yolları:** son üç bug'ın
   bölgesi — `QFile::copy`'nin var-olan-hedefe yazamaması,
   `QTemporaryFile` yaşam süresi/otomatik silme, `QFileSystemWatcher`'ın
   silinen/yeniden-oluşan dosyada izleme kaybı, kısmî yazım/atomik
   değiştirme eksikliği, hata dönüşlerinin kontrol edilmemesi.

## 5. Beklenen Çıktı Formatı

Her bulgu için (V8/V9 rapor desenine uygun):

```
### <Kısa başlık>
- **Konum:** <dosya yolu>:<satır veya fonksiyon adı>
- **Şiddet:** kritik | orta | düşük
- **Güven:** kod-kanıtlı | spekülatif (düşük güven)
- **İddia:** <sorunun tek cümlelik tanımı>
- **Kanıt/akıl yürütme:** <ilgili kod parçası + adım adım neden bug
  olduğu; hangi thread/çağrı sırası/girdi ile tetiklenir>
- **Önerilen düzeltme:** <kısa>
```

Kurallar:

- Somut kod kanıtı olmayan "olabilir/muhtemelen" bulgularını raporun
  **ayrı bir "Düşük Güven / Spekülatif" bölümüne** koy — ana bulgularla
  karıştırma.
- Bölüm 3c'deki bilinen kalemleri raporlama; rapor sonunda "bilinen
  kalemlerle çakıştığı için elenenler" diye tek satırlık liste verebilirsin.
- Şiddet tanımı: **kritik** = bellek güvenliği / veri kaybı / crash /
  güvenlik; **orta** = yanlış davranış, sızıntı, race; **düşük** =
  bakım/performans/tutarlılık.
- Bulgu numarası verme — sentez aşamasında L-numaraları atanacak.
- Rapor sonuna taradığın dosya sayısını ve giremediğin dosya varsa
  hangileri olduğunu yaz.
