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

**Ninja build sistem kullanılıyor (hızlı, paralel):**

```cmd
# İlk kez: configure + build
scripts\configure.bat
scripts\build.bat

# Sonraki build'ler (tüm app)
scripts\build.bat

# Belirli hedef
scripts\build.bat reji_pipeline
scripts\build.bat reji_ui

# Çalıştırma
build\src\ui\reji_app.exe
```

Scripts otomatik olarak:
- `vswhere` ile VS'yi algılar
- `vcvars64.bat` çalıştırır
- Ninja ile configure eder
- 8 thread paralel build

**Eski NMake build hâlâ çalışıyor (compatibility için):**
```cmd
cmake -B build -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --target reji_app
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
