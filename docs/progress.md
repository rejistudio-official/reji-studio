# Reji Studio — Oturum İlerleme Günlüğü

---

## Oturum: 2026-06-02 (v0.3 tamamlandı, v0.4 planı)

### v0.3 Tamamlandı ✅ — tag: v0.3

**Commit geçmişi (bu oturum, v0.3 kapsamı):**

```
fab4111  feat(ui): sahne yönetimi — rename, add, remove (v0.3)
310aeb9  fix(build): vswhere + cmake discovery, tek cmd.exe session
add2336  chore: reji-build-tools Python build script — cross-platform wrapper
1d7328d  decision: NV_DX_INTEROP skip (v0.3), benchmark → Vulkan pivot (v0.5)
4fc4e14  docs: Oturum 2026-06-02 teknik düzeltmeleri ve optimizasyonlar
```

**v0.3 kapsamı özeti:**
- `render_capability.h`: `RenderPath` enum, `RenderProfile`, `CapabilityDetector`
- `pipeline.h/cpp`: `display_vendor_id()` eklendi
- `preview_widget`: PBO ping-pong (AMD/Intel ✅, NVIDIA PBO fallback)
- `preview_widget`: BGRA→GL_BGRA renk düzeltmesi, QImage dönüşümü kaldırıldı
- `main_window`: Sahne listesi — rename (F2), ekleme (+), silme (−), drag-drop
- `scripts/build.py`: Cross-platform build wrapper (VS 18/22 auto-detect)
- **NV_DX_INTEROP kararı:** skip → v0.5 Vulkan external memory

**Tag:** `v0.3` → `github.com/rejistudio-official/reji-studio/releases/tag/v0.3`

---

## v0.4 GPU Frame Timing Benchmark (2026-06-02 — v0.4 başlangıcı)

### Tamamlananlar ✅

#### FrameProfiler Module Implementasyonu
- `src/pipeline/include/frame_profiler.h` / `frame_profiler.cpp`: 3-phase timing instrumentation
- Thread-safe measurements (`std::mutex`, `std::lock_guard`)
- Percentile statistics: p50, p95, max (independent per phase)
- Automatic finalization with stderr logging

#### Instrumentation Integration
- **DXGI Acquire:** `DxgiCaptureSession::acquire()` → `markAcquireStart/End`
- **CPU Copy:** `PreviewWidget::uploadFrame()` → `markCopyStart/End`
- **PaintGL:** `PreviewWidget::paintGL()` → `markPaintGLStart/End` (around `glFinish()` + `DwmFlush()`)
- **Pipeline Propagation:** `DxgiCapturePipeline::setProfiler()` → all subsystems

#### Build & CI/CD Updates
- CMake: FetchContent for Google Test v1.14.0 (auto-download/build)
- `.github/workflows/build.yml`:
  - Added Rust orchestrator build (before CMake)
  - Added Rust lib verification step
  - Added `--verbose` flag to cargo for full diagnostics
- `.github/workflows/quality.yml`:
  - Cargo audit via CLI (`--locked` mode)
  - cppcheck via PowerShell with `continue-on-error: true`
  - Separated install and run steps

#### Code Quality
- Removed debug fprintf statements:
  - `preview_widget.cpp` initializeGL START log
  - `preview_widget.cpp` paintGL frame entry log
  - `main_window.cpp` profiler ptr debug output
- Header-CPP sync: forward declarations, method exports, lazy buffer init

### Benchmark Status
- **Code Implementation:** ✅ Complete
- **Unit Tests:** ✅ Passing (3/3: basic marking, percentile calculation, missing marks graceful)
- **Integration Test:** ⏸️ Blocked (Windows SDK: mt.exe, rc.exe, kernel32.lib missing)
  - Not a code issue — external build environment limitation
  - FrameProfiler logic verified via unit tests
  - Instrumentation points validated in code review
- **Next Steps:** Profiler output logs will show during v0.5 benchmarking

### Architecture
```
Pipeline init()
  ├─ profiler = std::make_unique<FrameProfiler>()
  ├─ capture_->setProfiler(profiler.get())    [DxgiCaptureSession]
  └─ preview_widget->setProfiler(profiler.get())
     ├─ markAcquireStart/End (DXGI frame capture)
     ├─ markCopyStart/End (BGRA buffer copy)
     └─ markPaintGLStart/End (GPU render + DwmFlush + glFinish)

Pipeline shutdown()
  └─ profiler->finalize()
     └─ [stderr] p50/p95/max microseconds per phase
```

### Commits (v0.4)
```
5a5505b  feat: propagate FrameProfiler to DxgiCaptureSession
06118ad  refactor: move PBO initialization to paintGL lazy init
94e4735  refactor: add renderPath() getter and render_capability.h include
1038d07  feat: add selectRenderPath() and setProfiler() methods
41010d5  refactor: add forward declaration for FrameProfiler
[... earlier v0.4 commits ...]
```

---

## Karar Motoru — 6 Seviye Yol Haritası (Mimari)

Reji Studio'nun donanım, ağ ve bağlam koşullarına göre otomatik uyum sağlama mimarisi.
`DeviceProfiler → RuleEngine (Rust) → ActionDispatcher → HealingOverlay (UI)`

| Seviye | Versiyon | Kapasite | Durum |
|---|---|---|---|
| **1** | v0.2 ✅ | Hardware Discovery | GPU vendor, VRAM, D3D11 feature level |
| **2** | v0.3 ✅ | Capability Detection | OpenGL extensions, render path seçimi |
| **3** | v0.4 | Runtime Adaptation | Frame drop, sıcaklık, ağ, pil, bellek, disk |
| **4** | v0.5 | Context Awareness | Saat, izleyici, platform, sahne optimizasyonu |
| **5** | v1.0 | Learning System | Oturum analizi, hardware yaşlanma, anomali tespiti |
| **6** | v2.0 | External Integration | Stream Deck, OBS/vMix, bulut, webhook |

**Detaylar:** Bkz. `docs/memory.md` / Karar Motoru Bölümü

---

## v0.4 Açık Görevler

| # | Görev | Dosya / Modül | Öncelik |
|---|---|---|---|
| 1 | **GPU performance benchmark** — PBO CPU overhead, frame timing profili | `src/ui/preview_widget.cpp` | Yüksek |
| 2 | **Runtime Adaptation Seviye 3** — frame drop %, sıcaklık, ağ metrikleri → adaptasyon | `src/orchestrator/metrics.rs::AdaptationDecider` | Yüksek |
| 3 | **Self-Healing UI bağlantısı** — HealingOverlay + Dört mod (Auto-Pilot/Co-Pilot/Assist/Manual) | `src/ui/healing_overlay.cpp`, `src/orchestrator/metrics.rs` | Yüksek |
| 4 | **Çoklu monitör desteği** — `EnumOutputs()` dropdown, her monitör bağımsız capture | `src/pipeline/capture/capture_dxgi.cpp` | Orta |
| 5 | **Frame rate limiter** — preview 30fps cap, encode 60fps cap (ayrı thread'ler) | `src/pipeline/frame_limiter.h` (yeni) | Orta |
| 6 | **Bitrate/frame drop/GPU temp göstergeleri** — real-time graph (30s), UI stats widget | `src/ui/stats_widget.cpp` (yeni) | Orta |

---

## Oturum: 2026-06-02 (v0.4 planı + Ninja build geçişi)

### Tamamlananlar

#### Yol Haritası & Planlama
- **Extism/WASM Plugin Sandbox Yol Haritası** eklendi
  - v1.0: C ABI in-process + Ed25519 imza
  - v1.5: Extism opsiyonel, "Sandbox plugin" rozeti, marketplace başlar
  - v2.0: Extism zorunlu, in-process sadece certified core plugins
  - Ref: github.com/extism/extism (WASI, 12 dil, Shopify/Discord production)

- **v0.4 Planı** kapsamlı tasarlandı:
  - Zorunlu: Runtime Adaptation Seviye 3, WGL_NV_DX_INTEROP real impl, Self-healing UI
  - Güçlü: Çoklu monitör, preview kalite seçimi, frame limiter, UI stats
  - Teknik Borç: strncpy→strncpy_s, QMenu deprecated, /EHs /EHa, Ninja, build.bat

- **v0.5 Hazırlığı** başlandı (stubs):
  - NDI output, virtual camera, OBS scene import

#### Build Sistemi
- **Ninja build sistemine kademeli geçiş başlatıldı**
  - `scripts/configure.bat`: vswhere detection, cmake -G Ninja setup
  - `scripts/build.bat`: parallel build (-j 8), otomatik MSVC environment
  - VS 2024 Ninja path: `Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja`
  - vswhere fallback: Program Files (x86) support
  - Durum: NMake compat ✓, Ninja configuration test (SDK linker debug needed)

#### Teknik Düzeltmeler & Optimizasyonlar
- **MSVC Compatibility** (strncpy → strncpy_s)
  - `src/pipeline/pipeline.cpp:394` — std::strncpy → strncpy_s conversion
  - MSVC /W4 secure CRT function

- **Qt6 Modern API** (deprecated QMenu::addAction)
  - `src/ui/main_window.cpp` — deprecated addAction signature'ları modernize
  - File menu: New, Open, Save, Quit
  - View menu: Full Screen
  - Help menu: About
  - Explicit signal/slot connections, forward compatible

- **Preview Color Corruption Fix** (BGRA→RGBA)
  - DXGI BGRA formatı doğrudan GPU'ya upload
  - `src/ui/preview_widget.cpp:161` — QImage::Format_BGRA8888 kullan
  - `glTexImage2D/SubImage2D` — GL_BGRA format (GL_RGBA değil)
  - Red/Blue channel swap sorununu çözdü ✓ Renkler doğru

- **Preview Performance Optimization** (QImage dönüşümü kaldır)
  - `src/ui/preview_widget.cpp` — uploadFrame() QImage removed
  - Raw BGRA buffer copy (row_pitch'i dikkate alarak)
  - Direct glTexImage2D/glTexSubImage2D upload
  - Hot-path hız artışı, memory allocation pattern basitleşti

#### Dokumentasyon
- docs/progress.md: Plugin roadmap, v0.4 plan, v0.5 prep, tech debt
- CONTEXT.md: Build komutları güncellendi (scripts/build.bat), plugin sandbox görevler
- AGENTS.md: Build komutları Ninja-centric, NMake legacy note
- docs/memory.md: Plugin güvenliği, Extism long-term çözüm
- Auto-memory: Extism/WASM roadmap project memory kaydedildi

#### Test & Verification
- Build: `cmake --build build --target reji_app` ✓ Success
- Runtime: reji_app.exe 3+ saniye test ✓ No crashes
- PreviewWidget: Shader compile OK, pipeline init OK 1920x1080@60fps
- Preview colors: BGRA correctly mapped ✓ No color corruption

### Commit Geçmişi (bu oturum)

```
4a86042  perf: preview_widget uploadFrame() QImage dönüşümü kaldır, doğrudan BGRA upload
7e489a2  fix: preview_widget.cpp BGRA→RGBA renk bozukluğu düzelt
db06653  fix: main_window.cpp deprecated QMenu::addAction → Qt6 uyumlu API
55f46d8  fix: pipeline.cpp strncpy → strncpy_s (MSVC /W4 uyumluluk)
f94cb4f  chore: Ninja build sistemi geçişi başlat, build scripts ekle
492f3d9  docs: v0.4 planı, v0.5 hazırlığı, teknik borç listesi ekle
c38c139  docs: Extism/WASM plugin sandbox yol haritası ekle (v1.0→v1.5→v2.0)
```

---

## Oturum: 2026-06-01 (v0.2 tamamlama + v0.3 başlangıcı)

### Tamamlananlar

#### Pipeline & Yakalama
- `reji_app.exe` çalıştırıldı, `run.log`'da "First frame" ve "preview" doğrulandı
- `capture_dxgi.cpp` — 5 debug printf grubu kaldırıldı:
  `acquire: hr=`, `acquire: WAIT_TIMEOUT`, `AcquireNextFrame failed`, `acquire: texture OK`, `transfer() OK`
- `WIN32_EXECUTABLE OFF → ON` (CMakeLists.txt) — konsol penceresi kapatıldı

#### Repo Hijyeni
- `.gitignore` genişletildi: `*.log`, `*.png`, `*.tmp`, `*.obj`, `*.ps1`, `/pipeline.cpp`, `fix_*.*`
- 22 geçici dosya `git rm --cached` ile index'ten çıkarıldı

#### CI / Kalite
- `.github/workflows/quality.yml` oluşturuldu:
  - `cargo audit` → `rustsec/audit-check@v2`
  - `cppcheck --enable=warning,performance,portability`
  - CMake build doğrulaması
- Haftalık zamanlama eklendi: Her Pazartesi 09:00 UTC

#### Güvenlik
- `src/pipeline/` güvenlik incelemesi yapıldı (6 aday, 1 doğrulandı)
- **Düzeltildi:** `main_window.cpp:312` — `rj_command_drain` dönüşü clamp edilmedi
  - `n < 0 → 0`, `n > 8 → 8` eklendi (pipeline.cpp'deki pattern ile tutarlı)

#### PreviewWidget Render Path Seçimi (v0.3 başlangıcı)
- `render_capability.h` oluşturuldu: `RenderPath` enum, `RenderProfile`, `CapabilityDetector`
- `pipeline.h` + `pipeline.cpp`: `display_vendor_id()` eklendi
- `preview_widget`: PBO ping-pong implementasyonu
  - `initializeGL()`: `glGenBuffers(2, pbo)`
  - `paintGL()`: write_idx / read_idx ping-pong, frame 0 guard, boyut değişimi orphan
  - `~PreviewWidget()`: `glDeleteBuffers(2, pbo)`
- `main_window.cpp`: `pipeline_.init()` sonrası `selectRenderPath(display_vendor_id())`
- `docs/superpowers/specs/2026-06-01-preview-render-path-design.md` spec belgesi

### Commit Geçmişi (bu oturum)

```
7518881  feat: PreviewWidget PBO ping-pong + render path secimi (CapabilityDetector)
9102dd8  docs: preview render path secimi tasarim belgesi ekle
a4b8c48  fix(security): pollMetrics rj_command_drain donusunu sinirla
56ecfdb  ci: quality workflow haftalik zamanlama ekle
88efffb  ci: quality workflow ekle (cargo audit, cppcheck, build)
6ac667b  chore: debug printf'leri kaldir, WIN32_EXECUTABLE ON yap
b7a7e8f  chore: .gitignore genislet, gecici dosyalari tracked listeden cikar
0dfc5ae  v0.2: DXGI preview pipeline, GPU scan, staging fix, frame_held bug fix
```

### Çalışma Zamanı Doğrulaması
- AMD (vendor=0x1002) → `PBO` path seçildi ✓
- `[PreviewWidget] shader compile vs=1 fs=1 link=1` ✓
- `[Pipeline] init OK 1920x1080@60 fps 6000 kbps` ✓
- Frame 0 guard aktif, ilk kare bozuk görünmüyor ✓

---

## v0.4 Planı (Sonraki Oturum)

### Zorunlu
- **Runtime Adaptation Seviye 3 Karar Motoru**
  - Frame drop (%) → bitrate adaptasyonu (artar/azalır)
  - GPU temp → kalite aşağı çekmesi (full → half → quarter resolution)
  - Hedef: 60fps preview, sabit bitrate streaming
  - Impl: `src/orchestrator/metrics.rs::AdaptationDecider` (state machine)

- **WGL_NV_DX_INTEROP Gerçek Implementasyon**
  - `wglDXRegisterObjectNV` → NVIDIA DX12 iGPU render
  - PBO fallback ile test edilmeli
  - Ref: `src/ui/preview_widget.cpp` (kNvDxInterop stub yerine)

- **Self-Healing UI Bağlantısı — Dört Davranış Modu**
  
  | Mod | Kullanıcı | Davranış | Örnek |
  |---|---|---|---|
  | **Auto-Pilot** | Başlangıç | Kritik + Orta aksiyonlar otomatik, bildirim | Frame drop → bitrate düşür |
  | **Co-Pilot** | Standart | Aksiyonlar checkbox listesi, seçilenler otomatik | Kullanıcı seçer: "Kaynak reconnect" ✓, "Bitrate düşür" ✗ |
  | **Assist** | Uzman | Kritik otomatik, orta/düşük log + bildirim | Thermal throttle → log, manual override |
  | **Manual** | Uzman | Devre dışı, sadece log (uyarı dialog başlangıçta) | Tüm aksiyonlar kapalı |
  
  **Aksiyon Örnekleri:**
  - Bitrate otomatik düşür (frame drop % > 10)
  - Kaynak yeniden bağlan (timeout/disconnect)
  - Çözünürlük düşür (GPU stall)
  - Encode kalitesi değiştir (thermal throttle)
  
  **Impl:** 
  - `src/ui/healing_overlay.cpp` — modu seçim UI, bildirim gösterimi
  - `src/orchestrator/metrics.rs::AdaptationDecider` — aksiyon kararı
  - `rj_command_t` ring buffer — pipeline komutları

### Güçlü Eklemeler

#### Core Performance Monitoring
- **GPU Sıcaklık İzleme**
  - AMD ADL (AMD Display Library) — AMD GPU temp
  - NVIDIA NVAPI — NVIDIA GPU temp, throttle state
  - WMI fallback — generic thermal sensor query
  - Impl: `src/pipeline/include/gpu_thermal.h`, `gpu_thermal.cpp`
  - UI: realtime temp gauge + throttle alert 🔴

- **Çoklu Monitör Desteği**
  - `DXGI_OUTPUT` enumeration → dropdown seçim
  - `EnumOutputs()` implementasyonu
  - Her monitör independent çözünürlük/colorspace
  - Preview her monitöre independently renderable
  - Impl: `src/pipeline/include/display_selector.h`

- **Frame Rate Limiter (Ayrı Threads)**
  - Preview 30fps cap (power saving)
  - Encode 60fps cap (bitrate vs quality trade-off)
  - Impl: `src/pipeline/include/frame_limiter.h`
  - Per-thread timing, sleep precision ±1ms

#### Advanced Monitoring UI
- **Bitrate / Frame Drop / GPU Temp Göstergeleri**
  - Bitrate real-time graph (past 30s, min/avg/max)
  - Frame drop counter + warning % (>5% 🟡, >15% 🔴)
  - GPU temp real-time + throttle state
  - CPU/RAM usage mini gauges
  - Impl: `src/ui/stats_widget.cpp` (new)
  - Layout: `src/ui/main_window.cpp` (dock widget)

#### Teknoloji Eklemeleri (v0.4)
- **Windows Performance Counters (PDH API)**
  - CPU, GPU (D3D), RAM real-time izleme
  - Impl: `src/pipeline/include/perf_counters.h`
  - Stability: WinAPI native, zero-copy metric buffer

- **WMI (Windows Management Instrumentation)**
  - GPU sıcaklık (ADL/NVAPI fallback)
  - Disk I/O, ağ bant genişliği sampling
  - Async WMI query → async event callback
  - Impl: `src/pipeline/include/wmi_monitor.h`

- **DirectX DXGI Statistics**
  - `DXGI_FRAME_STATISTICS` → present timing
  - Frame pacing, dropped frame detection
  - Per-output timing analysis
  - Impl: `src/pipeline/capture/capture_dxgi.cpp` (extend)

- **Windows ETW (Event Tracing for Windows)**
  - System-wide CPU/GPU/RAM sampling (sıfır overhead)
  - GPU context switch, memory allocation profiling
  - Optional: `!etwtrace` CLI flag → profiler output
  - Ref: Windows Performance Analyzer (WPA) compat
  - Impl: `src/pipeline/include/etw_tracer.h` (optional v0.4.1+)

#### Rust Orchestrator Genişletme
- **Kural Motoru (Rule Engine)**
  - JSON/TOML config: bitrate adapt rules, thermal scaling, failover
  - User-customizable: `~/.reji/rules.toml`
  - Hot-reload capability (signal handler)
  - Impl: `src/orchestrator/rules.rs` (new)
  
- **Event Bus (Pipeline ↔ UI)**
  - Thread-safe lock-free design (crossbeam mpsc)
  - Pipeline events: frame_captured, temp_spike, bitrate_drop, cpu_load
  - UI subscribers: stats_widget, healing_overlay
  - Impl: `src/orchestrator/event_bus.rs` (extend)

---

## v0.5 Hazırlığı

- [ ] **NDI Output Stub** (`src/pipeline/output/ndi_output_stub.cpp`)
  - NewTek NDI protokolü (2024 SDK)
  - v0.5'de entegrasyonu

- [ ] **Sanal Kamera (DirectShow Filter)**
  - Virtual camera device → OBS, Teams, Zoom
  - Stub: `src/pipeline/output/virtualcam_stub.cpp`

- [ ] **OBS Scene Import Parser**
  - JSON (v28.0+) → Reji scene format
  - Multi-source layout import
  - Stub: `src/ui/obs_importer_stub.cpp`

---

## Yeni Teknoloji Önerileri (Gelecek Sürümler)

### Ses Geliştirme (v0.5+)
- **WASAPI Advanced Features**
  - Noise cancellation (Windows.Media.Audio ML model)
  - Audio normalization (loudness gate, compression)
  - Multi-channel mixer, per-source volume control
  - Echo cancellation (AEC) — built-in Windows API
  - Impl: `src/pipeline/audio/wasapi_advanced.cpp`

### Direct3D 11 Overlay (OSD, v0.5+)
- **On-Screen Display (OSD)**
  - Live bitrate, frame drop %, GPU temp, CPU load
  - Custom text/graphics overlay → stream
  - Configurable position, opacity, font size
  - Impl: `src/ui/osd_overlay.cpp` (D3D11 texture rendering)

### IPC & Hardware Integration (v0.5+)
- **Named Pipe IPC**
  - Stream Deck plugin integration (per-key bitrate/scene control)
  - Loupedeck integration (touch panel dashboard)
  - Macro server: `\\.\\pipe\\reji-macro` (TCP alt)
  - Impl: `src/orchestrator/ipc_server.rs` (tokio named pipe)

### Persistent Metrics & Analytics (v0.5+)
- **SQLite Session Recording**
  - Per-session metrics: timestamp, bitrate, fps, gpu_temp, cpu_load
  - Trend analysis: rolling avg, spike detection, correlation
  - Export: CSV/JSON for analysis tools
  - Impl: `src/pipeline/metrics_recorder.cpp` (SQLite3)

### Macro Engine (v0.5+)
- **Keybind/Trigger Macro System**
  - JSON trigger configs: hotkey, timer, event-driven
  - Actions: switch scene, adjust bitrate, toggle recording, etc.
  - Persistence: `~/.reji/macros.json`
  - Named Pipe trigger API: `MACRO:run:macro_name`
  - Impl: `src/orchestrator/macro_engine.rs`

### ETW Full Integration (v0.6+)
- **Windows Performance Profiling**
  - Real-time ETW trace → GPU context switch, memory allocation heatmap
  - Windows Performance Analyzer (WPA) export
  - UI: embedded trace viewer or link to WPA
  - Use case: frame time variance diagnosis, thermal scaling validation
  - Impl: `src/pipeline/etw_profiler.cpp` (full, not stub)

---

## Teknik Borç

| Sorun | Dosya | Öncelik | Detay |
|---|---|---|---|
| `strncpy` deprecated | `src/pipeline/*.cpp` | medium | MSVC /W4: `strncpy_s` veya `strcpy_n` |
| `QMenu::addAction` deprecated | `src/ui/main_window.cpp` | low | Qt 6.8→6.9 geçişinde update |
| `/EHs /EHa` çakışması | `src/CMakeLists.txt` | high | SEH + C++ exception karışması |
| Ninja build geçişi | build system | low | NMake → Ninja (daha hızlı, parallel) |
| `build.bat` script | repo root | low | Windows-first dev flow (PowerShell yerine) |

---

## Önceki Oturumlar

### 2026-05-22 (v0.1 tamamlama)
- Build sistemi (CMake + Cargo workspace) kuruldu
- FFI köprüsü (ffi_bridge.h/.c) tamamlandı
- CI pipeline (GitHub Actions build.yml) eklendi
- DXGI Desktop Duplication (capture_dxgi.cpp) implementasyonu
- NVENC encode entegrasyonu (stub modda)
- SRT çıkış entegrasyonu
- Qt6 UI iskeleti — MainWindow, PreviewWidget, ProgramWidget, HealingOverlay, RustBridge
- Self-healing monitör (healing.rs) Rust tarafı
- Tüm FFI sınır testleri geçiyor

---

## Plugin Sandbox Yol Haritası (Uzun Vadeli)

**Sorun:** In-process plugin C ABI güvenlik riski — hastalıklı plugin tüm süreci düşürebilir.

**Çözüm:** Extism/WASM sandbox (https://github.com/extism/extism) — WASI tabanlı, 12 dil desteği.

| Versiyon | Hedef | Detay |
|---|---|---|
| **v1.0** | C ABI in-process + Ed25519 imza | Plugin kodu imzalanmalı, marketplace yok |
| **v1.5** | Extism/WASM opsiyonel | "Sandbox plugin" rozeti, güvenli marketplace başlar |
| **v2.0** | Extism zorunlu | In-process sadece certified core plugins, 3. parti sandbox'ta |

**Marketplace Akışı (v1.5+):**
1. Plugin geliştirici WASM compile
2. `reji plugin submit <wasm> --sign <privkey>`
3. Ed25519 + binary scan + code review
4. Onaylandı: `reji marketplace:published/tag-name:v1.0`
5. Kullanıcı: `reji plugin install marketplace/tag-name:v1.0` → sandbox'ta yüklenir, process izole

---

## NV_DX_INTEROP Yol Haritası (2026-06-02 — Kararı: Skip, Vulkan'a Pivot)

**Karar:** v0.3'te `NV_DX_INTEROP` gerçek implementasyonu yapmıyoruz. Bunun yerine:
1. **v0.3:** PBO ping-pong yeterli — çalışıyor, optimize etme
2. **v0.4:** Benchmark — PBO performansı profile et
3. **v0.5:** Vulkan external memory — cross-platform çözüm

| Versiyon | Karar | Detay |
|---|---|---|
| **v0.3** | PBO ping-pong yeterli | Şu an çalışıyor, darboğaz yok |
| **v0.4** | Benchmark | Frame timing, CPU overhead, GPU stall analizi |
| **v0.5** | Vulkan external memory | `VK_KHR_external_memory_win32` + Qt6 Vulkan backend |
| **v2.0+** | NV_DX_INTEROP opsiyonel | NVIDIA-only masaüstü, deprecated |

**Neden NV_DX_INTEROP skip ediyoruz:**
- `NvOptimusEnablement=0` (Qt Application default) → AMD iGPU context seçiliyor
- NVIDIA DX resource'a erişim sınırlı/unstable
- PBO + DwmFlush() zaten Optimus race condition'ı çözdü

**Uzun vadeli çözüm:**
- Vulkan external memory: cross-vendor (AMD/NVIDIA/Intel) + cross-platform (Windows/Linux/macOS)
- Qt6 Vulkan backend: native Vulkan renderer
- macOS Metal: long-term parity
