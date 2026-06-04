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

## Kronik Sorun Çözüm Yol Haritası (MiniMax M3 + Gemini 2.5 Pro Analizi)

Aider multi-model analizi (MiniMax M3 $0.01/1M tokens, Gemini 2.5 Pro $0.05/1M) ile tanımlanan sistem-çapı sorunlar ve çözüm roadmap'ı.

### Öncelik 1 — Hemen (v0.5.1)

**CMake Generator Determinizm & CI Guard**

- [ ] **CMakePresets.json ekle** (`build-release` preset → Ninja zorunlu)
  - Rationale: NMake vs Ninja generator karışıklığı, PowerShell vs x64 Native Tools environment varyans
  - File: `CMakePresets.json` (root)
  - Content: `generator: Ninja`, `cacheVariables: {CMAKE_BUILD_TYPE: Release}`
  - CI: `.github/workflows/build.yml` → `cmake --preset build-release` (explicit)

- [ ] **scripts/build.py fail-fast generator kontrolü**
  - Current: vswhere + Ninja detection, ama fallback muhtemel
  - Add: `ninja --version` || error "Ninja not found, install: choco install ninja"
  - Add: Environment var check `set CMAKE_GENERATOR=Ninja` (Windows batch setup)

- [ ] **CI Guard: Visual Studio generator tespit → build break**
  - Problem: CMake "NMake Makefiles"'ı default seçebilir (slow, old)
  - Add to CMakeLists.txt:
    ```cmake
    if(CMAKE_GENERATOR MATCHES "Visual Studio|NMake")
      message(FATAL_ERROR "Unsupported generator: ${CMAKE_GENERATOR}. Use 'Ninja' (cmake -G Ninja)")
    endif()
    ```
  - Impact: PR check, Windows SDK env issue'sini erken yakalar

### Öncelik 2 — v0.5.2

**Rust ↔ C++ FFI Otomasyonu & Versioning**

- [ ] **cbindgen entegrasyonu** (Rust struct → C header auto-generate)
  - Current: RjFrameStats, RjCommand vb. manually sync (error-prone)
  - Tool: `cbindgen` (Mozilla), Cargo build step
  - Add to `Cargo.toml`:
    ```toml
    [build]
    cbindgen = "0.27"
    ```
  - Output: `src/ffi/ffi_auto_generated.h` (read-only, git-ignored)
  - Benefit: Struct field mismatch compile-error, no runtime ABI silent corruption

- [ ] **FFI ABI Runtime Selftest** (offset assert)
  - Add to `src/ffi/ffi_bridge.c`:
    ```cpp
    // Static assert: C struct layout matches Rust
    static_assert(sizeof(RjFrameStats) == sizeof(RjFrameStatsRust), "ABI mismatch");
    static_assert(offsetof(RjFrameStats, copy_gpu_time_ms) == 16, "Field offset changed");
    ```
  - Rationale: struct padding changes (C vs Rust repr) → silent field misalignment
  - Detection: FFI call returns garbage, no crash (subtle bug)
  - CI: Link-time error if offset changes (fail-safe)

- [ ] **Wire Format Versioning** (future-proof protocol)
  - Add version byte to RjFrameStats:
    ```cpp
    struct RjFrameStats {
      uint8_t version;  // = 1, increment if struct changes
      uint8_t _reserved[7];
      // ... fields ...
    };
    ```
  - Receiver checks: `if (version != EXPECTED_VERSION) { error; return; }`
  - Benefit: v0.5.2 → v1.0 FFI break doesn't crash, logs error instead

### Öncelik 3 — v1.0 öncesi

**Compiler & Language Flags Enforcement**

- [ ] **Global /EHa (SEH + C++ exceptions) enforce + C4577 error**
  - Problem: copy_optimizer.cpp `__try/__except` bazı compile path'lerinde disabled
  - Solution: CMakeLists.txt root'ta `add_compile_options(/EHa /we4577)`
  - Impact: SEH syntax check her dosyada, no silent disabling

- [ ] **Rust Panic Handling: -C panic=abort** (Cargo profile)
  - Problem: Rust panic default = unwind, C++ try/catch confuse
  - Add to Cargo.toml:
    ```toml
    [profile.release]
    panic = "abort"
    
    [profile.dev]
    panic = "abort"
    ```
  - Benefit: panic → abort (fast), no unwind across FFI boundary

- [ ] **preview_widget.h: #ifdef QT6_AVAILABLE kaldır, final ekle**
  - Problem: Qt version guard = unused code path (dead code risk)
  - Current: `#ifdef QT6_AVAILABLE ... #else ... #endif` (dual path)
  - Remove: Qt6 hardcoded (require Qt6.8+), conditional code gone
  - Add: `class PreviewWidget final : public QOpenGLWidget`
  - Benefit: vtable seal, no accidental override, ABI stable

---

## Vendor ID Referansı

| vendor_id | GPU | Render Path |
|---|---|---|
| 0x10DE | NVIDIA | `kNvDxInterop` (stub) |
| 0x1002 | AMD | `kPbo` |
| 0x8086 | Intel | `kPbo` |
| 0x0000 | Bilinmiyor / init yok | `kPbo` |

---

## Self-Healing Modları

Reji Studio'da dört self-healing davranış modu mevcuttur. Kullanıcı rolüne ve tercihine göre seçilir.

| Kullanıcı modu | Self-healing modu | Davranış |
|---|---|---|
| **Başlangıç** | Auto-Pilot | Kritik + Orta önem aksiyonlar tam otomatik gerçekleşir, bildirim gönderilir |
| **Standart** | Co-Pilot | Aksiyonlar kullanıcı onayına sunulur — checkbox listesiyle seçim, seçilmeyenler bildirim gösterir |
| **Uzman** | Assist | Kritik otomatik, orta/düşük aksiyonlar log + bildirim (onay gerekmez) |
| **Uzman** | Manual | Açılışta tek seferlik uyarı dialog, self-healing tamamen devre dışı, sadece log |

**Co-Pilot Aksiyon Örnekleri:**
- Bitrate otomatik düşür (frame drop % > 10)
- Kaynak yeniden bağlan (timeout/disconnect)
- Çözünürlük düşür (GPU stall)
- Encode kalitesi değiştir (thermal throttle)

**Implementasyon:** `src/ui/healing_overlay.cpp` + `src/orchestrator/metrics.rs::AdaptationDecider`

---

## Karar Motoru — 6 Seviye Yol Haritası

Reji Studio'nun donanım, ağ ve bağlam koşullarına göre otomatik uyum sağlama yeteneği.
DeviceProfiler → RuleEngine (Rust) → ActionDispatcher → HealingOverlay (UI)

| Seviye | Versiyon | Kapasite | Detay |
|---|---|---|---|
| **1** | v0.2 ✅ | Hardware Discovery | GPU vendor, VRAM, D3D11 feature level; `CapabilityDetector`, `RenderProfile` |
| **2** | v0.3 ✅ | Capability Detection | OpenGL extensions, render path seçimi (AMD→PBO, NVIDIA→NV_DX_INTEROP stub) |
| **3** | v0.4 | Runtime Adaptation | Frame drop → bitrate düşür; GPU/CPU sıcaklık → kalite ayarı; RTT/jitter izleme; pil durumu; bellek baskısı; disk I/O |
| **4** | v0.5 | Context Awareness | Saat bazlı profil (sabah test, akşam canlı); izleyici sayısı adapt; platform limitleri (Twitch 6000, YouTube 8000); sahne bazlı optimizasyon |
| **5** | v1.0 | Learning System | SQLite oturum geçmişi analizi; donanım yaşlanma tespiti; başarılı config hafızası; anomali tespiti & auto-diagnosis |
| **6** | v2.0 | External Integration | Stream Deck/Loupedeck fiziksel kontrol; OBS/vMix köprüsü; bulut profil sync; webhook (Discord/Slack) |

### Seviye 3 (v0.4) Detayları

**Uyum Mekanizmaları:**
- Frame drop > eşik → bitrate otomatik düşür/yükselt
- GPU sıcaklık > 85°C → encode kalite aşağı çek (full → half → quarter res)
- CPU load > 90% → preview kalite düşür, frame cap (60→30fps)
- Ağ metrikleri (RTT, jitter, paket kaybı) → codec/bitrate ayarı
- Laptop pil modu → CPU/GPU işleri azalt, preview FPS cap
- Disk I/O yüksek (kayıt modu) → buffer size artır, frame skip tolerance

**Impl:** `src/orchestrator/rules.rs` (rule engine), `src/orchestrator/metrics.rs` (AdaptationDecider genişlet)

### Seviye 4 (v0.5) Detayları

**Context-Aware Profiller:**
- Saat: 6-12 → "Morning Test" (low bitrate, high quality)
- Saat: 18-23 → "Evening Live" (high bitrate, motion optimization)
- İzleyici sayısı: 0-10 → "Low", 10-100 → "Medium", 100+ → "High"
- Platform limit: Twitch 6000 kbps max, YouTube 8000 kbps, custom RTMP özel sınır
- Sahne content type: statik/PowerPoint → intra frame artırma; hareketli/kamera → motion estimation

**Impl:** Time-based scheduler, viewer count API poller, platform detection, scene analyzer

### Seviye 5 (v1.0) Detayları

**Öğrenme Sistemi:**
- Her oturum: bitrate, FPS, GPU temp, frame drop → SQLite kaydı
- Trend: 7 gün ortalama, anomali flag (sudden temp spike, frame drop burst)
- Başarılı config: "RTX 4070 + 1080p60 @ 6000kbps, avg temp 72°C" → hafızaya al
- Benzer hardware görüldüğünde otomatik preset uygula
- Anomali: GPU temp anormal yükseliş → "thermal paste degradation?" hint

**Impl:** `src/pipeline/metrics_recorder.cpp` (SQLite), `src/orchestrator/learning.rs` (pattern match)

### Seviye 6 (v2.0) Detayları

**Dış Sistem Entegrasyonu:**
- **Fiziksel Kontrol:** Stream Deck "Bitrate up/down" butonları, Loupedeck fader → bitrate smooth interpolation
- **OBS Köprüsü:** OBS scene switch → Reji profil otomatik sync (encoder settings match)
- **Bulut:** Optional user cloud profile (GitHub gist veya custom server)
- **Webhook:** Critical event → Discord DM ("GPU throttle detected, switching to backup camera")

**Impl:** Named Pipe IPC server, OBS WebSocket client, optional cloud sync, webhook dispatcher

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

---

## Architectural Decision: v0.5 Vulkan Pivot

### Karar: OpenGL → Vulkan Migration

**İçerik:**
Reji Studio'nun preview render path'ini OpenGL'den Vulkan'a geçiş yapmak.

**Sebep:**
1. **DwmFlush Race Condition:** AMD iGPU (display) + NVIDIA dGPU (encode) yapılandırmasında, DXGI Present'ın DWM compositor'a sinkronizasyon sorunu:
   - paintGL(): `glFinish()` (GL queue'sini bekle) → `DwmFlush()` (DWM blit bariyeri)
   - Şu an: 7.6ms latency, frame drop recovery > 15s
   - Neden: DwmFlush() DWM compositor'ın bir önceki frame'i bitirinceye kadar bekler (DX12 sync yok)

2. **OpenGL Deprecated:** Qt6'da OpenGL modern değil, Vulkan/Metal/D3D12 öneriliyor
   - Qt6 QRhi (Rendering Hardware Interface) native Vulkan desteği
   - OpenGL validation warnings GitHub Actions'ta görülüyor

3. **Zero-Copy GPU Interop:** Vulkan `VK_KHR_external_memory_win32`
   - D3D11 texture (DXGI capture) → Windows handle → Vulkan texture
   - No staging copy, no DwmFlush, no serialize
   - Expected: 7.6ms → <2ms (3.8x speedup)

**Çözüm Mimarisi:**
```
DXGI Capture (D3D11)
  ↓ (Windows SharedHandle)
Vulkan External Memory
  ↓ (No sync needed)
QVulkanWindow / QRhi (Vulkan backend)
  ↓
DwmCompositor (modern DXGI present)
```

**Uygulama Plan (v0.5):**
1. `src/pipeline/include/vulkan_interop.h` — External memory wrapper
2. `src/ui/preview_widget.cpp` — Vulkan render path (QRhi)
3. `src/ui/render_capability.h` — RenderPath enum: kVulkanDirect (new)
4. Qt6 Vulkan module depend, glslang (SPIR-V)
5. Shader cache: ~/.reji/shader_cache/ (startup perf)
6. Validation layers: debug mode enabled, release mode disabled

**Fallback Strategy:**
- Qt6 Vulkan unavailable → OpenGL PBO fallback (v0.4 compat)
- Vulkan device init fail → OpenGL fallback
- Runtime selection: CapabilityDetector → best available path

**Testing:**
- AMD iGPU (0x1002) + Vulkan: multi-adapter interop
- NVIDIA dGPU (0x10DE) + Vulkan: timestamp querying
- Frame timing: FrameProfiler v2.0 (GPU-side timestamps)

**Timeline:** v0.5 (2026-H2), 20 days estimate
**Risk:** Vulkan SDK complexity, platform-specific bugs, validation performance
**Mitigation:** Feature branch, thorough testing, OpenGL fallback always available

---

## Architectural Decision: Multi-Monitor Support (v0.5)

**İçerik:** Per-monitor capture seçeneği ve DXGI output enumeration

**Sebep:**
- Şu an: hardcoded primary adapter (AMD iGPU)
- Gerçek yayıncılar: 2-4 monitor (primary + secondary), selective capture ister
- DXGI: EnumOutputs() loop ile tüm adapter'lar bulunabilir

**Uygulama:**
1. UI: dropdown → "Monitor seç" (default: primary)
2. Capture: per-monitor thread pool (concurrent capture)
3. Encode: single encoder (monitor switch = input source change)

**Impact:** Minor, istenirse skip edilebilir (v0.6'ya ertele)

---

## Architectural Decision: NDI/Virtual Camera Stubs (v0.5)

**İçerik:** Newtek NDI output + DirectShow virtual camera stub implementations

**Sebep:**
- v1.0 planning: OBS integration, Teams/Zoom virtualization
- Şu an: stubs only (/tmp/ dump files)
- Allows: integration test framework, early adoption feedback

**Uygulama:**
1. `src/pipeline/output/ndi_output_stub.cpp` — write frames to /tmp/ndi_stub.raw
2. `src/pipeline/output/virtual_camera_stub.cpp` — register fake DirectShow device
3. No real NDI SDK dependency (v0.5)
4. v1.0: real impl (if user demand validates)

**Impact:** Zero on core paths, testing infrastructure only
