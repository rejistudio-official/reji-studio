# v0.5.1 — Copy Optimization & Frame Pacing Spec

**Date:** 2026-06-04  
**Version:** v0.5.1 (v0.5 Vulkan Pivot refinement)  
**Scope:** CPU staging copy elimination + DXGI Statistics frame pacing + GPU query timing  
**Target:** p50 copy latency 1.8ms → <1ms, frame stability +15%

---

## Executive Summary

v0.5 Vulkan pivot introduced `ExternalMemoryBridge` for zero-copy D3D11→Vulkan interop, but CPU staging copy (D3D11 Map/Unmap + memcpy) remains in the `preview_widget.cpp` render loop. This spec eliminates that bottleneck by shifting all data movement to GPU-side operations (Vulkan internal transfers), adding frame timing analysis via DXGI Statistics, and implementing zero-overhead GPU query timing for latency root-cause analysis.

**Three parallel workstreams:**
1. **Copy Optimization**: GPU-only pipeline (no CPU staging texture reads)
2. **Frame Pacing**: DXGI Statistics collection + real-time frame timing graphs
3. **GPU Query Timing**: Vulkan timestamp queries for intra-frame latency breakdown

**Expected outcome:** p50 copy phase latency <1ms, frame-to-frame jitter <2ms, 60fps stable streaming.

---

## Scope & Constraints

### In Scope
- Remove `Map(D3D_MAP_READ)` + `memcpy` from `preview_widget::paintGL()` render loop
- Implement Vulkan GPU→GPU copy path (shader-based or `vkCmdCopyBuffer`)
- Add DXGI Statistics polling (present timing, GPU stalls)
- Implement GPU timestamp queries for pipeline stage latency
- Extend `HealingOverlay` to show real-time frame timing metrics
- Backward compatibility: keep PBO path for non-Vulkan fallback

### Out of Scope
- Audio sync optimization (separate v0.5.2 feature)
- Network-based frame pacing (SRT timing, v0.6)
- Machine learning frame prediction (future research)
- Shader optimization beyond copy (`vkCmdCopyBuffer` sufficient)

### Constraints
- **Backward compat:** AMD PBO path must continue working (v0.5 feature flag)
- **Cross-adapter safety:** SharedHandle validation mandatory (RTX 4070 dGPU + AMD iGPU setup)
- **SEH rules:** All D3D calls wrapped in `__declspec(noinline)` SEH leaves
- **FFI boundary:** No blocking calls, metrics flow through FFI to Rust orchestrator
- **Real-time constraint:** Frame pacing polls <1ms, GPU query overhead <0.5ms per frame

---

## Architecture

### Current State (v0.5, Before Optimization)
```
DXGI Desktop Duplication (AMD iGPU)
    ↓
D3D11 Staging Texture (GPU)
    ↓
Map(D3D_MAP_READ) + memcpy to QImage (CPU) ← ⚠️ BOTTLENECK (1.8ms p50)
    ↓
QImage ↔ OpenGL PBO (GPU)
    ↓
QOpenGLWidget::paintGL() → frame display
```

### Target State (v0.5.1, GPU-only)
```
DXGI Desktop Duplication (AMD iGPU)
    ↓
D3D11 Staging Texture (GPU)
    ↓
[GPU→GPU Copy Path]
   ├─ Option A: vkCmdCopyBuffer (D3D11 handle → Vulkan buffer)
   ├─ Option B: Compute shader (D3D11 texture → Vulkan image, format conversion)
   └─ Option C: Direct sampling (Vulkan shader samples D3D11 texture via external memory)
    ↓
Vulkan Image / OpenGL Interop
    ↓
QOpenGLWidget::paintGL() → frame display
    ↓
[GPU Query Timestamps] ← latency measurement (0.1ms overhead)
```

**Key difference:** No CPU involvement in frame data movement. All transfers happen on GPU.

---

## Component 1: Copy Optimization (GPU-only Pipeline)

### Architecture Decision: Vulkan Compute Shader Path (Recommended)

**Rationale:**
- Format conversion (BGRA D3D11 → RGBA Vulkan) best done on GPU
- Leverages existing `VulkanRenderPath` infrastructure (v0.5 Phase 2)
- Shader compilation caching (v0.5 shader cache) reduces overhead
- Cross-adapter safe: GPU handles synchronization

**Data Flow:**

```
1. DXGI capture → D3D11 staging texture (existing)
2. ExternalMemoryBridge export D3D11 handle → Vulkan VkImage (existing)
3. [NEW] Vulkan compute shader:
   - Bind D3D11 VkImage as input (external memory import)
   - Bind target VkImage (OpenGL interop) as output
   - Dispatch compute: BGRA→RGBA, scale if needed, copy to PBO
4. vkQueueSubmit → GPU fence
5. OpenGL waits on fence → safe to read
6. paintGL reads PBO → display
```

### Implementation Details

#### 1.1 Vulkan Compute Shader (`src/pipeline/shaders/copy_convert.comp`)
```glsl
#version 450
layout(set=0, binding=0) uniform sampler2D inputImage;   // D3D11 texture (external)
layout(set=0, binding=1, rgba8) uniform image2D outputImage;  // Vulkan target

layout(local_size_x = 8, local_size_y = 8) in;
void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= imageSize(outputImage).x || pos.y >= imageSize(outputImage).y) return;
    
    vec4 bgra = texelFetch(inputImage, pos, 0);  // BGRA from D3D11
    vec4 rgba = vec4(bgra.z, bgra.y, bgra.x, bgra.w);  // BGRA → RGBA
    imageStore(outputImage, pos, rgba);
}
```

**Compile & cache:** `ShaderCache::compile("copy_convert.comp")` → SPIR-V cached

#### 1.2 C++ Copy Pipeline (`src/pipeline/copy_optimizer.h/.cpp`)

**New class: `GpuCopyOptimizer`**
```cpp
class GpuCopyOptimizer {
public:
    bool init(VkDevice device, VkQueue queue, const GpuResourceManager& res_mgr);
    bool execute_copy(VkImage d3d11_staging,     // External D3D11 texture
                      VkImage vulkan_target,      // Target Vulkan image
                      uint32_t width, uint32_t height,
                      VkFence* out_fence);        // GPU completion fence
    void shutdown();
    
private:
    VkDevice device_;
    VkQueue queue_;
    VkPipelineLayout pipeline_layout_;
    VkPipeline compute_pipeline_;
    VkDescriptorSet descriptor_set_;
    VkCommandBuffer cmd_buffer_;
    VkFence gpu_fence_;
    uint32_t dispatch_x_, dispatch_y_;  // Cached dispatch groups
};
```

**Key methods:**
- `init()`: Load compute shader, create descriptor sets, allocate command buffer
- `execute_copy()`: Record compute dispatch, submit to GPU queue, return fence
- `shutdown()`: Cleanup pipeline, descriptor sets, fences

#### 1.3 Integration with `preview_widget.cpp`

**Replace CPU Map/Unmap block:**

**Before (CPU copy bottleneck):**
```cpp
void PreviewWidget::paintGL() {
    auto fence = gpu_resource_mgr_->wait_display_gpu_idle();  // GPU finish prev frame
    
    // ⚠️ CPU BOTTLENECK (1.8ms)
    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(d3d_context_->Map(staging_texture_, 0, D3D_MAP_READ, 0, &mapped))) {
        memcpy(qimage_.bits(), mapped.pData, qimage_.sizeInBytes());  // CPU copy
        d3d_context_->Unmap(staging_texture_, 0);
    }
    
    // ... rest of render
}
```

**After (GPU copy):**
```cpp
void PreviewWidget::paintGL() {
    if (!vulkan_mode_) {
        // Fallback: PBO ping-pong (v0.5 AMD path)
        return paintGL_pbo();
    }
    
    // GPU-only copy path
    VkFence copy_fence = nullptr;
    if (!copy_optimizer_->execute_copy(
            d3d11_staging_vk_image_,
            vulkan_target_image_,
            width_, height_,
            &copy_fence)) {
        log_error("GPU copy failed, falling back to PBO");
        return paintGL_pbo();
    }
    
    // Wait for GPU copy on GPU (minimal CPU overhead)
    vkWaitForFences(device_, 1, &copy_fence, VK_TRUE, UINT64_MAX);
    
    // Rest of render (PBO/OpenGL display)
    // ...
}
```

#### 1.4 Synchronization Strategy: Timeline Semaphore (GPU-Only)

**GPU-side timeline semaphore (no CPU blocking):**

Per AGENTS.md rule: "**Blocking FFI çağrısı yasak**" — CPU'yu hot-path'te block etmeyiz.

1. **Vulkan copy submits with timeline semaphore:**
   - `vkQueueSubmit(copy_cmd, signal: timeline_semaphore{value=1})`
   - CPU returns immediately (non-blocking)

2. **OpenGL waits on Vulkan completion (GPU-side):**
   - Vulkan→OpenGL interop fence: `glSignalSemaphoreEXT()` (VK_KHR_synchronization2)
   - OR fallback: `vkWaitSemaphores()` if OpenGL unavailable (still on GPU, CPU queries result)

3. **No `vkWaitForFences` in hot-path:**
   - Query semaphore value asynchronously: `vkGetSemaphoreCounterValue()` (polls without blocking)
   - If ready, proceed; else skip this frame (frame drop acceptable, stall avoided)

**Data structure:**
```cpp
class GpuCopyOptimizer {
private:
    VkSemaphore timeline_semaphore_;      // VkSemaphoreTypeKHR::VK_SEMAPHORE_TYPE_TIMELINE
    uint64_t timeline_counter_;            // Current timeline value
    static constexpr uint64_t FRAME_INTERVAL = 1;  // Increment per submit
};
```

**Submit with timeline semaphore:**
```cpp
bool GpuCopyOptimizer::execute_copy(...) {
    __try {
        VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info{
            .sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR,
            .signalSemaphoreValueCount = 1,
            .pSignalSemaphoreValues = &timeline_counter_
        };
        
        VkSubmitInfo submit_info{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = &timeline_submit_info,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd_buffer_,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &timeline_semaphore_
        };
        
        vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE);  // No fence, no CPU block
        timeline_counter_ += FRAME_INTERVAL;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        log_error("Vulkan compute dispatch failed: 0x%x", GetExceptionCode());
        return false;
    }
}
```

**Asynchronous ready check (non-blocking):**
```cpp
bool PreviewWidget::is_gpu_copy_ready() {
    uint64_t counter_value = 0;
    vkGetSemaphoreCounterValueKHR(device_, timeline_semaphore_, &counter_value);
    return counter_value >= expected_timeline_value_;
}

void PreviewWidget::paintGL() {
    // Async check (no CPU block, no fence wait)
    if (!is_gpu_copy_ready()) {
        // Frame drop: skip render this frame, try next
        log_warn("GPU copy not ready, dropping frame (acceptable)");
        return;
    }
    
    // Safe to proceed with PBO render
    glDrawArrays(...);
}
```

**SEH Safety:**
All Vulkan calls wrapped in SEH; no C++ objects in scope.

---

## Component 2: Frame Pacing (DXGI Statistics)

### Architecture: Real-time Frame Timing Collection

**Goal:** Measure frame-to-frame timing and detect GPU stalls, presents, and drops.

### 2.1 DXGI Statistics Polling (`src/pipeline/frame_pacing.h/.cpp`)

**New class: `DxgiFramePacing`**
```cpp
class DxgiFramePacing {
public:
    struct FrameStats {
        uint32_t present_count;
        uint32_t present_refresh_count;
        uint32_t sync_refresh_count;
        uint32_t sync_qpc_time;
        uint32_t sync_gpu_time;
        float frame_time_ms;
        float gpu_busy_ms;
        bool gpu_stall;  // GPU stall detected (>5ms frame time)
    };
    
    bool init(IDXGISwapChain* swap_chain);
    bool poll_frame_stats(FrameStats* out_stats);
    void shutdown();
    
private:
    IDXGISwapChain* swap_chain_;
    uint32_t last_present_count_;
    uint64_t last_qpc_time_;
};
```

**Key metrics collected:**
- **Present count**: Number of buffers presented (frame delivery tracking)
- **GPU busy time**: Duration GPU was rendering (from `sync_gpu_time`)
- **Frame time**: Time since last present
- **GPU stall**: Detected when frame time > 5ms (VSYNC-waiting scenario)

### 2.2 Vulkan GPU Timestamps Integration

**Extend `DxgiFramePacing` with Vulkan query data:**

```cpp
struct FrameStats {
    // ... existing DXGI fields ...
    
    // Vulkan GPU timestamps (nanoseconds)
    uint64_t vk_copy_start_ns;
    uint64_t vk_copy_end_ns;
    uint64_t vk_render_start_ns;
    uint64_t vk_render_end_ns;
    float copy_gpu_time_ms;
    float render_gpu_time_ms;
};
```

**Timestamp placement (see Component 3 for details):**
1. `vk_copy_start_ns`: Before `vkCmdDispatchCompute` (GPU copy)
2. `vk_copy_end_ns`: After compute shader finishes
3. `vk_render_start_ns`: Before OpenGL render
4. `vk_render_end_ns`: After OpenGL present

### 2.3 Frame Stats Publishing to Rust Orchestrator

**FFI extension** (`src/ffi/ffi_bridge.h`):

```c
typedef struct {
    uint32_t present_count;
    float frame_time_ms;
    float gpu_busy_ms;
    float copy_gpu_time_ms;
    float render_gpu_time_ms;
    bool gpu_stall;
    uint64_t timestamp_us;  // Wallclock time
} RjFrameStats;

// Publish frame stats to Rust (called from paintGL)
bool rj_frame_pacing_publish(const RjFrameStats* stats);
```

**Rust side** (`src/orchestrator/src/metrics.rs`):

```rust
pub struct FramePacingMetrics {
    pub frame_time_ms: f32,
    pub gpu_busy_ms: f32,
    pub copy_gpu_time_ms: f32,
    pub gpu_stall: bool,
    pub rolling_avg_frame_time: f32,  // 30-frame rolling average
}

pub fn update_frame_pacing(stats: RjFrameStats) {
    // Update rolling averages
    // Detect anomalies (GPU stall, frame drop)
    // Emit event to healing_overlay
}
```

### 2.4 Real-time UI Display (`src/ui/healing_overlay.cpp`)

**New metrics display:**
- Frame time (current): 16.7ms (60fps target)
- GPU busy: 14.2ms
- Copy latency: 0.8ms ← **v0.5.1 optimization success metric**
- GPU stall rate: 0/60 (last second)

**UI update** (slots driven by Rust events):
```cpp
void HealingOverlay::onFramePacingUpdate(float frame_time, float copy_latency, bool stall) {
    frame_time_label_->setText(QString("Frame: %1ms").arg(frame_time, 0, 'f', 1));
    copy_latency_label_->setText(QString("Copy: %1ms").arg(copy_latency, 0, 'f', 1));
    if (stall) {
        stall_indicator_->setStyleSheet("background: red;");
    } else {
        stall_indicator_->setStyleSheet("background: green;");
    }
}
```

---

## Component 3: GPU Query Timing (Vulkan Timestamps)

### Architecture: Zero-Overhead Per-Frame Latency Breakdown

**Goal:** Measure Vulkan compute copy and render pipeline latency without CPU stalling.

### 3.1 Vulkan Timestamp Query Pool (`src/pipeline/gpu_query_timing.h/.cpp`)

**New class: `GpuQueryTiming`**
```cpp
class GpuQueryTiming {
public:
    struct QueryResult {
        uint64_t copy_start_ns;
        uint64_t copy_end_ns;
        uint64_t render_start_ns;
        uint64_t render_end_ns;
        float copy_duration_ms;
        float render_duration_ms;
    };
    
    bool init(VkDevice device, VkQueue queue, VkPhysicalDevice phys_device);
    bool record_timestamp(VkCommandBuffer cmd, const char* label);
    bool retrieve_results(QueryResult* out_result);  // Non-blocking
    void shutdown();
    
private:
    VkQueryPool query_pool_;
    VkDevice device_;
    VkQueue queue_;
    float timestamp_period_ns_;  // GPU timestamp frequency
    std::array<const char*, 4> query_labels_{"copy_start", "copy_end", "render_start", "render_end"};
};
```

### 3.2 Timestamp Recording Points

**In `GpuCopyOptimizer::execute_copy()`:**
```cpp
bool GpuCopyOptimizer::execute_copy(..., VkFence* out_fence) {
    vkCmdResetQueryPool(cmd_buffer_, query_pool_, 0, 4);
    
    // Start copy timing
    vkCmdWriteTimestamp(cmd_buffer_, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 
                        query_pool_, 0);  // copy_start
    
    vkCmdDispatchCompute(cmd_buffer_, dispatch_x_, dispatch_y_, 1);
    vkCmdPipelineBarrier(...);  // Ensure compute writes visible
    
    // End copy timing
    vkCmdWriteTimestamp(cmd_buffer_, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query_pool_, 1);  // copy_end
    
    vkEndCommandBuffer(cmd_buffer_);
    vkQueueSubmit(queue_, ..., out_fence);
    
    return true;
}
```

**In `preview_widget.cpp::paintGL()` (render timing):**
```cpp
void PreviewWidget::paintGL() {
    // ... GPU copy (timestamps 0-1) ...
    
    // Start render timing
    gpu_query_->record_timestamp(render_cmd_, "render_start");  // timestamp 2
    
    glDrawArrays(GL_TRIANGLES, 0, vertex_count_);
    glFinish();
    
    gpu_query_->record_timestamp(render_cmd_, "render_end");  // timestamp 3
}
```

### 3.3 Result Retrieval (Non-blocking)

**Every frame (asynchronous):**
```cpp
void PreviewWidget::pollQueryResults() {
    GpuQueryTiming::QueryResult result;
    if (gpu_query_->retrieve_results(&result)) {  // Non-blocking check
        frame_stats_.copy_gpu_time_ms = result.copy_duration_ms;
        frame_stats_.render_gpu_time_ms = result.render_duration_ms;
        
        rj_frame_pacing_publish(&frame_stats_);  // Send to Rust
    }
}
```

**Overhead:** <0.5ms per frame (query pool is ring-buffered, no CPU stall).

### 3.4 Timestamp Conversion

```cpp
float GpuQueryTiming::convert_timestamp(uint64_t t1, uint64_t t2) {
    // timestamp_period_ns_ = GPU frequency (from vkGetPhysicalDeviceProperties)
    uint64_t delta_ns = (t2 - t1) * timestamp_period_ns_;
    return delta_ns / 1_000_000.0f;  // Convert to milliseconds
}
```

---

## Data Flow Diagram (Text)

```
Frame N:
├─ DXGI capture → D3D11 staging
├─ [Vulkan GPU copy]
│  ├─ WriteTimestamp(copy_start)
│  ├─ Compute dispatch (BGRA→RGBA)
│  ├─ PipelineBarrier
│  └─ WriteTimestamp(copy_end)
├─ [OpenGL render]
│  ├─ RecordTimestamp(render_start)
│  ├─ Draw PBO → framebuffer
│  ├─ Finish (GPU fence)
│  └─ RecordTimestamp(render_end)
└─ Present to DWM
   └─ Poll DXGI stats (frame time, stall)
   └─ Poll Vulkan query results (copy/render latency)
   └─ Publish RjFrameStats to Rust
      └─ HealingOverlay updates UI with metrics

Frame N+1:
└─ Repeat (query pool is ring-buffered, no stall)
```

---

## Files & Changes

### New Files
| File | Purpose |
|---|---|
| `src/pipeline/copy_optimizer.h/.cpp` | GPU-only copy pipeline, compute shader dispatch |
| `src/pipeline/frame_pacing.h/.cpp` | DXGI Statistics polling, frame time collection |
| `src/pipeline/gpu_query_timing.h/.cpp` | Vulkan timestamp queries, latency measurement |
| `src/pipeline/shaders/copy_convert.comp` | Vulkan compute shader (BGRA→RGBA format conversion) |
| `docs/superpowers/specs/2026-06-04-v051-frame-timing.md` | Frame timing measurement & debugging guide |

### Modified Files
| File | Change | Lines |
|---|---|---|
| `src/ui/preview_widget.cpp` | Replace Map/memcpy block with `copy_optimizer_->execute_copy()` | ~30 |
| `src/ui/preview_widget.h` | Add `GpuCopyOptimizer`, `DxgiFramePacing`, `GpuQueryTiming` members | ~15 |
| `src/ui/healing_overlay.cpp` | Add frame timing metrics display | ~25 |
| `src/ui/healing_overlay.h` | Add slots for frame pacing events | ~5 |
| `src/ffi/ffi_bridge.h` | Add `RjFrameStats` struct, `rj_frame_pacing_publish()` | ~20 |
| `src/ffi/ffi_bridge.c` | Implement FFI frame stats publishing | ~15 |
| `src/orchestrator/src/metrics.rs` | Add `FramePacingMetrics`, event emission | ~40 |
| `CMakeLists.txt` | Add shader compilation for `copy_convert.comp` | ~5 |
| `src/pipeline/CMakeLists.txt` | Add new object files (copy_optimizer, frame_pacing, gpu_query_timing) | ~10 |

**Total new code:** ~200 lines C++, ~40 lines Rust, ~1 shader file

---

## Success Criteria

### Performance Targets
| Metric | Current | Target | Acceptance |
|---|---|---|---|
| **Copy latency p50** | 1.8ms | <1.0ms | ✅ p50 <1.0ms, p99 <1.5ms |
| **Frame time jitter** | ±3ms | ±2ms | ✅ Coefficient of variance <0.05 |
| **GPU stall rate** | >5% | <1% | ✅ Stall count <1 per 100 frames |
| **Query overhead** | N/A | <0.5ms | ✅ Timestamp retrieval <0.5ms |

### Functional Tests
- [ ] GPU copy produces identical pixel output to CPU memcpy (bit-exact or visually indistinguishable)
- [ ] Frame pacing metrics correlate with on-screen smoothness (no stutter)
- [ ] GPU query timestamps are monotonically increasing per frame
- [ ] Fallback to PBO path works (non-Vulkan or error path)
- [ ] Cross-adapter (iGPU + dGPU) handles D3D11→Vulkan interop correctly
- [ ] HealingOverlay displays copy latency <1ms 95% of time at 60fps

### Stability Tests
- [ ] 5-minute streaming test (300 frames) with zero crashes
- [ ] Memory leaks check (DXGI resources, Vulkan objects, descriptor sets)
- [ ] SEH exception handling (corrupt GPU handles, missing extensions)
- [ ] Fallback recovery (Vulkan unavailable → PBO path)

### Benchmark Results
Run `python scripts/benchmark.py` before/after:
- Copy phase latency histogram (p50, p95, p99)
- Frame time distribution (mean, stddev, max)
- GPU busy time vs. wall-clock frame time
- Stall detection accuracy (expected stalls during load tests)

---

## Implementation Plan Outline

**Phase 1: Infrastructure (Week 1)**
- `GpuCopyOptimizer` skeleton + compute shader compilation
- `DxgiFramePacing` DXGI statistics polling
- `GpuQueryTiming` Vulkan query pool setup

**Phase 2: Integration (Week 2)**
- Replace CPU Map/Unmap in `preview_widget::paintGL()`
- FFI publishing to Rust `FramePacingMetrics`
- HealingOverlay metrics display (text labels)

**Phase 3: Testing & Optimization (Week 3)**
- Performance benchmark suite
- Cross-adapter validation (iGPU + dGPU)
- Fallback path testing (PBO compatibility)
- Stress tests (5min streaming, memory leaks)

**Phase 4: Documentation (Week 4)**
- Frame timing debugging guide
- GPU query timestamp interpretation
- Performance tuning best practices

---

## Risk & Mitigation

| Risk | Impact | Mitigation |
|---|---|---|
| Vulkan compute unavailable (old GPU) | Cannot optimize copy | Fallback to PBO path (existing) |
| D3D11↔Vulkan interop instability | Crashes, pixel corruption | Validate pixel output (test), SEH wrapping |
| GPU timestamp frequency unknown | Incorrect latency math | Query `vkGetPhysicalDeviceProperties`, cache |
| Cross-adapter handle export fails | iGPU+dGPU setup broken | Validate `IDXGIResource1::GetSharedHandle()` in init |
| Query pool exhaustion | Missing timestamp results | Ring-buffer query pool (x4 frames capacity) |
| Frame pacing DXGI unavailable | Cannot collect stats | Fallback to CPU `GetTickCount()` (lower precision) |

---

## Backward Compatibility

- ✅ PBO ping-pong path remains for AMD iGPU (non-Vulkan builds)
- ✅ Existing `preview_widget` interface unchanged (new methods added)
- ✅ No breaking changes to FFI or public APIs
- ✅ Rust orchestrator receives same `RjMetricSample` structure (extended with frame pacing)

---

## Spec Version History

| Date | Version | Change |
|---|---|---|
| 2026-06-04 | v1.0 | Initial spec: GPU-only copy, DXGI statistics, Vulkan timestamps |

---

## References

- CONTEXT.md — v0.5 Vulkan pivot architecture
- AGENTS.md — coding rules, performance constraints
- `docs/progress.md` — v0.4 Runtime Adaptation Faz 5 (completed)
- Vulkan external memory spec: https://www.khronos.org/registry/vulkan/specs/1.3/html/vkspec.html#VK_KHR_external_memory_win32
- DXGI swap chain statistics: https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiswapchain1-getframestatistics
