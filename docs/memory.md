# Reji Studio — Proje Hafızası

Mimari kararlar, öğrenilen dersler, tekrar edilmemesi gereken hatalar.
Her oturumda okunmalı.

---

## Mimari Kararlar

### GPU Pipeline
- **RTX 4070 Laptop'ta ekran AMD iGPU'ya bağlı, NVENC NVIDIA dGPU'da.**
  DXGI Desktop Duplication yalnızca AMD adapter'ında çalışır, NVIDIA'da `E_ACCESSDENIED` döner.
  Çözüm: `GpuResourceManager` AMD→NVIDIA `SharedHandle` transferi (cross-adapter path).
  Şu an: `same_adapter_ = true` hardcode (preview-only, cross-adapter aktif değil).

- **`display_vendor_id()`** `gpu_scan_.entries[0]` = display adapter (AMD iGPU).
  `entries[1]` veya sonrası = encode adapter (NVIDIA dGPU).

- **Preview render path seçimi (v0.3):**
  - `CapabilityDetector::detect(vendor_id)` → `RenderProfile{RenderPath, name}`
  - 0x10DE (NVIDIA) → `kNvDxInterop` stub (şimdilik PBO çalışır)
  - Diğer → `kPbo` ping-pong
  - `selectRenderPath()` — pipeline init sonrası bir kez çağrılır, GL thread'de

### FFI Sınırı
- **Her `extern "C"` FFI çağrısı `__declspec(noinline)` SEH leaf function'ına sarılmalı.**
  `pipeline.cpp`'de `seh_command_drain`, `seh_shutdown_subsystems` vb. mevcut.
  `main_window.cpp`'de `rj_command_drain` doğrudan çağrılıyor — SEH wrapper eksik (reliability debt).

- **`rj_command_drain` dönüşü her zaman `[0, max]`'a clamp edilmeli.**
  Güvenlik açığı: `main_window.cpp:312`'de düzeltildi (2026-06-01).
  Pattern: `if (n < 0) n = 0; else if (n > max) n = max;`

- **Blocking FFI çağrısı kesinlikle yasak.** Ring buffer üzerinden async.

### Qt6 Bilinen Sorunlar
- **`QSplitter::setSizes()` + `QOpenGLWidget`** → crash (Qt6.8 + MSVC). `setStretchFactor()` kullan.
- **`statusBar()->showMessage()`** permanent widget'ları gizler. `QLabel` kullan.
- **`setChildrenCollapsible(false)`** + `QOpenGLWidget` → crash. Kaldırıldı.
- **`WIN32_EXECUTABLE ON`** → konsol yok, `printf`/`fprintf(stderr,...)` görünmez.
  Test için: `Start-Process -RedirectStandardError err.log`

### CI / Build
- **`vcvarsall.bat` bu sistemde Windows SDK'yı otomatik bulamıyor.**
  `vswhere.exe` ve `findstr.exe` PATH'te yok.
  Çözüm: INCLUDE/LIB/PATH manuel set edilmeli (bkz. Build Komutları).

- **`&&` operatörü PowerShell'de çalışmaz.**
  GitHub Actions workflow'larında ayrı `run:` adımları kullan.

- **`quality.yml`'de `--enable=all`** çok gürültülü.
  `--enable=warning,performance,portability` kullan.
  `--suppress=missingIncludeSystem` eklenmeli.

### Güvenlik
- **`copy_fence_` (GpuResourceManager)** hiç `CreateQuery` edilmemiş, `nullptr`.
  Cross-adapter path aktif olunca null deref. Reliability debt — DoS kapsamında.

- **`wait_display_gpu_idle()` sonsuz döngü** `DXGI_ERROR_DEVICE_REMOVED` durumunda.
  Reliability debt — cross-adapter aktif olunca düzeltilmeli.

---

## Tekrar Edilmemesi Gereken Hatalar

| Hata | Neden Oldu | Çözüm |
|---|---|---|
| `setSizes()` crash | Qt6.8 + OpenGL widget uyumsuzluğu | `setStretchFactor()` kullan |
| 22 geçici dosya commit'e girdi | `.gitignore` eksikti | v0.2'de temizlendi |
| `WIN32_EXECUTABLE OFF` unutuldu | Debug için açıktı | v0.2'de ON yapıldı |
| `cargo audit` kurulum eksik | `run: cargo audit` doğrudan çalışmaz | `rustsec/audit-check@v2` action kullan |
| `n` clamp yok (FFI dönüşü) | Güvenlik gözden kaçtı | Her FFI dönüşünü clamp et |

---

## PBO Ping-Pong Detayları

```
paintGL() akışı:
  write_idx = pbo_idx
  read_idx  = pbo_idx ^ 1

  Boyut değişimi → her iki PBO orphan, pbo_frame = 0 (guard sıfırla!)
  pbo_size = w * h * 4  (img.sizeInBytes() değil — Qt versiyon farkı var)

  CPU → PBO[write]: glBufferData(data, GL_STREAM_DRAW)  ← async DMA başlar
  PBO[read] → tex:  glTexSubImage2D(nullptr)            ← GPU DMA, önceki frame

  pbo_frame < 1 → read adımı atla (boş PBO okunmaz → ilk kare bozuk olmaz)
  pbo_idx ^= 1
  pbo_frame++

Destructor: glDeleteBuffers(2, pbo)  ← unutulmamalı
```

---

## Açık Reliability Debt

Bu sorunlar güvenlik kapsamında değil, ileride düzeltilmeli:

1. `copy_fence_` asla create edilmemiş → cross-adapter aktifken crash
2. `wait_display_gpu_idle()` `DXGI_ERROR_DEVICE_REMOVED` döngüsü
3. `main_window.cpp::pollMetrics()` SEH wrapper eksik (`seh_command_drain` kullanılmalı)
4. `NV_DX_INTEROP` stub → v0.3'te gerçek `wglDXRegisterObjectNV` implementasyonu

---

## Vendor ID Referansı

| vendor_id | GPU | Render Path |
|---|---|---|
| 0x10DE | NVIDIA | `kNvDxInterop` (stub) |
| 0x1002 | AMD | `kPbo` |
| 0x8086 | Intel | `kPbo` |
| 0x0000 | Bilinmiyor / init yok | `kPbo` |

---

## Plugin Güvenliği

### In-Process Plugin Riski (v0.x)
**Sorun:** Şu an plugin'ler C ABI üzerinden in-process çalışıyor.
Hastalıklı veya kötü amaçlı plugin tüm `reji_app.exe` süreci düşürebilir (segfault, infinite loop, memory leak).

**Uzun Vadeli Çözüm:** Extism/WASM Sandbox
- **Kaynağı:** https://github.com/extism/extism
- **Özellikleri:** WASI tabanlı, 12 dil desteği (Rust, Go, Python, C, ...), production (Shopify, Discord)
- **Avantajı:** Plugin kodu process-isolated, sandbox kaçış zor, memory-safe WASM
- **Entegrasyon:** Rust orchestrator'a `extism::PluginManager`, v1.5'te opsiyonel, v2.0'de zorunlu
- **Marketplace:** Ed25519 imza + binary scan + human review (v1.5+)
