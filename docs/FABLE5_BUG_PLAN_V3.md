# Reji Studio — Fable 5 Üçüncü Tarama Düzeltme Planı (v3)

**Tarih:** 11.06.2026
**Kaynak:** fable5-review-all-2026-06-11_18-26.md — 62 dosya, 209K token, $2.73
**Önceki:** FABLE5_BUG_PLAN_V2.md (C1-C9, T1-T3 tamamlandı)

---

## Öncelik Matrisi

| #  | Sorun | Dosya | Öncelik | Etki | Sprint |
|----|-------|-------|---------|------|--------|
| D1 | Binary GL semaphore yeniden-sinyal ihlali (§1.4) | copy_optimizer.cpp + preview_widget.cpp | 🔴 Kritik | GL/VK veri yarışı | Sprint 1 |
| D2 | Per-image layout tracking eksik — UNDEFINED oldLayout (§1.2) | external_memory_bridge.cpp + copy_optimizer.cpp | 🔴 Kritik | VUID ihlali / bozuk görüntü | Sprint 1 |
| D3 | Keyed-mutex submit zinciri stack lifetime hatası (§1.1) | copy_optimizer.cpp | 🔴 Kritik | Dangling pNext pointer / UB | Sprint 1 |
| D4 | GPU idle garantisi yok — semaphore/pool/image destroy (§2.1, §2.2) | copy_optimizer.cpp + external_memory_bridge.cpp | 🔴 Kritik | VUID device loss | Sprint 1 |
| D5 | GL draw fence eksik — pool slot GL okurken Vulkan yazar (§3.2) | preview_widget.cpp | 🔴 Kritik | GL/VK tearing, data race | Sprint 2 |
| D6 | `~MainWindow()` frame thread join etmiyor (§1.6) | main_window.cpp | 🔴 Kritik | Pipeline UAF / DXGI leak | Sprint 2 |
| D7 | Cursor-only frame serbest bırakılmış DXGI surface döndürüyor (§1.13) | capture_dxgi.cpp | 🟠 Yüksek | Torn frame / undefined content | Sprint 2 |
| D8 | Duplicate Rust tree — iki çelişkili rule engine (§5.1, §1.10, §1.11) | src/orchestrator/*.rs (root) | 🟠 Yüksek | Derleme hatası potansiyeli | Sprint 2 |

---

## Sprint 1 — Kritik VK/GL Sync + GPU Teardown

**Hedef:** Production blocker senkronizasyon hataları
**Tahmini süre:** 1 oturum

---

### D1 — Binary GL Semaphore Yeniden-Sinyal İhlali (§1.4)

**Sorun:**
3-slotlu binary semaphore pool azaltıyor ama ortadan kaldırmıyor:
`paintGL()` slot'u atlarsa (timeout/execute_copy başarısız) aynı slot
`frame_counter % 3` ile tekrar geldiğinde `vkQueueSubmit` zaten
sinyallenmiş binary semaphore'u tekrar sinyalliyor.
VUID-vkQueueSubmit-pSignalSemaphores-00067.

**Çözüm:**
Per-slot "signaled ama wait edilmedi" state takibi;
önceki sinyal tüketilmediyse `gl_sync_sem = VK_NULL_HANDLE` geçir.

---

### D2 — Per-Image Layout Tracking (§1.2)

**Sorun:**
`staging_layout_` / `target_layout_` tek member variable — 3 pool
slot için paylaşılıyor. Slot 0'ı `TRANSFER_SRC_OPTIMAL`'e taşıyan
barrier, slot 1 için `oldLayout` olarak aynı değeri kullanıyor;
slot 1 henüz `UNDEFINED`. VUID-VkImageMemoryBarrier-oldLayout ihlali.

**Çözüm:**
```cpp
std::array<VkImageLayout, POOL_SIZE> staging_layouts_;
std::array<VkImageLayout, POOL_SIZE> target_layouts_;
// D3D11-imported staging için her zaman UNDEFINED başlat
```

---

### D3 — Keyed-Mutex Stack Lifetime (§1.1)

**Sorun:**
`keyed_mutex_info`, `km_acquire_key`, `km_release_key` stack local.
`timeline_submit_info_.pNext = &keyed_mutex_info` persist eden member
struct'a stack adres zincirliyor. Sonraki re-submit dangling pNext.

**Çözüm:**
`keyed_mutex_info_` ve key değerlerini member field yap;
`timeline_submit_info_.pNext = nullptr` submit sonrası temizle.

---

### D4 — GPU Idle Garantisi (§2.1, §2.2)

**Sorun:**
`GpuCopyOptimizer::shutdown()` `vkQueueWaitIdle` olmadan
semaphore/command pool destroy ediyor. In-flight submit GPU
destroy edilen kaynağa erişiyor.

**Çözüm:**
```cpp
// shutdown() başında:
if (queue_ != VK_NULL_HANDLE)
    vkQueueWaitIdle(queue_);
```

---

## Sprint 2 — GL Fence + Thread Safety + DXGI + Rust Tree

**Hedef:** Runtime stabilite + kaynak sızdırmama
**Tahmini süre:** 1 oturum

---

### D5 — GL Draw Fence — Pool Slot Reuse Sync (§3.2)

**Sorun:**
Vulkan bir sonraki frame'i blit ederken GL aynı pool slot
texture'ından okumaya devam edebilir. `glWaitSemaphoreEXT` Vulkan→GL
yönünü kapatiyor ama GL→Vulkan (slot serbest bırakılana kadar blit
başlatma) korunmuyor.

**Çözüm:**
```cpp
// preview_widget.h
GLsync gl_draw_fences_[3] = {nullptr, nullptr, nullptr};

// paintGL() sonunda:
if (pfn_DeleteSync_ && gl_draw_fences_[slot]) pfn_DeleteSync_(gl_draw_fences_[slot]);
gl_draw_fences_[slot] = pfn_FenceSync_
    ? pfn_FenceSync_(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;

// execute_copy() öncesinde:
if (gl_draw_fences_[pool_idx] && pfn_ClientWaitSync_)
    pfn_ClientWaitSync_(gl_draw_fences_[pool_idx], GL_SYNC_FLUSH_COMMANDS_BIT, 1000000);
```

Not: `glFenceSync`/`glClientWaitSync`/`glDeleteSync` GL 3.2 core —
`QOpenGLFunctions` (ES2 level) expose etmez; `getProcAddress` ile yükle.

---

### D6 — stopFrameThread Helper — Destructor Join (§1.6)

**Sorun:**
`~MainWindow()` frame thread join etmiyor, yalnızca `closeEvent()`
yapıyor. Menü Quit → `qApp->quit()` → parent deletion yolunda
`frame_thread_` hâlâ `run_frame()` çalıştırırken `pipeline_` (by-value
member) destroy ediliyor → use-after-free.
Ayrıca `frame_thread_->terminate()` GPU API (DXGI/NVENC) ortasında
keyed-mutex leak + duplication `frame_held_` state bırakıyor.

**Çözüm:**
```cpp
void MainWindow::stopFrameThread() {
    if (frame_thread_ && frame_thread_->isRunning()) {
        frame_thread_->requestInterruption();
        if (!frame_thread_->wait(5000))
            fprintf(stderr, "[MainWindow] Frame thread 5s timeout\n");
        // terminate() YOK — GPU API ortasında kesilmez
    }
}
// closeEvent() ve ~MainWindow() her ikisi de stopFrameThread() çağırır
```

---

### D7 — Cursor-Only Frame — Released DXGI Surface (§1.13)

**Sorun:**
`AccumulatedFrames == 0` (cursor hareketi): yeni `AcquireNextFrame`
çağrısı S_OK döndü ama `frame_tex_` önceki acquire'dan kalan texture.
O texture'ın DXGI sahipliği önceki `ReleaseFrame()` ile geri verildi.
`capture_next()` içinde `CopyResource` bu surface'ı okurken DXGI
aynı anda yazabilir → torn frame / undefined contents.

**Çözüm:**
```cpp
// acquire() içinde, FAILED(hr) kontrolünden sonra:
if (info.AccumulatedFrames == 0) {
    duplication_->ReleaseFrame();
    frame_held_ = false;
    return false;
}
```

---

### D8 — Duplicate Rust Tree Temizliği (§5.1, §1.10, §1.11)

**Sorun:**
`src/orchestrator/{lib.rs, rule_engine.rs, metrics.rs}` ile
`src/orchestrator/src/{lib.rs, rules.rs, ffi.rs, ...}` iki paralel ağaç.
- Root `rule_engine.rs`: `"frame_drop_pct>"` pattern (space olmadan)
  `"frame_drop_pct > 10"` içinde bulunamaz → tüm birim testler fail.
- Root `metrics.rs`: `crate::enqueue_action` yok → derleme hatası
  (canonical crate root bu dosyayı include etmiyor, yani dead code).
- `build.rs` cbindgen: `src/orchestrator/src/metrics.rs`'e bakıyor ✅

**Çözüm:**
Root üç dosyayı sil; canonical tree `src/orchestrator/src/` kalır.
`cargo build` ile doğrula.

---

## Dosya Düzenleme Sırası

```
Sprint 1 (bağımsız, paralel):
  D1 → copy_optimizer.cpp semaphore slot state
  D2 → external_memory_bridge.cpp + copy_optimizer.cpp per-image layout
  D3 → copy_optimizer.cpp keyed_mutex_info_ member
  D4 → copy_optimizer.cpp + external_memory_bridge.cpp vkQueueWaitIdle

Sprint 2:
  D5 → preview_widget.h + preview_widget.cpp GL draw fence
  D6 → main_window.h + main_window.cpp stopFrameThread()
  D7 → capture_dxgi.cpp AccumulatedFrames==0 guard
  D8 → src/orchestrator/ root .rs sil
```

---

## Build ve Test Komutu (Her Sprint Sonrası)

```cmd
cd C:\reji-studio
python scripts/build.py
cargo test --manifest-path src/orchestrator/Cargo.toml
build\src\ui\reji_app.exe 2> err.log
type err.log | findstr "ERROR|FAILED|VUID|assert|timeout"
```

---

## Takip

- [x] Sprint 1 tamamlandı (D1, D2, D3, D4)
- [x] Sprint 2 tamamlandı (D5, D6, D7, D8)
- [x] Sprint 3 tamamlandı (D9, D10, D11, D12, D13, D14, D15, D16)
- [x] Sprint 4 tamamlandı (D17, D18)
- [ ] Fable 5 dördüncü tarama yapıldı

---

*Bu belge C:\reji-studio\docs\FABLE5_BUG_PLAN_V3.md olarak kaydedildi.*
