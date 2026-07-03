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
