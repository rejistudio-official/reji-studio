# Reji Studio — Vulkan/D3D11/GL Senkronizasyon Diyagramı

**Tarih:** 2026-06-11  
**Kapsam:** v0.5.2 frame pipeline — capture → encode/GL zinciri

---

## Pipeline Genel Görünümü

```
 DXGI Capture Thread             Frame Thread (pipeline.cpp)             GL Thread (Qt)
 ─────────────────               ────────────────────────────            ──────────────
 D3D11 VRAM                      Vulkan Queue (AMD 780M)                 OpenGL Context
 (display GPU)                   (same device via LUID match)            (QOpenGLWidget)

 ┌─────────────┐
 │ DXGI frame  │
 │ tex (AMD)   │
 └──────┬──────┘
        │ D3D11::CopyResource
        ▼
 ┌─────────────────┐
 │ shared_tex_     │  ──── [SYNC 1] ────►  keyed mutex key 0→1
 │ display_        │                        D3D11::Flush +
 └─────────────────┘                        wait_display_gpu_idle()
        │
        │  NT SharedHandle (IDXGIResource1::CreateSharedHandle)
        ▼
 ┌─────────────────────┐
 │ VkImage             │  ──── [SYNC 2] ────►  VkWin32KeyedMutexAcquireReleaseInfoKHR
 │ (D3D11 import)      │                        acquire key=1 → release key=0
 │ staging pool[slot]  │
 └──────┬──────────────┘
        │ vkCmdBlitImage
        │ (layout: UNDEFINED→TRANSFER_SRC→SHADER_READ_ONLY)
        ▼
 ┌─────────────────────┐
 │ VkImage             │  ──── [SYNC 3] ────►  timeline semaphore signal N
 │ (GL interop target) │                        VK_KHR_timeline_semaphore
 │ gl_target_pool[slot]│
 └──────┬──────────────┘
        │ NT handle (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT)
        ▼
 ┌─────────────────────┐
 │ GL texture          │  ──── [SYNC 4] ────►  gl_sync_semaphore[slot] signal
 │ gl_interop_         │                        glWaitSemaphoreEXT wait
 │ textures_[slot]     │
 └──────┬──────────────┘
        │ glDrawArrays
        ▼
 ┌─────────────────────┐
 │ GL render output    │  ──── [SYNC 5] ────►  3-slot round-robin pool
 │ (QOpenGLWidget)     │                        frame_counter_ % 3
 └─────────────────────┘
```

---

## Sync 1 — D3D11 Capture → Shared Texture

| Alan            | Değer |
|-----------------|-------|
| **Primitive**   | D3D11 GPU Event Query (`ID3D11Query`, `D3D11_QUERY_EVENT`) |
| **Kaynak**      | DXGI capture frame texture (AMD VRAM) |
| **Hedef**       | `shared_tex_display_` (AMD VRAM, shared handle) |
| **Operasyon**   | `d3d_context()->CopyResource()` + `Flush()` |
| **Acquire key** | — (display GPU tamamen sahip, kilit yok) |
| **Release key** | `ReleaseSync(1)` — encode GPU'ya geçer |
| **Signal**      | `ID3D11Query::End()` → CPU spin: `GetData()` döngüsü |
| **Wait**        | `GetData(D3D11_ASYNC_GETDATA_DONOTFLUSH)` == `S_OK && done==TRUE` |

**Durum:** `same_adapter_ = true` modunda aktif (cross-adapter path devre dışı).  
**Dosya:** `src/pipeline/capture/gpu_resource_manager.cpp:119` (`wait_display_gpu_idle`)

---

## Sync 2 — Shared Texture → Vulkan Staging (Keyed Mutex)

| Alan              | Değer |
|-------------------|-------|
| **Primitive**     | `VkWin32KeyedMutexAcquireReleaseInfoKHR` |
| **Extension**     | `VK_KHR_win32_keyed_mutex` |
| **Acquire key**   | `1` (D3D11 `ReleaseSync(1)` ile bıraktı) |
| **Release key**   | `0` (D3D11 tekrar `AcquireSync(0)` yapabilir) |
| **Acquire timeout** | `UINT32_MAX` (sonsuz bekle) |
| **pNext zinciri** | `VkSubmitInfo.pNext` → `VkTimelineSemaphoreSubmitInfoKHR.pNext` → `VkWin32KeyedMutexAcquireReleaseInfoKHR` |
| **Memory**        | `d3d11_staging_memory` (VkDeviceMemory, D3D11 NT handle'dan import) |
| **Handle type**   | `VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT` |

**Not:** `d3d11_staging_memory == VK_NULL_HANDLE` ise keyed mutex atlanır (same-adapter yolu).  
**Dosya:** `src/pipeline/copy_optimizer.cpp:249-268`

---

## Sync 3 — Vulkan Staging → Vulkan Target (Timeline + Blit)

### Layout Geçişleri

| Geçiş | Image | Old Layout | New Layout | srcStage | dstStage |
|-------|-------|------------|------------|----------|----------|
| Blit öncesi (staging) | `d3d11_staging_vk` | `staging_layout_` (ilk çağrıda UNDEFINED) | `TRANSFER_SRC_OPTIMAL` | `TRANSFER_BIT` | `TRANSFER_BIT` |
| Blit öncesi (target) | `vulkan_target` | `target_layout_` (ilk çağrıda UNDEFINED) | `TRANSFER_DST_OPTIMAL` | `TOP_OF_PIPE_BIT` | `TRANSFER_BIT` |
| Blit sonrası (target) | `vulkan_target` | `TRANSFER_DST_OPTIMAL` | `SHADER_READ_ONLY_OPTIMAL` | `TRANSFER_BIT` | `FRAGMENT_SHADER_BIT` |

### Timeline Semaphore

| Alan              | Değer |
|-------------------|-------|
| **Primitive**     | `VkSemaphore` (timeline, `VK_SEMAPHORE_TYPE_TIMELINE`) |
| **Extension**     | `VK_KHR_timeline_semaphore` |
| **Signal value**  | `timeline_counter_ += FRAME_INCREMENT` (`FRAME_INCREMENT = 1`) |
| **Wait value**    | `signal_value_for_submit_` (bir önceki frame'in sinyal değeri) |
| **Wait point**    | `execute_copy()` giriş: `pfn_wait_semaphores_(device_, ..., UINT64_MAX)` |
| **Poll**          | `is_copy_ready()`: `pfn_get_semaphore_counter_value_()` + `pfn_wait_semaphores_(..., timeout=0)` |
| **Amaç**          | Komut buffer'ını önceki frame tamamlanmadan yeniden kullanmama |

**Dosya:** `src/pipeline/copy_optimizer.cpp:133-147` (wait), `230-247` (signal)

---

## Sync 4 — Vulkan Target → GL Texture (Binary Semaphore Pool)

### Pool Seçimi

| Alan              | Değer |
|-------------------|-------|
| **Strateji**      | Round-robin, 3 slot |
| **Slot hesabı**   | `pool_idx = frame_counter_ % 3` (paintGL'de artırılır) |
| **Amaç**          | Aynı binary semaphore'u tüketilmeden yeniden sinyallemeyi önler (Vulkan spec §7.4) |

### VK Tarafı (Signal)

| Alan              | Değer |
|-------------------|-------|
| **Primitive**     | `VkSemaphore` (binary, `VK_SEMAPHORE_TYPE_BINARY`) |
| **Signal value**  | `0` (binary semaphore — değer yoksayılır, `pSignalSemaphoreValues[1] = 0`) |
| **Export type**   | `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT` |
| **Handle alımı**  | `vkGetSemaphoreWin32HandleKHR` |
| **Submit pozisyonu** | `signal_semaphores_[1]` — timeline ile birlikte aynı `vkQueueSubmit` |

### GL Tarafı (Wait)

| Alan              | Değer |
|-------------------|-------|
| **Primitive**     | `GLuint gl_sync_semaphores_[3]` |
| **Import**        | `glImportSemaphoreWin32HandleEXT(sem, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle)` |
| **Wait çağrısı**  | `glWaitSemaphoreEXT(gl_sync_semaphores_[pool_idx], 0, nullptr, 1, &tex, &layout)` |
| **Beklenen layout** | `GL_LAYOUT_SHADER_READ_ONLY_EXT` |
| **Wait noktası**  | `paintGL()` — `glDrawArrays` öncesi |

**Dosya:**  
- VK: `src/pipeline/gpu/external_memory_bridge.cpp:346-401` (create), `src/pipeline/copy_optimizer.cpp:236-247` (signal)  
- GL: `src/ui/preview_widget.cpp:449-453` (wait)

---

## Sync 5 — GL Render → Vulkan Reuse (Pool Rotasyonu)

| Alan              | Değer |
|-------------------|-------|
| **Primitive**     | Implicit: 3-slot round-robin pool |
| **Mekanizma**     | `frame_counter_ % 3` — slot N+3 kullanılana kadar slot N'in GL renderi biter |
| **Explicit sync** | Yok — pool derinliği (3) yeterli buffer sağlıyor |
| **Ek güvence**    | Sonraki `execute_copy()` girişindeki timeline wait, önceki Vulkan submit'in bittiğini garanti eder |
| **GL fence**      | `glFenceSync` / `glClientWaitSync` henüz eklenmemiş; pool rotasyonu yeterli |

**Not:** `is_copy_ready()` timeline polling'i Vulkan→GL veri bağımlılığını kapatır.  
Gelecekte GL→Vulkan explicit fence eklenecekse: `glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0)` + `glClientWaitSync(sync, 0, timeout)` paintGL sonunda, execute_copy öncesinde çağrılmalı.  
**Dosya:** `src/ui/preview_widget.cpp:360-363` (slot seçimi), `src/pipeline/copy_optimizer.cpp:296-318` (is_copy_ready)

---

## Tam pNext Zinciri — vkQueueSubmit

```
VkSubmitInfo
  └─ pNext → VkTimelineSemaphoreSubmitInfoKHR
               ├─ signalSemaphoreValueCount = 2
               │   [0] timeline_counter_      (timeline semaphore)
               │   [1] 0                      (binary gl_sync_sem — değer yok sayılır)
               └─ pNext → VkWin32KeyedMutexAcquireReleaseInfoKHR  (keyed mutex varsa)
                            acquireCount  = 1, acquireKey  = 1
                            releaseCount  = 1, releaseKey  = 0
```

---

## Kaynak Dosya Haritası

| Bileşen | Dosya |
|---------|-------|
| D3D11 device paylaşımı, CopyResource | `src/pipeline/capture/gpu_resource_manager.cpp` |
| Vulkan LUID eşlemesi, cihaz seçimi | `src/pipeline/gpu/vulkan_initializer.cpp` |
| D3D11 NT handle export, GL target pool | `src/pipeline/gpu/external_memory_bridge.cpp` |
| vkCmdBlitImage, keyed mutex, timeline submit | `src/pipeline/copy_optimizer.cpp` |
| GL import, glWaitSemaphoreEXT, paintGL | `src/ui/preview_widget.cpp` |
