# Reji Studio — Claude Code Bağlam Dosyası

> Bu dosya Claude Code VS Code eklentisi için proje bağlamını özetler.
> Her oturumda bu dosyayı referans al.

---

## Proje Kimliği

| Alan | Değer |
|---|---|
| Ad | Reji Studio |
| Tür | Açık kaynak canlı yayın yazılımı + altyapı motoru |
| Lisans | Apache 2.0 |
| Repo | github.com/rejistudio-official/reji-studio |
| Yerel yol | C:\reji-studio |
| Durum | v0.1 geliştirme aşaması |

---

## Teknoloji Yığını

| Katman | Teknoloji | Klasör |
|---|---|---|
| Medya pipeline | C++ 17, MSVC | `src/pipeline/` |
| FFI köprüsü | C ABI | `src/ffi/` |
| Orkestrasyon | Rust + Tokio | `src/orchestrator/` |
| Arayüz | Qt6 (henüz boş) | `src/ui/` |
| Testler | C++ + Rust | `tests/` |
| CI | GitHub Actions | `.github/workflows/` |

---

## Mimari Temel Kararlar

**FFI köprüsü:**
- C++ → Rust: crossbeam SPSC ring buffer (lock-free, 256 slot)
- Rust → C++: MPSC komut kuyruğu (64 slot)
- Yalnızca 3 extern "C" fonksiyon: `rj_metrics_push`, `rj_command_drain`, `rj_pipeline_status`
- Canary değeri: `0xEEFF1234` — her MetricSample'da doğrulanır
- Blocking çağrı YASAK

**GPU pipeline:**
- Zero-copy VRAM-first (CPU'ya kopyalama yok)
- Windows: DXGI Desktop Duplication → NVENC
- Cross-adapter (AMD iGPU + NVIDIA dGPU): GpuResourceManager SharedHandle
- Platform soyutlama: `GpuContext` arayüzü

**Seçici sandbox:**
- Video/ses filtresi: in-process thread (16.6ms kare bütçesi)
- Chat/overlay/makro: izole OS process (Unix socket / Named Pipe)
- VST3: in-process host, Windows SEH crash koruması

**Self-healing üçlü model:**
- Katman 1 Reaktif: < 500ms — kaynak kopma, plugin çökme
- Katman 2 Prediktif: 3-5s trend — bitrate düşüşü, CPU yükselme
- Katman 3 Adaptif: oturumlar arası — Redb ile eşik kalibrasyonu
- Histeresis: aksiyon sonrası 60s cooldown (bitrate), 20s (CPU)
- Kullanıcı modu: Auto-Pilot (başlangıç) / Co-Pilot (standart) / Manual Assist (uzman)

**Preview/Program bariyeri:**
- Başlangıç modu: kapalı (OBS davranışı)
- Standart/Uzman: açık (studio mode)
- Atomic değişim — yeniden başlatma gerekmez

---

## Klasör Yapısı

```
C:\reji-studio\
├── .github/workflows/build.yml   ← CI pipeline (Windows build + test)
├── src/
│   ├── pipeline/                 ← C++ medya pipeline
│   │   ├── include/pipeline.h
│   │   ├── pipeline.cpp          ← İskelet (v0.1'de genişletilecek)
│   │   └── CMakeLists.txt
│   ├── ffi/                      ← C ABI köprüsü
│   │   ├── ffi_bridge.h          ← RjMetricSample, RjCommand struct'ları
│   │   ├── ffi_bridge.c
│   │   └── CMakeLists.txt
│   ├── orchestrator/             ← Rust + Tokio orkestrasyon
│   │   ├── Cargo.toml
│   │   └── src/
│   │       ├── lib.rs            ← Modül tanımları
│   │       ├── event_bus.rs      ← MediaEvent, SystemEvent, UserEvent, HealingEvent
│   │       ├── metrics.rs        ← MetricSample, MetricState (atomic)
│   │       └── healing.rs        ← HealingMonitor, HealingThresholds, CooldownTracker
│   └── ui/                       ← Qt6 (henüz boş)
├── tests/
│   └── test_ffi_boundary.cpp     ← FFI sınır testi (geçiyor)
├── CMakeLists.txt                ← C++ workspace root
├── Cargo.toml                    ← Rust workspace root
├── .gitignore
└── CONTEXT.md                    ← Bu dosya
```

---

## PoC Sonuçları (Tamamlandı)

| PoC | Sonuç | Karar |
|---|---|---|
| PoC 1: C++/Rust FFI | C++→Rust ~480us ✓ | Ham C ABI + crossbeam yeterli |
| PoC 2: Zero-Copy GPU | NVENC session açıldı ✓ | Cross-adapter v0.1'de çözülecek |
| PoC 3: Plugin C ABI | C++ 3400ns, Rust 3182ns ✓ | C ABI kararlı |

**Önemli bulgu:** RTX 4070 Laptop'ta ekran AMD iGPU'ya bağlı, NVENC NVIDIA dGPU'da.
DXGI Desktop Duplication AMD adapter'ında çalışıyor, NVIDIA'da değil.
Çözüm: GpuResourceManager AMD→NVIDIA SharedHandle transferi.

---

## v0.1 Yol Haritası

- [x] Build sistemi (CMake + Cargo workspace)
- [x] FFI köprüsü (ffi_bridge.h/.c)
- [x] CI pipeline (GitHub Actions)
- [x] Event bus iskeleti (event_bus.rs)
- [x] Metrik modülü (metrics.rs)
- [x] Self-healing monitör (healing.rs)
- [x] reji_app.exe linker hatası düzelt
- [x] Qt6 UI iskeleti
- [x] Qt6 pencere aç
- [x] Rust event bus → C++ FFI entegrasyonu
- [x] DXGI ekran yakalama (capture_dxgi.cpp)
- [x] NVENC encode entegrasyonu (encode_nvenc.cpp)
- [x] SRT çıkış entegrasyonu
- [x] Temel sahne yönetimi
- [x] Qt DLL'lerini kalıcı PATH'e ekle
- [x] monitor_splitter setSizes düzeltmesi (setStretchFactor ile çözüldü)

---

## v0.2 Yol Haritası

### Pipeline → UI Frame Akışı
→ [x] Staging + CPU copy — DxgiCapturePipeline::init_preview_staging / map_preview_frame / unmap_preview_frame
→ [x] GPU Tarama: scan_gpus() — DXGI Factory ile tüm adapter'lar listelenir, loglanır
→ [x] GpuResourceManager::init() — display_info_ / encode_info_ (vendor, VRAM) kaydedilir
→ [x] Self-healing UI entegrasyonu (startMonitor aktif, main_window.cpp)
→ [ ] Sahne yönetimi genişletme (gerçek sahne içerikleri)

## v0.3 Yol Haritası

### Preview Optimizasyonu
→ Staging + Çift PBO (Double Buffering DMA)
→ Adaptif mod seçimi:
   - AMD/Intel veya hibrit (AMD+NVIDIA) → PBO modu
   - Tek NVIDIA → v0.4'e hazır

## v0.4 Yol Haritası

### Zero-Copy Preview
→ WGL_NV_DX_interop (sadece tek NVIDIA GPU sistemlerde)
→ Otomatik mod anahtarlaması:
   [GPU Tarama]
       → NVIDIA yok veya hibrit → PBO modu (v0.3)
       → Tek NVIDIA         → WGL_NV_DX_interop (v0.4)

## Bilinen Mimari Kararlar

- Çift DMA (Double Buffering PBO) doğrudan v0.3 hedefi — v0.2'ye eklenmez
- Donanım keşfi (GPU tarama) v0.2'de başlar, mod anahtarlaması v0.3/v0.4'te
- wglDXRegisterObjectNV sadece "encode ve display aynı NVIDIA GPU" senaryosunda

---

## Build Durumu

| Hedef | Durum | Tarih |
|---|---|---|
| reji_app.exe | ✓ — v0.1 pencere açılıyor | 2026-05-22 |
| reji_app.exe | ✓ — v0.1 tüm işler tamamlandı | 2026-05-22 |
| reji_app.exe | ✓ — v0.2 GPU scan + preview staging düzeltildi | 2026-06-01 |

---

## Bilinen Sorunlar

- **Qt6 QSplitter setSizes() crash**: `setSizes()` `QOpenGLWidget` içeren splitter'da crash ediyor (Qt6.8 + MSVC bug). `setStretchFactor()` kullan — `setSizes()` çağırma.
- **Qt6 addPermanentWidget + showMessage**: `statusBar()->showMessage()` permanent widget'ları gizliyor. `lbl_status_` `QLabel` kullan.
- **QSplitter setChildrenCollapsible(false)**: `QOpenGLWidget` içeren splitter'da crash ediyor — kaldırıldı.
- **QSplitter setSizes() QOpenGLWidget ile**: crash ediyor — `setStretchFactor()` kullan.

---

## Derleme Komutları

```cmd
# x64 Native Tools Command Prompt'ta:
cd C:\reji-studio
cmake -B build -G "NMake Makefiles"
cmake --build build
build\tests\test_ffi_boundary.exe

# PowerShell'de (PATH ekleyerek):
$env:PATH += ";$env:USERPROFILE\.cargo\bin"
cd C:\reji-studio
cargo build
cargo test
```

---

## Kod Stili Kuralları

### C++ Kuralları
- Tüm public fonksiyonlar bool veya RjError döner — void yasak
- shutdown() SEH ile sarılı olmalı
- SEH içinde C++ nesnesi yasak — __declspec(noinline) leaf function kullan
- /EHa derleyici bayrağı zorunlu (CMakeLists.txt'de)
- run_frame single-thread varsayımı — her fonksiyonda belge yorumu
- PTS kare düştüğünde artmaz — frame_drops_ artır
- rj_command_drain dönüşü [0,8] arasında clamp edilmeli
- rj_command_drain negatif dönüşünü logla
- #pragma pack(push,1) + static_assert ile FFI struct boyutu doğrula
- std::atomic<bool> ile thread-safe flag'ler
- CoInitializeEx pipeline init'te çağrılmalı, CoUninitialize shutdown'da
- srt_startup/cleanup std::call_once + instance counter ile yönet
- Hot-path'de heap tahsis yasak
- Blocking FFI çağrısı yasak
- setjmp/longjmp yasak
- Her public fonksiyonun doc comment'i olmalı

### Rust Kuralları
- Hata yönetimi Result<T, E> — unwrap() production kodunda yasak
- Tüm extern "C" fonksiyonlarda catch_unwind zorunlu
- Her public fonksiyonun doc comment'i olmalı
- Her modülde en az 3 test

### Genel
- Fonksiyon isimleri snake_case, İngilizce
- Her modülde en az 3 test
- Fusion blind spot listesi her modül sonrası güncellenir

---

## Kritik Kurallar (Unutma)

1. **Hot-path'de JSON yasak** — struct pointer kullan
2. **FFI sınırında blocking çağrı yasak** — ring buffer üzerinden async
3. **Video/ses eklentisi in-process** — IPC gecikme bütçesini aşar
4. **CRT heap kuralı** — tahsis eden serbest bırakır, `rj_plugin_free()` export zorunlu
5. **Canary doğrulama** — her MetricSample'da `0xEEFF1234` kontrol et
6. **setjmp/longjmp yasak** — Windows SEH veya Rust panic::catch_unwind
7. **Tek kod tabanı** — platform farkı `#ifdef` ile, ayrı repo değil

---

## Açık Kararlar (Kapatıldı)

- Repo adı: `reji-studio` ✓
- Plugin marketplace: v1.5/v2.0 ✓
- OBS import: v28.0+ ✓
- Mobil kontrol: PWA v2.0 ✓
- vMix şablonlar: 4 şablon ✓
- İki katmanlı strateji: Reji Engine + Reji Studio ✓

---

*Son güncelleme: 2026-05-22 | ARCHITECTURE.md v0.5 ile senkronize*
