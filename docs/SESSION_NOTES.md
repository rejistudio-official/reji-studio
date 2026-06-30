## Oturum: 30 Haziran 2026

### Tamamlananlar
- Self-healing motoru kapsamlı iyileştirme:
  - evaluate_adaptive() artık gerçek anlık metrikler kullanıyor (sabit değer kaldırıldı)
  - hysteresis_ms kural dosyasından okunuyor ve uygulanıyor (5s içinde tekrar tetiklenme yok)
  - log_only aksiyonu düzeltildi (artık gerçekten log_only=true set ediliyor)
  - NetworkStats SystemEvent eklendi (rtt_ms, loss_pct EventBus'a besleniyor)
  - OR (||) koşul desteği eklendi — eval_condition() özyinelemeli
  - Çakışma çözümü: BitrateReduce + BitrateRecover aynı anda → sadece Reduce
  - Kural öncelik sıralaması: BitrateReduce > CapFps > ScaleResolution > Recover > LogOnly
  - rules.json'da new() başlangıçta kural yüklemiyordu — düzeltildi
  - Örnek kural dosyası: C:\Users\Çağlar\.reji\rules.json (4 kural, || ve && kombinasyonu)
- Vulkan extension sorgusu eklendi (NVIDIA: VK_KHR_external_memory_win32 + semaphore OK)
- WebSocket metrik push: fps, kbps, drop, CPU, GPU tarayıcıya gerçek zamanlı

### Açık Kalemler
- GPU preview path — CPU fallback yeterli, dirty rect optimizasyonu opsiyonel
- Cross-device Vulkan transfer (NVIDIA→AMD) — extension'lar mevcut, implementasyon ayrı branch
- default_mode alanı rules.json'da parse ediliyor ama kullanılmıyor
- Audio metrikleri izlenmiyor (WASAPI tarafı kör nokta)
- WebSocket uzak erişim — şu an localhost, ağdaki IP gösterimi eksik

### Mimari Notlar
- Self-healing kural motoru hot-reload + hysteresis + çakışma çözümü ile production-ready
- OR/AND kombinasyonu destekleniyor: "cpu > 80 || gpu > 85 && mem > 70"
- resolve_conflicts() öncelik sırasıyla aksiyonları filtreler

## Oturum: 30 Haziran 2026 (Devam) — Code Review Düzeltmeleri

### Bağlam
Üç farklı modelden (Claude Opus 4.8, GLM-5, MiniMax M3) kapsamlı kod review alındı, 67 dosya analiz edildi. Bulgular birleştirilip önceliklendirildi, sırayla düzeltildi.

### Tamamlanan Düzeltmeler
- bitrate_kbps → std::atomic<uint32_t> (frame/action thread race)
- frame_counter_ → std::atomic<uint32_t> (GL/frame thread slot race)
- WebSocket bind: 0.0.0.0 → 127.0.0.1 + REJI_WS_BIND env var override
- uploadCpuFrame (PreviewWidget): QByteArray her frame alloc → pre-allocated buffer + memcpy
- uploadFrame (ProgramWidget): QImage(...).copy() → pre-allocated QImage, sadece boyut değişince realloc
- copy_optimizer: target_layouts_/staging_layouts_/slot_gl_signaled_ shutdown+init'te reset (TDR sonrası stale state önlendi)
- g_pipeline global pointer → PipelineRegistry (weak_ptr tabanlı) — UAF riski kökten çözüldü
  - Pipeline → enable_shared_from_this<Pipeline>
  - MainWindow::pipeline_ → std::shared_ptr<rj::Pipeline>
  - rj_ws_command(uint64_t handle, int cmd) — handle + weak_ptr::lock()
  - rj_register_pipeline_handle yeni FFI fonksiyonu
  - main.cpp headless: stack Pipeline → make_shared (bad_weak_ptr fix)
- WGC capture_wgc.cpp: last_frame_/last_tex_ mutex ile korundu (next_frame/shutdown race)
- wgc_staging_tex_: resolution değişiminde otomatik yeniden oluşturma
- gpu_resource_manager: shared_handle_ leak fix (OpenSharedResource1 fail path)
- cbindgen.toml: explicit RjCommand export, parse_deps=false, include guard ismi düzeltildi

### Açık Kalemler (FFI sağlamlaştırma devamı)
- ~~catch_unwind eksik — Rust panic C++ tarafına sızabilir (extern "C" fn'lerin çoğunda yok)~~ ✅
- ~~offsetof static_assert — sadece sizeof kontrol ediliyor, alan offsetleri değil~~ ✅
- ~~rj_metrics_push (WebSocket): format!() her frame alloc, throttle yok~~ ✅
- ~~srt_output.cpp: her SRT paketinde metrik push, throttle yok (P5 bulgusu)~~ ✅
- FFI_CONTRACT.md dokümantasyonu yok

### Güncelleme — Tüm FFI Sağlamlaştırma Kalemleri Tamamlandı
- catch_unwind: 11/11 extern "C" fonksiyon korunuyor ✅
- offsetof static_assert: 25 alan, 3 struct, derleme zamanı doğrulandı ✅
- rj_metrics_push: format!() → pre-allocated buffer + write! ✅
- SRT metrik push: per-packet → 1 saniyede bir throttle, gerçek bitrate hesabı ✅

FFI sınırı artık: lifetime güvenli (weak_ptr registry), panic güvenli (catch_unwind),
ABI güvenli (offsetof assert + otomatik cbindgen), performans güvenli (throttle + buffer reuse).

### Düzeltilmemiş/Ertelenen Bulgular (review'lardan)
- GPU preview path (cross-vendor Vulkan transfer) — extension'lar mevcut (doğrulandı), implementasyon ayrı branch
- MetricsCollector thermal queries (GPU/CPU temp) — hâlâ stub
- TopologyDecider / GpuDiscovery — kapsamlı GPU karar motoru fikri, ayrı feature olarak planlandı
- Audio metrikleri izlenmiyor

### Mimari Notlar
- Üç-model code review yöntemi: Opus derinlemesine + doğru, GLM güvenlik bulgularında güçlü, MiniMax çapraz kontrol için kullanılabilir ama bağımsız güvenilirliği düşük
- PipelineRegistry pattern: handle-based FFI, ham pointer yerine opak uint64_t ID — Rust tarafı artık lifetime bilmeden güvenle çağırabiliyor
- cbindgen zaten otomatikti, sadece config netleştirildi — ffi_auto.h manuel düzenleme riski ortadan kalktı

### Test Coverage Eklendi
- Rust: rules.rs için 5 integration test (OR/AND koşullar, hysteresis, çakışma çözümü) — 34 toplam test geçiyor
- C++: pipeline_integration_test.cpp — 5 test (init/shutdown cycle, double shutdown safety, pre-init guards)
- FFI: RjActionType enum değer eşleşmesi için 7 static_assert
- Önceden var olan FrameProfilerTest/ShaderCacheTest başarısızlıkları dokunulmadı (kapsam dışı, ayrı incelenmeli)
