# AGENTS.md â€” Reji Studio AI GeliÅŸtirme KÄ±lavuzu

> Bu dosya her yeni oturumda veya baÄŸlam sÄ±fÄ±rlandÄ±ÄŸÄ±nda ilk okunacak belgedir.
> Claude Code, Cursor, Windsurf veya baÅŸka bir AI aracÄ± bu dosyayÄ± referans alÄ±r.

---

## Proje KimliÄŸi

| Alan | DeÄŸer |
|---|---|
| Proje | Reji Studio |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | C:\reji-studio |
| Stack | C++17/MSVC + Rust/Tokio + Qt6 6.8.0 + CMake |
| GPU | AMD Radeon 780M (display/iGPU) + NVIDIA RTX 4070 Laptop (encode/dGPU) |
| OS | Windows 11 |

---

## Teknoloji YÄ±ÄŸÄ±nÄ±

```
src/
â”œâ”€â”€ pipeline/          # C++17 â€” DXGI capture, NVENC encode, WASAPI audio
â”‚   â”œâ”€â”€ capture/       # DxgiCapturePipeline, GpuResourceManager
â”‚   â”œâ”€â”€ encode/        # NvencEncoder (stub â€” NVENC SDK gerekli)
â”‚   â”œâ”€â”€ audio/         # WasapiCapture
â”‚   â””â”€â”€ output/        # SrtOutput (stub â€” SRT SDK gerekli)
â”œâ”€â”€ ui/                # Qt6 â€” QOpenGLWidget, preview, sahne yÃ¶netimi
â”‚   â”œâ”€â”€ preview_widget # PBO ping-pong, CapabilityDetector, DwmFlush
â”‚   â”œâ”€â”€ main_window    # Pipeline entegrasyonu
â”‚   â””â”€â”€ healing_overlay# Self-healing UI
â”œâ”€â”€ ffi/               # C ABI kÃ¶prÃ¼sÃ¼ â€” C++ â†” Rust
â””â”€â”€ orchestrator/      # Rust/Tokio â€” event bus, self-healing, makro motoru
```

---

## Build KomutlarÄ±

**Ã–nerilen: Python build script (`python scripts/build.py`)**

```cmd
cd C:\reji-studio

# Normal build (Release, reji_app)
python scripts/build.py

# Clean build
python scripts/build.py --clean

# Belirli hedef
python scripts/build.py --target reji_pipeline
python scripts/build.py --target all

# Build + Ã§alÄ±ÅŸtÄ±r
python scripts/build.py --run

# Debug modu
python scripts/build.py --config Debug

# Explicit generator
python scripts/build.py --generator Ninja
python scripts/build.py --generator NMake
```

**Script Ã¶zellikler:**
- âœ… `vswhere` ile VS 2022/2026 otomatik algÄ±lar
- âœ… Ninja / NMake otomatik seÃ§er (Ninja Ã¶ncelikli)
- âœ… Colorized output, build sÃ¼resi gÃ¶ster
- âœ… Hata log'a yaz, son 20 satÄ±rÄ± gÃ¶ster
- âœ… macOS / Linux stub'larÄ± (v0.3+)

**Eski batch scripts (legacy, compatibility iÃ§in):**
```cmd
scripts\configure.bat
scripts\build.bat
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
```

---

## Kodlama KurallarÄ±

### C++ (pipeline/)
- RAII â€” owning raw pointer yasak
- Hot-path'te heap allocation yasak (frame baÅŸÄ±na)
- Her public metot `bool` dÃ¶ndÃ¼rÃ¼r (void yasak)
- SEH gerektiren fonksiyonlar `__declspec(noinline)` + `__try/__except`
- C++ nesneleri `__try` scope iÃ§inde yasak
- `printf` yerine `fprintf(stderr, ...)` + `fflush(stderr)`
- Cross-device `CopyResource` yasak (same_adapter_ zorunlu)

### Qt6 (ui/)
- `QOpenGLWidget` â€” `initializeGL`, `paintGL`, `resizeGL` override
- `DwmFlush()` â€” `paintGL` baÅŸÄ±nda zorunlu (NVIDIA Optimus race condition)
- Thread-safe frame upload: `QMutexLocker` + `QMetaObject::invokeMethod`
- PBO ping-pong: frame 0 guard zorunlu, boyut deÄŸiÅŸiminde `pbo_frame=0`

### Rust (orchestrator/)
- `cargo audit` â€” her commit Ã¶ncesi
- `catch_unwind` â€” FFI sÄ±nÄ±rÄ±nda zorunlu
- crossbeam SPSC ring buffer â€” FFI veri transferi iÃ§in

---

## Performans KurallarÄ± (v0.4+ Runtime Adaptation)

### WMI & System Queries
- **ASLA hot-path'te WMI query Ã§aÄŸÄ±rma** â€” thermal, CPU load queries ayrÄ± thread'te
- **Min 1 Hz polling rate** â€” 1000ms minimum interval
- **Timeout 5s** â€” WMI query timeout'Ä±, fallback 0Â°C (unavailable)
- âŒ **YASAK:** `run_frame()` â†’ `rj_metrics_poll()` â†’ WMI (deadlock risk)
- âœ… **DOÄRU:** background thread (MetricsCollector) â†’ poll â†’ metrics buffer

### Rule Evaluation
- **Pre-compile conditions** â€” JSON load'ta condition parsing, frame'de deÄŸil
- âŒ **YASAK:** `std::string` condition parsing in `RuleEngine::eval_condition()` per frame
- âœ… **DOÄRU:** condition AST pre-build, frame'de boolean check only
- **Threshold:** condition eval <1ms per rule (8 rules = <8ms total)
- **Rust:** Rule evaluation thread-spawned, async, result queue'ya push

### Frame Drop Calculation
- âŒ **YASAK:** `std::deque<uint32_t>` 1800 frame window (slow deque operations)
- âœ… **DOÄRU:** circular ring buffer (fixed 1800 capacity, O(1) push/pop)
  ```cpp
  // Preferred:
  uint32_t frame_drop_window[1800];
  size_t window_head = 0;
  frame_drop_window[window_head++ % 1800] = current_drop;
  ```
- **Update rate:** 1 Hz (every 1800 frames @ 60fps)
- **Lock:** `std::mutex` minimal scope (calculate, unlock, return)

### UI Updates (Qt)
- âŒ **YASAK:** Direct `healing_overlay->updateUI()` from Rust/FFI thread
- âœ… **DOÄRU:** `QMetaObject::invokeMethod(healing_overlay, "onActionEvent", Qt::QueuedConnection, ...)`
- **Pattern:**
  ```cpp
  QMetaObject::invokeMethod(healing_overlay, "onActionEvent",
      Qt::QueuedConnection,
      Q_ARG(QString, action_description),
      Q_ARG(bool, require_approval));
  ```
- **Slots:** must be `public` or `public slots` for invokeMethod
- **Signal:** alternative: emit from C++ thread, connect with Qt::QueuedConnection

### Action Queue Management
- **Capacity:** 64 fixed (crossbeam ArrayQueue)
- **Overflow:** drop newest (low priority) actions, log warning
- **Timeout:** Co-Pilot 30s â†’ action cancelled (not auto-executed)
- **Dequeue rate:** UI polls `rj_action_dequeue()` every 100ms

### Metrics Buffer
- **Canary check:** every `RjMetricSample` has `magic_head/tail` validation
- **Alignment:** `#pragma pack(1)` if struct padding matters for FFI
- **Size:** ~80 bytes per sample, circular buffer 10 samples = 800 bytes (negligible)

---

## DokunulmamasÄ± Gereken Dosyalar

```
# DeÄŸiÅŸtirme â€” kararlÄ± ABI
src/ffi/ffi_bridge.h
src/ffi/ffi_bridge.c
src/orchestrator/src/metrics.rs  # RjMetricSample layout â€” Rust/C++ ABI

# Build sistemi â€” sadece gerekirse deÄŸiÅŸtir
CMakeLists.txt (root)
src/pipeline/CMakeLists.txt      # SRT/NVENC stub mantÄ±ÄŸÄ± hassas

# Kritik GPU kodu â€” test etmeden deÄŸiÅŸtirme
src/pipeline/capture/gpu_resource_manager.cpp  # same_adapter_ mantÄ±ÄŸÄ±
src/ui/preview_widget.cpp                       # DwmFlush sÄ±rasÄ± kritik
```

---

## Render Path KararlarÄ±

| GPU Vendor | vendor_id | Render Path |
|---|---|---|
| AMD | 0x1002 | PBO ping-pong âœ… |
| NVIDIA | 0x10DE | NV_DX_INTEROP stub (PBO Ã§alÄ±ÅŸÄ±r) |
| Intel | 0x8086 | PBO ping-pong |

**DwmFlush zorunluluÄŸu:** NVIDIA dGPU render + AMD iGPU DWM blit race condition.
`glFinish()` yetmez â€” sadece GL queue bekler. `DwmFlush()` DWM compositor bariyeri.

---

## Bilinen Sorunlar / Teknik BorÃ§

| Sorun | Durum | Ã‡Ã¶zÃ¼m |
|---|---|---|
| NVENC SDK yok | Preview-only mod | NVENC_SDK_PATH set et |
| SRT stub | YayÄ±n Ã§Ä±kÄ±ÅŸÄ± yok | vcpkg install libsrt |
| WGL_NV_DX_INTEROP | Stub â€” v0.4 | NVIDIA path gerÃ§ek impl |
| Renk bozukluÄŸu | BGRAâ†’RGBA | v0.3 devam |
| nvwgf2umx.dll crash | DwmFlush ile kÄ±smen Ã§Ã¶zÃ¼ldÃ¼ | Ä°zleniyor |

---

## Hata AyÄ±klama Rehberi

### Crash â€” nvwgf2umx.dll 0xC0000005
```cmd
# Event Log
powershell -command "Get-EventLog -LogName Application -Source 'Application Error' -Newest 3 | Select-Object TimeGenerated, Message | Format-List"

# Log kontrol
findstr "initializeGL" C:\reji-studio\run.log
findstr "paintGL" C:\reji-studio\run.log
```

### Build hatasÄ± â€” LNK2019
- SRT/NVENC stub eksik olabilir
- `src/pipeline/CMakeLists.txt` kontrol et
- Stub dosyalarÄ±: `srt_output_stub.cpp`, `encode_nvenc.cpp` (stub modda)

### Preview gelmiyor
- `findstr "Staging" run.log` â€” staging texture oluÅŸtu mu?
- `findstr "preview_cb set OK" run.log` â€” callback set edildi mi?
- `findstr "render path" run.log` â€” hangi path seÃ§ildi?

---

## GÃ¶rev Verme KurallarÄ±

**AI'ya gÃ¶rev verirken ÅŸunlarÄ± belirt:**

1. **Hangi dosya** â€” tam path ver
2. **Ne deÄŸiÅŸtirilecek** â€” mevcut kodu gÃ¶ster
3. **Neden** â€” baÄŸlamÄ± aÃ§Ä±kla
4. **Test** â€” nasÄ±l doÄŸrulanacak

**KÃ¶tÃ¼:**
```
preview_widget'Ä± dÃ¼zelt
```

**Ä°yi:**
```
src/ui/preview_widget.cpp iÃ§inde initializeGL() fonksiyonunda
glFinish() satÄ±rÄ±ndan sonra DwmFlush() ekle.
Sebep: NVIDIA Optimus race condition â€” DWM blit tamamlanmadan
yeni frame gÃ¶nderilmemeli.
Test: cmake --build + reji_app.exe Ã§alÄ±ÅŸtÄ±r, 30s crash olmadan
Ã§alÄ±ÅŸmalÄ±.
```

---

## Mimari Savunma KurallarÄ± (Pre-mortem 2026)

### Senaryo 4 â€” DonanÄ±m DeÄŸiÅŸimi (Symbian Dersi)
**Risk:** Unified memory mimarisi yaygÄ±nlaÅŸÄ±rsa D3D11â†”Vulkan zero-copy bridge atÄ±l kalÄ±r.

**Kural â€” DonanÄ±m baÄŸÄ±mlÄ± kod izolasyonu:**
- DonanÄ±m baÄŸÄ±mlÄ± kod SADECE ÅŸu dosyalarda yaÅŸar:
  - `src/pipeline/gpu/external_memory_bridge.*`
  - `src/pipeline/capture/capture_dxgi.*`
- `pipeline.cpp` bu dosyalarÄ± doÄŸrudan include etmez â€” sadece abstract callback Ã¼zerinden iletiÅŸim kurar
- v0.6'da `IRenderBridge` abstract arayÃ¼zÃ¼ eklenecek: `ZeroCopyLegacyBridge` ve `UnifiedMemoryBridge` ayrÄ± implementasyon olarak yaÅŸayacak

**Erken uyarÄ± â€” GitHub Actions (haftalÄ±k):**
```yaml
- name: Check Vulkan deprecation notices
  run: |
    curl -s https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/CHANGELOG.adoc |
    grep -i "deprecated\|removed" | head -20
```

---

### Senaryo 5 â€” Ekosistem Direnci (Itanium Dersi)
**Risk:** NVENC SDK lisansÄ± deÄŸiÅŸirse veya SRT Ã¶lÃ¼rse baÄŸÄ±mlÄ±lÄ±klar projeyi bloke eder.

**Kural â€” Vendor lock-in yasaÄŸÄ±:**
- NVENC ve SRT implementasyonlarÄ±nda vendor-specific struct'lar `pipeline.cpp`'e sÄ±zmaz
- Encoder arayÃ¼zÃ¼ `IVideoEncoder` olacak â€” NVENC bir implementasyon, FFmpeg/VAAPI fallback mimaride hazÄ±r
- AÄŸ katmanÄ± `INetworkSender` soyut arayÃ¼zÃ¼ â€” SRT bir implementasyon, plugin olarak deÄŸiÅŸtirilebilir
- HiÃ§bir NVENC-specific veya SRT-specific tip `src/pipeline/include/` dÄ±ÅŸÄ±na Ã§Ä±kmaz

**Erken uyarÄ± â€” CI lisans tarayÄ±cÄ±:**
```yaml
# .github/workflows/license-audit.yml â€” her PR'da Ã§alÄ±ÅŸÄ±r
- name: Rust license audit
  run: cargo deny check licenses

- name: Dependency audit
  run: cargo audit
```

---

## BaÄŸlam SÄ±fÄ±rlama

**Her bÃ¼yÃ¼k gÃ¶revden sonra `/clear` veya yeni pencere aÃ§.**

Uzun baÄŸlam model performansÄ±nÄ± dÃ¼ÅŸÃ¼rÃ¼r. Yeni pencerede:
1. Bu dosyayÄ± oku: `AGENTS.md`
2. Durumu oku: `CONTEXT.md`
3. Ä°lerlemeyi oku: `docs/progress.md`
4. Yol haritasını oku: `docs/ROADMAP.md` — teknik yol haritası
5. Aktif sprint planını oku: `docs/FABLE5_BUG_PLAN_V2.md` — aktif sprint planı

---

## Vulkan/D3D11 Geliştirme Kuralları

### Yeni Vulkan özelliği eklenirken zorunlu:
- Vulkan spec VUID listesi okunmalı
- validation layer aktifken test edilmeli (just run → VUID = 0)
- docs/vulkan-sync-diagram.md güncellenmeli

### Her PR öncesi kontrol:
- just run → type err.log | findstr "VUID" → boş olmalı
- just test → 100% pass
- just abi-check → pass

### Zorunlu okuma (bir kez):
- VK_KHR_external_memory_win32 spec
- VK_KHR_external_semaphore_win32 spec
- VK_KHR_win32_keyed_mutex spec
- VkImageMemoryBarrier oldLayout/newLayout kuralları
- VK_QUEUE_FAMILY_EXTERNAL semantiği

---

## Oturum Başlangıcı

Her oturum başında şu dosyaları oku:
- docs/ROADMAP.md
- docs/FABLE5_BUG_PLAN_V5.md (aktif — tamamlandı)
- docs/FABLE5_BUG_PLAN_V4.md (tamamlandı)
- docs/FABLE5_BUG_PLAN_V2.md
- docs/FABLE5_BUG_PLAN.md

**Son sprint notu:** V5 F1-F18 tamamlandı — 12.06.2026

---

## Model SeÃ§imi

| GÃ¶rev | Model |
|---|---|
| Mimari karar, karmaÅŸÄ±k debug | claude-sonnet-4-6 |
| Dosya dÃ¼zenleme, dokÃ¼mantasyon | claude-haiku-4-5 |
| Security review | claude-sonnet-4-6 |
| Build, test, basit fix | claude-haiku-4-5 |

CLI'da geÃ§iÅŸ: `/model claude-haiku-4-5`

## Build KomutlarÄ± (Claude Code CLI iÃ§in)
### Preset kullanÄ±mÄ± (tercih edilen)
cmake --preset release
cmake --build --preset release

### Her bash komutuna cd prefix ekle
cd C:/reji-studio && cmake --build --preset release

### Path separator kuralÄ±
- Claude Code bash: forward slash -> C:/reji-studio
- Windows CMD: backslash -> C:\reji-studio


## Model Selection Policy

### claude-haiku-4-5-20251001 — Mekanik, düşük riskli görevler
- Namespace / forward declaration düzeltmeleri
- Warning temizleme (-Wall çıktısı)
- Log mesajı ekleme / düzenleme
- CMakeLists.txt küçük değişiklikler
- Dokümantasyon ve yorum güncellemeleri
- CLI: `/model claude-haiku-4-5-20251001`

### claude-sonnet-4-6 — Varsayılan (çoğu geliştirme görevi)
- C++ implementasyon ve refactor
- Build hata çözümü
- Test yazımı
- GL / Vulkan API çağrıları
- preview_widget, copy_optimizer, pipeline modülleri
- CLI: `/model claude-sonnet-4-6`

### claude-opus-4-8 — Karmaşık mimari ve derin analiz
- Vulkan pipeline tasarımı (timeline semaphore, sync)
- GL interop mimarisi (ExternalMemoryBridge)
- FFI / ABI tasarımı (RjFrameData, Rust<->C++ sınırı)
- VK_ERROR_DEVICE_LOST gibi derin hata analizi
- Cross-GPU senkronizasyon (AMD 780M + RTX 4070)
- Güvenlik ve bellek güvenliği review
- CLI: `/model claude-opus-4-8`

### anthropic/claude-5-fable-20260609 — Otonom, uzun süreli görevler (OpenRouter)
Model string: anthropic/claude-5-fable-20260609
Context: 1M token | Reasoning: destekleniyor
- Saatler / günler süren otonom geliştirme görevleri
- Tüm Reji Studio kod tabanı üzerinde uçtan uca analiz
- v0.6.0+ roadmap mimari planlaması
- Çoklu GPU scheduler (AMD iGPU + NVIDIA dGPU tam koordinasyon)
- DXGI capture hot-path SEH + FFI ABI tam yeniden tasarımı
- Uzun süreli agentic pipeline (Claude Code ile birlikte)
- Sıfır insan müdahalesi gerektiren verification loop'lu görevler

### /model-route kullanımı
Görev başlamadan önce modeli belirlemek için:
  /model-route "görev açıklaması" --budget low|med|high
