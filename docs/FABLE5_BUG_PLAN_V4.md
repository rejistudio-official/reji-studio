# Reji Studio — Fable 5 Dördüncü Tarama Düzeltme Planı (V4)

**Tarih:** 12.06.2026
**Kaynak:** fable5-review-all-2026-06-12_09-44.md — 59 dosya, 204K token, $2.69
**Önceki:** FABLE5_BUG_PLAN_V3.md (D1-D18 tamamlandı)

---

## Öncelik Matrisi

| #   | Sorun                                                        | Dosya                                              | Öncelik      | Sprint   |
|-----|--------------------------------------------------------------|----------------------------------------------------|--------------|----------|
| E1  | RjAction/RjCommand ABI const_assert eksik                    | ffi.rs + build.rs                                  | 🔴 Kritik    | Sprint 1 |
| E2  | last_staging_vk data race — pipeline ↔ GL                   | pipeline.cpp                                       | 🔴 Kritik    | Sprint 1 |
| E3  | GL wait on unsignaled semaphore — device hang                | preview_widget.cpp                                 | 🔴 Kritik    | Sprint 1 |
| E4  | VK_QUEUE_FAMILY_EXTERNAL barrier eksik                       | copy_optimizer.cpp                                 | 🔴 Kritik    | Sprint 1 |
| E5  | cached_d3d11_handle_ resolution değişiminde stale            | external_memory_bridge.cpp                         | 🔴 Kritik    | Sprint 2 |
| E6  | GpuCopyOptimizer::shutdown() hiç çağrılmıyor                 | main_window.cpp                                    | 🔴 Kritik    | Sprint 2 |
| E7  | NVENC AMD device'da açılıyor — RTX 4070 kullanılmıyor        | gpu_resource_manager.cpp                           | 🔴 Kritik    | Sprint 2 |
| E8  | Healing mode iki static — UI'dan değişmiyor                  | ffi.rs + healing.rs                                | 🟠 Yüksek   | Sprint 2 |
| E9  | glImportMemoryWin32HandleEXT her frame çağrılıyor            | preview_widget.cpp                                 | 🟠 Yüksek   | Sprint 2 |
| E10 | vkGetQueryPoolResults — reset yok, wait bit yok              | gpu_query_timing.cpp                               | 🟠 Yüksek   | Sprint 3 |
| E11 | timestamp_us = qpc/1000 yanlış hesap                         | frame_pacing.cpp                                   | 🟡 Orta     | Sprint 3 |
| E12 | sizeof_check.cpp _reserved field compile hatası              | sizeof_check.cpp                                   | 🟡 Orta     | Sprint 3 |
| E13 | poll_frames static counter — member olmalı                   | preview_widget.cpp                                 | 🟡 Orta     | Sprint 3 |
| E14 | D3D11 image memory/image destroy sırası ters                 | external_memory_bridge.cpp                         | 🟡 Orta     | Sprint 3 |
| E15 | Dead subsystems — RuleEngine, metrics timer                  | pipeline.cpp + main_window.cpp                     | 🟡 Orta     | Sprint 4 |
| E16 | Per-frame stderr logging — hot path                          | pipeline.cpp + preview_widget.cpp                  | 🔵 Düşük    | Sprint 4 |
| E17 | MetricsCollector window semantics yanlış                     | metrics_collector.cpp                              | 🔵 Düşük    | Sprint 4 |
| E18 | WasapiCapture publish_metrics cumulative değer               | wasapi_capture.cpp                                 | 🔵 Düşük    | Sprint 4 |

---

## Sprint 1 — Kritik GPU Sync ve ABI

**Hedef:** Spec ihlalleri, veri yarışları, cihaz hang
**Tahmini süre:** 1 oturum

---

### E1 — RjAction/RjCommand ABI const_assert Eksik

**Sorun:**
`RjActionType` `#[repr(C)] pub enum` — boyut garantisi yok.
`build.rs` name_map'te sadece `MetricSample` var; `RjAction` ve `RjCommand` yok.
Rust enum ile C++ struct arasında sessiz boyut kayması olabilir.

**Çözüm:**
```rust
// ffi.rs:
#[repr(u32)]  // C'den repr(C) yerine kesin u32
pub enum RjActionType { ... }

const _: () = assert!(core::mem::size_of::<RjAction>() == 20);
const _: () = assert!(core::mem::size_of::<RjCommand>() == 24);
```

```rust
// build.rs name_map'e ekle:
("RjAction", "RjAction"),
("RjCommand", "RjCommand"),
```

---

### E2 — last_staging_vk Data Race

**Sorun:**
`run_frame()` (frame thread) `last_staging_vk` / `last_target_vk` yazıyor.
`get_last_frame_images()` başka thread'den çağrılabilir.
3-slotlu pool, GL thread kullanırken bir sonraki round-robin çağrısıyla
üzerine yazılabilir — 3-frame race window, kilit yok.

**Çözüm:**
```cpp
// pipeline.h:
std::atomic<VkImage> last_staging_vk_{VK_NULL_HANDLE};
std::atomic<VkImage> last_target_vk_{VK_NULL_HANDLE};
```

---

### E3 — GL Wait on Unsignaled Semaphore

**Sorun:**
`paintGL`'de yeni frame yoksa `glWaitSemaphoreEXT` daha önce
tüketilmiş (unsignaled) semaphore üzerinde bekliyor → device hang.
`slot_gl_signaled_` sadece double-signal'ı önlüyor, double-wait'i değil.

**Çözüm:**
```cpp
// preview_widget.cpp paintGL():
// Sadece signal bekleniyorsa wait yap:
if (copy_optimizer_ && copy_optimizer_->is_slot_signaled(pool_idx)) {
    pfn_WaitSemaphore_(...);
    copy_optimizer_->clear_gl_signal(pool_idx);
}
```

```cpp
// copy_optimizer.h:
bool is_slot_signaled(uint32_t slot) const {
    return slot < 3 && slot_gl_signaled_[slot];
}
```

---

### E4 — VK_QUEUE_FAMILY_EXTERNAL Barrier Eksik

**Sorun:**
D3D11'den import edilen staging image için
`srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL` acquire barrier yok.
Vulkan spec ihlali — bazı driver'larda corruption.
`barrier_staging.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT` ile
`oldLayout = VK_IMAGE_LAYOUT_UNDEFINED` kombinasyonu da hatalı:
UNDEFINED oldLayout ile srcAccessMask 0 olmalı.

**Çözüm:**
```cpp
// copy_optimizer.cpp execute_copy() barrier_staging:
barrier_staging.srcAccessMask = 0;  // D3D11 yazdı, Vulkan bilmiyor
barrier_staging.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
barrier_staging.dstQueueFamilyIndex = graphics_queue_family_;

// Blit sonrası release barrier:
VkImageMemoryBarrier release_barrier{};
release_barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
release_barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
release_barrier.srcQueueFamilyIndex = graphics_queue_family_;
release_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
```

---

## Sprint 2 — Lifetime, NVENC, Healing

**Hedef:** Resource leak, yanlış cihaz seçimi, state senkronizasyonu
**Tahmini süre:** 1 oturum

---

### E5 — cached_d3d11_handle_ Stale Kalıyor

**Sorun:**
NT handle ilk `shared_texture_`'dan export ediliyor ve cache'leniyor.
Resolution değişimi / ACCESS_LOST sonrası `shared_texture_` yeniden
oluşturulsa bile bridge eski handle'ı kullanmaya devam ediyor.

**Çözüm:**
```cpp
// external_memory_bridge.cpp:
// Cache key olarak texture pointer kullan:
ID3D11Texture2D* cached_texture_ptr_ = nullptr;

VkImage get_frame_images(..., ID3D11Texture2D* tex, ...) {
    if (tex != cached_texture_ptr_) {
        invalidate_pool();
        cached_texture_ptr_ = tex;
        re_export_handle(tex);
    }
    // ...
}
```

---

### E6 — GpuCopyOptimizer::shutdown() Çağrılmıyor

**Sorun:**
`GpuCopyOptimizer` `MainWindow`'un üyesi.
`~GpuCopyOptimizer() = default` — `shutdown()` çağırmıyor.
Timeline semaphore, command pool, binary semaphore'lar leak.
`vkDestroyDevice` öncesinde temizlenmezse UAF.

**Çözüm:**
```cpp
// main_window.cpp ~MainWindow():
void MainWindow::~MainWindow() {
    stopFrameThread();           // önce thread durdur
    copy_optimizer_.shutdown();  // GPU kaynakları temizle
    // pipeline_ sonra yıkılır
}
```

veya `GpuCopyOptimizer` destructor'ına `shutdown()` ekle.

---

### E7 — NVENC AMD Device'da Açılıyor

**Sorun:**
`same_adapter_ = true` hardcode → `encode_gpu_ = display_gpu_` (AMD).
`NvencEncoder::init` AMD D3D11 device ile çağrılıyor →
`nvEncOpenEncodeSessionEx` başarısız → "preview-only mode".
RTX 4070 hiç kullanılmıyor.

**Çözüm:**
```cpp
// gpu_resource_manager.cpp init():
// same_adapter_ = true; // KALDIR
// find_encode_adapter() sonucunu kullan:
if (encode_adapter != display_adapter) {
    same_adapter_ = false;
    // cross-adapter shared texture yolu (B6 implementasyonu)
} else {
    same_adapter_ = true;
    encode_gpu_ = display_gpu_;
}
```

Not: Cross-adapter path için B6 keyed mutex implementasyonu
tamamlanmış olmalı.

---

### E8 — Healing Mode İki Static

**Sorun:**
`ffi.rs::HEALING_MODE` ve `healing.rs::HEALING_MODE` — iki ayrı static.
`rj_set_healing_mode` sadece ffi.rs'tekini yazıyor.
`HealingMonitor` kendi `AtomicCell`'ini kullanıyor, hiç okumadan.
UI'dan mod değişikliği healing davranışını etkilemiyor.

**Çözüm:**
```rust
// healing.rs'teki HEALING_MODE'u kaldır.
// ffi.rs'teki HEALING_MODE'u Arc<AtomicU32> yap.
// HealingMonitor'a Arc<AtomicU32> inject et:
pub struct HealingMonitor {
    healing_mode: Arc<AtomicU32>,
    // ...
}

// run() içinde her tick'te kontrol:
let mode = self.healing_mode.load(Ordering::Relaxed);
if mode == 1 { /* Manual — komut üretme */ continue; }
```

---

### E9 — glImportMemoryWin32HandleEXT Her Frame

**Sorun:**
`paintGL` her frame için `pfn_ImportMemoryWin32Handle_` çağırıyor.
Import sonrası memory object zaten backed — tekrar import invalid.

**Çözüm:**
```cpp
// preview_widget.cpp:
// Import'u texture oluşturma bloğunun içine taşı:
if (!gl_memory_objects_[pool_idx]) {
    glCreateMemoryObjectsEXT(1, &gl_memory_objects_[pool_idx]);
    pfn_ImportMemoryWin32Handle_(...);  // sadece bir kez
}
```

---

## Sprint 3 — Orta Öncelikli Düzeltmeler

**Hedef:** Vulkan query doğruluğu, zaman hesabı, compile hataları, state izolasyonu
**Tahmini süre:** 1 oturum

---

### E10 — vkGetQueryPoolResults Reset/Wait Eksik

**Sorun:**
Query pool reset edilmeden yeniden kullanılıyor → validation error.
`VK_QUERY_RESULT_WAIT_BIT` yok → yazılmamış query'ler undefined.
"render_*" timestamp'leri için caller yok — bazı query slotları hiç yazılmıyor.

**Çözüm:**
```cpp
// gpu_query_timing.cpp record_timestamp() başında:
vkCmdResetQueryPool(cmd_buf, query_pool_, 0, NUM_QUERIES);

// retrieve_results():
vkGetQueryPoolResults(...,
    VK_QUERY_RESULT_64_BIT |
    VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
```

---

### E11 — timestamp_us Yanlış Hesap

**Sorun:**
`timestamp_us = current_qpc / 1000` — QPC freq 10MHz'de
1000'e bölmek ~100 mikrosaniye verir, 1 mikrosaniye değil.
Downstream tüketiciler 100× hatalı değer görüyor.

**Çözüm:**
```cpp
// frame_pacing.cpp:
LARGE_INTEGER freq;
QueryPerformanceFrequency(&freq);
// Cache freq — sadece bir kez sorgula

out_stats->timestamp_us =
    current_qpc * 1'000'000ULL / freq.QuadPart;
```

---

### E12 — sizeof_check.cpp _reserved Field Compile Hatası

**Sorun:**
`offsetof(RjMetricSample, reserved)` — field adı `_reserved`.
Compile hatası — CI bu dosyayı build etmiyor.

**Çözüm:**
```cpp
// sizeof_check.cpp:
static_assert(offsetof(RjMetricSample, _reserved) == XX,
    "ABI: _reserved offset değişti");
```

---

### E13 — poll_frames Static Counter

**Sorun:**
`static int poll_frames = 0` — tüm widget instance'ları paylaşıyor.
50 birikimli poll sonrası sağlıklı copy force-clear ediliyor.

**Çözüm:**
```cpp
// preview_widget.h:
int poll_frames_ = 0;  // member

// paintGL():
// static yerine this->poll_frames_ kullan
// is_ready olduğunda: poll_frames_ = 0;
```

---

### E14 — D3D11 Image/Memory Destroy Sırası

**Sorun:**
`shutdown()` memory'yi image'dan önce serbest bırakıyor.
Vulkan spec: image önce destroy edilmeli (memory'ye bağlı resource).

**Çözüm:**
```cpp
// external_memory_bridge.cpp shutdown():
// Önce image'ları yok et:
for (auto img : image_pool_) {
    if (img) vkDestroyImage(device_, img, nullptr);
}
// Sonra memory'yi serbest bırak:
for (auto mem : pool_memory_) {
    if (mem) vkFreeMemory(device_, mem, nullptr);
}
```

---

## Sprint 4 — Temizlik ve Dead Code

**Hedef:** Ölü subsystem bağlantısı, logging performansı, metrik doğruluğu
**Tahmini süre:** 1 oturum

---

### E15 — Dead Subsystems Bağlantısı

**Sorun:**
- `RuleEngine::evaluate()` hiç çağrılmıyor
- `enqueue_action()` caller'ı yok
- `metrics_timer_` hiç oluşturulmamış
- Status bar metrikleri hiç güncellenmiyor

**Çözüm (minimal):**
```cpp
// main_window.cpp constructor:
metrics_timer_ = new QTimer(this);
connect(metrics_timer_, &QTimer::timeout,
        this, &MainWindow::pollMetrics);
metrics_timer_->start(1000); // 1s

// pollMetrics() stub'ını gerçek implementasyonla doldur
```

Rust tarafı:
```rust
// Periyodik Rust task: metric_ring drain →
// RuleEngine::evaluate → enqueue_action
```

---

### E16 — Per-Frame stderr Logging Kaldır

**Sorun:**
`pipeline.cpp::run_frame` her frame `fprintf + fflush` yapıyor.
`preview_widget.cpp::paintGL` her frame log basıyor.
60fps'de 120+ flushed write/s — frame time'ı etkiliyor.

**Çözüm:**
```cpp
// Debug flag arkasına al:
#ifdef RJ_DEBUG_VERBOSE
    fprintf(stderr, "[PreviewWidget] paintGL called...\n");
#endif
```

---

### E17 — MetricsCollector Window Semantics

**Sorun:**
`MAX_WINDOW_FRAMES = 1800` — 1Hz poll'da 30 dakikalık pencere.
Drop % poll sayısına bölünüyor, frame sayısına değil → anlamsız.

**Çözüm:**
```cpp
static constexpr size_t MAX_WINDOW = 30; // 30s @ 1Hz
// Delta drops / window frames ile hesapla
```

---

### E18 — WasapiCapture Cumulative Metric

**Sorun:**
`publish_metrics` `frame_drops_.load()` (kümülatif) gönderiyor.
`pipeline.cpp` delta için `exchange(0)` kullanıyor; WASAPI kullanmıyor.
Mutlak değer gönderiliyor, delta değil → Rust tarafında double-count.

**Çözüm:**
```cpp
// wasapi_capture.cpp:
// pipeline.cpp'nin yaptığı gibi exchange(0) kullan:
uint32_t drops = frame_drops_.exchange(0);
sample.frame_drop_pct = calculate_pct(drops);
```

---

## Dosya Düzenleme Sırası

```
Sprint 1 (bağımsız, paralel):
  E1 → src/orchestrator/src/ffi.rs + build.rs ABI assert
  E2 → src/pipeline/pipeline.cpp atomic VkImage
  E3 → src/ui/preview_widget.cpp + copy_optimizer.h is_slot_signaled
  E4 → src/pipeline/copy_optimizer.cpp VK_QUEUE_FAMILY_EXTERNAL barrier

Sprint 2:
  E5 → src/pipeline/external_memory_bridge.cpp texture pointer cache
  E6 → src/ui/main_window.cpp ~MainWindow shutdown çağrısı
  E7 → src/pipeline/gpu/gpu_resource_manager.cpp same_adapter_ fix
  E8 → src/orchestrator/src/ffi.rs + healing.rs Arc<AtomicU32>
  E9 → src/ui/preview_widget.cpp import guard

Sprint 3:
  E10 → src/pipeline/gpu_query_timing.cpp reset + availability bit
  E11 → src/pipeline/frame_pacing.cpp timestamp_us formülü
  E12 → src/ffi/sizeof_check.cpp _reserved field
  E13 → src/ui/preview_widget.h + .cpp poll_frames_ member
  E14 → src/pipeline/external_memory_bridge.cpp destroy sırası

Sprint 4:
  E15 → src/ui/main_window.cpp metrics_timer_ + pollMetrics()
  E16 → src/pipeline/pipeline.cpp + src/ui/preview_widget.cpp RJ_DEBUG_VERBOSE
  E17 → src/pipeline/metrics_collector.cpp MAX_WINDOW = 30
  E18 → src/pipeline/audio/wasapi_capture.cpp exchange(0)
```

---

## Build ve Test Komutu (Her Sprint Sonrası)

```cmd
cd C:\reji-studio
python scripts/build.py
cargo test --manifest-path src/orchestrator/Cargo.toml
build\src\ui\reji_app.exe 2> err.log
type err.log | findstr "VUID|ERROR|semaphore|race"
just abi-check
```

---

## Takip

- [ ] Sprint 1 tamamlandı (E1-E4)
- [ ] Sprint 2 tamamlandı (E5-E9)
- [ ] Sprint 3 tamamlandı (E10-E14)
- [ ] Sprint 4 tamamlandı (E15-E18)
- [ ] Fable 5 beşinci tarama yapıldı

---

*Bu belge C:\reji-studio\docs\FABLE5_BUG_PLAN_V4.md olarak kaydedildi.*
