# Reji Studio — Claude Code Bağlam Dosyası

> Her oturumda bu dosyayı oku. `docs/progress.md` ve `docs/memory.md` dosyalarını da kontrol et.
> Son güncelleme: 2026-06-07

---

## Proje Kimliği

| Alan | Değer |
|---|---|
| Ad | Reji Studio |
| Tür | Açık kaynak canlı yayın yazılımı |
| Lisans | Apache 2.0 |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | `C:\reji-studio` |
| Versiyon | v0.5.1 ✅ tamamlandı |
| Donanım | AMD Radeon 780M (display/iGPU) + NVIDIA RTX 4070 Laptop (encode/dGPU) |
| OS | Windows 11 |

---

## Teknoloji Yığını

| Katman | Teknoloji | Klasör |
|---|---|---|
| Medya pipeline | C++17/MSVC | `src/pipeline/` |
| GPU interop | Vulkan 1.4 + D3D11 | `src/pipeline/gpu/` |
| FFI köprüsü | C ABI | `src/ffi/` |
| Orkestrasyon | Rust + Tokio | `src/orchestrator/` |
| Arayüz | Qt6 6.8.0 + OpenGL | `src/ui/` |
| Build | CMake + Ninja + CMakePresets.json | root |
| CI | GitHub Actions | `.github/workflows/` |

---

## Build Komutları

```cmd
# Tercih edilen (CMakePresets.json ile)
cmake --preset release
cmake --build --preset release

# Temiz build gerekirse
rmdir /S /Q build
cmake --preset release
cmake --build --preset release

# Test
cd build\src\ui
reji_app.exe 2>&1 | more

# Mock build (Vulkan donanımı olmadan)
cmake --preset mock
cmake --build --preset mock
```

**Kurallar:**
- Build: x64 Native Tools Command Prompt (MSVC environment gerekli)
- PowerShell'de build YAPMA — cstdint hatası alırsın
- Claude Code bash: `cd C:/reji-studio && cmake --build --preset release`
- Windows CMD: backslash `C:\reji-studio`

---

## Kritik Dosyalar

| Dosya | Açıklama |
|---|---|
| `src/pipeline/gpu/external_memory_bridge.h/.cpp` | D3D11↔Vulkan bridge |
| `src/pipeline/gpu/vulkan_initializer.h/.cpp` | VulkanInitializer singleton (`get()`) |
| `src/pipeline/capture/capture_dxgi.h/.cpp` | Dual texture: shared_ + staging_ |
| `src/pipeline/pipeline.cpp` | D3D11FrameCallback, lazy init, notify_vulkan_ready |
| `src/ui/preview_widget.h/.cpp` | submitD3D11Frame, setPipeline, initializeGL |
| `src/ui/main_window.cpp` | Pipeline wiring, notify_vulkan_ready |
| `src/ui/render_capability.cpp` | VulkanInitializer singleton kullanımı |
| `CMakePresets.json` | release + mock presets |
| `AGENTS.md` | AI geliştirme kuralları |

---

## Versiyon Geçmişi

### v0.5.0 ✅ — Vulkan Pivot
- OpenGL PBO'dan Vulkan render path'e geçiş
- AMD 780M paintGL: 7.6ms → 4.3ms (%43 iyileşme)
- DwmFlush kaldırıldı
- Ninja generator entegre

### v0.5.1 ✅ — D3D11↔Vulkan Zero-Copy Pipeline
- `GpuCopyOptimizer` — Vulkan compute pipeline skeleton
- `DxgiFramePacing` — QPC frequency static lambda cache
- `GpuQueryTiming` — GPU timing skeleton
- `ExternalMemoryBridge` — D3D11 NT handle export + VkImage import
- Dual texture: `shared_texture_` (DEFAULT+SHARED) + `staging_texture_` (STAGING)
- `VulkanInitializer` singleton — `get()` metodu
- `notify_vulkan_ready()` — render_capability.cpp → pipeline bridge
- **Sonuç:** VkImage'lar oluşuyor, frame'ler GPU'da aktarılıyor ✅

```
[ExternalMemoryBridge] Created Vulkan image from D3D11 handle ✅
[Pipeline] get_frame_images: staging=0x... target=0x... ✅
```

---

## Mevcut Durum (2026-06-09)

### Çalışan ✅
- D3D11 → NT handle export → VkImage import (zero-copy)
- VulkanInitializer singleton — render_capability.cpp üzerinden init
- ExternalMemoryBridge::get_frame_images() — round-robin pool (POOL_SIZE=3)
- Pipeline → PreviewWidget callback zinciri
- CMakePresets.json — `cmake --preset release`
- Git repack kapalı — `gc.auto=0`
- Build sistemi: `python scripts/build.py` (cmd.exe + vcvars64.bat otomatik)

### Root Cause Found & Fixed ✅
- **VK_ERROR_DEVICE_LOST原因:** VK_IMAGE_USAGE_TRANSFER_SRC_BIT eksikti
  - D3D11 staging images (image_pool_) source olarak vkCmdBlitImage'e gidiyor
  - Ama creation flags'e TRANSFER_SRC_BIT eklenmemişti
  - Vulkan spec violation → device lost
  - **FIX:** external_memory_bridge.cpp:94 → flags += TRANSFER_SRC_BIT
  - Yeni flags: TRANSFER_SRC_BIT | TRANSFER_DST_BIT | COLOR_ATTACHMENT_BIT

### GL Interop Bridge Wiring ✅ (v0.5.2 Step 4)
- **Pipeline accessor:** Pipeline::get_external_memory_bridge() (pipeline.cpp:717-720)
- **Main window wiring:** preview_widget_->setBridge() after copy_optimizer init (main_window.cpp:75-76)
- **PreviewWidget:** GL extension function pointer resolution in initializeGL() (preview_widget.cpp:179-202)
- **paintGL interop:** NT handle import + GL texture creation (preview_widget.cpp:290-318)
- **Render path:** Prefer gl_interop_texture, fallback to d_->tex_id (preview_widget.cpp:335)

### Stub / Eksik ❌
- NVENC encoder — SDK yok, preview-only mode
- SRT output — stub
- Build environment — CMake MSVC include paths from bash/PowerShell (x64 Native Tools required)
- FrameProfiler benchmark — No samples collected

---

## Sonraki Milestone: v0.5.2

### Öncelik 1 (Kritik)
- [ ] `paintGL` içinde VkImage → OpenGL texture transfer
  - `VK_KHR_external_memory` + GL interop extension
  - Hedef: frame'lerin ekranda görünmesi
- [ ] FrameProfiler benchmark — copy p50 hedef <1ms

### Öncelik 2
- [ ] cbindgen entegrasyonu — ABI güvenliği, manuel senkronizasyon riski giderilecek
- [ ] `static_assert` ABI kontrolleri otomatikleştirilecek

### Öncelik 3 (v0.6+)
- [ ] reji-bridge-guard kütüphanesi — FFI proc macro, `#[reji_ffi_boundary]`
- [ ] reji-zero-copy kütüphanesi — ExternalMemoryBridge izole crate
- [ ] reji-sys-metrics kütüphanesi — MetricsCollector Rust crate

### Uzun Vade (v1.0+)
- [ ] NVENC gerçek implementasyon
- [ ] SRT gerçek implementasyon
- [ ] NDI entegrasyonu
- [ ] WASM plugin sandbox (Extism)

---

## Mimari: GPU Pipeline

```
AMD 780M (display)          NVIDIA RTX 4070 (encode)
DXGI Desktop Duplication
    ↓
ID3D11Texture2D (captured)
    ├─ CopyResource → shared_texture_ (DEFAULT+SHARED_NTHANDLE)
    │       ↓
    │   CreateSharedHandle → NT handle
    │       ↓
    │   VkImportMemoryWin32HandleInfoKHR
    │       ↓
    │   VkImage (zero-copy) → ExternalMemoryBridge pool
    │       ↓
    │   d3d11_frame_cb → PreviewWidget::submitD3D11Frame
    │
    └─ CopyResource → staging_texture_ (STAGING+CPU_ACCESS_READ)
            ↓
        map_preview_frame() → CPU preview (fallback)
```

---

## Mimari: VulkanInitializer Singleton

```
render_capability.cpp
    VulkanInitializer* vk = VulkanInitializer::get()  ← singleton
    vk->initialize()  ← Vulkan init + device oluşturma
    // shutdown() ÇAĞIRMA — singleton ömür boyu yaşıyor

main_window.cpp
    pipeline_.notify_vulkan_ready(vk->device(), vk->physical_device())
        ↓
    ExternalMemoryBridge::set_device(device, phys_device)
```

**Önemli:** `render_capability.cpp`'de local `VulkanInitializer vk_init` KULLANMA.
Singleton `get()` kullan, `shutdown()` çağırma.

---

## Bilinen Sorunlar / Teknik Borç

| Sorun | Risk | Versiyon |
|---|---|---|
| Manuel ABI senkronizasyonu (cbindgen yok) | Silent corruption | v0.5.2 |
| SEH + Rust panic aynı process | Teorik crash | v0.6 |
| main_window.cpp SEH wrapper eksik | Reliability | v0.6 |
| VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT — cross-adapter test yok | Potansiyel crash | v0.5.2 |
| notify_vulkan_ready duplicate blok — main_window.cpp satır 63+72 | Harmless, temizlenmeli | v0.5.2 |

---

## Kullanım Alışkanlıkları

### AI Araç Kullanımı
- **Claude Code CLI** — kod yazma, commit (build ortamı sorunu var, cmake çalıştıramıyor)
- **Aider + MiniMax M3** — basit değişiklikler, ucuz ($0.01/mesaj)
- **Aider + Sonnet** — kritik FFI/Vulkan değişiklikleri
- **Fusion** — mimari analiz, multi-model karşılaştırma
- **Manuel Python script** — Claude Code/Aider uygulamadığında dosya patch'leme

### Build Akışı
1. Claude Code / Aider ile kod değişikliği + commit
2. x64 Native Tools Command Prompt'ta `cmake --build --preset release`
3. `reji_app.exe 2>&1 | more` ile test
4. Log'da beklenen satırları kontrol et

### Sık Karşılaşılan Sorunlar
- **Aider dosyaya yazmadı** → `git diff` ile kontrol et, Python patch script kullan
- **Claude Code build edemedi** → x64 Native Tools'ta manuel build
- **Git pack permission** → `git config gc.auto 0` (kalıcı çözüm uygulandı)
- **Build dizini bozuldu** → `rmdir /S /Q build && cmake --preset release`
- **PowerShell build hatası** → x64 Native Tools CMD kullan (cstdint sorunu)

### Patch Script Şablonu
```python
# scripts/patch_XXX.py
content = open('src/ui/XXX.cpp', 'r', encoding='utf-8').read()
old = 'eski kod'
new = 'yeni kod'
result = content.replace(old, new, 1)
print('Changed:', content != result)
open('src/ui/XXX.cpp', 'w', encoding='utf-8').write(result)
```

---

## Kod Kuralları

### C++
- Tüm public fonksiyonlar `bool` döner — `void` yasak
- Hot-path'de heap allocation yasak
- Hot-path'de blocking call yasak (`vkWaitForFences`, `vkDeviceWaitIdle` yasak)
- SEH bloğu içinde C++ nesnesi yasak — `__declspec(noinline)` leaf kullan
- `rj_command_drain` dönüşü `[0, max]` clamp zorunlu

### Rust
- `unwrap()` production'da yasak
- `extern "C"` fonksiyonlarda `catch_unwind` zorunlu

### Git
- Commit mesajı: `feat:`, `fix:`, `refactor:`, `docs:`, `build:` prefix
- Her task sonrası commit + push
- Versiyon tamamlandığında tag: `git tag -a v0.X.Y -m "..."`

---

## Yol Haritası Özeti

```
v0.5.1 ✅  D3D11↔Vulkan zero-copy pipeline
v0.5.2     paintGL VkImage→GL transfer, cbindgen, benchmark
v0.6.0     reji-bridge-guard (FFI macro), SEH cleanup
v0.7.0     reji-zero-copy (izole crate), NVENC gerçek impl
v0.8.0     reji-sys-metrics (Rust crate), SRT gerçek impl
v1.0.0     NDI, multi-monitor, WASM plugin sandbox
v2.0.0     Stream Deck, OBS entegrasyon, Extism zorunlu
```

