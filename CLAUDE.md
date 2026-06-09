# CLAUDE.md — Reji Studio Claude Code Oturum Kılavuzu

> Her Claude Code oturumunda otomatik okunur.
> Detaylı kurallar: AGENTS.md | Durum: CONTEXT.md | Geçmiş: docs/memory.md

---

## 1. Proje Kimliği

| Alan | Değer |
|---|---|
| Proje | Reji Studio — Açık kaynak canlı yayın yazılımı |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | `C:\reji-studio` |
| Stack | C++17/MSVC + Rust/Tokio + Qt6 6.8.0 + Vulkan 1.4 |
| GPU | AMD Radeon 780M (display/iGPU) + NVIDIA RTX 4070 Laptop (encode/dGPU) |
| OS | Windows 11 |
| Versiyon | v0.5.1 ✅ tamamlandı → v0.5.2 devam ediyor |

---

## 2. Mimari Katmanlar

```
src/
├── pipeline/gpu/      # D3D11↔Vulkan zero-copy (external_memory_bridge, vulkan_initializer)
├── pipeline/capture/  # DXGI Desktop Duplication (capture_dxgi)
├── pipeline/encode/   # NVENC stub — SDK gerekli
├── pipeline/output/   # SRT stub — SDK gerekli
├── ui/                # Qt6 — PreviewWidget, MainWindow, HealingOverlay
├── ffi/               # C ABI köprüsü — C++ ↔ Rust (DOKUNMA)
└── orchestrator/      # Rust/Tokio — event bus, rule engine, self-healing
```

**Donanım bağımlı kod yalnızca şu dosyalarda yaşar:**
- `src/pipeline/gpu/external_memory_bridge.*`
- `src/pipeline/capture/capture_dxgi.*`
- `pipeline.cpp` bu dosyaları doğrudan include etmez — sadece callback üzerinden

---

## 3. Kırmızı Çizgiler (ASLA İHLAL ETME)

### Hot-Path Kuralları
- `paintGL()`, `run_frame()`, `get_frame_images()` içinde:
  - ❌ `new`, `malloc`, `std::vector`, `std::string` — heap allocation yasak
  - ❌ `vkWaitForFences`, `vkDeviceWaitIdle` — blocking call yasak
  - ❌ JSON parsing, WMI query, string formatting
  - ✅ Pre-allocated buffer, stack primitive, fixed-size pool

### SEH Kuralları
- D3D11 ve DXGI çağrıları `__try/__except` ile sarılmalı
- `__try` scope içinde destructor'lı C++ nesnesi yasak
- SEH fonksiyonları `__declspec(noinline)` leaf wrapper olmalı

### FFI Kuralları
- C++ ↔ Rust struct'larında `static_assert(sizeof(MyStruct) == X)` zorunlu
- `src/ffi/ffi_bridge.h` ve `ffi_bridge.c` — **DOKUNMA**
- `src/orchestrator/src/metrics.rs` — **DOKUNMA** (RjMetricSample ABI)

### Vendor Lock-in Yasağı
- NVENC-specific struct'lar `src/pipeline/include/` dışına çıkmaz
- SRT-specific tip `pipeline.cpp`'e sızmaz
- Her encoder/output için fallback (FFmpeg/VAAPI) mimaride hazır tutulur

### CMakeLists.txt
- Root `CMakeLists.txt` — sadece zorunlu değişikliklerde, onay alarak
- `src/pipeline/CMakeLists.txt` — SRT/NVENC stub mantığı hassas, dikkatli ol

---

## 4. Build Komutları (Claude Code için)

```bash
# Build
cd C:/reji-studio && python scripts/build.py

# Build + test çalıştır
cd C:/reji-studio && python scripts/build.py --run

# Temiz build
cd C:/reji-studio && python scripts/build.py --clean
```

**Kurallar:**
- Claude Code bash'ten çalıştır — Windows cmd.exe ve vcvars64.bat otomatik yönetilir
- `--run` bayrağı binary çalıştırır ve `run.log`'a yazar
- CMakeLists.txt'e dokunma

---

## 5. Araç Zinciri Entegrasyonu

### clangd (C++ LSP)
- `compile_commands.json` — CMake'in ürettiği dosyayı kullan: `build/compile_commands.json`
- MSVC/Windows SDK path uyumsuzluklarını clangd üzerinden tespit et
- Alignment bug ve semantic warning'ler için clangd çıktısını referans al
- Hot-path'te alignment sorunu varsa clangd uyarısını kod değişikliğinden önce oku

### CodeGraph
- C++/Rust cross-language dependency graph için kullan
- `ExternalMemoryBridge` → `pipeline.cpp` → `preview_widget.cpp` bağlantısını sorgula
- Rule değişikliği Rust orchestrator'ı etkiliyor mu? CodeGraph'ta izle
- Kullanım: `@codegraph trace <fonksiyon_adı>` — değişiklik öncesi bağımlılık haritası çıkar

### claude-mem
- Oturumlar arası Vulkan kararları, FFI fix geçmişi ve mimari borçları hatırlar
- `docs/memory.md` ile çapraz kontrol yap — claude-mem özetleri ile elle tutulan notlar çelişiyorsa `docs/memory.md` önceliklidir
- Her büyük fix sonrası: `docs/memory.md` güncelle + claude-mem otomatik yakalar

---

## 6. Görev Akışı (Her Adımda)

```
1. PLAN   — Hangi dosyalar değişecek, yan etkiler ne?
2. SPEC   — Fonksiyon imzaları, kritik satırlar, struct alanları
3. ONAY   — "Onaylıyor musun? (e/h)" — kullanıcı onaylamadan kod yazma
4. UYGULA — Kodu yaz, commit et
5. TEST   — cmake --build → reji_app.exe → log kontrol → beklenen satırları göster
```

Adım testi geçmeden sonraki adıma geçme.

---

## 7. Scope Kontrol Listesi (Kod Yazmadan Önce)

Her değişiklik için şu soruları sor:

- [ ] Bu değişiklik hot-path dosyasını etkiliyor mu? → Hot-path kuralları uygula
- [ ] FFI sınırı geçiliyor mu? → `static_assert` ve ABI kontrolü yap
- [ ] D3D11/DXGI/Vulkan çağrısı var mı? → SEH wrapper ekle
- [ ] Donanım bağımlı kod soyutlama dışına mı çıkıyor? → Hayır — izolasyonu koru
- [ ] Yeni vendor bağımlılığı ekleniyor mu? → Vendor lock-in yasağını kontrol et
- [ ] clangd bu değişiklik için uyarı veriyor mu? → Uyarıyı önce çöz

---

## 8. Mühürleme (Oturum Sonu)

Her büyük görev bittikten sonra:

```
1. docs/memory.md güncelle — ne yapıldı, neden, hangi karar alındı
2. CONTEXT.md güncelle — mevcut durum bölümünü yansıt
3. git commit -m "fix/feat/docs: ..."
4. git push
5. /clear — yeni görev için bağlamı temizle
```

---

## 9. Kritik Dosya Referansları

| Dosya | Açıklama | Risk |
|---|---|---|
| `external_memory_bridge.h/.cpp` | D3D11↔Vulkan bridge | Yüksek |
| `vulkan_initializer.h/.cpp` | Singleton — `get()` kullan, `shutdown()` çağırma | Yüksek |
| `preview_widget.h/.cpp` | GL interop, paintGL | Yüksek |
| `pipeline.cpp` | Frame callback, notify_vulkan_ready | Yüksek |
| `main_window.cpp` | Pipeline wiring | Orta |
| `ffi_bridge.h/.cpp` | C ABI — DOKUNMA | Kritik |
| `metrics.rs` | Rust ABI — DOKUNMA | Kritik |
| `CMakeLists.txt` | Build sistemi — onay al | Orta |

---

## 10. Şu Anki Görev: v0.5.2

### Tamamlanan ✅
- Adım 1: `ExternalMemoryBridge` GL target pool + NT handle export

### Devam Eden
- Adım 2: `copy_optimizer.cpp` — gerçek `vkCmdBlitImage` + timeline signal
- Adım 3: `PreviewWidget` — GL interop extension resolve
- Adım 4: `paintGL` — NT handle import → GL texture
- Adım 5: `main_window` — bridge wire
- Adım 6: `PreviewWidget` — `setBridge()` bağlantısı

### Hedef Log (v0.5.2 tamamlandığında)
```
[PreviewWidget] GL_EXT_memory_object=1 win32=1
[PreviewWidget] GL interop texture created: 1920x1080
[GpuCopyOptimizer] execute_copy: blit submitted, timeline=1
```
