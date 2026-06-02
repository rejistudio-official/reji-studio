# Reji Studio — Claude Code Bağlam Dosyası

> Her oturumda bu dosyayı oku. Ayrıca `docs/memory.md` ve `docs/progress.md` dosyalarını kontrol et.

---

## Proje Kimliği

| Alan | Değer |
|---|---|
| Ad | Reji Studio |
| Tür | Açık kaynak canlı yayın yazılımı + altyapı motoru |
| Lisans | Apache 2.0 |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | `C:\reji-studio` |
| Versiyon | v0.2 tamamlandı, v0.3 başladı |
| Son güncelleme | 2026-06-01 |

---

## Teknoloji Yığını

| Katman | Teknoloji | Klasör |
|---|---|---|
| Medya pipeline | C++17, MSVC | `src/pipeline/` |
| FFI köprüsü | C ABI | `src/ffi/` |
| Orkestrasyon | Rust + Tokio | `src/orchestrator/` |
| Arayüz | Qt6 + OpenGL | `src/ui/` |
| Testler | C++ + Rust | `tests/` |
| CI | GitHub Actions | `.github/workflows/` |

---

## Klasör Yapısı

```
C:\reji-studio\
├── .github/workflows/
│   ├── build.yml          ← CI: build + test (push/PR)
│   └── quality.yml        ← CI: cargo audit + cppcheck + build (push/PR + Pazartesi 09:00 UTC)
├── src/
│   ├── pipeline/
│   │   ├── include/pipeline.h           ← rj::Pipeline public API
│   │   ├── pipeline.cpp                 ← implementasyon (SEH wrapper'lar burada)
│   │   ├── capture/
│   │   │   ├── capture_dxgi.cpp/.h      ← DXGI Desktop Duplication
│   │   │   └── gpu_resource_manager.cpp/.h  ← cross-adapter SharedHandle
│   │   └── CMakeLists.txt
│   ├── ffi/
│   │   ├── ffi_bridge.h   ← RjMetricSample, RjCommand, extern "C" fonksiyonlar
│   │   └── ffi_bridge.c
│   ├── orchestrator/      ← Rust: event_bus, metrics, healing
│   └── ui/
│       ├── main_window.cpp/.h
│       ├── preview_widget.cpp/.h   ← PBO ping-pong render path
│       ├── render_capability.h     ← RenderPath enum, RenderProfile, CapabilityDetector
│       ├── program_widget.cpp/.h
│       ├── healing_overlay.cpp/.h
│       └── rust_bridge.cpp/.h
├── docs/
│   ├── progress.md        ← oturum günlüğü
│   ├── memory.md          ← mimari kararlar, öğrenilen dersler
│   └── superpowers/specs/ ← tasarım belgeleri
├── tests/
│   └── test_ffi_boundary.cpp
├── build/                 ← CMake çıktısı (NMake Makefiles, Debug)
│   └── src/ui/reji_app.exe
├── CMakeLists.txt
├── Cargo.toml
└── CONTEXT.md             ← bu dosya
```

---

## Mevcut Durum (2026-06-02)

### v0.2 — Tamamlandı ✓
- DXGI preview pipeline çalışıyor (AMD iGPU → staging → QImage → GL)
- GPU scan (`scan_gpus`): tüm adapter'lar listeleniyor
- `frame_held_` bug düzeltildi
- `WIN32_EXECUTABLE ON` (konsol yok)
- Debug printf'ler temizlendi
- `.gitignore` kapsamlı

### v0.3 — Başladı (2026-06-01)
- `render_capability.h`: `CapabilityDetector` + `RenderProfile`
- `pipeline.h`: `display_vendor_id()` eklendi
- `preview_widget`: PBO ping-pong implementasyonu
  - AMD (0x1002) → `kPbo` ✓ (doğrulandı, çalışıyor)
  - NVIDIA (0x10DE) → `kNvDxInterop` stub (PBO çalışır)

### v0.4 — Planlandı (2026-06-02)
- **Roadmap**: Runtime Adaptation Seviye 3, WGL_NV_DX_INTEROP real impl, Self-healing UI
- **Güçlü eklemeler**: Çoklu monitör, preview kalite, frame limiter, UI stats
- **v0.5 hazırlık**: NDI, virtual camera, OBS import (stubs)

### Build Sistemi — Ninja Geçişi Başladı (2026-06-02)
- `scripts/configure.bat` + `scripts/build.bat` oluşturuldu
- vswhere detection (Program Files / Program Files (x86))
- Ninja 1.13.2 tespit edildi ✓
- Configuration test: kernel32.lib SDK path (next session debug)
- NMake build/ existing — compatibility maintained

### CI
- `build.yml`: push/PR → build + FFI test + cargo test
- `quality.yml`: push/PR + Pazartesi 09:00 UTC → cargo audit + cppcheck + build

### Plugin Sandbox (Uzun Vadeli — 2026-06-02)
- **Extism/WASM roadmap**: v1.0 (C ABI) → v1.5 (optional) → v2.0 (mandatory)
- **Marketplace**: Ed25519 + binary scan + human review (v1.5+)
- **Auto-memory**: Extism project roadmap kaydedildi

---

## Açık Görevler

### v0.3 Devam
- [ ] Sahne yönetimi genişletme (gerçek içerikler)

### v0.4 — GPU Optimization Benchmark
- [ ] PBO performans profili: CPU overhead, GPU stall, frame timing
  - Eğer CPU darboğaz varsa → v0.5'te DXGI shared handle dene
  - Darboğaz yoksa → NV_DX_INTEROP skip et, Vulkan'a pivot

### v0.5+ — Cross-Platform GPU Interop Roadmap
- [ ] **Vulkan external memory** (`VK_KHR_external_memory_win32`)
  - Qt6 Vulkan backend entegre
  - Windows + Linux + macOS desteği
  - DXGI → Vulkan zero-copy bridge
- [ ] **macOS Metal backend** (long-term)

### Plugin Sandbox (Uzun Vadeli — v1.5+)
- [ ] **v1.0 → v1.5:** Extism/WASM opsiyonel destek, "Sandbox plugin" UI rozeti
  - Rust orchestrator'a `extism::PluginManager` entegrasyonu
  - Plugin kodu Ed25519 imzalanmalı (CLI: `reji plugin submit --sign`)
  - Güvenli marketplace başlama — binary scan + human code review workflow
- [ ] **v1.5 → v2.0:** Extism zorunlu, in-process sadece certified core plugins
  - In-process plugin loader kaldırılacak (breaking change)
  - Tüm 3. parti plugin WASM sandbox'ta çalışacak
  - Ref: https://github.com/extism/extism (WASI, 12 dil, Shopify/Discord production)

### Reliability Debt (güvenlik değil, ileride)
- [ ] `copy_fence_` → `CreateQuery` eksik, cross-adapter aktifken crash
- [ ] `wait_display_gpu_idle()` → `DXGI_ERROR_DEVICE_REMOVED` sonsuz döngü
- [ ] `pollMetrics()` → `rj_command_drain` SEH wrapper eksik

---

## Build Komutları

### Python Build Script (Önerilen — Çapraz Platform)

```cmd
cd C:\reji-studio

# Normal build (Release, reji_app)
python scripts/build.py

# Clean build
python scripts/build.py --clean

# Belirli hedef
python scripts/build.py --target reji_pipeline   # sadece pipeline
python scripts/build.py --target reji_ui         # sadece UI
python scripts/build.py --target all             # tüm

# Build + çalıştır
python scripts/build.py --run
```

**Script özellikler:**
- ✅ `vswhere` ile VS 2022/2026 algılar
- ✅ Ninja / NMake otomatik seçer (Ninja öncelikli)
- ✅ Colorized output, build süresi
- ✅ Hata reporting (log file + terminal)

### NMake Build (Eski, Yavaş — Uyumluluk için devam ediyor)

Şu an Ninja ile yan yana test ediliyor. İhtiyaç duyarsanız:

```powershell
# Manual MSVC setup
$sdk    = "C:\Program Files (x86)\Windows Kits\10"
$sdkVer = "10.0.26100.0"
$msvc   = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\14.51.36231"

$env:INCLUDE = "$msvc\include;$msvc\ATLMFC\include;$sdk\Include\$sdkVer\ucrt;$sdk\Include\$sdkVer\shared;$sdk\Include\$sdkVer\um;$sdk\Include\$sdkVer\winrt"
$env:LIB     = "$msvc\lib\x64;$sdk\Lib\$sdkVer\ucrt\x64;$sdk\Lib\$sdkVer\um\x64"
$env:PATH    = "$msvc\bin\HostX64\x64;C:\Program Files\CMake\bin;$env:PATH"

cd C:\reji-studio\build
cmake --build . --target reji_app
```

### Rust
```powershell
cd C:\reji-studio
cargo build          # debug
cargo build --release
cargo test
```

### Test çalıştırma
```powershell
# FFI sınır testi
C:\reji-studio\build\tests\test_ffi_boundary.exe

# App stderr çıktısı ile çalıştır
Start-Process C:\reji-studio\build\src\ui\reji_app.exe `
    -RedirectStandardError C:\reji-studio\run_err.log `
    -NoNewWindow -Wait:$false
Start-Sleep 5
Get-Content C:\reji-studio\run_err.log
```

---

## Mimari Temel Kararlar

### GPU Pipeline
- RTX 4070 Laptop: **ekran AMD iGPU'ya bağlı**, NVENC NVIDIA dGPU'da
- DXGI Desktop Duplication yalnızca display adapter'ında çalışır
- `GpuResourceManager`: cross-adapter SharedHandle (şu an `same_adapter_ = true`)
- `display_vendor_id()` → `gpu_scan_.entries[0].vendor_id` (display adapter)
- Preview render path: `CapabilityDetector::detect(vendor_id)` → `RenderProfile`

### Render Path
| vendor_id | GPU | Seçilen Path |
|---|---|---|
| 0x10DE | NVIDIA | `kNvDxInterop` (stub, PBO çalışır) |
| 0x1002 | AMD | `kPbo` ping-pong |
| 0x8086 | Intel | `kPbo` ping-pong |
| 0x0000 | bilinmiyor | `kPbo` ping-pong |

### FFI Sınırı
- Her `extern "C"` çağrısı `__declspec(noinline)` SEH leaf'e sarılmalı
- `rj_command_drain` dönüşü `[0, max]` arasında clamp zorunlu
- Blocking çağrı kesinlikle yasak

### PBO Ping-Pong (preview_widget.cpp)
- `write_idx = pbo_idx`, `read_idx = pbo_idx ^ 1`
- `pbo_size = w * h * 4` (img.sizeInBytes() kullanma — Qt versiyon farkı)
- Boyut değişiminde: her iki PBO orphan + `pbo_frame = 0` (guard sıfırla!)
- Frame 0 guard: `pbo_frame < 1` iken read adımı atla

---

## Qt6 Bilinen Sorunlar

```
QSplitter::setSizes() + QOpenGLWidget  → crash → setStretchFactor() kullan
statusBar()->showMessage()             → permanent widget'ları gizler → QLabel kullan
setChildrenCollapsible(false)          → crash → kaldırıldı
WIN32_EXECUTABLE ON                    → printf görünmez → stderr redirect kullan
```

---

## Kod Stili Kuralları

### C++
- Tüm public fonksiyonlar `bool` veya hata kodu döner — `void` yasak
- `shutdown()` SEH ile sarılı olmalı
- SEH bloğu içinde C++ nesnesi yasak — `__declspec(noinline)` leaf kullan
- Hot-path'de heap tahsis yasak
- Blocking FFI çağrısı yasak
- `rj_command_drain` dönüşü clamp edilmeli (`n < 0 → 0`, `n > max → max`)
- `setjmp/longjmp` yasak — Windows SEH kullan

### Rust
- `Result<T, E>` — `unwrap()` production'da yasak
- `extern "C"` fonksiyonlarda `catch_unwind` zorunlu

---

## Kritik Kurallar

1. Hot-path'de JSON yasak — struct pointer kullan
2. FFI sınırında blocking çağrı yasak
3. CRT heap kuralı — tahsis eden serbest bırakır
4. Canary `0xEEFF1234` her MetricSample'da doğrula
5. `setjmp/longjmp` yasak
6. `rj_command_drain` dönüşünü her zaman clamp et
7. `glDeleteBuffers` — PBO oluşturan her destructor'da çağrılmalı

---

## PoC Sonuçları

| PoC | Sonuç |
|---|---|
| C++/Rust FFI | ~480µs ✓ — ham C ABI + crossbeam yeterli |
| Zero-Copy GPU | NVENC session açıldı ✓ — cross-adapter v0.3'te |
| Plugin C ABI | C++ 3400ns, Rust 3182ns ✓ — C ABI kararlı |
