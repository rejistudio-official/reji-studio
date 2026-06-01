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
