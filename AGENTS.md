# AGENTS.md — Reji Studio AI Geliştirme Kılavuzu

> Bu dosya her yeni oturumda veya bağlam sıfırlandığında ilk okunacak belgedir.
> Claude Code, Cursor, Windsurf veya başka bir AI aracı bu dosyayı referans alır.

---

## Proje Kimliği

| Alan | Değer |
|---|---|
| Proje | Reji Studio |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | C:\reji-studio |
| Stack | C++17/MSVC + Rust/Tokio + Qt6 6.8.0 + CMake |
| GPU | AMD Radeon 780M (display/iGPU) + NVIDIA RTX 4070 Laptop (encode/dGPU) |
| OS | Windows 11 |

---

## Teknoloji Yığını

```
src/
├── pipeline/          # C++17 — DXGI capture, NVENC encode, WASAPI audio
│   ├── capture/       # DxgiCapturePipeline, GpuResourceManager
│   ├── encode/        # NvencEncoder (stub — NVENC SDK gerekli)
│   ├── audio/         # WasapiCapture
│   └── output/        # SrtOutput (stub — SRT SDK gerekli)
├── ui/                # Qt6 — QOpenGLWidget, preview, sahne yönetimi
│   ├── preview_widget # PBO ping-pong, CapabilityDetector, DwmFlush
│   ├── main_window    # Pipeline entegrasyonu
│   └── healing_overlay# Self-healing UI
├── ffi/               # C ABI köprüsü — C++ ↔ Rust
└── orchestrator/      # Rust/Tokio — event bus, self-healing, makro motoru
```

---

## Build Komutları

**Önerilen: Python build script (`python scripts/build.py`)**

```cmd
cd C:\reji-studio

# Normal build (Release, reji_app)
python scripts/build.py

# Clean build
python scripts/build.py --clean

# Belirli hedef
python scripts/build.py --target reji_pipeline
python scripts/build.py --target all

# Build + çalıştır
python scripts/build.py --run

# Debug modu
python scripts/build.py --config Debug

# Explicit generator
python scripts/build.py --generator Ninja
python scripts/build.py --generator NMake
```

**Script özellikler:**
- ✅ `vswhere` ile VS 2022/2026 otomatik algılar
- ✅ Ninja / NMake otomatik seçer (Ninja öncelikli)
- ✅ Colorized output, build süresi göster
- ✅ Hata log'a yaz, son 20 satırı göster
- ✅ macOS / Linux stub'ları (v0.3+)

**Eski batch scripts (legacy, compatibility için):**
```cmd
scripts\configure.bat
scripts\build.bat
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
```

---

## Kodlama Kuralları

### C++ (pipeline/)
- RAII — owning raw pointer yasak
- Hot-path'te heap allocation yasak (frame başına)
- Her public metot `bool` döndürür (void yasak)
- SEH gerektiren fonksiyonlar `__declspec(noinline)` + `__try/__except`
- C++ nesneleri `__try` scope içinde yasak
- `printf` yerine `fprintf(stderr, ...)` + `fflush(stderr)`
- Cross-device `CopyResource` yasak (same_adapter_ zorunlu)

### Qt6 (ui/)
- `QOpenGLWidget` — `initializeGL`, `paintGL`, `resizeGL` override
- `DwmFlush()` — `paintGL` başında zorunlu (NVIDIA Optimus race condition)
- Thread-safe frame upload: `QMutexLocker` + `QMetaObject::invokeMethod`
- PBO ping-pong: frame 0 guard zorunlu, boyut değişiminde `pbo_frame=0`

### Rust (orchestrator/)
- `cargo audit` — her commit öncesi
- `catch_unwind` — FFI sınırında zorunlu
- crossbeam SPSC ring buffer — FFI veri transferi için

---

## Performans Kuralları (v0.4+ Runtime Adaptation)

### WMI & System Queries
- **ASLA hot-path'te WMI query çağırma** — thermal, CPU load queries ayrı thread'te
- **Min 1 Hz polling rate** — 1000ms minimum interval
- **Timeout 5s** — WMI query timeout'ı, fallback 0°C (unavailable)
- ❌ **YASAK:** `run_frame()` → `rj_metrics_poll()` → WMI (deadlock risk)
- ✅ **DOĞRU:** background thread (MetricsCollector) → poll → metrics buffer

### Rule Evaluation
- **Pre-compile conditions** — JSON load'ta condition parsing, frame'de değil
- ❌ **YASAK:** `std::string` condition parsing in `RuleEngine::eval_condition()` per frame
- ✅ **DOĞRU:** condition AST pre-build, frame'de boolean check only
- **Threshold:** condition eval <1ms per rule (8 rules = <8ms total)
- **Rust:** Rule evaluation thread-spawned, async, result queue'ya push

### Frame Drop Calculation
- ❌ **YASAK:** `std::deque<uint32_t>` 1800 frame window (slow deque operations)
- ✅ **DOĞRU:** circular ring buffer (fixed 1800 capacity, O(1) push/pop)
  ```cpp
  // Preferred:
  uint32_t frame_drop_window[1800];
  size_t window_head = 0;
  frame_drop_window[window_head++ % 1800] = current_drop;
  ```
- **Update rate:** 1 Hz (every 1800 frames @ 60fps)
- **Lock:** `std::mutex` minimal scope (calculate, unlock, return)

### UI Updates (Qt)
- ❌ **YASAK:** Direct `healing_overlay->updateUI()` from Rust/FFI thread
- ✅ **DOĞRU:** `QMetaObject::invokeMethod(healing_overlay, "onActionEvent", Qt::QueuedConnection, ...)`
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
- **Timeout:** Co-Pilot 30s → action cancelled (not auto-executed)
- **Dequeue rate:** UI polls `rj_action_dequeue()` every 100ms

### Metrics Buffer
- **Canary check:** every `RjMetricSample` has `magic_head/tail` validation
- **Alignment:** `#pragma pack(1)` if struct padding matters for FFI
- **Size:** ~80 bytes per sample, circular buffer 10 samples = 800 bytes (negligible)

---

## Dokunulmaması Gereken Dosyalar

```
# Değiştirme — kararlı ABI
src/ffi/ffi_bridge.h
src/ffi/ffi_bridge.c
src/orchestrator/src/metrics.rs  # RjMetricSample layout — Rust/C++ ABI

# Build sistemi — sadece gerekirse değiştir
CMakeLists.txt (root)
src/pipeline/CMakeLists.txt      # SRT/NVENC stub mantığı hassas

# Kritik GPU kodu — test etmeden değiştirme
src/pipeline/capture/gpu_resource_manager.cpp  # same_adapter_ mantığı
src/ui/preview_widget.cpp                       # DwmFlush sırası kritik
```

---

## Render Path Kararları

| GPU Vendor | vendor_id | Render Path |
|---|---|---|
| AMD | 0x1002 | PBO ping-pong ✅ |
| NVIDIA | 0x10DE | NV_DX_INTEROP stub (PBO çalışır) |
| Intel | 0x8086 | PBO ping-pong |

**DwmFlush zorunluluğu:** NVIDIA dGPU render + AMD iGPU DWM blit race condition.
`glFinish()` yetmez — sadece GL queue bekler. `DwmFlush()` DWM compositor bariyeri.

---

## Bilinen Sorunlar / Teknik Borç

| Sorun | Durum | Çözüm |
|---|---|---|
| NVENC SDK yok | Preview-only mod | NVENC_SDK_PATH set et |
| SRT stub | Yayın çıkışı yok | vcpkg install libsrt |
| WGL_NV_DX_INTEROP | Stub — v0.4 | NVIDIA path gerçek impl |
| Renk bozukluğu | BGRA→RGBA | v0.3 devam |
| nvwgf2umx.dll crash | DwmFlush ile kısmen çözüldü | İzleniyor |

---

## Hata Ayıklama Rehberi

### Crash — nvwgf2umx.dll 0xC0000005
```cmd
# Event Log
powershell -command "Get-EventLog -LogName Application -Source 'Application Error' -Newest 3 | Select-Object TimeGenerated, Message | Format-List"

# Log kontrol
findstr "initializeGL" C:\reji-studio\run.log
findstr "paintGL" C:\reji-studio\run.log
```

### Build hatası — LNK2019
- SRT/NVENC stub eksik olabilir
- `src/pipeline/CMakeLists.txt` kontrol et
- Stub dosyaları: `srt_output_stub.cpp`, `encode_nvenc.cpp` (stub modda)

### Preview gelmiyor
- `findstr "Staging" run.log` — staging texture oluştu mu?
- `findstr "preview_cb set OK" run.log` — callback set edildi mi?
- `findstr "render path" run.log` — hangi path seçildi?

---

## Görev Verme Kuralları

**AI'ya görev verirken şunları belirt:**

1. **Hangi dosya** — tam path ver
2. **Ne değiştirilecek** — mevcut kodu göster
3. **Neden** — bağlamı açıkla
4. **Test** — nasıl doğrulanacak

**Kötü:**
```
preview_widget'ı düzelt
```

**İyi:**
```
src/ui/preview_widget.cpp içinde initializeGL() fonksiyonunda
glFinish() satırından sonra DwmFlush() ekle.
Sebep: NVIDIA Optimus race condition — DWM blit tamamlanmadan
yeni frame gönderilmemeli.
Test: cmake --build + reji_app.exe çalıştır, 30s crash olmadan
çalışmalı.
```

---

## Mimari Savunma Kuralları (Pre-mortem 2026)

### Senaryo 4 — Donanım Değişimi (Symbian Dersi)
**Risk:** Unified memory mimarisi yaygınlaşırsa D3D11↔Vulkan zero-copy bridge atıl kalır.

**Kural — Donanım bağımlı kod izolasyonu:**
- Donanım bağımlı kod SADECE şu dosyalarda yaşar:
  - `src/pipeline/gpu/external_memory_bridge.*`
  - `src/pipeline/capture/capture_dxgi.*`
- `pipeline.cpp` bu dosyaları doğrudan include etmez — sadece abstract callback üzerinden iletişim kurar
- v0.6'da `IRenderBridge` abstract arayüzü eklenecek: `ZeroCopyLegacyBridge` ve `UnifiedMemoryBridge` ayrı implementasyon olarak yaşayacak

**Erken uyarı — GitHub Actions (haftalık):**
```yaml
- name: Check Vulkan deprecation notices
  run: |
    curl -s https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/main/CHANGELOG.adoc |
    grep -i "deprecated\|removed" | head -20
```

---

### Senaryo 5 — Ekosistem Direnci (Itanium Dersi)
**Risk:** NVENC SDK lisansı değişirse veya SRT ölürse bağımlılıklar projeyi bloke eder.

**Kural — Vendor lock-in yasağı:**
- NVENC ve SRT implementasyonlarında vendor-specific struct'lar `pipeline.cpp`'e sızmaz
- Encoder arayüzü `IVideoEncoder` olacak — NVENC bir implementasyon, FFmpeg/VAAPI fallback mimaride hazır
- Ağ katmanı `INetworkSender` soyut arayüzü — SRT bir implementasyon, plugin olarak değiştirilebilir
- Hiçbir NVENC-specific veya SRT-specific tip `src/pipeline/include/` dışına çıkmaz

**Erken uyarı — CI lisans tarayıcı:**
```yaml
# .github/workflows/license-audit.yml — her PR'da çalışır
- name: Rust license audit
  run: cargo deny check licenses

- name: Dependency audit
  run: cargo audit
```

---

## Bağlam Sıfırlama

**Her büyük görevden sonra `/clear` veya yeni pencere aç.**

Uzun bağlam model performansını düşürür. Yeni pencerede:
1. Bu dosyayı oku: `AGENTS.md`
2. Durumu oku: `CONTEXT.md`
3. İlerlemeyi oku: `docs/progress.md`

---

## Model Seçimi

| Görev | Model |
|---|---|
| Mimari karar, karmaşık debug | claude-sonnet-4-6 |
| Dosya düzenleme, dokümantasyon | claude-haiku-4-5 |
| Security review | claude-sonnet-4-6 |
| Build, test, basit fix | claude-haiku-4-5 |

CLI'da geçiş: `/model claude-haiku-4-5`

## Build Komutları (Claude Code CLI için)
### Preset kullanımı (tercih edilen)
cmake --preset release
cmake --build --preset release

### Her bash komutuna cd prefix ekle
cd C:/reji-studio && cmake --build --preset release

### Path separator kuralı
- Claude Code bash: forward slash -> C:/reji-studio
- Windows CMD: backslash -> C:\reji-studio
