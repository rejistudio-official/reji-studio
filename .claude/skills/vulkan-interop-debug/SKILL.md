---
name: vulkan-interop-debug
description: Reji Studio'da Vulkan / D3D11 interop, GPU ve görüntü sorunlarını ayıklama prosedürü. Device-lost, TDR, VK_ERROR_DEVICE_LOST, siyah ekran, tearing, donmuş preview, yanlış renk (BGRA/RGBA), validation layer hatası, VUID ihlali, keyed mutex, timeline semaphore, external memory, copy_optimizer, RecoveryCoordinator veya healing davranışıyla ilgili HER hata ayıklama görevinde bu skill'i kullan. Kullanıcı "GPU", "Vulkan", "interop", "TDR", "device lost", "siyah ekran", "tearing" veya "preview bozuk" dediğinde de tetiklenir.
---

# Vulkan Interop Debug — Reji Studio

Çift GPU referans donanımı: **AMD Radeon 780M (iGPU, display) + NVIDIA RTX 4070
Laptop (dGPU, encode)**. Bu topolojinin tuhaflıkları hata analizinin başlangıç
noktasıdır — genel Vulkan bilgisiyle değil, buradaki gerçeklerle başla.

## Donanım topolojisi gerçekleri

> **AKTİF RUNTIME YOLU = WGC** (2026-07-10 dual-GPU HW'de run.log ile doğrulandı).
> `IScreenCapture::create()` (screen_capture_factory.cpp) **WGC-tercihli, DXGI-fallback**;
> `WgcScreenCapture::is_supported()` Win10 1803+'da her zaman true → Win11'de fiilen
> **HER ZAMAN WGC** seçilir. Aşağıdaki "DXGI zero-copy" modeli yalnız WGC başarısızsa
> canlanan **fallback**'tir — normal koşuda **İNAKTİF**. Uçtan uca detay:
> docs/SESSION_NOTES.md "Gerçek Runtime Topolojisi — Uçtan Uca Harita".

- Ekran **AMD 780M iGPU'ya** bağlı; NVENC **NVIDIA dGPU'da**.
- `display_vendor_id()` → `gpu_scan_.entries[0]` = display (AMD); `entries[1]+` = encode (NVIDIA).
- Vulkan device seçimi (`vulkan_initializer.zig select_device`): skor NVIDIA 210 > AMD 100
  olsa da bu hibrit konfigde app'in Vulkan instance'ı **yalnız AMD'yi** enumere ediyor →
  fiili seçim **AMD 780M** (`[VulkanZig] Selected: vendorID=0x1002`). AMD `VK_KHR_win32_keyed_mutex`'i
  destekliyor → `use_keyed_mutex=1` — ama aşağıda görüldüğü gibi WGC'de bu bayrak inert.

### Aktif yol — WGC (tek kaynak, iki bağımsız dal)
- **Capture:** `WgcScreenCapture` (capture_wgc.cpp) **NVIDIA (0x10DE)** adapter'ında D3D11
  device (`BGRA|VIDEO_SUPPORT`) açar; `next_frame()` bir **NVIDIA D3D11 texture** (`B8G8R8A8`)
  döndürür. `dynamic_cast<DxgiScreenCapture*>` **null** → tüm `capture_sub_.dxgi()` dalları atlanır.
- **Encode dalı:** `encode_frame(tex)` (pipeline.cpp:600) → **NVENC**, orijinal NVIDIA
  texture'ı doğrudan (aynı device'ta encode session, zero-copy). **AMD'ye HİÇ uğramaz.**
- **Preview dalı:** `emit_wgc_preview` (capture_subsystem.cpp:52) → NVIDIA'da STAGING texture,
  `CopyResource`+`Map(READ)` → **CPU pointer** → `preview_cb`→`uploadCpuFrame` → **AMD 780M GL**
  (display). **CPU-bounce cross-adapter; keyed-mutex/Vulkan-interop YOK.**
- **Bu yolda İNAKTİF (init edilse bile beslenmeyen) parçalar:**
  - `GpuResourceManager::transfer()` — çağrılmaz (yalnız capture_dxgi.cpp:366'dan).
  - `external_memory_bridge` — `get_external_memory_bridge()`→**null** (run.log `bridge=0`).
  - AMD Vulkan `GpuCopyOptimizer` keyed-mutex — init edilir ama `d3d11_frame_cb`→
    `get_last_frame_images()` `got=false` → `submitD3D11Frame`/`execute_copy` hiç koşmaz → **inert**.
  - `AcquireSync(0,16)` + `[Capture] KeyedMutex timeout/fail` — `DxgiCapturePipeline`'a ait, erişilmez.
- `same_adapter=false` hâlâ geçerli ama zero-copy zincirinin hiçbiri buna bağlı **çalışmıyor.**

### Fallback yol — DXGI (yalnız WGC başarısızsa; normalde İNAKTİF)
Aşağıdakiler **yalnız** `DxgiScreenCapture` seçilirse geçerlidir:
- DXGI Desktop Duplication **yalnız AMD adapter'ında** çalışır; NVIDIA'da `E_ACCESSDENIED`
  (hata değil, platform gerçeği).
- İKİ AYRI cross-GPU mekanizması, karıştırılmamalı:
  - (A) Encode `GpuResourceManager::transfer()` (AMD→NVIDIA cross-vendor): `same_adapter_`
    LUID ile false, `create_cross_adapter_shared()` runtime'da `CreateTexture2D E_INVALIDARG`
    (0x80070057) → gerçek yol CPU-fallback (`use_cpu_fallback_=true`); `keyed_mutex_display_`/
    `encode_`/`copy_fence_` ölü kod.
  - (B) Preview `capture_dxgi.cpp` `shared_texture_`→Vulkan→GL (hepsi AMD iGPU, cross-vendor
    DEĞİL): gerçek keyed-mutex `AcquireSync`/`ReleaseSync` (satır ~373/381).

> **Cross-vendor zero-copy KAPALI KONU (araştırma 10.07):** Cross-vendor (AMD+NVIDIA)
> zero-copy D3D11 paylaşımı denenmiş ve `E_INVALIDARG` ile başarısız — bu, endüstri
> çapında bilinen bir D3D/hibrit-GPU sınırlaması (OBS Studio dâhil kimse çözememiş; OBS
> resmî dokümanı "OBS can only run on one of these GPUs" der ve çok-GPU'da tam olarak
> Reji'nin kullandığı WGC yöntemini önerir), **Reji'ye özgü bir bug değil.** Bu yönde
> tekrar araştırma yapmadan önce bu notu oku. Detay: FABLE5_BUG_PLAN_V8 I2 satırı.

### Render path (her iki yolda ortak — capture backend'inden bağımsız)
`selectRenderPath()` **display** vendor'ına göre seçer (`display_vendor_id()`, init sonrası
bir kez, GL thread'de): NVIDIA (0x10DE) → `kNvDxInterop` stub (fiilen PBO çalışır); diğer →
`kPbo`. Referans HW'de display **AMD** → **`kPbo`**; WGC preview'ının `uploadCpuFrame`→GL
yüklemesi bu PBO yolunu kullanır.

## Semptom → ilk şüpheli haritası

> Aktif yol WGC olduğundan **önce WGC sütununa bak**; Vulkan/keyed-mutex/external-memory
> ipuçları yalnız DXGI-fallback zorlandıysa geçerlidir.

| Semptom | WGC (aktif) — ilk bakılacak | DXGI (fallback) — yalnız WGC başarısızsa |
|---|---|---|
| Preview donuk / güncellenmiyor | `is_wgc()&&preview_cb` dalı (pipeline.cpp:644) çalışıyor mu; `emit_wgc_preview` staging `Map` başarısı; `uploadCpuFrame`→GL upload; run.log `[WgcStaging] preview frame` akıyor mu | timeline semaphore wait vs signal (V7-H17); `execute_copy`/`is_copy_ready` |
| Tearing / yarım frame | CPU staging `CopyResource`/`Map` timing + GL upload senkronu; preview_cb kare atlama | keyed mutex hangi kaynağı koruyor (V7-H2: yanlış VkDeviceMemory) + acquire/release eşleşmesi |
| Siyah ekran (preview) | `preview_cb` hiç ateşliyor mu (run.log `[WgcStaging]`); GL texture completeness/upload (V7-H19); `uploadCpuFrame` pitch doğru mu | external memory import başarısı → sonra GL texture |
| Yanlış renk | Format zinciri **WGC:** `B8G8R8A8`(NVIDIA)→CPU→GL; BGRA/RGBA swap (V7-H7) CPU→GL ucunda mı | zincir DXGI→Vulkan→GL uçtan uca yaz |
| Encode yok / NVENC boş | `encode_frame(tex)` NVIDIA texture alıyor mu; NVENC session NVIDIA device'ta mı (`VIDEO_SUPPORT`); WGC frame null-streak (60→reinit) | encode_gpu `transfer()` CPU-fallback yolu |
| Capture hiç başlamıyor | `WgcScreenCapture::init` NVIDIA adapter bulundu mu (`[WgcCapture] NVIDIA adapter`); `CreateForMonitor` HRESULT; monitor index | Duplication AMD dışıysa `E_ACCESSDENIED` normaldir |
| Rastgele crash / VK_ERROR_DEVICE_LOST | Validation layer AÇ (aşağıda), VUID topla; NOT: Vulkan yolu WGC'de büyük ölçüde inert, önce D3D11/NVENC/GL'e bak | command buffer yaşam döngüsü (reset-while-pending, V7-H1) |
| İyileşme döngüsü (sürekli recreate) | RecoveryCoordinator log'ları + healing.rs hysteresis (5s) — kural fırtınası mı, gerçek device-lost mu ayır (her iki yolda ortak) ||
| Kopya sonrası siyah/çöp (`oldLayout=UNDEFINED`, D2/E4) | — (WGC'de zero-copy yolu erişilmez) | I28 — 10.07 dual-GPU HW'de doğrulandı, kasıtlı tasarım, validation temiz (yalnız sahne-geçişi slot reuse alt-senaryosu açık) |

## Standart ayıklama prosedürü

1. **Reprodüksiyon + log:** Uygulama stderr'ini (`fprintf`/`std.debug.print` →
   `HATA`/`DEVICE_LOST`/`recovery`) yakalamak için **açık redirect gerekir**:
   `build\src\ui\reji_app.exe > run.log 2>&1` (veya `just run > run.log 2>&1`).
   ⚠️ Düz `just run` (redirect'siz) uygulama çıktısını run.log'a YAZMAZ —
   `build.py` exe'yi yakalamasız çalıştırır, run.log'a yalnızca build satırını
   ekler. `findstr /i "HATA DEVICE_LOST recovery" run.log` ile ilk hata anını bul;
   sonraki hatalar genelde kaskaddır — köke odaklan.
2. **Validation layer'ı aç** — build tipine göre iki yol:
   - **Debug build'de OTOMATİK** (`vulkan_initializer.zig:45-65`): layer app
     tarafından `ppEnabledLayerNames` ile açılır, env var GEREKMEZ. Debug build:
     `python scripts\build.py --config Debug` (veya `just shield`'in ilk satırı).
   - **Release build'de (default!) manuel:** layer app tarafından açılmaz →
     `set VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation` ile loader'dan enjekte et
     (yeni SDK alternatifi: `set VK_LOADER_LAYERS_ENABLE=*validation`).
   - **⚠️ ÖLÜ FLAG'LER — kullanma:** `RJ_VALIDATION` (CMake `-D...=ON`) ve
     `RJ_ENABLE_VULKAN_VALIDATION` (env var) yalnızca `src/pipeline/CMakeLists.txt:159-165`
     yorum/tanımında var; **kaynak kodun hiçbir yerinde okunmuyor** (aktif giriş yolu
     `vulkan_initializer.cpp` yalnızca FFI sarmalayıcı → `vulkan_init_*`; asıl instance
     `vulkan_initializer.zig` `builtin.mode == .Debug`'a bakar, bu iki bayrağa DEĞİL).
     Yani Release'te validation'ı açmanın tek yolu yukarıdaki loader env var'ı — CMake
     flag'i ya da `RJ_ENABLE_...` env var'ı Release'te validation açmaz.
   - **⚠️ Debug messenger YOK** (kod tabanında `vkCreateDebugUtilsMessenger` sıfır):
     VUID mesajları uygulama callback'ine düşmez, VVL'nin varsayılan çıkışına gider.
     GUI'de stderr detach olabildiği için **DebugView (Sysinternals, OutputDebugString)
     asıl güvenilir yakalama yoludur** (Capture Win32 + Capture Global Win32); stdout
     redirect ikincil (VVL sürümüne bağlı).
   - **Deterministik dosya-log (REJI_RTMP_LOG eşdeğeri):** VVL'nin kendi dosya
     çıktısı stderr detach'ından etkilenmez. Çalışma dizinine (`C:\reji-studio`)
     `vk_layer_settings.txt` koy — loader otomatik bulur:
     ```
     khronos_validation.debug_action = VK_DBG_LAYER_ACTION_LOG_MSG
     khronos_validation.log_filename  = C:\reji-studio\vvl_output.txt
     khronos_validation.report_flags  = error,warn,perf
     ```
     VUID'ler doğrudan `vvl_output.txt`'e yazılır; app'in stderr'ine hiç bağlı değil.
     `just run`/GUI çıktı yakalama sorunu olan senaryolar için en güvenilir yol budur.
   - **⚠️ App'in KENDİ tanı çıktısı DebugView'dan AYRI bir kanal:** app'in kendi
     `fprintf(stderr)` teşhisi (`[WgcCapture]`, `[VulkanZig]`, `[NVENC]`, `[Pipeline]`,
     `[WgcStaging]`, `set_use_keyed_mutex`, `KeyedMutex timeout` vb.) `main.cpp:64`
     `freopen(...stderr)` ile doğrudan **`C:\reji-studio\run.log`**'a yönlendirilir.
     Dış redirect (`2>&1 | Tee-Object`, `-RedirectStandardError`) bunu **YAKALAYAMAZ** —
     `freopen` handle'ı ezer (2026-07 I2/I3 tespitinde 0 bayt ile doğrulandı). İki kanalı
     karıştırma: **DebugView = OutputDebugString** (Windows/driver + `dbglog`), **run.log =
     app'in kendi `fprintf`'i**. App davranışını izlerken önce `run.log` oku.
   Çıkan **VUID kodunu** aynen not et — düzeltme commit'inde referans ver
   (ev stili: V7-H1'deki gibi `VUID-vkResetCommandBuffer-commandBuffer-00045`).
3. **Sınıflandır:** senkronizasyon mu (semaphore/mutex/barrier), yaşam döngüsü mü
   (reset/destroy sırası), format mı, topoloji mi (yanlış adapter)?
4. **Tek değişkenli deney:** mock preset'te de oluyor mu?
   (`cmake --preset mock`) Oluyorsa Vulkan'a değil mantığa bak — çok zaman kazandırır.
5. **Frame içi görsel analiz gerekiyorsa RenderDoc** ile capture al;
   keyed mutex/semaphore sorunlarında Nsight Graphics daha iyi görür.
6. **Düzeltme kuralı:** Senkronizasyon düzeltmeleri davranış değiştirir —
   önce karakterizasyon/timing testi (`tests/test_gpu_query_timing.cpp`)
   yeşilken başla, düzeltme sonrası tekrar koş.

## Proje ilkeleri (bulgu geçmişinden damıtılmış — ihlal etme)

- **Per-slot kaynak ilkesi:** Pending olabilecek her şey (command buffer,
  semaphore, staging) slot başına ayrılır; tek kaynağı `SIMULTANEOUS_USE`
  ile paylaşmak yasak (H1/H11/H17 dersi).
- **Cross-thread erişilen her bayrak atomic** olmalı (H4/H18 dersi);
  "sadece okuyorum" savunması geçersiz.
- **Windows HANDLE'lar RAII/CloseHandle ile kapanır** (H5 dersi).
- **Shutdown sırası:** GpuInterop shutdown → VulkanInitializer release.
  Statik yıkım sırasına güvenme (H20 dersi).
- Keyed mutex D3D11 **texture**'ı korur; Vulkan tarafında beklenen şeyin
  aynı fiziksel kaynak olduğunu import zincirinden doğrula.
- **Tek-kaynak slot indexleme (I23 dersi):** Aynı round-robin pool'u paylaşan
  bileşenler (bridge image pool + optimizer command-buffer/layout tracking +
  widget GL-interop texture) için slot index'i **tek bir yerde üret, taşı** —
  her bileşen kendi sayacını sürmesin. Bağımsız sayaçlar mod-N eşit olsalar bile
  off-by-one + farklı ilerleme koşullarıyla kalıcı drift'e kayar; sonuç fiziksel
  image ile GL texture/layout tracking'in yanlış eşleşmesi (bayat kare, yanlış
  `oldLayout` barrier → VUID). Kaynak: `slot_ring.h::next_pool_slot`, bridge slot'u
  `execute_copy`'ye parametre. (Bu kod WGC'de inert — DXGI-fallback zorlanırsa geçerli.)

## RecoveryCoordinator / self-healing etkileşimi

Device-lost analizi yaparken healing motorunu hesaba kat:
- `recovery_coordinator.cpp` pipeline tarafı yeniden kurulumu yönetir;
  `healing.rs` kural bazlı aksiyonları (hysteresis_ms=5s, öncelik:
  BitrateReduce > CapFps > ScaleResolution > Recover > LogOnly).
- Semptom "kendini tekrar eden recreate" ise: log'da recovery tetikleyen
  metriği bul — sahte metrik (NaN fps, H8 sınıfı) healing'i yanlış tetikleyebilir.
- Debug sırasında healing'i susturmak istersen kural dosyasını
  (`~/.reji/rules.json`) log_only'ye çevir; kodda devre dışı bırakma.

## Çıktı formatı

Analiz sonucu şunları içermeli: kök neden (VUID/log kanıtıyla), sınıfı
(sync/lifecycle/format/topoloji), önerilen düzeltme, hangi ilkeye bağlandığı,
ve doğrulama planı (hangi test + validation layer temiz mi).
Yeni bir hata SINIFI bulunursa bu skill'in ilkeler bölümüne ekle.
