# Reji Studio — Oturum İlerleme Günlüğü

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

- **Self-Healing UI Bağlantısı**
  - `HealingOverlay` → pipeline command ring arayüz
  - Auto-Pilot: metric anomaly → otomatik bitrate düşür
  - Co-Pilot: operatör onayı gerekli
  - Impl: `src/ui/healing_overlay.cpp` + `rj_command_t` ring buffer

### Güçlü Eklemeler
- **Çoklu Monitör Desteği**
  - `DXGI_OUTPUT` enumeration → dropdown seçim
  - `EnumOutputs()` implementasyonu
  - Preview her monitöre independently renderable

- **Preview Kalite Seçimi**
  - Tam (1:1) / Yarım (1:2) / Çeyrek (1:4) çözünürlük
  - UI: quality combobox + frame rate impact göstergesi

- **Frame Rate Limiter (Ayrı Threads)**
  - Preview 30fps cap (power saving)
  - Encode 60fps cap (bitrate vs quality trade-off)
  - Impl: `src/pipeline/frame_limiter.h`

- **UI Göstergeler**
  - Bitrate real-time graph (past 30s)
  - Frame drop counter + % warning
  - GPU temp + throttle alert
  - Impl: `src/ui/stats_widget.cpp`

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
