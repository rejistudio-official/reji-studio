## Oturum: 12 Temmuz 2026 — V8 Sprint 3-4 Grup A (Trivial Temizlik)

Faz 0 ön-triyaj (11 madde I15-I18/I21-I26/I34): 9× (a), I23 → (b) ayrı talimat,
I17 → (c) öncül çürük. Onaylı gruplama A→B→D. **Grup A tamamlandı** (6 commit
`cd97ee8..3401437`):

- **I26** — `lib.rs` demo `add()`+`it_works` silindi (dış çağrı yok). Rust 60→59 test.
- **I34** — inert `chk_source_auto` checkbox + `isSourceAuto()` getter kaldırıldı
  (source-switch aksiyonu yok, getter tüketicisiz). ffi.rs/main_window yorumları güncel.
- **I25** — `zig_win32_compat.c` stack guard: BCrypt-fail'de sabit `0xDEADBEEF` yerine
  `rdtsc ^ (pid*golden) ^ &stack` fallback.
- **I16** — `query_gpu_load_pct` PDH buffer'ı üye (`gpu_pdh_buf_`), yalnız büyüdükçe
  resize (1Hz alloc yok).
- **I22** — bayat ABI: `metrics.rs`/`ffi_auto.h` doc 60→64 byte; `check-abi.ps1`
  beklenen 56→64/52→56 (kırık wrapper düzeldi; `sizeof_check.cpp` zaten doğruydu).
- **I17** (c kapanış) — `DxgiFramePacing` üretimde HİÇ instantiate edilmemişti (ölü);
  "iki rakip pacing" öncülü çürük, tek otorite `FramePacer::pace()`. `frame_pacing.{h,cpp}`
  + orphan test + CMake/build.zig kayıtları silindi; 2 skill örnek-ref repointlendi.

**Doğrulama:** Rust 59+5+30 PASS; reji_app derlendi+linklendi (release orchestrator,
CMake reconfigure ile — kaynak silindi); ctest 6/8 (PipelineCharacterization dahil),
yalnız bilinen 2 kırık (FrameProfiler/ShaderCache). Hepsi kod incelemesi + build/test
ile doğrulandı; runtime davranış ölçümü yapılmadı (Grup A'da gerekmedi).

**Sırada:** Grup B (I21 hardcoded path/freopen + I24 CStr sınırlama), sonra Grup D
(I15 metrics_push alloc + I18 wasapi katman ihlali). I23 (b) → ayrı GPU-interop talimatı.

## Oturum: 12 Temmuz 2026 — V8/I14 (rj_metrics_poll) + Sprint 2 Kapanışı

### Tamamlananlar
- **I14 — `rj_metrics_poll` implementasyonu (Sprint 2'nin son açık maddesi):**
  - **Faz 0 teşhis:** Sorun link hatası değil — `ffi_bridge.zig`'deki stub daima `0`
    döndürüyordu, `reji_orchestrator`'da Rust karşılığı yoktu. `MainWindow::pollMetrics`
    çağrısı wired ama üreten uç boştu → UI durum barı (fps/bitrate/drop%) hiç güncellenmiyordu.
  - **Kaynak kararı:** Geçici `metric_ring` 16ms'de bir arka plan drainer'ı tarafından
    tam boşaltılıyor (healing'e besliyor) → poll oradan okusa yarışırdı. Doğru kaynak
    agregeli `MetricState` (WS `GetStreamStatus` ile aynı otoriter state).
  - **Fix (3 mantıksal adım):** (1) `MetricState`'e `frame_drop_pct` atomik + `snapshot()`
    helper'ı (UI'ın okuduğu ama state'te olmayan tek alan). (2) `rj_metrics_poll` Rust'ta
    (`ffi.rs`) — snapshot'ı `out`'a yazar, `1`/`0` döner, `catch_unwind`, WMI yok. (3) Zig
    stub kaldırıldı (iki lib'de aynı sembol = LNK2005). Sembol artık Rust'tan çözülüyor.
  - **Doğrulama:** 5 yeni metrics + 3 yeni ffi Rust testi PASS (toplam 60+5+30). reji_app
    linklendi (release orchestrator). ctest 6/8 PASS — PipelineCharacterization dahil;
    yalnız önceden bilinen 2 kırık (FrameProfiler/ShaderCache) kaldı, yeni kırık yok.
  - **Sapmalar giderildi:** `ffi_bridge.h` yorumu 56→64 byte/+51→+55; test yorumu
    "üç stub (Zig/C)" → tek Zig (ffi_bridge.c zaten kaldırılmış); pipeline seam yorumları
    "stub olduğu için" → "deterministik per-frame karakterizasyon için".
  - **GUI görsel onayı kullanıcıda** (fps/bitrate barının canlı güncellenmesi).
  - **Sprint 1 ve Sprint 2 tamamen kapandı.**
- **Gotcha (doğrulandı):** reji_app RELEASE orchestrator'a linkleniyor → `cargo build --release`
  şart (debug yeterli değil); build/ NMake, vcvars64 VS18 Community; `zig build ffi` ayrı.

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

### Madde 2 Tam Vizyon — FFI'dan Sadece Veri Geçişi
- rj_ws_command(handle, cmd) reverse FFI tamamen kaldırıldı
- ws_command_queue: ArrayQueue<(i32,i32)> — lock-free SPSC kuyruk, Rust yazar, C++ run_frame()'de drain eder
- PipelineRegistry sınıfı tamamen silindi — artık hiçbir taraf diğerinin pointer/handle'ına erişmiyor
- enable_shared_from_this<Pipeline> kaldırıldı (gereksiz hale geldi)
- Sonuç: FFI sınırı artık tamamen "sadece veri" prensibine uygun — pointer, handle, nesne referansı yok

### Ek Düzeltme — WS Port Fallback
- AnyDesk gibi araçlar 7070'i tutabiliyor — otomatik fallback eklendi (7070→7071→7072→7073)
- rj_get_ws_port() ile gerçek port C++'a bildiriliyor, loglanıyor
- control.html zaten location.port kullandığı için otomatik doğru porta bağlanıyor

### Vulkan Blit Capability Check (Code Review #4 — Opus/Sonnet/GLM ortak bulgu)
- init(): vkGetPhysicalDeviceFormatProperties ile VK_FORMAT_B8G8R8A8_UNORM için blit_src/blit_dst desteği sorgulanıyor
- execute_copy(): use_blit_ false ise vkCmdCopyImage fallback devreye giriyor
- AMD 780M sonucu: src=1 dst=1 linear=1 — mevcut donanımda sorun yoktu ama capability check artık kalıcı güvenlik ağı

### Cross-Adapter DXGI Fallback Düzeltmesi (Code Review — 4 model ortak bulgu)
- Sorun: DXGI capture path'te (WGC desteklenmezse fallback) cross-adapter NT handle sharing
  AMD+NVIDIA'da E_INVALIDARG ile başarısız oluyordu, transfer() sessizce nullptr dönüyordu —
  encode kalıcı ve sessizce bozuluyordu
- Düzeltme: create_cpu_fallback_staging() eklendi — NT handle başarısız olursa CPU üzerinden
  display→encode GPU transfer devreye giriyor
- Production etkisi: WGC aktifken (mevcut durum) bu kod hiç tetiklenmiyor, sadece WGC
  desteklenmeyen ortamlarda güvenlik ağı olarak duruyor
- Doğrulama: run.log'da [GpuRM] satırı yok (beklenen), pipeline WGC ile sorunsuz çalışıyor

### HealingOverlay Bağlantısı Kuruldu (GLM 5.2 özgün bulgusu)
- Sorun: onActionEvent() tamamen implement edilmişti ama hiçbir yerden çağrılmıyordu —
  Co-Pilot modunda kullanıcı onay akışı tamamen kopuktu
- Düzeltme: 200ms poll timer eklendi, rj_action_dequeue → ActionEvent dönüşümü kuruldu,
  actionApproved signal → rj_action_approve bağlandı
- Ek düzeltme: ActionType enum'ına Recover/Restore/LogOnly değerleri eklendi —
  önceden Recover aksiyonları yanlışlıkla "Reduce" olarak gösteriliyordu (yanıltıcı UI)
- Doğrulama: rules.json eşiği cpu>80% — test yükü 50%'de kaldı, aksiyon tetiklenmedi (beklenen);
  pipeline drop=0% ile sorunsuz çalışıyor

### ExternalMemoryBridge NT Handle Leak + Stack Canary (GLM 5.2 özgün bulgular)
- NT handle leak: create_vulkan_image_from_d3d11() her çağrıldığında (texture pointer değişiminde)
  handle state'e kaydedilmiyordu, invalidate_pool() bunları hiç kapatmıyordu — düzeltildi,
  d3d11_nt_handles[] array'i eklendi
- Dead code temizliği: PoolSlot.gl_handle hiç set edilmeyen, hiç çalışmayan bir CloseHandle
  bloğuna sahipti — kaldırıldı
- Stack canary: __stack_chk_guard sabit 0xDEADBEEF değerindeydi (SSP etkisiz) — BCryptGenRandom
  ile .CRT$XCU section üzerinden main() öncesi rastgele değer atanıyor artık

### Karmaşıklık ve Kırılganlık Taraması (Yeni Değerlendirme Turu)
Punktüel bug avlamak yerine sistematik kırılganlık taraması yapıldı: dosya boyutları,
fonksiyon uzunlukları, God Object adayları, sihirli sayılar, döngüsel FFI veri akışı,
tek nokta arızaları.

**Bulunan kritik sorunlar:**
- pipeline.cpp 986 satır (proje kuralı: 800 satır sınırı) — %23 aşım
- run_frame() 234 satır, paintGL() 272 satır — tek fonksiyonda çok fazla sorumluluk
- Pipeline::Impl God Object — 7 alt sistem (capture/encode/audio/srt/metrics/ext_bridge/thread)
  + ~25 alan tek struct'ta
- POOL_SIZE=3 üç bağımsız yerde tanımlıydı (senkron riski)
- 6000/3500 kbps ve 200ms gibi sabitler 5+ farklı dosyada hardcoded
- FFI_STATE (Rust OnceLock) — restart mekanizması yok, sessiz hata
- action_queue/ws_command_queue kapasitesi dolduğunda sessizce mesaj kaybı riski

**Bu turda düzeltilenler (düşük risk, yüksek değer):**
- reji_constants.h oluşturuldu — POOL_SIZE tek kaynaktan (kGpuPoolSize)
- Bitrate/timeout sabitleri C++ (reji_constants.h) ve Rust (constants.rs) tarafında merkezi hale geldi
- FFI_STATE.get() başarısız olduğunda artık 8 fonksiyonda loglanıyor (önceden sessizdi)

**Ertelenen, yüksek riskli (ayrı oturum gerektiriyor):**
- Pipeline::Impl God Object refactoring — alt sistemlere bölme
- run_frame()/paintGL() fonksiyon bölme
- action_queue/ws_command_queue backpressure/overflow handling
- Zig modül-global state (external_memory_bridge.zig, vulkan_initializer.zig) —
  çoklu instance senaryosu için hazır değil

### Sonnet 5 Review — 4 Kritik Bulgu Düzeltildi
- Frame thread busy-loop: init() başarısız olursa frame_thread_ artık hiç başlamıyor
  (önceden %100 CPU busy-loop'a giriyordu)
- SrtOutput çift shutdown: already_cleaned_up_ atomic flag ile do_seh_cleanup() idempotent hale geldi
  (önceden SrtGlobalRegistry refcount negatife düşebiliyordu)
- AMD path GPU sync: amd_copy_fence_ (ID3D11Query) eklendi, Flush() sonrası gerçek completion wait
  (önceden Flush() sadece komut gönderiyordu, tamamlanma garantisi yoktu — torn frame riski)
- SrtOutput::send_internal static state: last_metric_push_us_/bytes_since_last_push_
  Impl instance field'ına taşındı (önceden tüm SrtOutput instance'ları arasında paylaşılıyordu)

### Kırılganlık Turu — Madde 3 ve 4
- action_queue/ws_command_queue push() artık başarısızlığı logluyor (DROPPED_ACTIONS_COUNT/
  DROPPED_WS_CMDS_COUNT sayaçları eklendi) — önceden sessizce mesaj kaybediliyordu
- Zig modül-global state (external_memory_bridge.zig, vulkan_initializer.zig):
  initialized bool guard eklendi, ikinci init() çağrısında uyarı basıyor
  ⚠️ Bu geçici önlem — gerçek çözüm state'i global'den instance-level struct'a taşımak
  (API imza değişikliği gerektirir, ayrı büyük refactor olarak not edildi)

## Oturum: 1 Temmuz 2026

### Modülerlik ve Endüstri Uyumluluğu — Strateji Belirlendi
Uygulamanın uzun vadeli hedefi netleştirildi: modüler, ölçeklenebilir, sektör
standardı araç/donanımlarla uyumlu (resmi destek olmasa bile uyumluluk zemini).

docs/ROADMAP.md oluşturuldu — 5 fazlı plan:
- Faz 0: Temel hazırlık (FFI_CONTRACT.md ✅ tamamlandı, Pipeline::Impl God Object
  refactoring — henüz başlanmadı, Opus ile yapılmalı)
- Faz 1: OBS-WebSocket protokol uyumluluğu (Stream Deck/Companion ekosistemi)
- Faz 2: RTMP çıkışı (ITransport üzerinden — gerçek platform yayını için)
- Faz 3: Çoklu kaynak mimarisi (ISource — webcam/capture card/NDI girişi)
- Faz 4: NDI desteği (profesyonel yayıncılık standardı)
- Faz 5: Zig global state tam çözümü (çoklu instance senaryosunda gerçek ihtiyaç doğunca)

docs/FFI_CONTRACT.md oluşturuldu — 13 extern "C" fonksiyonun resmi sözleşmesi:
- Her fonksiyon için thread-safety, panic davranışı, çağıran thread dokümante edildi
- Düzeltme: rj_start_monitor OnceLock kullandığı için gerçekte idempotent
- Struct ABI: RjCommand=24, RjAction=20, RjMetricSample=64 byte — offsetof tablosu eklendi

## Oturum: 1 Temmuz 2026 — Pipeline::Impl Refactoring Başlangıcı

### Pipeline::Impl Refactoring — Aşama 0 Tamamlandı
Opus ile detaylı analiz: 9 alt sistem tespit edildi (Capture/Encode/Audio/Output/Metrics/
GpuInterop/Command/Timing/Lifecycle), 2 sıkı düğüm (on_packet, handle_device_lost)
orkestratörde kalacak şekilde planlandı. 9 aşamalı, en izole parçadan en düğümlüye
giden bir sıra belirlendi.

Aşama 0 (güvenlik ağı) bulguları:
- NVENC callback tamamen senkron, frame thread'de çalışıyor — EncodeSubsystem çıkarımı
  öngörülenden daha basit olacak
- Karakterizasyon test harness'i kuruldu (tests/pipeline_characterization_test.cpp,
  baseline_metrics.txt) — her aşama sonrası regresyon kontrolü için
- Gerçek bug bulundu ve düzeltildi: width/height cfg üzerinden formal veri yarışı
  (handle_device_lost frame thread'de yazıyor, notify_vulkan_ready başka thread'den
  okuyor, senkronizasyon yoktu) → atomic<uint32_t> width/height eklendi
- cfg.bitrate_kbps ölü kod tespit edildi (hiç okunmuyordu, sadece gölge yazım) → kaldırıldı,
  tek gerçek kaynak atomic bitrate_kbps

Sıradaki: Aşama 1 — FramePacer alt sistemini çıkar (en izole, en düşük riskli)

### Pipeline::Impl Refactoring — Aşama 1-5 Tamamlandı
- Aşama 1: FramePacer (timing/pacing) — davranış korundu
- Aşama 2: MetricsSubsystem (CpuMeter+MetricsCollector+fps) — last_frame_ticks buraya taşındı
- Aşama 3: AudioSubsystem (WasapiCapture lifecycle) — SEH raw() accessor pattern kuruldu
- Aşama 4: OutputSubsystem (SRT+srt_atomic) — send() üç-yollu semantik korundu
- Aşama 5: CommandRouter (cmd/ws drain + SPSC ring + action thread) — en riskli aşama,
  callback-tabanlı bağımlılık çözümü ile Encode/Impl'e sıkı bağ olmadan çalışıyor

Her aşamada: build temiz + PipelineIntegration + PipelineCharacterization testleri PASS +
baseline_metrics.txt karşılaştırması (fps~60, bitrate 6000→3500 geçişi 60. frame'de,
frame_drops 0-1 gürültü bandı) — davranışsal regresyon yok.

Kalan aşamalar: 6 (EncodeSubsystem), 7 (GpuInteropSubsystem), 8 (CaptureSubsystem),
9 (RecoveryCoordinator + Impl'i ince orkestratöre indirme) — bunlar en düğümlü/riskli
kısımlar, ayrı oturumda dikkatle ele alınacak.

### Pipeline::Impl God Object Refactoring — TAMAMLANDI (Aşama 0-9)

Opus ile başlatılan 9 aşamalı refactoring tamamlandı. Sonuç:
- pipeline.cpp: 986 → 780 satır (−21%)
- run_frame(): 234 → 111 satır (−53%)
- 9 alt sistem çıkarıldı: FramePacer, MetricsSubsystem, AudioSubsystem, OutputSubsystem, 
  CommandRouter, EncodeSubsystem, GpuInteropSubsystem, CaptureSubsystem, RecoveryCoordinator
- Impl artık ince orkestratör: 8 alt sistem üyesi + 4 lifecycle flag + indirgenemez 
  cross-cutting state (cfg, width/height atomics, frame_drops, UI callback'leri, 
  sıkı-düğüm applier'ları: on_packet, apply_command, apply_frame_cmd)

Her aşamada: build temiz + PipelineIntegration + PipelineCharacterization testleri PASS + 
baseline_metrics.txt karşılaştırması + (kritik aşamalarda) GUI görsel doğrulama — 
sıfır davranışsal regresyon.

Aşama 0'da ayrıca gerçek bir veri yarışı (width/height) ve ölü kod (cfg.bitrate_kbps) 
düzeltildi.

Kalan sınırlar (kabul edilen, doğal): RecoveryCoordinator stateless static fonksiyon 
(subsystem değil, cross-cutting orchestration); on_packet sıkı düğümü Impl'de kalıyor 
(Output+Metrics'e dokunuyor); UI callback'leri (preview_cb, d3d11_frame_cb, scene_cmd_cb) 
doğaları gereği orkestratörde.

Linear: REJ-5 (Faz 0) artık tamamen tamamlanabilir durumda.

## Oturum: 2 Temmuz 2026 — Faz 1 Başlangıcı (obs-websocket)

### Faz 1 — Aşama 1

obs-websocket v5 handshake iskeleti eklendi: Hello (op 0) → Identify (op 1) →
Identified (op 2). Bu aşamada henüz gerçek komut (Request/RequestResponse, op 6/7)
işlenmiyor — yalnızca bağlantı açılışı obs-websocket uyumlu hale getirildi.

Yapılanlar:
- `src/orchestrator/src/obs_protocol.rs` (yeni): `WsEnvelope {op, d}` zarfı, opcode
  sabitleri, `hello()`/`identified()` zarf üreticileri, RPC_VERSION=1.
- `ws_server.rs`: `handle_socket()` bağlantı açılışında Hello gönderir ve **hemen
  ardından** normal `tokio::select!` döngüsüne girer. Identify ve soft-timeout döngü
  İÇİNDE ele alınır; ayrı bloklayan "Identify bekleniyor" adımı YOK.
- Mesaj sınıflandırma `classify()` içinde: `op:1` → Identify (Identified dönülür),
  başka `op` → obs mesajı (Aşama 1'de sadece log), `op` yok → legacy `{cmd}` yolu
  (stream_start vb.) `handle_legacy_cmd()` ile aynen çalışıyor.

Tasarım kararı — **non-blocking toleranslı handshake**: Katı "ilk mesaj op:1 değilse
kapat + 5 sn bloklayan bekleme" davranışı mevcut `control.html` ile çakışıyordu
(control.html Identify göndermiyor, sadece metrik izliyor). İki katmanlı regresyon
riski vardı: (1) bağlantının kopması, (2) bloklayan bekleme yüzünden ilk 5 sn metrik
akışının donması. Çözüm: Hello sonrası doğrudan select! döngüsü — evt_rx metrik akışı
ilk andan itibaren kesintisiz iletiliyor. Identify gelirse Identified dönülüyor;
gelmezse 5 sn sonra yalnızca log'lanıp legacy olarak sürdürülüyor (bağlantı KAPATILMAZ,
akış KESİLMEZ). (Not: bu, katı timeout/kaynak-sızıntısı korumasını bilinçli gevşetir;
obs-uyum + control.html uyumu birlikte korunur.)

Testler (`tests/ws_obs_protocol_test.rs`, tokio-tungstenite dev-dependency):
- Hello/Identify/Identified akışı → PASS
- Legacy `{cmd}` regresyonu (handshake sonrası) → PASS
- Legacy istemci Identify göndermeden çalışıyor → PASS
- **Event akışı handshake beklenirken bloklanmıyor** (Identify yokken metrik 2sn < 5sn
  içinde iletiliyor) → PASS — kritik regresyon guard'ı
- Identify timeout sonrası bağlantı legacy olarak sürüyor (kapanmıyor) → PASS

`cargo test -p reji-orchestrator`: 29 + 5 + 5 = 39 test PASS, regresyon yok.

Sıradaki: Aşama 2 — Request/RequestResponse (op 6/7), GetVersion/StartStream/
GetSceneList gibi requestType'ların işlenmesi.

### Faz 1 — Aşama 2 (GetVersion/StartStream/StopStream/GetStreamStatus)

Request (op 6) / RequestResponse (op 7) genel dispatch mekanizması kuruldu; dört
requestType gerçek işleve bağlandı: **GetVersion, StartStream, StopStream, GetStreamStatus**.
(GetSceneList/SetCurrentProgramScene bu aşamada YOK — FFI/scene-list state'i gerektiriyor,
Aşama 4-5.)

- `obs_protocol.rs`: `request_response_ok()` / `request_response_err()` zarf üreticileri
  ve `request_status` sabitleri (100 Success, 204 UnknownRequestType). Başarılı yanıt
  verisi spec'e uygun `responseData` alanına konur.
- `ws_server.rs`: `ClientMsg::Request` dalı + `classify()` op 6 ayrımı; `dispatch_request()`
  requestType eşleşmesini tek `match`'te toplar. Bilinmeyen tip → 204, **bağlantı
  kapatılmaz** (aynı soketten sonraki istek çalışmaya devam eder).

Tasarım kararları:
- **Identify zorunlu değil (Request için de).** Aşama 1'in toleranslı felsefesiyle tutarlı:
  Identify gelmeden gelen `{"op":6,...}` de işlenir (yalnızca log'lanır, reddedilmez).
- **`streaming_active` tek yazma noktası:** `StartStream`/`StopStream` handler'ı bayrağı
  YAZMAZ; mevcut `cmd_tx.send("stream_start"|"stream_stop")` yoluna (legacy `{cmd}` ile
  **aynı kanal**) delege eder. Bayrak, komutu tüketen tek noktada güncellenir:
  `ws_server::process_stream_cmd()` (üretimde ffi.rs cmd_rx döngüsünden çağrılır). Böylece
  hem legacy hem obs-websocket yolu tek doğruluk kaynağından geçer, iki yerden yazma yok.
- **İyimser flag:** `process_stream_cmd` bayrağı "komut gönderildi" anlamında günceller.
  Encode/output tarafının yayının gerçekten başladığını doğrulayan bir onay mekanizması
  **henüz yok** → gerçek durum senkronizasyonu ayrı bir iş (ileri aşama).

Kapsam sınırı (bilinçli, eksiklik değil): `GetStreamStatus` yanıtında **`outputBytes`,
`outputDuration`, `outputCongestion` sabit 0** bırakıldı. Neden: WsState'te düşük maliyetle
erişilebilen bir MetricSample/son-örnek kaynağı yok (evt_rx yalnızca serialize edilmiş event
string broadcast'i); gerçek metrik entegrasyonu Aşama 2'nin dört-komut kapsamının dışında bir
genişletme. Bitrate/bytes/duration alanları Aşama 3'te metrics state'i bağlanınca doldurulacak.

Testler (`tests/ws_obs_protocol_test.rs`, +4 yeni):
- `get_version_doner_dogru_alanlar` → PASS (responseData.rpcVersion==1, requestStatus.code==100)
- `start_stop_stream_streaming_active_gunceller` → PASS (StartStream→outputActive true,
  StopStream→false; tüketici üretimdeki ffi döngüsünün test karşılığı)
- `bilinmeyen_request_type_204_doner_baglanti_kapanmaz` → PASS (204 sonrası aynı soketten
  GetVersion hâlâ çalışıyor = bağlantı kapanmadı kanıtı)
- `identify_olmadan_request_yine_islenir` → PASS (tasarım kararı 1)
- Regresyon: Aşama 1'in 5 testi hâlâ PASS.

`cargo test -p reji-orchestrator`: 29 + 5 + 9 = 43 test PASS, regresyon yok.

Sıradaki: Aşama 3 — GetStreamStatus metrik alanları (bytes/duration/bitrate) metrics
state'inden doldurma; Aşama 4-5 — GetSceneList/SetCurrentProgramScene (FFI/scene-list).

### Faz 1 — Aşama 3 (GetStreamStatus tam alan seti + MetricState entegrasyonu)

İki iş yapıldı: (1) Aşama 2'nin eksik GetStreamStatus yanıtı obs-websocket v5 spec'inin
**tam 8 alanına** tamamlandı; (2) `MetricState` (metrics.rs, atomic/lock-free) WsState'e
bağlandı — artık gerçek `frame_drops` okunuyor.

- `obs_protocol.rs`: `format_timecode(ms) -> "HH:MM:SS.mmm"` eklendi (spec örneğiyle birebir).
- `ws_server.rs`: `now_epoch_ms()` yardımcısı; `WsState`'e `metric_state: Arc<MetricState>` ve
  `stream_started_at_ms: Arc<AtomicU64>` (0 = akış kapalı). `process_stream_cmd()` genişletildi:
  stream_start→started_at=now, stream_stop→started_at=0 (TEK yazma noktası korunur).
  `GetStreamStatus` kolu tam alan setiyle yeniden yazıldı.
- `ffi.rs`: WsState'e `metric_state.clone()` geçirildi — **FfiState._metric_state ile AYNI Arc**
  (üretimde `MetricState::new()` tek çağrı, satır 122; iki instance yok, kod incelemesiyle doğrulandı).

Dürüstlük ilkesi — her alan gerçek mi, değil mi ve NEDEN:

| Alan | Değer | Karar |
|---|---|---|
| `outputActive` | streaming_active | **Gerçek** |
| `outputDuration` | now − stream_started_at_ms (aktifken) | **Gerçek** — stub encode'a bağımlı değil |
| `outputTimecode` | duration'dan formatlanır | **Gerçek** (duration gerçekse timecode da) |
| `outputSkippedFrames` | MetricState.frame_drops() | **Gerçek** — atomic olarak tutuluyor |
| `outputBytes` | 0 | SRT output stub (README: "output layer stub"); bitrate×duration tahmini yanıltıcı olur → üretilmedi |
| `outputReconnecting` | false | Reconnect mantığı yok |
| `outputCongestion` | 0.0 | Network congestion sinyali yok |
| `outputTotalFrames` | 0 | Toplam kare sayacı tutulmuyor |

0/false bırakılan 4 alan **bilinçli** — SRT/NVENC gerçek implementasyonu (şu an stub) ve/veya
yeni sayaçlar gerektiriyor. Sahte/tahmini değer ÜRETİLMEDİ: gerçek bir obs istemcisi (Stream
Deck) bu sayılara güvenip kullanıcıya gösterebilir; yanlış bilgi "hiç bilgi vermemekten" kötü.

Testler (`tests/ws_obs_protocol_test.rs`, +4 yeni):
- `get_stream_status_tam_alan_seti` → PASS (8 alanın da isim+tip varlığı — regresyon guard'ı)
- `stream_start_sonrasi_duration_artar` → PASS (outputDuration > 0, timecode ilerliyor)
- `stream_stop_sonrasi_duration_sifirlanir` → PASS (outputDuration == 0)
- `frame_drops_metric_state_uzerinden_yansir` → PASS (MetricState'e 42 yazıldı → outputSkippedFrames 42)
- Regresyon: Aşama 1+2'nin 9 testi hâlâ PASS.

`cargo test -p reji-orchestrator`: 29 + 5 + 13 = 47 test PASS, regresyon yok.

Sıradaki: Aşama 4-5 — GetSceneList/SetCurrentProgramScene (FFI/scene-list state); ileride
outputBytes/outputTotalFrames gerçek SRT/NVENC sayaçlarına bağlanınca doldurulacak.

### Faz 1 — Aşama 4 (FFI + C++ boru hattı: scene names push + gerçek scene_switch + SetScene)

Amaç: sahne senkronizasyonunun FFI/C++ altyapısını kurmak (Aşama 5'teki GetSceneList/
SetCurrentProgramScene handler'ları için zemin). Handler'lar bu commit'te YOK — Aşama 5.

**Rust (ffi.rs):**
- `rj_push_scene_names(names, count)` — C++ UI sahne isimlerini WsState.scene_names'e yazar.
  Ownership: C++ pointer verir, Rust HEMEN kopyalar (`into_owned`), dönünce ham pointer saklamaz.
  Güvenlik: null-check, MAX_SCENES=256 clamp, char-sınırında güvenli truncate (ham byte slice
  UTF-8 ortasında panik yapabilirdi → `is_char_boundary`), catch_unwind.
- `rj_user_event_scene_switch(scene_id)` — C++ stub'ı GERÇEK impl'e taşındı; WsState.current_scene_idx'i
  yazar. Tek gerçek kaynak: Rust tahmin etmez, C++'ın gerçekte yaptığını dinler (sendSceneSwitchEvent
  her sahne değişiminde çağrılıyor: UI tıklaması + legacy cut + Aşama 5 SetScene geçişi).
- `RJ_WS_CMD_SET_SCENE = 5` sabiti (Aşama 5'te ws_server kullanacak, şimdilik #[allow(dead_code)]).
- WsState'e alanlar: `scene_names: Arc<Mutex<Vec<String>>>`, `current_scene_idx: Arc<AtomicU32>`,
  `ws_command_queue`: FfiState'teki ile AYNI Arc (metric_state deseni — iki kuyruk yok).

**sizeof_check neden ATLANDI (ffi-safety checklist'te N/A):** Yeni `#[repr(C)]` struct EKLENMEDİ —
yalnızca pointer+count parametreli fonksiyonlar ve mevcut WsState'e (FFI ABI'sinde olmayan, tamamen
Rust-içi bir struct) alan eklemesi var. ABI boyut/offset sözleşmesi değişmedi, dolayısıyla
sizeof_check.cpp/.zig'e yeni giriş gerekmez.

**cbindgen sızıntısı düzeltildi (Aşama 1'den gizli kalan bug, C++ ilk kez şimdi derlendi):**
cbindgen `obs_protocol.rs`'in `pub` sabitlerini (`RPC_VERSION`, `HELLO`, `SUCCESS`, …) ffi_auto.h'a
export ediyordu; `RPC_VERSION` Windows SDK `rpcdcep.h` ile çakışıp C2378 veriyordu (`SUCCESS` de riskliydi).
Bu sabitler obs protokolünün İÇ değerleri, FFI ABI'si değil → `pub(crate)` yapıldı (ws_server yine
crate-içi kullanıyor, cbindgen artık export etmiyor). ffi_auto.h gitignore'lı üretilmiş artefakt.

**C++ tarafı:**
- rust_bridge.cpp/.h: `rj_user_event_scene_switch` no-op stub SİLİNDİ → gerçek Rust sembolüne
  (ffi_auto.h) bağlandı. stream_start/stop hâlâ stub (kapsam dışı).
- SceneCommandCallback/SceneCallback imzası `void(int)` → `void(int cmd, uint32_t param)` genişletildi
  (pipeline.h, command_router.h, pipeline.cpp invoke_scene_cmd_, main_window.cpp lambda).
- command_router.cpp switch: case 3/4 param'ı geçiriyor (cut/fade'de kullanılmaz, imza tutarlı);
  case 5 (SetScene) EKLENDİ.
- main_window.cpp: cmd==5 → `setCurrentRow(param)` + `onCutTransition()` (sendSceneSwitchEvent zaten
  içinde). Sınır dışı idx sessizce yok sayılır + log (crash yok). `pushSceneNamesToRust()` yardımcısı:
  buildCentralWidget/addScene/removeScene sonunda çağrılır. QByteArray ömür uyarısı yorumda.

**Test sonuçları:**
- Rust: 31 (lib, +2 yeni: `test_scene_switch_event_updates_current_idx`,
  `test_push_scene_names_null_does_not_crash`) + 5 + 13 = 49 PASS.
- C++ (release/NMake, gerçek Vulkan — bkz. aşağıdaki mock notu): 4 değişen dosya (pipeline.cpp,
  command_router.cpp, main_window.cpp, rust_bridge.cpp) derlendi; reji_app.exe linklendi.
  (İlk link LNK2019 verdi — `target/release/reji_orchestrator.lib` bayattı; `cargo build --release`
  ile çözüldü. Not: C++ build CMAKE_BUILD_TYPE=Release → release Rust lib'ini link eder.)
- ctest: **ffi_boundary PASS**, **PipelineIntegration PASS**, PipelineCharacterization PASS
  (scene callback + FFI ile ilgili olanlar). FrameProfilerTest & ShaderCacheTest FAILED —
  **önceden var olan, ilgisiz** başarısızlıklar (frame zamanlama sampleCount / shader hash eşitliği;
  diff'im bu alt sistemlere dokunmuyor).

**mock preset ÇALIŞMIYOR (önceden var olan repo sorunu, değişikliklerimle ilgisiz):**
`cmake --preset mock` derlenemiyor çünkü `copy_optimizer.cpp`/`gpu_query_timing.cpp` koşulsuz
`#include <vulkan/vulkan.h>` yapıyor; ama mock modda CMake `find_package(Vulkan)` çağırmıyor
(header yolu yok) VE `pipeline.h` mock dalı `using VkDevice = void*` tanımlıyor → gerçek vulkan.h
eklenirse C2371 çakışması. Repo'da mock vulkan.h shim yok. Bu yüzden doğrulama gerçek-Vulkan
**release** build'iyle yapıldı (çalışan konfigürasyon). Mock config'in düzeltilmesi kapsam dışı.

**Manuel test (Qt UI, otomatikleştirilemez):** Sahneye tıklama → crash yok / run.log HATA yok
doğrulaması HENÜZ YAPILMADI (headless ortamda GUI güvenilir çalıştırılamıyor). reji_app.exe başarıyla
üretildi; sahne-tıklama davranışı elle doğrulanmalı (talimatta kabul edilebilir işaretli).

Sıradaki: Aşama 5 — GetSceneList / SetCurrentProgramScene handler'ları (ws_server dispatch_request).

### Faz 1 — Aşama 5 (GetSceneList / SetCurrentProgramScene handler'ları)

Amaç: Aşama 4'te kurulan FFI/scene-state altyapısı üzerine iki WS handler'ı bağlamak.
Yalnızca `ws_server::dispatch_request` + `obs_protocol` değişti; FFI/C++ dokunulmadı.

- `GetSceneList` — `scene_names` (C++'ın `rj_push_scene_names` ile beslediği) + `current_scene_idx`
  (C++'ın `rj_user_event_scene_switch` ile doğruladığı) üzerinden yanıt üretir. GetStreamStatus
  gibi iyimser DEĞİL: her zaman C++'ın DOĞRULADIĞI son durumu yansıtır (tek gerçek kaynak).
- `SetCurrentProgramScene` — isim `scene_names`'te aranır; bulunursa `ws_command_queue`'ya
  `(RJ_WS_CMD_SET_SCENE=5, idx)` push edilir, `{}` + code 100 döner. `current_scene_idx` BURADA
  güncellenmez — gerçek geçiş C++'ta olup `rj_user_event_scene_switch` üzerinden geri bildirilene
  kadar beklenir (tek gerçek kaynak; iyimser güncelleme yok). Bulunamazsa code **600**
  (ResourceNotFound, resmi obs-websocket protokolünden doğrulandı), bağlantı kapatılmaz.

**Dürüstlük notları (kritik):**
- **pseudo-UUID sınırlaması:** Reji'de gerçek UUID kavramı yok. `obs_protocol::pseudo_uuid`
  isimden `DefaultHasher` ile deterministik 8-4-4-4-12 hex üretir (aynı isim → aynı UUID). Bu
  kriptografik olarak çakışmasız gerçek bir UUID DEĞİLDİR — yalnızca isim-başına kararlı bir
  tanımlayıcıdır (Aşama 3'teki 0/false dürüstlük ilkesiyle aynı çizgi). Aynı isimli iki sahne
  olursa UUID'leri çakışır; obs-websocket'te sahne isimleri zaten benzersiz olduğundan pratikte sorun değil.
- **Sahne sırası belirsizliği:** Gerçek OBS'in `scenes` dizisini ters çevirip çevirmediği
  DOĞRULANMADI (belirsiz bilgi). Şu an ters çevirmeden `scene_names` sırasıyla gönderiliyor.
  **Gerçek istemciyle (Aşama 6) doğrulanmalı** — sıra ters görünürse GetSceneList'in `scenes`
  üretimi burada düzeltilecek.
- `currentPreviewSceneName`/`currentPreviewSceneUuid` = `null` (studio mode yok, spec'e uygun).

Kapsam sınırı: `CreateScene`/`RemoveScene`/`SetSceneName` YOK — sahne CRUD'u obs-websocket
üzerinden yapılamıyor (yalnızca UI'dan). ROADMAP'in beş temel komutundan ikisi tamamlandı.

Testler (5 yeni): `pseudo_uuid_kararli` (obs_protocol birim testi) + `get_scene_list_bos_liste`,
`get_scene_list_isimler_dogru`, `set_current_program_scene_basarili`,
`set_current_program_scene_bulunamadi` (ws entegrasyon). Tüm suite yeşil: 32 lib + 5 rules +
17 ws = 54 test PASS, `cargo build -p reji-orchestrator` temiz (yalnızca önceden var olan uyarılar).

### Faz 1 — Aşama 6 (gerçek obs-websocket istemcisiyle doğrulama)

Amaç: Aşama 1-5'te yazılan obs-ws v5 sunucusunu GERÇEK üçüncü-parti istemcilerle sınamak;
4 açık soruyu (sahne sırası, pseudo-UUID, paralel bağlantı, StartStream/StopStream) kesin
sonuca bağlamak. Gerçek build (`cargo build --release` + `scripts/build.py`) + çalışan uygulama
üzerinde yapıldı. Ham çıktılar `docs/reviews/` (gitignore'lı) altında; script'ler `scripts/test_obs_*`.

**Kullanılan araçlar:** `simpleobsws` (Python), spec-uyumlu ham JSON istemcisi (`websockets`),
`obs-websocket-js` 5.0.8 (Node — Companion'ın kullandığı kütüphane). **Fiziksel Stream Deck /
Companion donanımı/yazılımı test EDİLMEDİ** (mevcut değil) — doğrulama gerçek istemci
kütüphaneleriyle yapıldı; bunu eksiklik olarak not düşüyoruz (dürüstlük ilkesi).

**BAŞLIK BULGU — alt-protokol müzakeresi kök blokörü (düzeltildi):**
Gerçek obs-websocket istemcileri handshake'te `Sec-WebSocket-Protocol` ile bir alt-protokol
teklif eder ve sunucunun onu SEÇMESİNİ bekler; Reji hiçbirini seçmiyordu → TÜM obs-websocket-js
istemcileri (json/msgpack fark etmez) daha Hello'ya varmadan *"Server sent no subprotocol"* ile
kopuyordu. `simpleobsws` de msgpack-only olduğu (JSON text frame'leri yok sayar) için hiç identify
olamadı. **Fix (commit 1fa47d5):** `ws_handler` artık `obswebsocket.json` teklif edilirse echo'lar.
Canlı doğrulama: `obs-websocket-js/json` artık bağlanıp identify oluyor (5.0.0-reji-compat, rpc 1).
`obswebsocket.msgpack` (binary serileştirme) HÂLÂ desteklenmiyor → **Aşama 7 adayı**; Companion'ın
Node-varsayılan msgpack modu ve simpleobsws bu yüzden hâlâ bağlanamaz (dürüst, beklenen).

**4 açık sorunun kesin sonuçları:**
1. **Sahne sırası — TERSTİ, düzeltildi (commit 3f6e2cf).** obs-websocket v5 konvansiyonu (OBS
   kaynağı `Obs_ArrayHelper.cpp`: azalan indeks + `std::reverse`; GitHub issue #1034 / disc #1022)
   `sceneIndex 0`'ı UI'nın EN ALTINA koyar, diziyi alttan üste verir (v5.0.1+, kasıtlı). Reji
   scene_names'i üstten alta veriyordu → sceneIndex 0 = üst (OBS'in tam tersi). `.rev().enumerate()`
   ile düzeltildi. Canlı doğrulama: [Sahne 1, 2, 3] (üstten alta) → sceneIndex 0=Sahne 3, 2=Sahne 1.
   `current_scene_idx`/`SetCurrentProgramScene` (isimle/iç index) bu sunum sırasından etkilenmiyor.
2. **pseudo-UUID — sorun yok.** Deterministik UUID'ler üretiliyor; hiçbir istemci akışı UUID ile
   seçim yapmıyor (SetCurrentProgramScene `sceneName` kullanır). Gerçek RFC-4122 UUID değil ama
   istemci tarafında kimse doğrulamıyor → zararsız. Çözüm gerektirmedi (Aşama 5 dürüstlük notu geçerli).
3. **Paralel bağlantı — ÇALIŞIYOR.** Identify'lı (A) + hiç Identify göndermeyen legacy (B) aynı
   anda: her ikisi de aynı metrik event akışını aldı (ör. 405/405), legacy 5s soft-timeout'ta
   KAPATILMADI, A'nın timeout sonrası request'i çalıştı, B'nin geç Identify'ı başarılı. Aşama 1'in
   temel iddiası (`test_obs_parallel.py`) gerçek dünyada doğrulandı. Not: legacy metrik akışı
   (`{"fps":..}`) yalnızca uygulama aktif render ederken akıyor (pencere arka plandayken duraklıyor).
4. **StartStream/StopStream — protokol seviyesinde çalışıyor, gerçek çıktı yok.** StartStream sonrası
   `outputActive=true`, `outputDuration`/`outputTimecode` gerçek zamanlı ilerliyor (2005ms →
   "00:00:02.005"); StopStream sıfırlıyor. Ama `outputBytes=0` (SRT stub) — yani obs-websocket
   seviyesinde çalışıyor, gerçek yayın çıktısı henüz yok (SRT stub, README ile tutarlı).

**Yan bulgular:**
- **Port fallback (olumlu):** 7070'i AnyDesk tuttuğu için Reji 7071'e düştü — fallback mantığı
  gerçek dünyada doğru çalıştı. Test script'leri portu otomatik tespit ediyor.
- **Build süreci açığı:** `scripts/build.py` (= `just build`) yalnızca C++ relink yapar, Rust'ı
  DERLEMEZ. Önce elle `cargo build --release` gerekir — yoksa stale Rust lib linklenir (ilk testte
  GetSceneList `204 Unknown request type` dönüyordu; Rust yeniden derlenince düzeldi). İleride
  build.py'ye cargo adımı eklenmesi düşünülebilir.
- **Encoding (Türkçe):** C++ `text().toUtf8().constData()` → Rust `CStr::to_string_lossy()` →
  serde_json → uçtan uca UTF-8, yapısal olarak güvenli (char-sınırında kesme dahil). Default
  isimler byte-exact round-trip ediyor (`"Sahne 1"` = `5361686e652031`). Türkçe isimle GUI rename
  otomasyonu (SendKeys+F2) QListWidget'ı edit moduna sokamadı (scripted focus sınırı, Reji kusuru
  değil) — GetSceneList'te Türkçe isim görsel round-trip'i elle test için açık kaldı.

Testler (4 yeni/güncel ws): `subprotocol_json_teklif_edilirse_secilir_ve_handshake_calisir`,
`subprotocol_msgpack_teklif_edilirse_secilmez_ve_istemci_koparir` (yeni),
`get_scene_list_obs_konvansiyonu_ters_sira` (eski `..._isimler_dogru`'nun ters-sıra güncellemesi).
ws suite: 17 → 19 test PASS. Commit'ler: `1fa47d5` (subprotocol), `3f6e2cf` (sahne sırası),
`7ca56a1` (doğrulama script'leri), + bu docs commit'i.

### Faz 1 — Aşama 7 (msgpack serileştirme desteği)

Amaç: Aşama 6'nın bilinen açığını kapatmak — `obswebsocket.msgpack` teklif eden istemciler
(Node-varsayılan obs-websocket-js = Companion'ın bağımlılığı, ve simpleobsws) artık bağlanabiliyor.

**Mimari: tek mantık, iki kodlama.** Tüm iş mantığı (dispatch_request, hello, identified)
`WsEnvelope`/`Value` üzerinde moddan bağımsız; yalnızca tele yazış/okuyuş değişir:
- `ws_server::WireMode { Json, Msgpack }` — bağlantı bazlı yerel değişken (`handle_socket`
  parametresi), WsState'e KONMADI (global değil, bağlantının ömrü boyunca sabit özelliği).
- `encode(mode, env)` — Json→Text+serde_json, Msgpack→Binary+`rmp_serde::to_vec_named`.
- Seçim: `ws.protocols([json, msgpack])` + upgrade sonrası `WebSocket::protocol()` (axum 0.7.9
  kaynak kodundan doğrulandı: seçilen protokol hem yanıt header'ına yazılır hem `protocol()`
  ile raporlanır; sunucu listesindeki İLK eşleşen kazanır → ikisini teklif edene JSON).
  Hiç teklif yoksa Json varsayılan (control.html teklif etmez → legacy yol birebir korunur).
- Gelen tarafta `classify` → `classify_value` refaktörü: op-sınıflandırma iki telin ortak noktası.
- **Yanlış çerçeve tipi = PROTOKOL İHLALİ → kapat** (spec MessageDecodeError). Aşama 1'in
  toleranslı handshake'iyle karıştırılmadı: orada "Identify gelmedi" belirsizliği vardı (legacy
  olabilir); burada istemci kodlamayı alt-protokolle ZATEN seçti, sonra kuralını çiğnedi.
  msgpack modunda Text frame ve çözülemeyen binary gövde bağlantıyı kapatır.
- Bağımlılık: `rmp-serde = "1"` (workspace; cargo tree ile doğrulandı — tek örnek 1.3.1,
  serde 1.0.228 ortak, çakışma yok).

**CANLI BULGU (Aşama 6'nın deney-öncelikli yaklaşımı yine işledi):** İlk implementasyonda
op'suz legacy metrik eventi (`{"fps":..}`) msgpack teline de aynen kodlanıp gönderiliyordu.
simpleobsws bağlandı, identify oldu, 3 request çalıştı — sonra recv döngüsü `KeyError('op')`
ile öldü ve kalan tüm request'ler timeout oldu (strict istemci zarf dışı mesajı kaldırmıyor).
**Fix:** msgpack teline YALNIZCA `{op, d}` zarfı olan eventler yazılır; op'suz legacy eventler
bu tele hiç sızmaz (JSON telinde davranış değişmedi — control.html ve obs-websocket-js/json
Aşama 6'da bu akışla canlı doğrulanmıştı). Regresyon testi: `msgpack_modunda_legacy_event_iletilmez`.

**Testler (5 yeni, TDD red→green):** `msgpack_handshake_calisir`,
`msgpack_identify_ve_request_calisir`, `msgpack_modunda_text_frame_reddedilir`,
`msgpack_modunda_legacy_event_iletilmez`, `json_modu_hala_calisir` (regresyon guard'ı).
Aşama 6'nın geçici `subprotocol_msgpack_teklif_edilirse_secilmez_ve_istemci_koparir` testi
KALDIRILDI — doğruladığı davranış (msgpack'in dürüst reddi) bu aşamada bilinçli olarak tersine
çevrildi; yerini msgpack_* testleri aldı. Suite: 32 lib + 5 rules + 23 ws = 60 PASS.

**Gerçek istemci doğrulaması** (gerçek build: `cargo build --release` + `scripts/build.py`,
çalışan reji_app.exe, port 7071 — 7070 yine AnyDesk'te; ham çıktılar `docs/reviews/`):
- `scripts/test_obs_websocket_js.js` — **msgpack varyantı (Node/Companion default) artık PASS**
  (Aşama 6'da "Server sent no subprotocol" ile FAIL'di): Identify OK (5.0.0-reji-compat rpc=1),
  GetSceneList doğru ters sırada. JSON varyantı da PASS (regresyon yok).
- `scripts/test_obs_client.py` (simpleobsws, msgpack-only) — **artık uçtan uca çalışıyor**:
  connect+identify, GetVersion/GetSceneList/GetStreamStatus, SetCurrentProgramScene (100 + 600
  yolları), StartStream→duration ilerliyor→StopStream→sıfırlanıyor, temiz disconnect.
- control.html regresyonu: alt-protokol teklif etmediği için Json varsayılanına düşer; mevcut
  legacy testleri (identify'sız {cmd}, soft-timeout, event akışı) hepsi PASS — davranış birebir.

**Sınır (dürüstlük):** Fiziksel Stream Deck donanımı ve gerçek Bitfocus Companion kurulumu bu
aşamada da test EDİLMEDİ — doğrulama kütüphane seviyesinde (obs-websocket-js 5.0.8, simpleobsws).
ROADMAP'teki 4. checkbox'ın niteliği güncellendi; tam [x] yapılmadı.

Not: script'leri tekrar koşmak için `npm install obs-websocket-js --no-save` (repo'da tutulmuyor)
ve `py -3.12` (simpleobsws 3.12'de kurulu) gerekir.

## Oturum: 5 Temmuz 2026 — Faz 2 Başlangıcı (RTMP öncesi temel)

### Faz 2 — Aşama 1 — ITransport'u gerçek implementasyona bağla (SrtTransport)

Amaç: ROADMAP Faz 2'nin "RtmpTransport" maddesi, ITransport'un çalışan bir soyutlama
olduğunu varsayıyordu — değildi (ITransport::create() implemente edilmemişti,
OutputSubsystem doğrudan somut SrtOutput kullanıyordu). Bu aşama saf taşıma/soyutlama:
davranış değişikliği YOK.

Yapılan:
- i_transport.h: Config'e bandwidth_kbps eklendi (SRT'ye özel, RTMP yok sayabilir);
  ayrıca header hiç include edilmediği için gizli kalan eksik <memory>/<cstddef> eklendi.
- YENİ output/srt_transport.h+.cpp: SrtTransport final : ITransport — kompozisyonla
  SrtOutput'u sarar (miras değil), yalnızca public arayüze delege eder.
- YENİ i_transport.cpp: ITransport::create() faktörü (şimdilik tek implementasyon;
  Aşama 2'de protokol seçimi parametresi eklenecek).
- output_subsystem.h/.cpp: srt_/srt_atomic_ → transport_/transport_atomic_
  (std::unique_ptr<ITransport> + std::atomic<ITransport*>); init artık
  ITransport::create() üzerinden. "Aktif çıkış yok → send true (drop sayılmaz)"
  semantiği bire bir korundu.
- pipeline.cpp: seh_shutdown_subsystems imzası SrtOutput* → rj::ITransport*;
  Config popülasyonunda strncpy_s → std::string ataması.
- CMakeLists (pipeline): srt_transport.cpp + i_transport.cpp SRT if/else'inin
  DIŞINA (koşulsuz, if(WIN32) içine) eklendi — talimattaki "srt_output.cpp ile aynı
  blok" yerleşimi stub build'de ITransport::create() link hatası verirdi; iki dosya
  SDK'dan bağımsız derlendiği için bilinçli sapma.

**SEH virtual-call kararı ve doğrulaması (kritik nokta):**
seh_shutdown_subsystems() ve seh_srt_send() içindeki out->shutdown()/send() çağrıları
artık virtual dispatch — SEH __try içinde MSVC'de yasak değil ama "POD-gibi basitlik"
tasarım niyetinden bilinçli sapma (pipeline.cpp'de yorumla işaretli). Doğrulama,
varsayım değil DENEYLE yapıldı (gerçek Release build, gerçek donanım, reji_app.exe):
1. Normal koşu: init OK (SRT init başarılı — 127.0.0.1 non-blocking connect →
   transport non-null → virtual shutdown gerçekten çalıştı), pencere kapatma →
   "[Pipeline] shutdown clean", exit 0.
2. Throw deneyi: SrtTransport::shutdown() içine GEÇİCİ throw std::runtime_error
   kondu (commit'e girmedi) → aynı senaryoda "[Pipeline] shutdown SEH fault"
   loglandı, çökme/AV YOK, uygulama teardown'ı tamamlayıp exit 0 ile kapandı —
   __except'in virtual call üzerinden fırlayan C++ exception'ını (/EHa) yakaladığı
   KANITLANDI. Throw kaldırılıp temiz shutdown yeniden doğrulandı.
   (Log yakalama: GUI alt sisteminde stderr boş kaldığından OutputDebugString/DBWIN
   dinleyicisiyle alındı — scratch/dbg_listen.ps1, commit dışı.)
RtmpTransport eklenirken kural: her iki implementasyonun shutdown()/send()'i bu
SEH-leaf'lerde exception fırlatmamalı (pipeline.cpp'deki NOT yorumu).

**Karakterizasyon karşılaştırması (izole, Faz1/Aşama4 yöntemi):**
PipelineCharacterization assert etmez, her koşuda snapshot yazar — karşılaştırma
elle yapıldı: git stash ile refactor öncesine dönülüp 2 koşu, refactor sonrası 3 koşu
alındı. Önce/sonra farkları, öncenin KENDİ iki koşusu arasındaki gürültüyle aynı
karakterde (fps ±0.1, bağlantı bayrağı kıpırdaması, 6000→3500 bitrate geçişi her iki
durumda 60-70. frame penceresinde). Yapısal davranış birebir: init=OK, 100 frame,
~60 fps. tests/baseline_metrics.txt commit'te DEĞİŞTİRİLMEDİ (davranış aynı →
referans snapshot güncellenmedi).

**Testler:**
- YENİ tests/test_output_subsystem.cpp (4 test, OutputSubsystemTest): init
  başarısızlığında inaktif kalma, aktif transport yokken send()==true (drop
  sayılmama), set_streaming(true) transport'suz güvenli. Gerçek SRT build'inde de
  stub'da da geçerli (geçersiz host → inet_pton fail → init false). Not: gerçek
  srt_output.cpp rj_* Rust FFI sembollerine başvurduğundan test, integration
  testleriyle aynı link kalıbını kullanır (_RUST_ORCH + ntdll + userenv).
- ctest: 6 test, yalnız bilinen 2 başarısızlık (FrameProfilerTest/ShaderCacheTest —
  refactor öncesi baseline'da da aynen FAIL); yeni kırılma yok.
- Stub-SRT link doğrulaması: vcpkg SRT'si CMAKE_IGNORE_PATH ile gizlenerek geçici
  build dizininde "SRT output: stub module" + test_pipeline_integration.exe linki
  başarılı (ITransport::create + SrtTransport + stub SrtOutput birlikte).

**Bilinen sınırlar:**
- `cmake --preset mock` (reji_app) HEAD'de ZATEN kırık — copy_optimizer.cpp
  vulkan/vulkan.h'ı koşulsuz include ediyor, mock modda find_package(Vulkan) atlanıyor
  (bu oturumda build-mock ilk kez yapılandırıldı ve fark edildi). Bu refactor'dan
  bağımsız, ayrı görev olarak ele alınmalı.
- Talimattaki "mock preset = SRT stub" varsayımı bu makinede geçerli değil (vcpkg'de
  gerçek SRT kurulu); stub doğrulaması yukarıdaki CMAKE_IGNORE_PATH yöntemiyle yapıldı.

## Oturum: 5-6 Temmuz 2026 — Faz 2 Aşama 2 (RtmpTransport)

### Aşama 2.1 — Keşif (5 Temmuz)

- OBS librtmp çekirdeği (yalnız LGPL 2.1 alt dizini, GPL dosyaları alınmadan)
  third_party/librtmp'e vendorlandı (obs-studio commit 30d3b89b).
- Zig 0.16 @cImport + `-DNO_CRYPTO` gerçek derleme/link/çalıştırmayla kanıtlandı
  (RTMP_Init OK, sizeof(RTMP)=17488). rtmp.h NO_CRYPTO tanımlı değilse CRYPTO'yu
  otomatik açıp OpenSSL'e düşüyor (rtmp.h:28) — beklenen SSL_CTX engeli buydu.
- İki OBS'e özel bağımlılık bulundu: happy-eyeballs (MIT ama libobs util'e
  bağımlı → Zig'de yeniden yazıldı) ve <util/platform.h> (stdbool +
  UNUSED_PARAMETER stub'ı yetti). TLS kararı: A (RTMPS'siz) onaylandı.

### Aşama 2.2 — Gerçek implementasyon (5-6 Temmuz, 5 commit)

Mimari: NVENC Annex-B → [Zig: NAL parse → AVCC/FLV mux → librtmp RTMP_Write]
→ rj_rtmp_* C ABI → [C++: RtmpTransport : ITransport] → OutputSubsystem
(create(cfg.protocol) faktörü). Zig lib MinGW ABI hedefiyle derlenir
(ext_bridge kalıbı: MSVC hedefi @cImport'ta winsock çeviriminden geçemiyor).

**Zig panic/ABI sınırı araştırması (checklist maddesi):** Zig'de Rust
catch_unwind karşılığı YOK; panik = süreç abort'u (mesaj + çıkış), C++ tarafına
unwind ETMEZ — yani UB değil ama uygulamayı öldürür. Benimsenen disiplin:
export sınırında panik yolu bırakılmaz — tüm ayırmalar `catch`'li (false/null
dönüşü), indekslemeler açık sınır kontrollü, aritmetik taşmalar `-%`/`*|`
operatörleriyle. SEH-leaf'ten çağrılan rj_rtmp_shutdown exception fırlatamaz
(Faz2/Aşama1 SEH virtual-call notuyla tutarlı).

**MinGW↔MSVC link dersleri:** Zig Debug C derlemesine UBSan ekler →
`-fno-sanitize=undefined` şart (MSVC linkinde __ubsan_* runtime yok);
MinGW obj'lerinin sscanf/_vsnprintf başvuruları `legacy_stdio_definitions.lib`
ile çözülür; `gai_strerrorA` MSVC'de inline/MinGW'de dış sembol → Zig'den weak
export verildi.

**Yerel gerçek ingest testinin bulduğu 2 kök neden (kritik ders):**
1. OBS librtmp URL modeli: RTMP_ParseURL app = path'in TAMAMI (parseurl.c
   "just.. whatever"), playpath AYRI RTMP_AddStream(key) ile. Birleşik URL
   gönderince sunucu app="live/test" görüp reddediyor. Arayüz url + stream_key
   olarak ayrıldı (OBS UI Server/Key ayrımının birebir karşılığı).
2. Yayına encoder çalışırken girilince akışta SPS/PPS/IDR olmuyor (ilk IDR
   t=0'da geçmiş) → muxer tüm kareleri düşürüyor. Çözüm: NVENC repeatSPSPPS=1
   + EncodeSubsystem::request_idr() + start_stream'de taze IDR. SRT geç-katılan
   decoder'lar için de doğru davranış.

**Teşhis kancası (kalıcı):** REJI_RTMP_LOG=<dosya> → librtmp RTMP_LOGDEBUG
çıktısı + muxer'ın ilk 10 send NAL dökümü dosyaya. GUI'de stderr kaybolduğundan
sahadaki tek görünürlük yolu. (Uygulama logları için ayrıca OutputDebugString/
DBWIN dinleyicisi: scratch/dbg_listen.ps1 deseni.)

**Test durumu:**
- zig build rtmp-test: 9/9 (yerel TCP connect + soket devri, NAL/AVCC/Buf).
- ctest: OutputSubsystemTest 7 test (SRT + RTMP simetrik sözleşme) PASS;
  bilinen 2 (FrameProfiler/ShaderCache) dışında kırılma yok.
- YEREL gerçek ingest: reji_app (gerçek NVENC 1080p60) → WS stream_start →
  ffmpeg -listen 1 → 8 sn → ingest_out.flv 827KB, ffprobe: h264 High yuv420p
  1920x1080 duration 8.00s, 341 kare TAM decode, akış SPS+PPS+IDR ile başlıyor.
- Twitch/YouTube gerçek ingest: HENÜZ YAPILMADI (stream key gerekli) — düz
  rtmp:// kabulünün tek kesin kanıtı bu test olacak (dürüstlük notu).

**Bilinçli kapsam sınırları:** yalnız H.264 video (AAC encoder yok → ses yolu
yok; HEVC FLV standardında yok — enhanced-RTMP ayrı iş); onMetaData script
tag'i gönderilmiyor; RTMPS yok (karar A); happy_eyeballs RFC 8305 paralel
yarışı değil sıralı blocking connect (rtmp.c blocking soket istiyor:
SO_RCVTIMEO/SNDTIMEO). ffmpeg dinleyici komutu (yerel test tekrarı için):
`ffmpeg -y -listen 1 -i rtmp://127.0.0.1:1935/live/test -c copy out.flv`.

## Oturum: 6 Temmuz 2026 — Faz2 Aşama 2.2 push-öncesi doğrulama

Push öncesi iki hedefli inceleme; ikisi de riski kabul edilebilir seviyeye indirdi,
6 commit push edildi.

**1) Sevkiyat optimize modu = Debug (ReleaseFast DEĞİL).** `build.zig:26`
`standardOptimizeOption(.{})` preferred'sız → varsayılan Debug; hiçbir script/CI
`-Doptimize=` geçmiyor (`zig build rtmp` düz çağrılıyor). `zig build rtmp-test
--summary all` çıktısı da `compile test Debug x86_64-windows-gnu` + 9/9 pass gösteriyor.
Sonuç: runtime safety (dizi sınır kontrolü, boyut/aritmetik taşma) ZATEN açık →
`@setRuntimeSafety(true)` override'ı gereksiz.

**2) "MPEG-TS muxer'da çift SPS/PPS" riski yapısal olarak yok.** Kodda TS muxer HİÇ
yok. SRT yolu (srt_output.cpp `send_internal` → SrtTransport::send) saf byte-passthrough
(≤1456B, ayrıştırma/mux yok); NVENC Annex-B ES ham gidiyor (repeatSPSPPS=1 tasarım gereği
geç-katılan decoder için). Tek gerçek muxer FLV/RTMP (rtmp_transport.zig): NAL switch'te
SPS(7)/PPS(8) yalnız `updateParamSet`'e gidiyor, `t.body`'ye EKLENMİYOR → in-band kopyalar
frame gövdesinden çıkarılıp sadece 1 kerelik AVCDecoderConfigurationRecord'a giriyor.
Çift işleme yok.

**İzlenecek kalemler (blocker DEĞİL):**
- **[Gelecekte fırsat bulununca] Canlı SRT testi:** reji_app SRT caller → ffmpeg listener
  (`ffmpeg -i "srt://...?mode=listener" -c copy -f h264 out.h264`) → ffprobe/-loglevel
  debug ile IDR başına tam bir SPS/PPS düştüğünü doğrula. Otonom sürülemiyor (etkileşimli
  donanım: DXGI capture + NVENC gerekiyor). Kod-düzeyi kanıt yeterli görüldü, blocker değil.
- **[Perf borcu] RTMP çekirdeği Debug'da sevk ediliyor** → hot muxing path optimize değil.
  Hız gerekince ReleaseSafe safety'yi korur; ReleaseFast'e geçilirse findStartCode/nextNal/
  buildAvcConfig/appendBe32'ye hedefli `@setRuntimeSafety(true)` uygulanmalı.

### Graphify (kod-grafiği aracı) değerlendirmesi — reddedildi

Graphify (kod-grafiği aracı) değerlendirildi — yapısal sorularda
(kim-kimi-implemente-ediyor) isabetli, davranışsal/negatif sorularda (X yapılıyor
mu) güvenilir bulunmadı, grep/find-references'ın üstüne katma değeri
kanıtlanmadı. Skill olarak eklenmedi, deneme kalıntıları temizlendi.

### V8/I4 — CPU fallback transfer() row-pitch düzeltmesi

**Kullanım durumu (Adım 1, kod yazmadan doğrulandı): "aktif ama düşük olasılıklı",
ölü kod DEĞİL.** `use_cpu_fallback_` canlı sıcak yola bağlı:
`DxgiCapturePipeline::capture_next()` → `resource_mgr_->transfer()` →
`gpu_resource_manager.cpp:272` fallback dalı. Bayrak yalnız şu iki koşul birlikte
sağlanınca true oluyor (init satır 253-256): (1) cross-adapter topoloji —
display adapter ≠ NVIDIA encode adapter, LUID'den hesaplanır (satır 229; hardcode
DEĞİL — memory'deki "same_adapter_=true hardcode" notu bu koda uymuyor, başka/eski
bir bağlam), VE (2) `create_cross_adapter_shared()` (NT-handle/keyed-mutex paylaşımı)
BAŞARISIZ. Tek-GPU / NVIDIA yok makinede encode=display → same_adapter → fallback
hiç girilmez. Referans donanımda (AMD 780M + RTX 4070) cross-adapter TRUE, yani
fallback ulaşılabilir ama sadece NT-handle paylaşımı başarısızsa — bir degradation
escape yolu, doğası gereği düşük olasılıklı. Aciliyet düşük ama tetiklenince eski
kod overrun edebildiğinden düzeltmek değerli.

**Düzeltme:** `transfer()` CPU fallback'teki tek-blok
`memcpy(dst, src, mapped.RowPitch * height_)` → satır-pitch güvenli
`reji::copy_mapped_rows` (`src/pipeline/capture/pitch_copy.h`). Satır satır, her
tarafın KENDİ pitch'iyle adresleme + satır başına `std::min(src_pitch, dst_pitch)`
byte. `width*bpp` yerine `min(pitch)` seçildi (talimat tercihi; yeni format→bpp
hesabı riski yok, gerekçe pitch_copy.h yorumunda). Yardımcı bilerek D3D11'den
bağımsız → GPU'suz birim testi mümkün.

**Test:** `tests/test_gpu_resource_pitch.cpp` (GpuResourcePitchTest), 3 sentetik
senaryo: (a) dst_pitch>src_pitch → satır kayması yok, (b) dst_pitch<src_pitch →
guard bölgesi bozulmaz (overrun yok) + piksel verisi korunur + sayısal kanıt
(eski src_pitch*height dst kapasitesini aşardı), (c) eşit pitch → birebir kopya.
Sonuç: **3/3 PASS**.

**Doğrulama:** `cmake --build build` (Release, NMake) temiz (exit 0);
`ctest --test-dir build` → 7 testten 5 PASS, yalnız bilinen 2 FAIL
(FrameProfilerTest, ShaderCacheTest) — yeni kırılma yok. reji_pipeline'a bağlı
testler (PipelineIntegration/Characterization/OutputSubsystem) yeni lib'le relink
edilip geçti.

### V8/I5 — execute_copy() layout state'i yalnızca submit başarılıysa güncelle

**Sorun:** copy_optimizer.cpp `execute_copy()` iki farklı state-güncelleme kalıbı
karıştırıyordu: `target_layouts_[slot]` (~283) ve `staging_layouts_[slot]` (~303)
submit'ten ÖNCE yazılıyordu; oysa `will_signal_gl`/`last_used_slot_`/`frame_counter_`
submit BAŞARISINDAN SONRA. Submit başarısızsa (return false) barrier hiç yürütülmediği
hâlde bu iki dizi SHADER_READ_ONLY_OPTIMAL/UNDEFINED sanıyordu → sonraki frame'in
barrier'ı yanlış `oldLayout` ile kurulur (VUID-VkImageMemoryBarrier-oldLayout riski).

**Düzeltme:** İki `= ...` ATAMASI submit sonrasına taşındı (~389-390); `vkCmdPipelineBarrier`
komut kayıtları (barrier_final ~279, barrier_staging_release ~299) yerinde kaldı.
Submit-fail yolu artık yalnız `timeline_counter_` H17 rollback'ini yapıyor, layout
dizilerine dokunmuyor.

**Doğrulama — STATİK (sentetik test DEĞİL, dürüstlük notu):** `vkQueueSubmit`'i
device-lost simülasyonu olmadan başarısız yaptırmak pratik olmadığından birim testi
yazılmadı. Bunun yerine kod satır satır okunarak doğrulandı: (1) submit-fail dalı
(377-382) yalnız `timeline_counter_ -= FRAME_INCREMENT` + `return false`; layout
state'ine HİÇ yazmıyor. (2) Taşınan diziler 283/303 ile submit (375) arasında hiç
OKUNMUYOR — tek okuma ~226 (barrier kurulumu, önceki frame değeri) → taşıma davranışı
başarılı yolda değiştirmiyor. Ayrıca `cmake --build build` temiz + `ctest` bilinen 2
dışında yeni kırılma yok (PipelineIntegration/Characterization relink edilip geçti).

**Validation layer karşılaştırması:** Gerçek-Vulkan interaktif oturum (reji_app +
VK_LAYER_KHRONOS_validation) gerektirdiğinden ve bu VUID zaten yalnız submit fiilen
başarısız olunca tetiklendiğinden otonom koşulmadı — talimatın kendisi rölatif
karşılaştırmayı yeterli/opsiyonel görüyor. Komut istenirse kullanıcıya verilebilir.

## Oturum: 9 Temmuz 2026 — V8/I27: ITransport `noexcept` Sağlamlaştırma

### Bağlam
06.07.2026 taze taramaları (Fable5 6.1 + Opus 4.8 5.3) bağımsız olarak Faz2/Aşama1'deki
SEH virtual-call kararını (`SrtTransport`/`RtmpTransport::shutdown()`'ın `__try` içinde
çağrılması) eleştirdi: bizim throw-deneyimiz o iki implementasyon için ampirik kanıttı
ama **yapısal garanti değildi** — yeni bir `ITransport` implementasyonu için tekrar elle
test gerekirdi. Garantiyi tip sistemine taşıdık.

### Yapılan
- `ITransport::send`/`shutdown` arayüz imzasına `noexcept` eklendi (`i_transport.h`).
- `SrtTransport` ve `RtmpTransport` implementasyonları `noexcept override` + iç
  `try{...}catch(...)` sarmalayıcı (send → `return false`, shutdown → yut). Böylece
  her yeni implementör exception→bool/void sözleşmesini atlayamaz.
- `init`/`is_connected`'a `noexcept` EKLENMEDİ (talimat opsiyonel bıraktı). **Karar:**
  eklemedik — `init` zaten "sıcak yol" değil (tek sefer, bağlantı kurulumu; ileride
  config doğrulama exception fırlatabilir, sözleşmeyi daraltmayalım) ve `send`/`shutdown`
  gerçek SEH-leaf/hot-path olduğundan sağlamlaştırma değeri orada. Tutarlılık kaybı
  minimal; gerekirse sonra eklenebilir.
- Dış SEH sarmalayıcılar (`pipeline.cpp`/`output_subsystem.cpp`) DOKUNULMADI — Opus'un
  "SEH'i leaf'lere it" önerisi ayrı/daha büyük mimari karar. noexcept + mevcut SEH
  çelişmiyor: noexcept ihlali SEH `__try`'a ulaşmadan `std::terminate`'e gider.

### Throw deneyi — SEH (belirsiz) → terminate (kesin) karşılaştırması
**ÖNCE (Faz2/Aşama1):** `shutdown()` normal (noexcept değil) idi; içine geçici `throw`
konunca exception dış SEH `__except(EXCEPTION_EXECUTE_HANDLER)` tarafından **yakalanıp
yutuluyordu** — "SEH C++ exception'ı gerçekten yakalar mı" davranışı derleyici/`/EHa`
ayarına bağlı, **belirsiz bir garanti**. Her yeni implementasyon için tekrar test şart.

**ŞİMDİ (I27):** `shutdown()` artık `noexcept`. İç `try/catch`'i bypass eden bir `throw`
(yeni implementörün catch'i unutması senaryosu) noexcept sınırından escape edince C++
standardı gereği **kesin olarak `std::terminate()`** çağrılır — SEH `__try`'a hiç
ulaşmaz. Net, öngörülebilir başarısızlık; sessiz UB/yutma yok.

**Nasıl doğrulandı (deterministik):** `SrtTransport::shutdown()`'a geçici `throw` +
`test_output_subsystem.cpp`'ye geçici `ASSERT_DEATH({ SrtTransport t; t.shutdown(); })`
testi konuldu. (1) Derleyici zaten **C4297** verdi ("noexcept belirtildi ama exception
oluşturuyor") — statik kanıt. (2) `ASSERT_DEATH` **PASS** — alt process gerçekten
terminate ile çöktü (runtime kanıt). Sonra **her iki geçici değişiklik geri alındı**,
yeniden derlendi (C4297/C4702 uyarıları kayboldu → temiz), `OutputSubsystemTest` 7/7 +
`GpuResourcePitchTest` PASS; `ctest` bilinen 2 (FrameProfilerTest, ShaderCacheTest —
ilgisiz) dışında yeni kırılma yok.

### Kapsam
Vulkan/GPU/I2-I3 senkronizasyon işine HİÇ dokunulmadı — tamamen `output/` katmanında
izole sağlamlaştırma. FABLE5_BUG_PLAN_V8.md'ye I27 [DÜZELTILDI] satırı eklendi;
`ffi-safety-review` skill'ine "C++ arayüz sınırları noexcept ile sağlamlaştırılmalı"
notu düşüldü.

---

## V8 I2/I3/I28-I31 — Keşif (Alt-Adım A)

**Görev:** kod değişikliği yok; `V8_I2_I3_KESIF_TALIMAT.md` uyarınca gerçek-durum
haritası. Sadece okuma + izleme. Skill fix ayrı commit (`5972724`, henüz push
edilmedi).

### 0. En önemli mimari netleştirme — İKİ AYRI cross-GPU mekanizması var

V8 planı I2/I3/I28-I31'i tek bir "AMD dual-GPU sync" bölgesi gibi ele alıyordu.
Keşif bunun **iki bağımsız yol** olduğunu gösterdi; çoğu I-maddesi bunları
karıştırıyor:

- **A) ENCODE yolu — `GpuResourceManager::transfer()`** (cross-vendor AMD→NVIDIA):
  - `same_adapter_` gerçek LUID karşılaştırması (`gpu_resource_manager.cpp:230`).
    Runtime log: `same_adapter=false (AMD Radeon 780M / NVIDIA RTX 4070)`.
  - `create_cross_adapter_shared()` **çağrılıyor ama BAŞARISIZ**. Runtime kanıtı
    (run.log): `[GpuRM] CreateTexture2D (shared) failed: 0x80070057` (E_INVALIDARG)
    — daha `OpenSharedResource1`'e gelmeden `CreateTexture2D` düşüyor
    (`SHARED|NTHANDLE`). → `create_cpu_fallback_staging()` → **`use_cpu_fallback_=true`**.
  - Referans donanımda GERÇEK encode transfer yolu = **CPU memcpy** (`transfer()`
    satır 273-296, `copy_mapped_rows`). Keyed mutex YOK.
  - `keyed_mutex_display_` / `keyed_mutex_encode_` / `copy_fence_` bu sınıfta
    **%100 ÖLÜ**: hiçbir yerde `QueryInterface`/`CreateQuery` ile doldurulmuyor,
    `transfer()` içinde kullanılmıyor, sadece `shutdown()`'da `Reset()` ediliyor.
    `wait_display_gpu_idle()` de `copy_fence_` hiç oluşturulmadığı için erken
    `return` (no-op) — ama zaten cross-adapter dalına girilmiyor (CPU fallback).

- **B) PREVIEW yolu — `capture_dxgi` `shared_texture_` → Vulkan → GL** (hepsi AMD iGPU):
  - `shared_texture_` `SHARED_NTHANDLE | SHARED_KEYEDMUTEX` ile oluşturuluyor
    (`capture_dxgi.cpp:452`), display-GPU (AMD) yerel; Vulkan (AMD) tarafına import.
  - Cross-vendor paylaşım YOK — bu yüzden çalışıyor. NVIDIA yalnızca NVENC için.
  - I3'ün tarif ettiği `AcquireSync(0)`/`ReleaseSync(1)` kalıbı **burada**:
    `capture_dxgi.cpp:373` (Acquire key 0) / `:381` (Release key 1),
    `use_keyed_mutex_` bayrağıyla korunuyor.

### 1. I30 cevabı — KEYEDMUTEX flag'i `create_cross_adapter_shared`'a eklenirse?

- `GpuResourceManager`'daki `keyed_mutex_display_`/`keyed_mutex_encode_` **tamamen
  ölü** (yukarı bkz). Flag eklemek onları OTOMATİK bağlamaz — ayrıca `transfer()`'a
  `QueryInterface(IDXGIKeyedMutex)` + `AcquireSync`/`ReleaseSync` çağrıları yazmak
  gerekir. Şu an öyle bir çağrı zinciri YOK.
- Flag eklenince bozulur mu? Runtime'da `SHARED|NTHANDLE` ile `CreateTexture2D`
  zaten E_INVALIDARG veriyor (cross-vendor D3D11 paylaşımı bu topolojide
  desteklenmiyor). `KEYEDMUTEX` eklemek bu kök nedeni çözmez; büyük olasılıkla yine
  düşer. Kodda flag kombinasyonunu test eden bir yer YOK — "çalışır" varsayımı da
  yok, aksine yorum (`gpu_resource_manager.cpp:92-98`) desteklenmediğini belgeliyor.
- **Sonuç:** I30'un öncülü ÖLÜ kod yolunu (encode-path GpuResourceManager) hedef
  alıyor. Çalışan keyed mutex zaten preview yolunda ve `SHARED_KEYEDMUTEX` mevcut
  (`capture_dxgi.cpp:452`) + `copy_optimizer` Vulkan tarafı. I30 yeniden
  konumlandırılmalı ya da "ölü üye temizliği" olarak ele alınmalı.

### 2. I2 cevabı — capture_next() AMD path senkronizasyonu

- Referans donanımda encode yolunun `use_cpu_fallback_` değeri **her zaman true**
  (NT paylaşımı başarısız). Yani I2'nin sorduğu "`use_cpu_fallback_==false` iken
  gerçek NT-handle paylaşımı" senaryosu referans donanımda **HİÇ oluşmuyor**.
- `transfer()`'ın cross-adapter dalı (`CopyResource`+`Flush`+`wait_display_gpu_idle`)
  fiilen çalışmıyor; çalışsa bile `wait_display_gpu_idle()` `copy_fence_` yokluğundan
  no-op olurdu.
- I3'ün `AcquireSync(0)`/`ReleaseSync(1)` kalıbı **encode yoluna ait DEĞİL** —
  preview yolundadır (`capture_dxgi.cpp:373/381`, `shared_texture_` +
  `keyed_mutex_shared_`, `use_keyed_mutex_` ile gate).

### 3. I28/I29/I31 aynı çağrı zincirinde

Çağrı zinciri (preview): `pipeline get_last_frame_images()` →
`ext_bridge_get_frame_images()` → `submitD3D11Frame()` → `paintGL` →
`copy_optimizer_->execute_copy(staging_vk, target_vk, …, staging_mem)`.

- **I28** (`copy_optimizer.cpp:133`): `slot = frame_counter_ % POOL_SIZE`
  (POOL_SIZE=3). Staging `oldLayout = VK_IMAGE_LAYOUT_UNDEFINED` (satır 206)
  **KASITLI** — D2/E4 yorumu: D3D11 her frame image'ı dışarıdan yazıyor (keyed
  mutex sahipliği), Vulkan frame'ler arası layout izleyemez; `srcQueueFamilyIndex=
  VK_QUEUE_FAMILY_EXTERNAL` ile acquire. Bu bir HATA DEĞİL, belgelenmiş tasarım.
  → I28 yeniden doğrulanmalı: "gerçek defekt mi, yoksa dokümante tasarım mı?".
- **I29** (`preview_widget.cpp:483` `get_shared_texture_memory()`): keyed mutex
  `km_memory_` = pooled image **slot 0**'ın memory'si (`external_memory_bridge.cpp:78`).
  Blit edilen `staging_vk` ise slot 0/1/2 arasında dönüyor. AMA zig bridge
  (`external_memory_bridge.zig:372-377`) **tek** import edilen `VkImage`+
  `VkDeviceMemory`'yi 3 slota da AYNI değerle kopyalıyor. Yani slot 0 memory ==
  blit edilen image'ın memory'si — keyed mutex doğru kaynağı koruyor. I29'un
  korktuğu uyuşmazlık YOK (tek fiziksel import, 3 slot alias).
  - **Yan bulgu (I29 komşusu, gerçek latent bug):** `invalidate_pool()`
    (`external_memory_bridge.zig:276-285`) 3 slotun her birinde `vkDestroyImage`+
    `vkFreeMemory` çağırıyor; slotlar aynı handle'ı alias ettiğinden texture
    pointer değişiminde (çözünürlük/reinit) **aynı VkImage/VkDeviceMemory 3 kez
    free ediliyor** (üçlü-free / UB). İlk frame güvenli (hepsi null). Dedup guard
    yok. Ayrı, izole düzeltilebilir.
- **I31** format zinciri (uçtan uca, preview):
  1. DXGI Desktop Duplication native: `DXGI_FORMAT_B8G8R8A8_UNORM` (fmt=87, BGRA).
  2. `capture_dxgi` `shared_texture_`: `desc.Format = surface_format()` = BGRA.
     D3D11 `CopyResource` BGRA→BGRA, swizzle yok.
  3. Vulkan import: `dxgi_to_vk_format(BGRA)` = `VK_FORMAT_B8G8R8A8_UNORM`
     (`external_memory_bridge.zig:86`). Swizzle yok.
  4. Vulkan target (gl_target_pool): `VK_FORMAT_B8G8R8A8_UNORM`
     (`gpu_interop_subsystem.cpp:30`). `vkCmdBlitImage` BGRA→BGRA, swizzle yok.
  5. GL interop: `TexStorageMem2D(GL_RGBA8, …)` (`preview_widget.cpp:532`) — BGRA
     bellek RGBA8 olarak yorumlanıyor → sample'da örtük R↔B takası.
  6. Shader: `FragColor = texture(uTex, vUV).bgra;` (`preview_widget.cpp:44`) —
     bu `.bgra` swizzle 5. adımdaki takası düzeltir → doğru RGBA. **Tek swizzle
     noktası = fragment shader.** CPU fallback yolu da aynı mantığa dayanıyor
     (satır 431-432). Encode yolu (GpuResourceManager) ayrıca RGBA'ya zorluyor
     (`gpu_resource_manager.cpp:87-88`) — ama o yol ölü/CPU-fallback, ayrı konu.

- **Keyed mutex anahtar protokolü (her iki taraf tutarlı):** D3D11 `AcquireSync(0)`→
  yaz→`ReleaseSync(1)`; Vulkan `km_acquire_key_=1`, `km_release_key_=0`
  (`copy_optimizer.h:102-103`). 0↔1 ping-pong doğru. Vulkan timeout `UINT32_MAX`
  (sonsuz), D3D11 16ms.

### 4. Her I-maddesi için "hâlâ tarif edildiği gibi mi?" (I4/I5 disiplini)

| Madde | Durum | Not |
|---|---|---|
| I2 | **YANLIŞ KONUMLANMIŞ** | Encode yolu referans HW'de her zaman CPU-fallback; "NT paylaşımı başarılı" senaryosu oluşmuyor. Keyed mutex encode'da yok. |
| I3 | **KISMEN GEÇERLİ, konum değişti** | `AcquireSync/ReleaseSync` var ama preview yolunda (`capture_dxgi.cpp`), `use_keyed_mutex_` ile gate; GpuResourceManager'da değil. |
| I28 | **YENİDEN DOĞRULA** | `oldLayout=UNDEFINED` kasıtlı ve dokümante (D2/E4). Defekt olduğu şüpheli. |
| I29 | **ÇÜRÜTÜLDÜ (asıl haliyle)** | slot 0 memory == blit image memory (tek import, 3 alias). Ama komşuda gerçek üçlü-free bug'ı (`invalidate_pool`). |
| I30 | **ÖLÜ KODU HEDEFLİYOR** | GpuResourceManager keyed mutex üyeleri ölü. Flag eklemek cross-vendor kök nedeni çözmez. |
| I31 | **HARİTALANDI, defekt yok (preview)** | Tek swizzle noktası shader `.bgra`; zincir tutarlı. |

### 5. Önerilen gerçek düzeltme sırası + gruplama

V8'in "hepsi tek oturumda / tek commit" ön-varsayımı **ÇÜRÜDÜ** — maddeler büyük
ölçüde bağımsız ve farklı dosyalara dokunuyor (zig bridge / `gpu_resource_manager.cpp`
/ dokümantasyon). Ayrı commit'ler önerilir:

1. **(Doküman/skill)** Adım 0 skill fix'ini ikinci kez düzelt: "cross-adapter yolu
   devrede" ifadesi yanlış — gerçek encode yolu CPU-fallback. (Aşağıdaki "Adım 0
   re-validation" notuna bak.) — push öncesi.
2. **I29-komşusu üçlü-free** (`external_memory_bridge.zig` `invalidate_pool`): izole,
   düşük riskli, kendi commit'i. Muhtemelen en yüksek gerçek-bug önceliği.
3. **I30 → ölü kod temizliği**: `keyed_mutex_display_/encode_/copy_fence_` üyelerini
   `GpuResourceManager`'dan kaldır (veya gerçekten preview-benzeri keyed mutex
   isteniyorsa ayrı tasarım). Kendi commit'i.
4. **I28**: kod değişikliği muhtemelen YOK — "kasıtlı UNDEFINED" kararını belgelemek
   yeterli olabilir; önce bunun gerçek bir VUID/validation sorunu üretip üretmediği
   validation-layer ile doğrulanmalı.
5. **I2/I3**: preview yolunda `use_keyed_mutex_=false` (AMD'de `VK_KHR_win32_keyed_mutex`
   yoksa) fallback senkronizasyon doğruluğu — ayrı inceleme. Encode yolu (CPU memcpy)
   çalışıyor ama yavaş; ayrı performans konusu.

**Tek commit adayı:** yok. Her biri ayrı. En fazla I30 (ölü üye temizliği) ile I28
(dokümantasyon) belge tarafında birleştirilebilir.

### Adım 0 skill fix — re-validation (ÖNEMLİ)

Talimatın verdiği diff aynen uygulandı (commit `5972724`). Ancak keşif, talimatın
**yeni** metninin de tam doğru olmadığını gösterdi:
- ✓ DOĞRU: eski "`same_adapter_ = true` hardcode" iddiası bayattı; LUID
  karşılaştırması gerçek, `same_adapter=false` (runtime log doğruladı).
- ✗ HÂLÂ YANLIŞ: yeni metindeki "cross-adapter yolu (`create_cross_adapter_shared`)
  **devrede**" ifadesi. `create_cross_adapter_shared()` çağrılıyor ama runtime'da
  `CreateTexture2D` E_INVALIDARG (0x80070057) ile düşüyor → gerçek aktif encode yolu
  **CPU-fallback (memcpy)**. Doğru ifade: "cross-adapter dalı seçiliyor ama referans
  donanımda CreateTexture2D başarısız → `use_cpu_fallback_=true`; keyed mutex/NT
  paylaşımı bu topolojide fiilen devrede DEĞİL."
- **Öneri:** skill push'undan önce metni bu daha doğru haliyle güncelle.

## Oturum: 09 Temmuz 2026 — V8/I32 invalidate_pool() Üçlü-Free Düzeltmesi

### Bağlam
I29 keşfinin yan bulgusu (I32): `external_memory_bridge.zig`'deki 3-slotlu image
pool, tek NT-handle import'undan gelen AYNI fiziksel `VkImage`/`VkDeviceMemory`'yi
üç slota da alias ediyor (`ext_bridge_get_frame_images` satır ~373 tüm slotlara aynı
değeri kopyalıyor). Ama `invalidate_pool()` her slotu bağımsız sanıp üçünde de ayrı
`vkDestroyImage`+`vkFreeMemory` çağırıyordu → çözünürlük değişimi/reinit'te üçlü-free,
UB / potansiyel heap corruption.

### Yapılan Düzeltme
- `invalidate_pool()` (`external_memory_bridge.zig`): image/memory free'si artık
  `image_pool[0]` (kanonik) üzerinden BİR KEZ; ardından üç slot da null'lanıyor.
  Sıra korundu (önce image, sonra memory — E14 dersi).

### D3D11 NT handle araştırması (talimat madde 2) — SONUÇ
`state.d3d11_nt_handles` **alias DEĞİL**, mevcut per-handle `CloseHandle` döngüsü
DOĞRU → **dokunulmadı**. Kod izleme gerekçesi:
- `d3d11_nt_handles[slot]` yalnızca `create_vulkan_image_from_d3d11` (satır ~252),
  çağrı başına TEK slot için doldurulur.
- O fonksiyon yalnızca `ext_bridge_get_frame_images` (satır ~366) `tex` değişince
  çağrılır; her çağrı TEK bir `CreateSharedHandle` (satır ~242) üretir.
- Windows NT handle'ları değer-alias mantığıyla çalışmaz — her `CreateSharedHandle`
  ayrı kernel referansı; ayrı `CloseHandle` şart. Üstelik texture başına yalnızca
  TEK slot dolduğundan (diğerleri invalidate sonrası null) çift-kapatma riski hiç yok.
- Bu, `image_pool`'un durumundan (satır 373-377'de aynı değer 3 slota AÇIKÇA
  kopyalanıyor) yapısal olarak farklı; o yüzden biri tekilleştirildi, diğeri korundu.
- Korele kanıt: `external_memory_bridge.cpp:78-81` `get_shared_texture_memory`
  yorumu — "All pool slots share the same imported D3D11 texture memory; slot 0 is
  canonical." Alias tasarımını doğruluyor.

### Doğrulama
- `zig build ext-bridge-check -Dvulkan-sdk=C:/VulkanSDK/1.4.350.0` → exit 0 (temiz).
- `ctest --test-dir build` → 5/7 geçti; başarısız yalnızca bilinen 2
  (FrameProfilerTest, ShaderCacheTest) — yeni kırılma yok, GPU bridge ile ilgisiz.
- Sınır: `invalidate_pool()` private + canlı `VkDevice` gerektiren static-state
  fonksiyonu; harness'ta çift-çağrı birim testi pratik değil. Runtime çözünürlük
  değişimi senaryosu + validation layer double-free VUID karşılaştırması çift-GPU
  referans donanımda ayrıca yapılmalı (henüz yapılmadı).

## Oturum: 09 Temmuz 2026 — V8/I30 GpuResourceManager Ölü Kod Temizliği

### Bağlam
I30 orijinalde "cross-adapter shared texture'a KEYEDMUTEX flag ekle + keyed_mutex_*
üyelerini transfer()'de kullan" öneriyordu. 09.07 keşfi bunu ÇÜRÜTTÜ: encode yolu
referans donanımda her zaman CPU-fallback (`create_cross_adapter_shared()` →
`CreateTexture2D`/`OpenSharedResource1` E_INVALIDARG, cross-vendor NT-handle paylaşımı
AMD iGPU+NVIDIA dGPU Optimus topolojisinde desteklenmiyor). Flag eklemek kök nedeni
çözmez; `keyed_mutex_display_`/`keyed_mutex_encode_`/`copy_fence_` %100 ölü kod.

### Yapılan (talimat: KEYEDMUTEX flag EKLENMEDİ, ölü kod temizlendi)
- `gpu_resource_manager.h`: `keyed_mutex_display_`, `keyed_mutex_encode_`,
  `copy_fence_` üyeleri + `wait_display_gpu_idle()` bildirimi kaldırıldı.
- `gpu_resource_manager.cpp`: `wait_display_gpu_idle()` tanımı kaldırıldı;
  `shutdown()`'daki üç `Reset()` kaldırıldı; artık orphan olan `#include <intrin.h>`
  (YieldProcessor tek kullanıcıydı) kaldırıldı.
- `transfer()` cross-adapter dalı (satır ~305): KORUNDU (talimat gereği tamamen
  silinmedi — gelecekte cross-adapter çalışır hale getirilirse iskelet lazım), ama
  başına V8/I30 açıklayıcı yorumu + `fprintf(stderr, "[GpuRM] WARNING: cross-adapter
  path reached — see V8/I30 comment, sync missing\n")` eklendi. `wait_display_gpu_idle()`
  çağrısı kaldırıldı → dal artık AÇIKÇA senkronizasyonsuz, uyarı bunu belirtiyor.
- `create_cross_adapter_shared()`'a DOKUNULMADI (init'te hâlâ çağrılıyor, CPU-fallback'e
  düşüren mantığın parçası).

### Kapsam dışı bırakılan (bilinçli)
- `capture_dxgi.cpp` satır ~387/484'teki iki YORUM, silinen `wait_display_gpu_idle()`'ı
  "aynı desen" olarak anıyor — artık dangling referans. capture_dxgi PREVIEW yolu
  (I2/I3, talimat sınırı DIŞINDA) + yalnız yorum (derleme/davranış etkilenmiyor) →
  dokunulmadı, not edildi. `amd_copy_fence_` FARKLI bir üye, kaldırılanla ilgisiz.

### Doğrulama
- `cmake --build build --target reji_pipeline` → temiz (yalnızca önceden var olan
  D9025 /EH ve C4324 hizalama uyarıları, değişiklikle ilgisiz). `reji_pipeline.lib`
  linklendi. Tüm proje `cmake --build build` → %100 built.
- `ctest --test-dir build` → 5/7; başarısız yalnızca bilinen 2 (FrameProfilerTest,
  ShaderCacheTest). Yeni kırılma yok. Davranış değişmedi (ölü kod + uyarı log'u).
- Sınır: runtime `run.log`'da same_adapter=false + E_INVALIDARG + CPU-fallback
  mesajlarının aynı çıktığı çift-GPU donanımda ayrıca doğrulanmalı (statik olarak
  değişmedikleri kesin — o kod yolları değişmedi).

## Oturum: 09 Temmuz 2026 — V8/I1 RuleEngine'i HealingMonitor'a Bağla

### İKİ PARALEL MEKANİZMA netleştirmesi (önemli)
V8'in "self-healing tamamen dead code" ifadesi KISMEN YANLIŞ. İki katman var:
1. `HealingMonitor::evaluate_predictive()`/`evaluate_adaptive()` — sabit-eşikli,
   ZATEN çalışıyordu (HealingEvent → healing_tx → command_queue).
2. `RuleEngine` (kullanıcı `~/.reji/rules.json`, hot-reload, JSON/TOML, sofistike) —
   `evaluate()` hiç çağrılmadığından TAMAMEN ölüydü.
Ölü olan (2), kullanıcı-kural katmanı. Bu düzeltme (1)'e dokunmadan (2)'yi bağladı.

### Yapılan
- `healing.rs`: `HealingMonitor`'a `rule_engine: Arc<Mutex<Option<RuleEngine>>>` alanı
  + `subscribe()` parametresi. `on_periodic()` artık `evaluate_rule_engine()` çağırıyor.
  Yapı ikiye ayrıldı: saf `collect_rule_actions()` (evaluate + ActionType→RjActionType
  dönüşümü → `Vec<RjAction>`, kuyruğa YAZMAZ) + `evaluate_rule_engine()` (listeyi
  `enqueue_action` ile action_queue'ya iter). `convert_action_type()` serbest fonksiyon
  (7 varyant birebir). Poison mutex → warn + skip.
- `ffi.rs`: `rule_engine` Arc'ı HealingMonitor'dan ÖNCE kuruluyor, `subscribe`'a
  `rule_engine.clone()` geçiriliyor (FfiState ile AYNI Arc → hot-reload monitörü de
  günceller). Yinelenen sonraki oluşturma bloğu kaldırıldı.
- `rules.rs`: `RuleEngine`'e `#[derive(Debug)]` (HealingMonitor `#[derive(Debug)]`
  olduğu için yeni alan Debug gerektiriyor).
- `command_router.cpp`: satır ~137 yorumu güncellendi ("enqueue_action kullanılmıyor"
  artık yanlış — I1 sonrası aksiyonlar buradan akıyor; 100ms poll ~1s eval'e göre OK).

### mode_str DÜZELTMESİ (talimat tahmini yanlıştı)
Talimat `"autopilot"/"copilot"/"manual"` tahmin etti — YANLIŞ. Gerçek şablon
(`docs/config/rules.json.template` + `rules_test.rs`) tireli değerler:
`AutoPilot→"auto-pilot"`, `CoPilot→"co-pilot"`, `ManualAssist→"assist"`. `HealingMode`
enum'unda ayrı `Manual` yok (ManualAssist tek varyant).

### canary kararı
`RjAction.canary=0` — `apply_action` (pipeline.cpp:757) canary'yi doğrulamıyor,
sadece action_type'a switch yapıyor; RjAction'ın Rust'ta is_valid()/magic'i yok
(MetricSample'ın aksine). Mevcut kalıp (ffi.rs test) da 0 kullanıyor.

### Test yaklaşımı (paralel-test izolasyonu)
Testler saf `collect_rule_actions()`'ı hedefliyor, global `FFI_STATE`/`action_queue`
üzerinden DEĞİL — çünkü suite paralel koşuyor ve başka testler (`test_panic_safety_
rj_action_dequeue`, `test_null_pointer_safety_all_functions`) aynı kuyruktan pop
ediyor → içerik-assert'i flaky olurdu. 5 yeni test: match / mode-filter / hysteresis /
none-engine / convert_action_type. `enqueue_action` sarmalayıcısı (evaluate_rule_engine)
ince üç satır; doğru aksiyonların üretildiği + enqueue'ya verildiği collect testleriyle
kanıtlandı.

### Doğrulama
- `cargo test -p reji-orchestrator --lib` → **37/37 PASS** (5 yeni dahil).
- `cargo test -p reji-orchestrator --tests` → **23/23 PASS** (entegrasyon, regresyon temiz).
- Build uyarıları: 4 adet, HEPSİ önceden var (constants unused ffi/metrics, default_mode
  never read) — değişikliğim SIFIR yeni uyarı üretti. ABI otomatik doğrulandı
  (build.rs: "ABI OK: RjAction = 20 bytes").

### Kapsam dışı (I33 olarak plana eklendi)
`rj_action_approve()` hâlâ stub (her zaman "1"). AutoPilot etkilenmez; CoPilot onay
akışı ayrı madde (I33, Sprint 2). self.mode ↔ global HEALING_MODE ayrışması (self.mode
subscribe'da set edilip güncellenmiyor; evaluate_adaptive de aynı) mevcut bir durum —
AutoPilot kapsamı için sorun değil, not edildi.

## Oturum: 10 Temmuz 2026 — V8/I28 Validation Layer Test Prosedürü Hazırlığı

**Bu bölüm kod değişikliği İÇERMİYOR** — `copy_optimizer.cpp` D2/E4 barrier'ının
(`oldLayout=UNDEFINED`, external acquire) validation layer ile doğrulanması için
somut test prosedürü + spec-alıntılı ön-değerlendirme. Gerçek çalıştırma çift-GPU
(AMD+NVIDIA) donanımı gerektiriyor; bu hazırlık Claude Code'un otonom kısmı.

### 1. Skill prosedürü güncelliği — DÜZELTME GEREKİYOR (SKILL.md yanıltıcı)
`.claude/skills/vulkan-interop-debug/SKILL.md` satır 49-52'deki
"`set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation`" hâlâ **çalışır** ama eksik/
yanıltıcı. Kod okumasıyla doğrulanan gerçekler:

- **Layer, app tarafından otomatik açılıyor (env var GEREKMEZ) — ama yalnızca Debug
  build'de.** `vulkan_initializer.zig:45-65`: `builtin.mode == .Debug` iken
  `VK_LAYER_KHRONOS_validation` `ppEnabledLayerNames` ile programatik ekleniyor,
  runtime availability kontrolü yapılıyor (yoksa "bulunamadi, atlanıyor" basıp
  geçiyor). Yani **Debug build'de env var'a hiç gerek yok.**
- **Default build RELEASE** (`scripts/build.py:110` → `--config` default=`Release`;
  `just run`/`just build` Release derler). Release build'de `builtin.mode != .Debug`
  → layer app tarafından AÇILMAZ. Bu durumda `VK_INSTANCE_LAYERS=...` (loader-level
  injection) tek yol — SKILL'deki env var burada geçerli. Not: yeni Vulkan SDK
  loader'ında (`1.3.234+`) tercih edilen değişken `VK_LOADER_LAYERS_ENABLE=*validation`;
  `VK_INSTANCE_LAYERS` deprecated ama hâlâ işler.
- **Debug messenger HİÇ yok** (kod tabanında `vkCreateDebugUtilsMessenger`/
  `pfnUserCallback` sıfır eşleşme). `VK_EXT_debug_utils` extension açık
  (`vulkan_initializer.zig:73`) ama `VkDebugUtilsMessengerEXT` oluşturulmuyor →
  VUID mesajları uygulama callback'ine DÜŞMÜYOR; VVL'nin varsayılan çıkışına
  (Windows'ta `OutputDebugString` + sürüme göre stdout) gidiyor.
- **`just run`, uygulamanın stderr'ini run.log'a YÖNLENDİRMİYOR.** `build.py:184-188`
  exe'yi `subprocess.run([exe])` ile yakalamasız çalıştırıyor; run.log'a sadece
  build satırı (`build.py:177`) yazılıyor. SKILL satır 48'deki "`just run` → run.log"
  ifadesi **build log'u** için doğru, **validation çıktısı** için yanıltıcı. GUI'de
  stderr detach olabildiği için VVL yakalamada **DebugView (OutputDebugString) birincil,
  güvenilir yol**; stdout redirect ikincil (VVL sürümüne bağlı).

→ SKILL.md'nin validation bölümü ayrı bir düzeltme talimatıyla güncellenmeli
(Debug-build-otomatik + DebugView birincil). Bu oturumda SKILL'e dokunulmadı (kapsam).

### 2. D2/E4'ün hedeflediği VUID — spec-alıntılı ön-değerlendirme (kesin karar DEĞİL)
Barrier (`copy_optimizer.cpp:204-218`): `oldLayout=UNDEFINED`,
`srcQueueFamilyIndex=VK_QUEUE_FAMILY_EXTERNAL`, `dst=graphics_queue_family_`,
`srcAccessMask=0`, `TOP_OF_PIPE → TRANSFER`. Bu bir **queue-family ownership ACQUIRE**
+ layout geçişi barrier'ı.

- **İlgili VUID: `VUID-VkImageMemoryBarrier-oldLayout-01197`** — *"If srcQueueFamilyIndex
  and dstQueueFamilyIndex define a queue family ownership transfer or oldLayout and
  newLayout define an image layout transition, oldLayout **must be
  VK_IMAGE_LAYOUT_UNDEFINED** or the current layout of the image subresources affected
  by the barrier."* → `UNDEFINED` bu barrier tipinde **spec-legal, açıkça izinli bir
  değer**. Dolayısıyla `oldLayout` seçimi için VVL'nin VUID basması BEKLENMİYOR.
- **Asıl risk VUID değil, SEMANTİK.** Spec, `VK_IMAGE_LAYOUT_UNDEFINED` tanımında:
  *"When transitioning out of this layout, the contents of the memory are **not
  guaranteed to be preserved**."* Bu barrier ise blit'in KAYNAĞI (D3D11'in yazdığı
  pikselleri okuyor). Yani tasarım, imported (LINEAR, external) belleğin no-op geçişte
  içeriğini koruduğu **implementasyona-özgü davranışa** dayanıyor — D2/E4 yorumunun
  "keyed mutex sahipliği aktardı" argümanı bunu varsayıyor.
- **Kritik dürüstlük notu:** Validation layer API-yasallığını denetler, **içerik
  korunumunu DENETLEMEZ.** Bu yüzden "temiz VVL çalışması" tasarımı yalnızca *kullanım*
  ekseninde doğrular, *içerik-korunumu* ekseninde DEĞİL. Yani beklenen sonuç: VUID
  çıkmayacak (01197 gereği), ama bu tek başına D2/E4'ün doğruluğunu kanıtlamaz —
  görsel doğruluk (preview'ın bozuk/garbage olmaması) ayrı bir kanıt ekseni.
- İkincil VUID adayları (ownership transfer eşleşmesi — release/acquire dengesi,
  external queue family index geçerliliği): VVL harici (D3D11) yarıyı göremediği için
  bu sınıfta genelde sessiz kalır; bu da yukarıdaki "temiz≠doğru" notunu pekiştirir.

### 3. I28 Test Prosedürü (kullanıcı çalıştırır — somut komutlar)
> Not: prosedür `C:\reji-studio` kökünden çalıştırılır. Exe yolu build.py'nin
> arama sırasına göre `build\src\ui\reji_app.exe`.

```bat
:: 1) Debug build — layer app tarafından otomatik açılır (env var'a gerek yok)
python scripts\build.py --config Debug --target reji_app
::    (eşdeğeri: `just shield`'in ilk satırı da Debug build yapar)

:: 2) YAKALAMA — iki yol. ⚠️ Düz `just run > dosya 2>&1` VUID YAKALAMAZ:
::    build.py exe'yi yakalamasız çalıştırır, GUI'de stderr detach olabilir.
::    Aşağıdaki A (dosya-log) veya B (DebugView) yolunu kullan.
::
:: 2A) ÖNERİLEN — VVL kendi dosyasına yazsın (stderr'den bağımsız, deterministik;
::     REJI_RTMP_LOG deseninin Vulkan eşdeğeri). C:\reji-studio köküne
::     vk_layer_settings.txt oluştur (loader otomatik bulur):
::       khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
::       khronos_validation.log_filename  = C:\reji-studio\vvl_output.txt
::       khronos_validation.report_flags  = error,warn,perf
::     sonra exe'yi doğrudan çalıştır → VUID'ler vvl_output.txt'e yazılır:
build\src\ui\reji_app.exe

:: 2B) ALTERNATİF — DebugView (canlı izleme, kurulumsuz):
::     Sysinternals DebugView'i Yönetici olarak aç; Capture menüsü:
::       [x] Capture Win32   [x] Capture Global Win32
::     sonra exe'yi doğrudan çalıştır (build.py --run yakalamaz, o yüzden elle):
build\src\ui\reji_app.exe

:: 3) En az 10-15 sn NORMAL çalıştır: preview aktifken 2-3 sahne değişimi yap
::    (VUID'ler genelde ilk birkaç karede veya sahne geçişinde tetiklenir)

:: 4) GÖRSEL DOĞRULUK KONTROLÜ (VUID grep'inden BAĞIMSIZ, ZORUNLU ikinci eksen):
::    AMD ekranındaki preview'a GÖZLE bak — çalışırken şunları NOT ET:
::      [ ] Görüntü DOĞRU mu? (kaynak sahnenin gerçek içeriği görünüyor mu)
::      [ ] Siyah/boş kare VAR mı? (blit garbage okuyorsa → içerik korunmadı)
::      [ ] Bozulma var mı: yarım/yırtık kare (tearing), donma, çöp piksel,
::          yanlış renk (BGRA/RGBA swap)?
::      [ ] Sahne GEÇİŞİNDE ilk kare bozuk gelip düzeliyor mu? (UNDEFINED
::          discard'ının ilk-kare belirtisi olabilir)
::    Şüpheliyse RenderDoc/Nsight ile tek kare capture al (SKILL adım 5).
::    Bu adım VVL'nin GÖREMEDİĞİ içerik-korunumu eksenini kanıtlar — atlanamaz.

:: 5) VUID/barrier hatası ara (2A kullandıysan hedef vvl_output.txt):
findstr /i "VUID VkImageMemoryBarrier oldLayout QueueFamily error" vvl_output.txt
::    (2B/DebugView kullandıysan: DebugView içinde "VUID" ara, ya da
::     File > Save As ile log'u kaydedip yukarıdaki findstr'i uygula)
```

**RELEASE build'de test etmek zorundaysan** (Debug alınamıyorsa) — layer'ı loader'dan
enjekte et. ⚠️ `-DRJ_VALIDATION=ON` ve `RJ_ENABLE_VULKAN_VALIDATION` env var **ÖLÜ**:
`src/pipeline/CMakeLists.txt:159-165`'te tanımlı ama kaynak kodda okunmuyor
(aktif `vulkan_initializer.zig` yalnızca `builtin.mode == .Debug`'a bakar). Release'te
validation açmanın tek yolu loader env var'ı; yakalama için 2A (vk_layer_settings.txt).
```bat
set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation
::   (yeni SDK loader alternatifi: set VK_LOADER_LAYERS_ENABLE=*validation)
::   Yakalama: yukarıdaki 2A vk_layer_settings.txt → vvl_output.txt (en güvenilir)
build\src\ui\reji_app.exe

### Karar matrisi — İKİ eksen birden gerekli (VUID + görsel), tek başına yetmez
I28 ancak **her iki eksen** de temizse tam kapanır. VVL API-yasallığını, görsel
kontrol içerik-korunumunu kanıtlar; biri diğerinin yerine geçmez.

| VUID (adım 5) | Görsel (adım 4) | Karar |
|---|---|---|
| Yok | Temiz (doğru görüntü) | **I28 `[KAPANDI: API-yasal + görsel doğru]`** — kod değişikliği gerekmez |
| Yok | Bozuk (siyah/çöp/ilk-kare) | **KAPATMA.** VUID temiz olması aldatıcı — `UNDEFINED` discard içeriği bozuyor olabilir; VVL bunu göremez. Görsel kanıtı paylaş → içerik-koruyan barrier düzeltmesi (muhtemelen release/acquire çifti) yazılır |
| Var | (her ikisi) | **Tam VUID metnini (kod dahil) paylaş** → gerçek düzeltme talimatı. Çözüm `oldLayout` değiştirmek DEĞİL olabilir; VUID hangi eksende yanlış olduğunu söyler (ör. ownership release/acquire dengesi) |

Kritik: "VUID yok" **tek başına** I28'i kapatmaya YETMEZ — görsel eksen zorunlu.
(Bu proje pattern'i: bayat "temiz=doğru" varsayımı I22/same_adapter_ ile aynı tuzak.)

## Oturum: 10 Temmuz 2026 — V8/I28 Kapanışı (dual-GPU HW doğrulaması)

### Sonuç
I28 (`execute_copy()` acquire barrier `oldLayout=UNDEFINED`) gerçek çift-GPU
donanımda (AMD 780M + RTX 4070) doğrulandı ve **KAPANDI**. Yukarıdaki iki-eksen
karar matrisi uygulandı, her iki eksen de temiz:

- **VUID ekseni (temiz):** `VK_LAYER_KHRONOS_validation` aktif olduğu
  **`vulkaninfo` çıktısı + loader debug çıktısıyla kanıtlandı** (her iki cihaz da
  layer zincirinde görünüyor). Normal çalışma sırasında `vvl_output.txt` **tamamen
  boş** — hiç VUID/error/warn yok. Yani layer gerçekten yükleniyor ama şikâyet
  üretmiyor; "layer sessiz kaldığı için temiz göründü" tuzağı `vulkaninfo` kanıtıyla
  elendi.
- **Görsel eksen (temiz):** normal açılış/preview doğru render ediliyor, görsel
  bozukluk (siyah/çöp/ilk-kare) yok.

D2/E4 yorumundaki kasıtlı tasarım (`oldLayout=UNDEFINED` + keyed mutex external
ownership transfer) böylece doğrulanmış sayılıyor; kod değişikliği gerekmedi.

### Açık kalan minör boşluk (dürüstlük notu)
Sahne geçişi anındaki **slot round-robin reuse** senaryosu ayrıca test edilmedi.
Sürekli/tekrarlayan kopya yolu doğrulandı, ama "farklı içerikli bir sahneden hemen
sonra aynı slotu yeniden kullanma" özel durumu kapsanmadı. Gelecekte fırsat olursa
(`Remove-Item vvl_output.txt` + sahne değişimli koşu) tamamlanabilir — **blocker
değil**.

---

## V8 I2/I3 — Gerçek Durum Tespiti (2026-07-10)

**Yöntem:** Uygulama gerçek dual-GPU donanımda (AMD 780M iGPU + RTX 4070 dGPU)
`VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` ile çalıştırıldı, ~8 sn normal
açılış+preview, nazik kapanış. **Kod değişikliği YOK** — yalnız çalıştır-gözlemle.

**Log yakalama gotcha'sı (önemli):** `-RedirectStandardError` / `2>&1 | Tee-Object`
ile stderr yakalamak **0 bayt** verdi. Sebep: `src/ui/main.cpp:64`
`freopen("C:\\reji-studio\\run.log", "w", stderr)` — app açılışta stderr'i kendi
`run.log`'una yönlendiriyor, dışarıdan verilen redirect handle'ını ezerek. **Tüm
`fprintf(stderr)` teşhisi `C:\reji-studio\run.log`'ta** (dbglog ayrı → OutputDebugString/
DBWIN). Doğru yakalama rotası bu oturumda kesinleşti: `run.log` oku, harici redirect değil.

### Bulgu 1 — `use_keyed_mutex = 1` (TRUE), seçilen Vulkan cihazı **AMD 780M**

`run.log` kanıtı:
```
[VulkanZig] Selected: vendorID=0x1002          ← AMD 780M seçildi (NVIDIA DEĞİL)
[VulkanZig] Device created, keyed_mutex=true    ← use_keyed_mutex = TRUE
[CapabilityDetector] Using Vulkan (vendor: 0x1002)
[PreviewWidget] render path: Vulkan (vendor=0x1002)
```
- **AMD 780M, `VK_KHR_win32_keyed_mutex`'i GERÇEKTEN destekliyor** — hem runtime
  (`keyed_mutex=true`, `check_extension_available` → true) hem bağımsız `vulkaninfo`
  (AMD device-ext bloğunda `VK_KHR_win32_keyed_mutex : extension revision 1`) ile
  çift doğrulandı. NVIDIA RTX 4070 de destekliyor (vulkaninfo, revision 1) — ama app'in
  Vulkan instance'ı bu hibrit konfigde yalnız AMD'yi enumere ediyor (NVIDIA dGPU Vulkan'a
  görünmüyor), o yüzden `select_device` skorlaması (NVIDIA 210 > AMD 100) NVIDIA'yı
  seçmiyor → fiili seçim AMD.
- **NOT (kavram düzeltmesi):** `pipeline.cpp`'deki `[Pipeline] notify_vulkan_ready:
  set_use_keyed_mutex=%d` ve `[Pipeline] VulkanInit: device=...` fprintf'leri `run.log`'ta
  **HİÇ YOK** — çünkü bunlar `if (s.capture_sub_.dxgi())` dalı içinde ve aktif capture
  **WGC** (aşağı bkz.), yani `dxgi()` == null → dal atlanıyor. Vulkan-seviyesi keyed-mutex
  bayrağı (zig) açık; DXGI-seviyesi `set_use_keyed_mutex()` çağrısı çalışmıyor.

### Bulgu 2 — `KeyedMutex timeout/fail` HİÇ görülmedi (ama path da aktif değil)

- `run.log`'ta `KeyedMutex timeout` / `[Capture] ... fail` **0 eşleşme**. Koşu temiz
  (`[Pipeline] shutdown clean`, tüm frame'lerde `drop=0%`).
- **Kritik reframe:** V8'in orijinal I3 senaryosu — `capture_dxgi.cpp:373`
  `keyed_mutex_shared_->AcquireSync(0, 16)`'nın paint araları 16 ms timeout'a girmesi —
  **aktif kod yolunda DEĞİL.** Runtime capture backend'i **WGC (Windows Graphics Capture)**:
  ```
  [WgcCapture] NVIDIA adapter: NVIDIA GeForce RTX 4070 Laptop GPU
  [WgcCapture] Session started  1920x1080
  ```
  `IScreenCapture::create()` (screen_capture_factory.cpp) **WGC-tercihli, DXGI-fallback**;
  WGC başarılı → `capture_` bir `WgcScreenCapture`, `capture_sub_.dxgi()` cast'i null →
  `capture_dxgi.cpp`'nin `AcquireSync` + `[Capture] KeyedMutex timeout/fail` fprintf yolu
  tamamen **ölü**. Yani "timeout görülmedi" = path hiç çalışmadı, yük altında test edilmedi.
- Aktif keyed-mutex Vulkan **copy_optimizer** seviyesinde (`VkWin32KeyedMutexAcquireReleaseInfoKHR`,
  `has_keyed_mutex = vulkan_init->use_keyed_mutex()`); bu seviyede de hata/uyarı loglanmadı.

### Runtime GPU topolojisi (bu koşuda gözlemlenen)
- **Capture (WGC):** NVIDIA RTX 4070 D3D11 (capture_wgc.cpp NVIDIA adapter seçer)
- **Encode (NVENC):** NVIDIA — `[NVENC] Ready 1920x1080@60`
- **Vulkan interop + OpenGL/display:** AMD 780M (`Selected vendorID=0x1002`, `[GL Caps]
  Renderer: AMD Radeon 780M`)
- Yani veri akışı NVIDIA(WGC/D3D11) → AMD(Vulkan) → AMD(GL) — **cross-adapter**;
  eski "DXGI duplication AMD" notu artık WGC-NVIDIA ile değişmiş. `same_adapter=false` teyit.

### Açık kalan (dürüstlük notu)
DXGI backend'i (dolayısıyla `capture_dxgi` keyed-mutex `AcquireSync` yolu) yalnız WGC
başarısız olursa devreye girer; bu makinede WGC hep başarılı. V8 I3'ün "AcquireSync 16 ms
timeout" hipotezi ancak DXGI-fallback zorlanarak (WGC'yi devre dışı bırakıp) test edilebilir
— bu koşuda kapsanmadı, **blocker değil** (aktif yol WGC ve temiz çalışıyor).

---

## Gerçek Runtime Topolojisi — Uçtan Uca Harita (2026-07-10)

Kod değiştirilmeden, gerçek akış uçtan uca izlendi (grep + kaynak okuma + `run.log`).
**Tek capture kaynağı → iki bağımsız dal (encode / preview).** Zero-copy Vulkan interop
bridge (external_memory_bridge) ve `GpuResourceManager::transfer()` bu runtime'da **HİÇ
kullanılmıyor** — ikisi de yalnız DXGI backend'ine bağlı, aktif backend ise WGC.

### 0. Capture kaynağı (WGC, NVIDIA)
`IScreenCapture::create()` (screen_capture_factory.cpp) → `WgcScreenCapture::is_supported()`
true → **WgcScreenCapture**. `init()` (capture_wgc.cpp:40) DXGI adapter tarama ile
**NVIDIA (0x10DE)** bulup o adapter'da `D3D11CreateDevice(BGRA|VIDEO_SUPPORT)` yapıyor.
`next_frame()` → WGC frame pool'dan (`B8G8R8A8`) bir **NVIDIA D3D11 texture** (borrowed,
`CapturedFrame{type=D3D11, handle=NVIDIA tex}`) döndürüyor.
- `CaptureSubsystem::init` (capture_subsystem.cpp:26): `dynamic_cast<DxgiScreenCapture*>`
  **null** → `capture_dxgi_ = nullptr`. Bu yüzden pipeline'daki tüm `capture_sub_.dxgi()`
  dalları (GpuInterop init, shared_texture, encode_gpu, set_use_keyed_mutex, DXGI CPU
  preview) atlanıyor.

### 1. Encode dalı → NVENC (NVIDIA), AMD'ye HİÇ uğramıyor
`run_frame()` (pipeline.cpp:593-600):
```
frame = capture_sub_.next_frame();          // NVIDIA WGC texture
tex    = frame.handle;
encode_sub_.encode_frame(tex, pts_us);       // → NvencEncoder::encode_frame(tex)
```
NVENC, **orijinal NVIDIA WGC texture'ını doğrudan** alıyor (aynı NVIDIA D3D11 device'ta
açılmış encode session — bu yüzden `D3D11_CREATE_DEVICE_VIDEO_SUPPORT`). **Zero-copy,
paralel dal, AMD'den dönen kopya DEĞİL.** (Görevin 3. sorusunun kesin cevabı.)

### 2. Preview dalı → CPU-bounce → AMD GL
`run_frame()` (pipeline.cpp:644): `if (capture_sub_.is_wgc() && preview_cb)` →
`emit_wgc_preview(preview_cb, tex, w, h)` (capture_subsystem.cpp:52):
```
NVIDIA device'ta STAGING texture (USAGE_STAGING, CPU_ACCESS_READ)
CopyResource(staging, tex)        // NVIDIA GPU→staging
Map(READ) → mapped.pData           // CPU pointer (pitch=7680=1920*4 BGRA)
preview_cb(pData, w, h, pitch)     // → uploadCpuFrame → AMD GL texture (display)
```
**CPU-bounce cross-adapter (NVIDIA→CPU→AMD GL), keyed-mutex/Vulkan-interop YOK.**
Display AMD 780M'de (`[GL Caps] Renderer: AMD Radeon 780M`).

### 3. Ölü/kullanılmayan yollar (bu runtime'da)
- **`GpuResourceManager::transfer()`** — yalnız `capture_dxgi.cpp:366`'dan çağrılır →
  WGC'de **çağrılmıyor**. (Görevin 2. sorusunun cevabı: HAYIR.)
- **external_memory_bridge (zero-copy D3D11↔Vulkan)** — `get_external_memory_bridge()`
  WGC'de **null** döner (yalnız DXGI `gpu_sub_.init()` dalında kurulur); `run.log`:
  `bridge=0000000000000000`.
- **AMD Vulkan `GpuCopyOptimizer` (keyed_mutex=true)** — init ediliyor ama **beslenmiyor**:
  `d3d11_frame_cb` (main_window.cpp:142) `get_last_frame_images()` çağırır, WGC'de
  `cache_last_images` hiç çalışmadığı için `got=false` → `submitD3D11Frame` çağrılmaz →
  `copy_optimizer_->execute_copy` hiç koşmaz. Keyed-mutex Vulkan yolu init edilmiş ama
  **inert**.
- **capture_dxgi.cpp `AcquireSync(0,16)`** — DxgiCapturePipeline'a ait, WGC'de erişilmiyor.

### Özet şema
```
                         ┌─────────────────────────────────────────────┐
   WGC (NVIDIA D3D11 tex)│                                             │
   ──────────────────────┤                                             │
      │                  │                                             │
      ├─► ENCODE:  NvencEncoder.encode_frame(tex)  → NVENC (NVIDIA)    │  zero-copy, AMD yok
      │                  │                                             │
      └─► PREVIEW: NVIDIA staging → Map(READ) → CPU ptr                │
                         │            → uploadCpuFrame → AMD 780M GL   │  CPU-bounce
                         └─────────────────────────────────────────────┘
   KULLANILMIYOR: transfer(), external_memory_bridge, AMD Vulkan copy_optimizer keyed-mutex
```

### Eski model ile fark (dikkat — SKILL güncellemesi gerekebilir)
Önceki "DXGI Desktop Duplication AMD → Vulkan zero-copy keyed-mutex" modeli **aktif değil**.
Gerçek aktif topoloji: **WGC-NVIDIA capture, NVENC-NVIDIA encode, CPU-bounce AMD-GL preview**.
`same_adapter=false` hâlâ geçerli ama sebep farklı; keyed-mutex zero-copy zincirinin tamamı
şu an devre dışı (WGC seçildiği sürece). DXGI zero-copy yolu yalnız WGC başarısızsa canlanır.

---

## Oturum: 10 Temmuz 2026 — V8/I6+I7+I12+I13 (GL/Vulkan shutdown & frame sırası)

### Bağlam
Aynı "GL/Vulkan shutdown ve frame sırası" bölgesindeki dört madde birlikte ele
alındı. Talimat iki aşamalı: önce doğrulama, sonra gerçekten boşluk olan yerlere
düzeltme. Sonuç: **2 doğrulama (I7, I13 — kod değişmedi), 2 düzeltme (I6, I12).**

### I7 — DOĞRULANDI (zaten çözülmüş, yinelenen madde)
`wasapi_capture.cpp::shutdown()` içindeki **D16 (V3 Sprint 3)** UAF sorununu zaten
kapatmış. Koddaki sıra `clear_owner()` ÖNCE → `Unregister` SONRA — V8/I7'nin
önerdiği "Unregister önce"nin tersi, ama **farklı ve geçerli** bir strateji:
`DeviceNotifyClient`'in tüm callback'leri (`OnDefaultDeviceChanged`,
`OnDeviceRemoved`, `OnDeviceStateChanged`) başlarken `owner_`'ı atomic yükleyip
`nullptr` ise erken dönüyor; `OnDeviceAdded`/`OnPropertyValueChanged` owner_'a hiç
dokunmuyor. clear_owner() sonrası callback tetiklense bile dangling pointer
dereference edilmez — `Unregister`'ın bloklayıcı olmasına bağımlı değil. Gerçek
boşluk yok. **Kod değişmedi.**

### I6 — DÜZELTILDI (alive_ atomic flag)
`is_copy_ready()` SEH ile sarılı; SEH stale handle'a erişimde access violation'ı
yakalar AMA `device_` "serbest bırakılmış ama geçerli görünen" belleğe işaret
ederse driver çağrısı **sessiz UB**'dir (SEH göremez). Ayrıca `if(!device_)`
kontrolü ile `pfn_wait_semaphores_(device_,...)` arasında TOCTOU var. Çözüm:
`std::atomic<bool> alive_{true}` üyesi. `shutdown()` en başında `false`,
`is_copy_ready()` en başında kontrol, `init()` yeniden `true` (re-init desteği).
Handle durumundan bağımsız, ucuz erken çıkış — I12 ile birlikte savunma katmanı.

### I12 — DÜZELTILDI (referans koparma)
`~MainWindow()`: `stopFrameThread()` SONRASI, `copy_optimizer_.shutdown()` ÖNCESİ
`preview_widget_->setCopyOptimizer(nullptr)` + `setBridge(nullptr)` eklendi.
Setter'lar borrowed raw pointer tutuyor. paintGL GUI thread'inde çalışır; referans
aynı thread'de (dtor) koparıldıktan sonra torn-down optimizer'a çağrı yapılamaz.
(Not: Fable'in taslağındaki `pipeline_->shutdown()` satırı bu projede yok — pipeline
ayrı sahiplenilmiyor.)

### I13 — DOĞRULANDI (zaten gate'li)
`current_pool_idx_ = copy_optimizer_->last_used_slot()` (preview_widget.cpp:476-477);
`last_used_slot_` sadece **başarılı submit'ten SONRA** güncellenir
(copy_optimizer.cpp:394). Yani render, o anki `execute_copy`'nin yazdığı yeni slot'u
değil bir önceki paintGL'de submit edilmiş slot'u örnekler → tam bir frame önce
gönderilmiş, tamamlanmış/tamamlanmakta olan slot. Üstelik çizimden önce (satır
584-590) o slot'un GL sync semaphore'unda `glWaitSemaphoreEXT` ile GPU-tarafı sert
bekleme yapılır. İlk-kullanım/slot-değişiminde bile tamamlanmamış blit örneklenmez.
`is_copy_ready()` poll'u yalnızca pacing içindir; asıl gate GL semaphore beklemesi.
**Kod değişmedi.**

### Test & Doğrulama
- **Build:** `py scripts/build.py --target reji_app --config Release` → OK (47.7s),
  sadece önceden var olan uyarılar.
- **ctest:** 5/7 geçti; başarısız 2 (`FrameProfilerTest`, `ShaderCacheTest`) bilinen
  baseline — timing/hash testleri, shutdown koduyla ilgisiz. Yeni kırılma yok.
- **Manuel GUI shutdown testi:** app 8s çalıştı, graceful WM_CLOSE (`taskkill` /F'siz)
  → exit 0. run.log sırası doğrulandı: `setCopyOptimizer(0x0)` → `setBridge(0x0)` →
  `[GpuCopyOptimizer] Shutdown complete` → `Pipeline shutdown clean`. run.log'da
  SEH / access violation / VUID / ERROR / timeout **yok**.
- **Açık minör:** `alive_` flag'inin race'i gerçekten kapattığını kanıtlayan
  throw/assert enjeksiyon deneyi (I27 tarzı) yapılmadı — flag mantığı trivial
  (`if(!alive_) return false`) ve temiz shutdown regresyon göstermedi; blocker değil.

### Değişen dosyalar
- `src/pipeline/copy_optimizer.h` — `alive_` üyesi
- `src/pipeline/copy_optimizer.cpp` — init/shutdown/is_copy_ready'de alive_
- `src/ui/main_window.cpp` — ~MainWindow'da referans koparma
- `docs/FABLE5_BUG_PLAN_V8.md` — I6/I7/I12/I13 işaretlendi

## Oturum: 11 Temmuz 2026 — V8/I19+I20 (Healing Mode semantiği) + I11 araştırması

### Bağlam
V8 I11+I19+I20 talimatı (`docs/V8_I11_I19_I20_TALIMAT.md`) uygulandı — hepsi
`healing.rs` bölgesinde, I1'in tazeliğinde. Talimat yazılırken bulunan kritik
ek bulgu: I1'de eklenen `evaluate_rule_engine()` I20'nin bug'ını miras almış
(donmuş `self.mode` okuyordu) — bu da düzeltildi.

### KULLANICIYA GÖRÜNEN DAVRANIŞ DEĞİŞİKLİĞİ (bugfix, gözlemlenebilir)
Önceden Rust `HealingMode` enum'u yalnız 3 varyanttı ve `ManualAssist` hem
Assist hem Manual'ı temsil ediyordu. Sonuç: **Manual ("tüm adaptasyon kapalı")
seçen kullanıcı sessizce Assist davranışı alıyordu** — kritik aksiyonlar hâlâ
otomatik uygulanıyordu. Artık:
- **Manual** gerçekten hiçbir otomatik aksiyon üretmiyor (RuleEngine hiç
  çağrılmıyor; reactive/periodic kanallar bastırılıyor).
- **Assist** yalnız `is_critical` aksiyonları (bitrate_reduce/scale_resolution)
  otomatik uyguluyor, diğerlerini logluyor — UI'nin verdiği söz artık kod ile
  tutuyor.

### Tamamlanan Düzeltmeler (I19 + I20)
- `HealingMode` enum'u 3 → 4 varyant (`AutoPilot`/`CoPilot`/`Assist`/`Manual`),
  `ManualAssist` kaldırıldı. Sıra FFI `HEALING_MODE` u32 (0..=3) ile birebir.
- `HealingMode::from_raw(u32)` + `HealingMode::current()` — tek dönüşüm noktası.
- Donmuş `self.mode: AtomicCell<HealingMode>` alanı ve `subscribe()`'ın `mode`
  parametresi tamamen KALDIRILDI (ölü-state bırakılmadı; I30 dersi).
- `evaluate_adaptive` + `evaluate_rule_engine`/`collect_rule_actions` artık canlı
  global okuyor. Mimari: mod `run()` ticker'ında periyot başına bir kez okunup
  `on_periodic(mode)` üzerinden aşağı geçiriliyor (DI) — periyot-içi tutarlı
  görünüm + testler global'e dokunmadan deterministik (paralel-test yarışı yok).
  Bu, talimatın "her fonksiyon global okusun" ifadesinden bilinçli sapma:
  fonksiyonel olarak aynı (tek periyot içinde tek okuma), test edilebilirlik
  daha iyi.
- `run`/`handle_system`/`handle_media`'daki magic `== 3` → `== HealingMode::Manual`.
- `collect_rule_actions` mode_str eşlemesi 4 varyanta; Manual erken-return (boş),
  Assist is_critical filtresi.
- ffi.rs `rj_set_healing_mode`/`rj_get_healing_mode` doc yorumları güncellendi
  (2=Assist, 3=Manual). ABI DEĞİŞMEDİ — enum Rust-içi, FFI'yi u32 geçiyor;
  extern imza/struct/cbindgen çıktısı dokunulmadı, abi-check gereksiz.

### "Kritik aksiyon" tanımı (netleştirildi)
`rules.rs::create_action` zaten `is_critical = (action == bitrate_reduce ||
action == scale_resolution)` hesaplıyor. Assist filtresi bunu kullanıyor.
Şablondaki (`rules.json.template`) 3 "assist" kuralının hepsi zaten kritik;
filtre defansif (kullanıcı non-kritik assist kuralı eklerse UI sözü tutar).
Not: `evaluate_adaptive` tekil arka-plan kalibrasyonudur, per-aksiyon is_critical
ayrımı yoktur — AutoPilot+Assist'te çalışır, granüler filtre yalnız rule-engine
yolunda anlamlı.

### C++ Wiring Boşluğu — DÜZELTİLDİ (kullanıcı onayıyla, aynı iş kapsamında)
Rust fix'i uygularken bulunan kritik bulgu: `rj_set_healing_mode` üretimde hiç
çağrılmıyordu → UI mod seçimi Rust'a hiç ulaşmıyordu, enum fix'i tek başına
uçtan uca etkisizdi. Kullanıcı onayıyla düzeltildi: `main_window.cpp`'deki iki
`healingModeChanged` handler'ına (ctor ~76 + onSettingsClicked ~528 guard)
`rj_set_healing_mode(static_cast<uint32_t>(mode))` eklendi. `reji::HealingMode`
(healing_overlay.h, tek enum) 0..=3 Rust `from_raw` ile birebir; sinyal
`onOkClicked`'te emit. `reji_app` derlendi+linklendi (sembol çözüldü, yalnız
pre-existing uyarılar).

**Startup senkronu EKLENDİ (kullanıcı onayıyla) — DAVRANIŞ DEĞİŞİKLİĞİ (gözlemlenebilir):**
`MainWindow` ctor'unda healing_overlay_/settings_dialog_ kurulduktan sonra bir kez
`rj_set_healing_mode(static_cast<uint32_t>(settings_dialog_->healingMode()))`
çağrılıyor. `healingModeChanged` yalnız OK'e basınca emit edildiğinden, bu olmadan
startup'ta HEALING_MODE=0 (AutoPilot) kalırken UI combo default'u CoPilot
gösteriyordu (ayrışık). Artık **varsayılan başlangıç modu AutoPilot yerine CoPilot**
— UI ile tutarlı, ama önceki üstü kapalı AutoPilot davranışından farklı: startup'ta
self-healing aksiyonları artık otomatik uygulanmıyor, kullanıcı onayına sunuluyor
(CoPilot semantiği). Küçük ama kullanıcı gözlemleyebilir; dürüstlük gereği açıkça
not edildi. Manuel GUI davranış testi (Manual seç+OK → `rj_get_healing_mode()==3`)
kullanıcının elinde (interaktif combo).

### I11 — ARAŞTIRILDI, KARAR BEKLİYOR (kod değişmedi)
Race doğrulandı ama planın "C++ thread'i kaldır" varsayımı yanlış: iki tüketici
FARKLI gerçek amaçlara sahip (aynı kuyruktan POP ediyorlar):
- `command_router::action_thread_main` → `apply_action` = encoder AKTUATÖRÜ.
- `main_window::pollHealingActions` → `HealingOverlay` = onay/UI GÖRÜNÜMÜ.
Her aksiyon rastgele birine gidiyor. İkisi de gerekli → talimatın "birlikte
karar verelim" maddesi gereği kod değiştirilmedi. Önerilen fix: tek tüketici POP
edip UI'ya fan-out (Qt queued signal), veya enqueue'da iki kuyruk. Derin akış
(CoPilot onay sırası) I33 `rj_action_approve` stub'ına bağlı — kapsam dışı.
Ayrıntı: FABLE5_BUG_PLAN_V8.md I11.

### Test & Doğrulama
- `cargo test -p reji-orchestrator`: 40 (lib) + 5 + 23 + 0 = tümü PASS. Yeni
  testler: `test_healing_mode_from_raw_round_trip`, `test_assist_runs_only_critical_actions`,
  `test_manual_mode_evaluates_no_rules`.
- Yeni uyarı yok (mevcut `crate::constants` / `default_mode` uyarıları temiz
  ağaçta da var, git stash ile doğrulandı).
- Manuel GUI testi (Assist/Manual modda canlı davranış farkı, CoPilot aksiyonun
  HealingOverlay'de görünmesi) YAPILMADI — I11 UI-taraflı akış çözülene kadar
  anlamlı değil; runtime doğrulaması I11 kararından sonraya.

### Değişen dosyalar
- `src/orchestrator/src/healing.rs` — enum 4 varyant, from_raw/current, self.mode
  kaldırıldı, mode DI, Assist/Manual semantiği, 3 yeni test
- `src/orchestrator/src/ffi.rs` — subscribe çağrısı (mode arg kaldırıldı), unused
  import temizliği, mode doc yorumları
- `docs/FABLE5_BUG_PLAN_V8.md` — I19/I20 DÜZELTILDI, I11 araştırma bulgusu

---

## Oturum: 11 Temmuz 2026 — V8/I33 + I11 (CoPilot onay kapısı, 7 commit'lik seri)

### Özet
I33 (rj_action_approve stub) + I11 (çift-consumer race) tek pakette çözüldü.
**Faz 0 keşfi kapsamı büyüttü** (I19 deseninin tekrarı): sorun yalnız FFI stub'ı
değildi — aktüatör (`action_thread_main`) POP'ta HER aksiyonu moddan/onaydan
bağımsız uyguluyordu; UI onayı (`rj_action_approve`) no-op'tu → CoPilot onayı
uçtan uca hayaliydi. Faz 0/1 (keşif + tasarım) kullanıcı onayından geçti, sonra
7 commit'lik seri.

### Kararlar (kullanıcı onaylı)
- **Mimari:** iki ayrı kuyruk (tek-tüketici+fan-out DEĞİL) — aktüatör + UI event.
- **ID:** u32-global (u64 önerisi RjAction 20B ABI çatallaşmasını doğuracağı için
  reddedildi; wrap gerçekçi hızda ~13 yıl).
- **Cooldown:** RuleEngine'de (hysteresis ailesi), 120s sabit.
- **Auto-onay:** kapı UI-yerel değil MOTORDA; UI startup senkronu (I19 deseni).
- **Reject UX:** explicit "Reddet" butonu → cooldown; 30s timeout → yalnız
  geçersizleştirme (cooldown yok). reject≠timeout.

### 7 commit (df1c163..b20608f, master'a HENÜZ push edilmedi)
1. `df1c163` — global monoton `next_action_id` (tick-yerel ID kaldırıldı) + `Action.rule_id`.
2. `af42cdb` — iki-kuyruk: `RjActionEvent` (24B) + `ui_event_queue` + `rj_action_event_dequeue` (I11).
3. `6133dae` — pending deposu (`pending_actions`, TTL 30s) + `rj_action_approve` gerçek + `rj_action_reject` + mod-değişimi/TTL süpürme→Invalidated.
4. `8e82ed4` — per-kategori auto-onay (`ACTION_AUTO_APPROVE` + `rj_set_action_auto_approve`) + producer routing (`requires_approval`) + `RoutedAction`.
5. `a58d428` — reject cooldown (`apply_cooldown` + `cooldown_until`, `REJECT_COOLDOWN=120s`).
6. `b20608f` — C++ wiring: `pollHealingActions`→event-queue, "Reddet" butonu (`actionRejected`→`rj_action_reject`), auto-onay startup senkronu, `onActionEvent` require_approval'a güvenir, `onActionInvalidated`.
7. docs (bu commit) — V8 plan I33/I11/I34, SESSION_NOTES, ffi-safety-review, CONTEXT, talimat arşivi.

### Doğrulama
- Rust: 50 lib + 5 rules + 23 ws = tümü PASS (10 yeni test: next_action_id,
  action_event roundtrip, pending approve/reject/sweep/mode-clear, auto_approve,
  requires_approval, reject cooldown).
- ABI: `RjActionEvent` static_assert (C++ sizeof_check.cpp) + zig comptime PASS;
  `sizeof_check.exe`=24B; cbindgen üretti. (check-abi.ps1 wrapper'ı I22'nin bayat
  RjMetricSample 56/64 beklentisinde ÖNCEDEN patlıyor — bu seriden bağımsız.)
- C++: `reji_app` derlendi + LİNKLENDİ (rj_action_event_dequeue/reject/
  set_action_auto_approve sembolleri çözüldü → wiring uçtan uca gerçek, I19 dersi).
- ctest: yalnız bilinen 2 kırık (FrameProfilerTest, ShaderCacheTest) — yeni regresyon yok.

### Kullanıcıya görünen davranış değişiklikleri
- CoPilot manuel-kategori aksiyonları artık gerçekten onaya kadar bekler.
- Explicit reddetme kuralı 120s bastırır (spam yok); timeout bastırmaz.
- Per-kategori auto-onay artık motorda uygulanır (eski UI-yerel + aktüatör yarışı yok).
- TTL(30s) dolan pending UI'dan düşer.

### KULLANICIDA (otonom yapılmadı) + gotcha
- **GUI davranış onayı:** CoPilot'ta gerçek eşik aşımı → pending görünür → onayla
  (uygulanır) / reddet (uygulanmaz + 120s bastırılır) / timeout (geçersizleşir,
  cooldown yok). Kod incelemesi + otomatik test + build/link ile doğrulandı;
  görsel/etkileşim onayı bekliyor.
- **Gotcha (ffi-safety-review skill'ine işlendi):** yeni FFI fonksiyonu eklerken
  C++ Release build'inden ÖNCE `cargo build --release` şart — aksi halde
  `target/release/reji_orchestrator.lib` bayat kalır, LINK `unresolved external
  rj_*`'da patlar (ffi_auto.h bildirimi görünse bile).
- **Açık minör:** `chk_source_auto` inert (I34, Sprint 4).

---

## Oturum: 11 Temmuz 2026 — V8/I8 WS Auth (7 commit'lik seri)

### Özet
obs-websocket kimlik doğrulaması. **Faz 0 iki kritik bulguyla kapsamı yeniden
çerçeveledi** (üçüncü kez): (1) I8 açığı obs handshake'inde DEĞİL, **legacy `{cmd}`
yolunda** (Identify'ı baypas ediyor) → yalnız obs-auth eklemek açığı kapatmazdı;
(2) **control.html bir obs istemcisi değil** (legacy `{cmd}`) → naif auth onu
kilitlerdi; (3) **Origin kontrolü çürütüldü** (saldırgan sekme = control.html,
aynı origin). Eski "token+Origin" tanımı reddedildi. Faz 0/1 onaydan geçti.

### Kararlar (kullanıcı onaylı)
- **Oturum-düzeyi auth (tek bayrak), yol-düzeyi değil:** doğrulanmış oturumda hem
  obs Request hem legacy `{cmd}` çalışır; doğrulanmamışta ikisi de reddedilir.
- **control.html:** paralel token İCAT ETME → obs-auth'a minimal yükselt.
- **Parola kaynağı:** FFI setter + SettingsDialog (env/config değil), startup senkronu.
- **Close-code altyapısı** ayrı/erken commit.
- **crypto.subtle** (Faz 0: bind 127.0.0.1 → localhost secure context → çalışır,
  saf-JS sha256 gereksiz).

### 7 commit (b00116d..da843fd, master'a HENÜZ push edilmedi)
1. `b00116d` — close-code altyapısı (`close_with` + obs `close_code` 4007/4009 + test).
2. `f99b240` — auth çekirdeği (saf): `compute_auth`/`verify_auth` (subtle sabit-zamanlı)/
   `gen_salt_challenge` (getrandom)/`hello_with_auth`; deps sha2/base64/getrandom/subtle.
3. `613f22a` — handshake entegrasyonu: `WsState.password` (RwLock, per-bağlantı
   snapshot) + `Session` + Identify doğrulama + 4009; parolasız bit-aynı; JSON+msgpack.
4. `2daeac5` — doğrulanmamış istek reddi (4007), legacy `{cmd}` açığı kapandı (tek bayrak).
5. `dc97b8c` — parola wiring: `rj_set_ws_password` FFI + SettingsDialog alanı +
   `syncWsPasswordToRust` startup senkronu (find-references).
6. `da843fd` — control.html minimal obs-auth handshake (crypto.subtle, 4009 retry).
7. docs (bu commit) — V8 I8 yeniden yazımı, SESSION_NOTES, obs-ws-protocol skill,
   CONTEXT, ROADMAP, talimat arşivi.

### Doğrulama
- Rust: 54 lib + 5 rules + 30 ws = tümü PASS (11 yeni test: close-code, auth çekirdeği
  ×3, handshake ×4, 4007-gate ×3). Client-side auth entegrasyon testinde BAĞIMSIZ
  hesaplanır (sha2/base64 dev-dep) — spec-exactness Faz 3 gerçek istemcide.
- C++: `reji_app` derlendi + LİNKLENDİ (rj_set_ws_password çözüldü). `cargo build
  --release` ÖNCE (I33 gotcha'sı uygulandı).
- Startup senkronu find-references ile uçtan uca: `syncWsPasswordToRust` 3 yerde
  (ctor/startup/onSettingsClicked); onOkClicked healingModeChanged'i koşulsuz emit
  ediyor → parola-only değişim bile Rust'a ulaşır (I19-boşluğu yok).

### Kullanıcıya görünen davranış değişiklikleri
- Parola ayarlanınca kimlik doğrulamayan eski toleranslı istemciler REDDEDİLİR
  (control.html yükseltmesi bu yüzden şart).
- **Parola yalnız YENİ bağlantılara uygulanır; tam etki için mevcut istemciler
  yeniden bağlanmalı** (parolasızken açılmış oturumlar sürer — OBS-tutarlı).
- Parolasızken davranış BİT-AYNI (23 mevcut ws testi regresyonsuz).

### KULLANICIDA (otonom yapılmadı)
- **Tarayıcı akışı:** control.html parolalı (prompt→bağlan / yanlış→4009 hata+retry)
  ve parolasız. Kod incelemesi + otomatik test + build/link ile doğrulandı; tarayıcı
  etkileşim onayı bekliyor.
- **Gerçek istemci kütüphaneleri (Aşama 6 tarzı):** obs-websocket-js/simpleobsws ile
  parolalı bağlantı + msgpack+parola (opsiyonel ek kanıt; otomatik ws testleri
  handshake'i zaten kapsıyor).

### Spec sapmaları (belgelendi) + kapsam
- İkinci Identify → yoksay+log (4008 değil); parola ayarlıyken de 5s soft-timeout
  yalnız log. Kapsam dışı: rate-limit/brute-force (V8'e yeni madde), TLS/wss.
- Açık: bind 0.0.0.0'a açılırsa control.html'e saf-JS sha256 gerekir (yorumda uyarı).

## Oturum: 11 Temmuz 2026 — V8/I9 + I10 (COM yaşam döngüsü + SEH filtreleri)

### Özet
İki Windows-özel hata/kaynak yönetimi maddesi. **Faz 0 iki bulguyla kapsamı
düzeltti:** (1) I9 planı "2 yer" dedi ama ÜRETIMDE TEK guard'sız konum var
(command_router); pipeline/srt/wasapi zaten doğru pattern'deydi. (2) I10 planının
step-3'ü (AV'yi CONTINUE_SEARCH ile çökert) projenin self-healing öncülüyle
çelişti → kullanıcı onayıyla plan geçersiz kılındı (I2/I3/I8 deseni).

### I9 (commit cdb9dcb)
`command_router.cpp::action_thread_main()` CoUninitialize'i koşulsuz çağırıyordu;
RPC_E_CHANGED_MODE'da apartment ref-count'u dengesizleşirdi. `const bool com_ok =
SUCCEEDED(hr); if (com_ok) CoUninitialize();`. Tek konum → RAII gerekmedi.
tools/dxgi_test.cpp (I9'un TERSİ deseni, süreç çıkışı geri alır) dokunulmadı.

### I10 (4 commit — kullanıcı onaylı 3 katman)
- `9658f51` **I10-a pass-through:** paylaşımlı `seh_filter.{h,cpp}`; 21 leaf
  (Grup A çıplak EXECUTE_HANDLER + Grup B wasapi/srt kopya filtreler) tek
  `rj::seh_filter`'a. SO/BP/SS artık CONTINUE_SEARCH (Grup A'da eksikti —
  stack overflow guard-page restore edilmeden yutuluyordu). AV davranışı bu
  commit'te değişmez.
- `9cb35fe` **I10-b görünürlük:** yutulan her istisna ERROR seviyesinde SENKRON
  loglanır (site+code+adres). copy_optimizer'ın ad-hoc SEH log'u da buna taşındı.
- `9d4fb22` **I10-c eskalasyon valfi:** same-site 60s'de ≥3 AV → FATAL log +
  `__fastfail`. Sayaç kilitsiz/tahsissiz (sabit atomik dizi + GetTickCount64) —
  AV filtresinde kilit(deadlock)/heap(bozulma) tehlikeli olduğundan bilinçli.
- `36036b9` **test:** seh_is_passthrough + seh_register_av saf mantık, header-only
  gtest (sentetik now_ms) — 7/7 PASS, gerçek fault tetiklenmedi.

### Kararlar (kullanıcı onaylı)
- **Eskalasyon = tüm sitelerde __fastfail (Seçenek A).** Seçenek B (network/audio'da
  önce rj_connection_lost) reddedildi: eşik zaten "geçici değil, bozulma" kararını
  verdikten sonra reconnect denemek fault→reconnect→fault döngüsünü yeniden açar.
- **Valf tüm leaf sitelerine uniform** (Grup A + B); pass-through zaten doğru olan
  yerde tekrar yazılmadı.
- **recovery_coordinator/rj_connection_lost eskalasyon hedefi DEĞİL:** ilki
  capture/encode ref ister + cihaz-kaybı (temiz HRESULT) semantiği; ikincisi
  reconnect tetikler (bozulmayı iyileştirmez). `__fastfail` OS-standart primitif.
- **__fastfail öncesi log SENKRON** (fflush + OutputDebugStringA) — unwind yok,
  satır çağrı anında gitmeli.

### Doğrulama
- reji_pipeline + reji_app derlendi/linklendi (her commit).
- ctest: PipelineCharacterization PASS (baseline = davranış korundu),
  OutputSubsystem/PipelineIntegration/GpuResourcePitch/**SehFilter** PASS.
  **Bilinen 2 kırık** (FrameProfilerTest, ShaderCacheTest — FNV hash/profiler,
  SEH/COM ile alakasız) dışında yeni kırık yok.
- I9 kod incelemesiyle doğrulandı (COM ref-count OS-state ister, birim test
  pratik değil).

### Kullanıcıya görünen davranış değişiklikleri
- Tek AV artık ERROR loglanır (önce sessizdi); akış düşmez (self-healing korunur).
- Same-site 60s'de 3 AV → deterministik sonlanma (__fastfail) — önce sessiz-bozuk-devam.
- SO/BP/SS artık WER'e çöker (önce Grup A'da yutuluyordu).

### KULLANICIDA / atlanmış (gerekçeli)
- Kasıtlı AV/stack-overflow tetikleyen entegrasyon testi atlandı (CI'ı çökertir);
  riskli mantık saf seam testiyle kapsandı.
- Eskalasyon __fastfail'in canlı süreçte gözlemi manuel doğrulanmadı (blocker değil).