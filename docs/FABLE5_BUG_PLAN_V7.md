# Reji Studio — Yedinci Tarama Duzeltme Plani (V7)

**Tarih:** 21.06.2026
**Kaynaklar:**
  Opus 4.8    ($1.18) — En kapsamli, 6 kritik yeni bulgu
  GLM-5.2     ($0.13) — 3-4 gercek bulgu
  Kimi K2.7   ($0.32) — build.rs eksikligi
  MiniMax M3  ($0.07) — drainer eksikligi

**Onceki:** FABLE5_BUG_PLAN_V6.md (G1-G13 tamamlandi)

---

## Model Karsilastirmasi

| Model    | Maliyet | Yeni Bulgu | Kalite   |
|----------|---------|-----------|----------|
| Opus 4.8 | $1.18   | 8 kritik  | 5/5      |
| GLM-5.2  | $0.13   | 4 gercek  | 4/5      |
| Kimi K2.7| $0.32   | 2 gercek  | 3/5      |
| MiniMax  | $0.07   | 1 gercek  | 2/5      |

---

## Oncelik Matrisi

| #   | Kaynak   | Sorun                                              | Dosya                    | Oncelik | Sprint   |
|-----|----------|----------------------------------------------------|--------------------------|---------|----------|
| H1  | Opus     | Tek command buffer SIMULTANEOUS_USE + reset spec ihlali | copy_optimizer.cpp  | Kritik  | Sprint 1 |
| H2  | Opus     | Keyed mutex yanlis VkDeviceMemory koruyor — tearing | copy_optimizer.cpp       | Kritik  | Sprint 1 |
| H3  | Opus     | AMD path cross-API sync tamamen yok                | copy_optimizer.cpp       | Kritik  | Sprint 1 |
| H4  | Opus+GLM | slot_gl_signaled_ non-atomic cross-thread erisim  | copy_optimizer.h         | Kritik  | Sprint 1 |
| H5  | GLM      | shared_handle_ CloseHandle eksik — handle leak     | gpu_resource_manager.cpp | Yuksek  | Sprint 2 |
| H6  | GLM      | Pipeline::shutdown() thread-safe degil            | pipeline.cpp             | Yuksek  | Sprint 2 |
| H7  | Opus     | BGRA/RGBA channel swap — preview renk hatasi       | preview_widget.cpp       | Yuksek  | Sprint 2 |
| H8  | Opus     | fps_actual NaN/negatif → MetricState cast UB       | pipeline.cpp + metrics.rs| Yuksek  | Sprint 2 |
| H9  | Kimi     | build.rs ffi.rs'i okumadigi icin RjAction assert eksik | build.rs            | Orta    | Sprint 3 |
| H10 | MiniMax  | Drainer sadece CpuUsage gonderiyor                 | ffi.rs                   | Orta    | Sprint 3 |
| H11 | GLM      | SIMULTANEOUS_USE_BIT gereksiz — per-slot cmd buffer| copy_optimizer.cpp       | Orta    | Sprint 3 |
| H12 | Opus     | compute_cpu_percent wall clock NTP jumpa karsi duyarli | wasapi_capture.cpp  | Dusuk   | Sprint 4 |
| H13 | Opus     | action_processor thread bos kuyrugu poll ediyor    | pipeline.cpp             | Dusuk   | Sprint 4 |
| H14 | Opus     | SEH filtresi cok genis — stack overflow maskeleniyor| wasapi+srt+pipeline     | Dusuk   | Sprint 4 |
| H15 | Opus     | gl_sync_sem_pool_ cift — fallback tehlikeli        | copy_optimizer.cpp       | Yuksek  | Sprint 5 |
| H16 | Kimi+MM  | HealingMonitor ticker starvation (biased select)   | healing.rs               | Yuksek  | Sprint 5 |
| H17 | Kimi     | execute_copy basarisiz submit sonrasi deadlock     | copy_optimizer.cpp       | Kritik  | Sprint 5 |
| H18 | Kimi     | WasapiCapture non-atomic members — data race       | wasapi_capture.h         | Yuksek  | Sprint 5 |
| H19 | Kimi     | GL texture filter eksik → incomplete → siyah ekran | preview_widget.cpp       | Yuksek  | Sprint 2 |
| H20 | GLM      | VulkanInitializer static destroy order (Zig sonrasi)| vulkan_initializer.cpp  | Orta    | Sprint 5 |

---

## Sprint 1 — Kritik Sync ve Race

---

### H1 — Tek Command Buffer SIMULTANEOUS_USE + Reset

**Kaynak:** Opus 4.8 (C-2), GLM-5.2 (1.2)

**Sorun:**
```cpp
// copy_optimizer.cpp — tek command_buffer_ her frame reset edilip yeniden kullaniliyor
// VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT ile reset = spec ihlali
// VUID-vkResetCommandBuffer-commandBuffer-00045
```
`SIMULTANEOUS_USE` birden fazla pending submit icin — ama timeline wait
bir sonraki execute_copy'de yapiliyor, ayni frame'de degil.

**Cozum:**
Per-slot command buffer pool ekle:
```cpp
// copy_optimizer.h:
VkCommandBuffer command_buffers_[POOL_SIZE];

// execute_copy() icinde:
VkCommandBuffer cmd = command_buffers_[slot];
// O slot'un timeline degerini bekle:
// (submit oncesinde zaten yapiliyor — sadece cmd buffer'i slot'a bagla)
vkResetCommandBuffer(cmd, 0);
vkBeginCommandBuffer(cmd, &begin_info_without_simultaneous_use);
```
`VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT` kaldir.

---

### H2 — Keyed Mutex Yanlis VkDeviceMemory

**Kaynak:** Opus 4.8 (V-1)

**Sorun:**
```cpp
// execute_copy() icinde:
km_memory_ = bridge_->get_staging_memory_for_image(staging_vk);
// staging_vk bridge pool'undan geliyor — shared_texture_'dan degil
// Keyed mutex shared_texture_'a ait — farkli memory!
// VkWin32KeyedMutexAcquireReleaseInfoKHR yanlis memory'yi koruyor
// Sonuc: D3D11-Vulkan sync yok, torn frames
```

**Cozum:**
Keyed mutex icin dogru memory path:
```cpp
// execute_copy() icinde:
// shared_texture_'dan import edilen VkDeviceMemory kullan:
km_memory_ = bridge_->get_shared_texture_memory();
// Yeni getter gerekiyor: ExternalMemoryBridge'de
// shared_texture_'dan import edilen VkDeviceMemory dondursun
```

---

### H3 — AMD Path Cross-API Sync Tamamen Yok

**Kaynak:** Opus 4.8 (V-2)

**Sorun:**
AMD'de `use_keyed_mutex_=false` — keyed mutex yok.
`VK_QUEUE_FAMILY_EXTERNAL` acquire barrier var ama
D3D11 yazisinin tamamlandigini garanti eden hicbir primitive yok.
`oldLayout=UNDEFINED` ile discard edilen image D3D11 yazisini kaybedebilir.

**Cozum (minimal):**
D3D11 Flush + CPU fence ile garantile:
```cpp
// capture_dxgi.cpp capture_next() — keyed mutex yoksa:
if (!use_keyed_mutex_) {
    // D3D11 komutlarinin bitmesini garanti et
    display_ctx_->d3d_context()->Flush();
    // GPU query ile tamamlanmayi bekle (mevcut copy_fence_ benzeri)
}
```
Veya ROADMAP'e "AMD path unsafe" olarak belgele, production'da keyed mutex zorunlu yap.

---

### H4 — slot_gl_signaled_ Non-Atomic Cross-Thread

**Kaynak:** Opus 4.8 (V-4)

**Sorun:**
```cpp
// copy_optimizer.h:
std::array<bool, 3> slot_gl_signaled_{false, false, false};
uint32_t last_used_slot_ = 0;
```
Frame thread yazar (`execute_copy`), GL thread okur (`paintGL`).
`bool` ve `uint32_t` — atomic degil — torn read mumkun.

**Cozum:**
```cpp
// copy_optimizer.h:
std::array<std::atomic<bool>, 3> slot_gl_signaled_{};
std::atomic<uint32_t> last_used_slot_{0};

// is_slot_signaled():
return slot_gl_signaled_[slot].load(std::memory_order_acquire);

// clear_gl_signal():
slot_gl_signaled_[slot].store(false, std::memory_order_release);

// execute_copy() basarili submit sonrasi:
slot_gl_signaled_[slot].store(true, std::memory_order_release);
last_used_slot_.store(slot, std::memory_order_release);

// next_slot() ve last_used_slot():
return last_used_slot_.load(std::memory_order_acquire);
```

---

## Sprint 2 — Yuksek Oncelikli Duzeltmeler

---

### H5 — shared_handle_ CloseHandle Eksik

**Kaynak:** GLM-5.2 (2.3)

**Sorun:**
```cpp
// gpu_resource_manager.cpp:
HANDLE shared_handle_ = nullptr;
// create_cross_adapter_shared(): GetSharedHandle(&shared_handle_)
// shutdown(): shared_handle_ = nullptr; // CloseHandle YOK!
```
Legacy shared handle (NT handle degil) CloseHandle gerektiriyor.

**Cozum:**
```cpp
// shutdown() icinde:
if (shared_handle_ && shared_handle_ != INVALID_HANDLE_VALUE) {
    CloseHandle(shared_handle_);
    shared_handle_ = nullptr;
}
```

---

### H6 — Pipeline::shutdown() Thread-Safe Degil

**Kaynak:** GLM-5.2 (1.4)

**Sorun:**
Iki thread ayni anda `Pipeline::shutdown()` cagirabilir.
`std::once_flag` yok — double-shutdown race.

**Cozum:**
```cpp
// pipeline.h:
std::once_flag shutdown_once_;

// pipeline.cpp shutdown():
std::call_once(shutdown_once_, [this]() {
    // mevcut shutdown kodu
});
```

---

### H7 — BGRA/RGBA Channel Swap Preview'da

**Kaynak:** Opus 4.8 (V-3)

**Sorun:**
Vulkan target: `VK_FORMAT_B8G8R8A8_UNORM` (BGRA)
GL texture: `GL_RGBA8` ile import ediliyor
`vkCmdBlitImage` channel swizzle yapmaz
Sonuc: preview'da kirmizi/mavi kanallar yer degistirilmis

**Cozum — en kolay:**
Fragment shader'da swizzle ekle:
```glsl
// preview.frag:
fragColor = vec4(texture(tex, uv).bgra); // BGRA → RGBA
```
Veya GL texture'i `GL_BGRA` internal format ile olustur.

---

### H8 — fps_actual NaN/Negatif Cast

**Kaynak:** Opus 4.8 (C-6)

**Sorun:**
```cpp
// pipeline.cpp CpuMeter:
m.fps_actual = float(qpc_freq) / float(frame_start - last_frame_ticks);
// frame_start - last_frame_ticks = 0 veya negatif olabilir ilk frame'de
// NaN veya inf → Rust tarafinda u32 cast = 0 (defined ama yanlis)
```

**Cozum:**
```cpp
auto delta = frame_start - last_frame_ticks;
m.fps_actual = (delta > 0)
    ? std::clamp(float(qpc_freq) / float(delta), 0.0f, 240.0f)
    : 0.0f;
```

Rust tarafinda da dogrulama:
```rust
// metrics.rs update():
let fps = sample.fps_actual;
if fps.is_finite() && fps >= 0.0 {
    self.fps = fps;
}
```

---

## Sprint 3 — Orta Oncelik

---

### H9 — build.rs ffi.rs'i Okumadigi icin RjAction Assert Eksik

**Kaynak:** Kimi K2.7

**Sorun:**
```rust
// build.rs check_abi_sizes():
// parse_rust_size_asserts("src/orchestrator/src/metrics.rs")
// metrics.rs'de sadece MetricSample asserts var
// RjAction ve RjCommand asserts ffi.rs'de — hic okunmuyor!
```
ABI check RjAction/RjCommand icin kordur.

**Cozum:**
```rust
// build.rs:
// metrics.rs + ffi.rs ikisini de oku:
let rust_sizes_metrics = parse_rust_size_asserts(
    "src/orchestrator/src/metrics.rs")?;
let rust_sizes_ffi = parse_rust_size_asserts(
    "src/orchestrator/src/ffi.rs")?;
// Her ikisini birlestir:
rust_sizes.extend(rust_sizes_ffi);
```

---

### H10 — Drainer Sadece CpuUsage Gonderiyor

**Kaynak:** MiniMax M3

**Sorun:**
```rust
// ffi.rs drainer loop:
bus_system.send(SystemEvent::CpuUsage { ratio: sample.cpu_percent / 100.0 });
// GpuUsage, MemUsage, DiskWarning hic gonderilmiyor
// HealingMonitor GpuUsage>=0.98'de ReduceBitrate tetikliyor
// Ama GpuUsage hic gelmedigi icin bu dal hic ateslenmiyor
```

**Cozum:**
```rust
// ffi.rs drainer:
bus_system.send(SystemEvent::CpuUsage {
    ratio: sample.cpu_percent / 100.0
});
// GPU metric ekle (simdilik cpu'dan tahmin veya 0):
if sample.gpu_load_pct > 0 {
    let _ = bus_system.send(SystemEvent::GpuUsage {
        ratio: sample.gpu_load_pct as f32 / 100.0
    });
}
```

---

### H11 — SIMULTANEOUS_USE_BIT Gereksiz

**Kaynak:** GLM-5.2 (1.2) — H1 ile birlikte cozulebilir

**Sorun:**
H1 duzeltmesi (per-slot command buffer) yapilinca
`VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT` tamamen gereksiz hale gelir.

**Cozum:** H1 ile birlikte — begin_info'dan kaldir.

---

## Sprint 4 — Dusuk Oncelik

---

### H12 — compute_cpu_percent NTP Jump

**Kaynak:** Opus 4.8 (C-5)

**Sorun:**
`GetSystemTimeAsFileTime` NTP jump'lardan etkileniyor.
Geri giden zaman → `d_wall` unsigned underflow → garbage CPU%.

**Cozum:**
```cpp
// wasapi_capture.cpp compute_cpu_percent():
// GetSystemTimeAsFileTime yerine:
LARGE_INTEGER qpc;
QueryPerformanceCounter(&qpc);
// wall reference olarak QPC kullan
```

---

### H13 — action_processor Bos Kuyrugu Poll Ediyor

**Kaynak:** Opus 4.8 (C-4/P-2)

**Sorun:**
`enqueue_action()` hicbir yerden cagrilmiyor.
Thread 5ms'de bir bos kuyrugu poll ediyor — wasted wakeup, laptop battery drain.

**Cozum (minimal):**
Sleep'i 100ms'e cikart:
```cpp
// pipeline.cpp action_processor_main():
QThread::msleep(100); // 5 → 100ms
```
Veya tamamen kaldir — C4 ile birlikte degerlendirilebilir.

---

### H14 — SEH Filtresi Cok Genis

**Kaynak:** Opus 4.8 (S-2)

**Sorun:**
`EXCEPTION_EXECUTE_HANDLER` tum exception'lari yakaliyor.
`STATUS_STACK_OVERFLOW` yakalanirsa guard page reset edilmiyor.

**Cozum:**
```cpp
// seh_filter():
LONG seh_filter(DWORD code) {
    // Stack overflow ve critical exception'lari gecir:
    if (code == STATUS_STACK_OVERFLOW ||
        code == EXCEPTION_BREAKPOINT) {
        return EXCEPTION_CONTINUE_SEARCH; // gecir
    }
    return EXCEPTION_EXECUTE_HANDLER; // yakala
}
```

---

## Sprint 5 — Ek Bulgular (Karsilastirma Analizinden)

---

### H15 — gl_sync_sem_pool_ Cift Semaphore Tehlikesi

**Kaynak:** Opus 4.8 (M-4)

**Sorun:**
```cpp
// GpuCopyOptimizer'da kendi gl_sync_sem_pool_[3] var
// ExternalMemoryBridge'de de ayri gl_sync_sem_pool_[3] var (GL'e export edilen)
// execute_copy bridge semaphore'u kullaniyor ama fallback olarak kendi pool'una duser
// GL tarafinda sadece bridge semaphore'lari import edilmis
// Fallback tetiklenirse GL hicbir zaman beklemez → GPU race
```

**Cozum:**
```cpp
// copy_optimizer.h'den gl_sync_sem_pool_[] kaldir
// Her zaman bridge'den gelen semaphore'u kullan:
// execute_copy(..., VkSemaphore gl_sync_sem) — caller saglamali
// Fallback yolu tamamen kaldir
```

---

### H16 — HealingMonitor Ticker Starvation

**Kaynak:** Kimi K2.7 + MiniMax M3

**Sorun:**
```rust
// healing.rs run() icinde:
tokio::select! { biased;
    result = self.system_rx.recv() => { ... }
    result = self.media_rx.recv() => { ... }
    _ = ticker.tick() => { ... } // STARVED!
}
// biased: system_rx surekli mesaj gelirse ticker hic calismaz
// evaluate_adaptive(), evaluate_predictive() hic tetiklenmez
// Cooldown recovery, adaptif esik guncelleme durur
```

**Cozum:**
```rust
// Ticker'i ayri task'a tasiy veya fair select kullan:
// Secenek A — biased kaldirilir (fair round-robin):
tokio::select! {
    result = self.system_rx.recv() => { ... }
    result = self.media_rx.recv() => { ... }
    _ = ticker.tick() => { ... }
}

// Secenek B — ticker ayri spawn:
tokio::spawn(async move {
    let mut ticker = tokio::time::interval(...);
    loop {
        ticker.tick().await;
        // periodic eval
    }
});
```

---

### H17 — execute_copy Basarisiz Submit Sonrasi Deadlock

**Kaynak:** Kimi K2.7

**Sorun:**
```cpp
// copy_optimizer.cpp execute_copy():
// F3 ile state corruption duzeltildi ama deadlock durumu kaldi:
//
// submit basarisiz olursa:
//   signal_value_for_submit_ = timeline_counter_ (arttirildi)
//   ama VkQueueSubmit basarisiz — GPU bu degeri hicbir zaman sinyallemez
//
// Bir sonraki execute_copy():
//   vkWaitSemaphores(signal_value_for_submit_) // SONSUZA BEKLER
```

**Cozum:**
```cpp
// execute_copy() basarisiz submit sonrasi:
VkResult result = vkQueueSubmit(...);
if (result != VK_SUCCESS) {
    // timeline_counter_'i geri al — submit olmadi
    --timeline_counter_;
    signal_value_for_submit_ = timeline_counter_; // onceki gecerli degere don
    fprintf(stderr, "[CopyOptimizer] submit failed: %d\n", result);
    return false;
}
// Basarili — simdi guncelle
signal_value_for_submit_ = timeline_counter_;
```

---

### H18 — WasapiCapture Non-Atomic Members

**Kaynak:** Kimi K2.7

**Sorun:**
```cpp
// wasapi_capture.h:
int actual_channels_ = 0;      // non-atomic
int actual_sample_rate_ = 0;   // non-atomic
int actual_bits_ = 0;          // non-atomic
bool actual_is_float_ = false; // non-atomic
uint32_t buffer_frames_ = 0;   // non-atomic

// capture thread yazıyor (open_device_and_init_engine)
// main thread okuyor (get_channels(), get_sample_rate() vb.)
// Data race — UB
```

**Cozum:**
```cpp
// wasapi_capture.h:
std::atomic<int> actual_channels_{0};
std::atomic<int> actual_sample_rate_{0};
std::atomic<int> actual_bits_{0};
std::atomic<bool> actual_is_float_{false};
std::atomic<uint32_t> buffer_frames_{0};
```

---

### H19 — GL Texture Min/Mag Filter Eksik

**Kaynak:** Kimi K2.7

**Sorun:**
```cpp
// preview_widget.cpp GL interop texture olusturulurken:
glBindTexture(GL_TEXTURE_2D, gl_interop_textures_[i]);
glTexStorageMem2DEXT(...);
// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR) YOK!
// glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR) YOK!
// Default min filter: GL_NEAREST_MIPMAP_LINEAR — mipmap olmayan texture = incomplete
// Incomplete texture = siyah ekran render eder
```

**Cozum:**
```cpp
// GL interop texture olusturulduktan sonra:
glBindTexture(GL_TEXTURE_2D, gl_interop_textures_[i]);
glTexStorageMem2DEXT(...);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
```

---

### H20 — VulkanInitializer Static Destroy Order (Zig Sonrasi Risk)

**Kaynak:** GLM-5.2 (Finding 2.2)

**Sorun:**
Zig migrasyonu oncesinde C++ singleton destructor'i vkDestroyDevice yapıyordu.
Simdi `vulkan_init_shutdown()` Zig tarafinda cagiriliyor.
C++ static destruction order TU'lar arasi belirsiz:
```cpp
// vulkan_initializer.cpp:
VulkanInitializer* VulkanInitializer::get() {
    static VulkanInitializer instance; // static local
    return &instance;
}
// ~VulkanInitializer() → vulkan_init_shutdown() → vkDestroyDevice (Zig)
//
// Eger Pipeline baska bir static tarafindan tutuluyorsa
// ve o static once yikilirsa:
// Pipeline::~Pipeline() → ExternalMemoryBridge::shutdown() → vkDestroyImage(device_)
// AMA device zaten Zig tarafinda yok edilmis olabilir → UAF
```

**Cozum:**
```cpp
// Pipeline'in statik omur tasimayacagini dokumante et
// Veya VulkanInitializer singleton'ini Pipeline destructor'indan once
// manuel olarak shutdown et:
// main.cpp'de:
//   pipeline.reset(); // once pipeline
//   VulkanInitializer::get()->shutdown(); // sonra vulkan
```

---

## Dogrulama

```cmd
cd C:\reji-studio
python scripts/build.py --clean
build\src\ui\reji_app.exe --headless --frames 5 2> err.log
type err.log | findstr "VUID\|ERROR\|race\|atomic"
cargo test --manifest-path src/orchestrator/Cargo.toml
just abi-check
```

---

## Takip

- [x] Sprint 1 tamamlandi (H1-H4)
- [x] Sprint 2 tamamlandi (H5-H8, H19)
- [ ] Sprint 3 tamamlandi (H9-H11)
- [ ] Sprint 4 tamamlandi (H12-H14)
- [ ] Sprint 5 tamamlandi (H15-H20)
- [ ] Sekizinci tarama yapildi

---

## Model Performans Notu

```
Opus 4.8   $1.18 — En derin, V-1(keyed mutex memory) + H7(BGRA swap) + H15(cift sem)
                    Fable 5'in ~%80'i kalitesinde
GLM-5.2    $0.13 — Surpriz kalite, H20(Zig static order) benzersiz
Kimi K2.7  $0.32 — H17(deadlock), H18(atomic), H19(GL filter) benzersiz
MiniMax    $0.07 — drainer bug, yuzeysel
Toplam     $1.70 — Fable 5 tek taramasinin ~%62'si maliyetle

Ideal kombinasyon: Opus 4.8 + GLM-5.2 = $1.31
                   Fable 5'in %80+ kalitesi, %48 maliyeti
```

---

*Bu belge C:\reji-studio\docs\FABLE5_BUG_PLAN_V7.md olarak kaydedilmeli.*
