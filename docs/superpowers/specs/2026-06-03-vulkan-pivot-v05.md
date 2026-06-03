# v0.5 Vulkan Pivot Specification

**Date:** 2026-06-03  
**Version:** Vulkan Infrastructure Foundation  
**Status:** Approved for Implementation  
**Author:** Reji Studio Team  

---

## Executive Summary

v0.5 represents a **Vulkan infrastructure pivot** for Reji Studio, replacing the D3D11→OpenGL pipeline with a zero-copy Vulkan path. This eliminates the DwmFlush race condition and targets **<2ms frame latency** (vs. 7.6ms current).

**Scope:**
- GPU interop via `VK_KHR_external_memory_win32` (D3D11 → Vulkan)
- Qt6 QRhi for window management + direct Vulkan hot-path
- SPIR-V shader caching (startup optimization)
- Graceful OpenGL fallback (unsupported hardware)
- iGPU→dGPU path only (AMD 780M → RTX 4070); multi-adapter full support deferred to v0.6

**Performance Features** (frame pacing, GPU query timing, multi-monitor) move to **v0.5.1**.

---

## 1. Architecture Overview

### Current Pipeline (v0.4)

```
D3D11 Desktop Duplication
  ↓ [DXGI Staging Texture]
Cross-adapter SharedHandle (same_adapter=true)
  ↓
PBO Ping-Pong (OpenGL)
  ↓
QOpenGLWidget paintGL() + DwmFlush()
  └─ 7.6ms latency (race condition: DWM blit vs GPU present)
```

### v0.5 Target Pipeline

```
D3D11 Desktop Duplication
  ↓ [DXGI Staging Texture]
ExternalMemoryBridge (NT handle via CreateSharedHandle)
  ↓
Vulkan Image (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_WIN32_BIT)
  ↓
QRhi (initialization, window management, fallback)
  ↓
Direct Vulkan Render (hot-path, inline candidate)
  ↓
vkQueuePresentKHR (no DwmFlush needed)
  └─ ~1.85ms latency (DwmFlush race condition eliminated)
```

### Key Benefits

1. **Zero-copy GPU transfer** — D3D11 texture borrowed by Vulkan (no staging)
2. **Race condition eliminated** — DwmFlush removed, Vulkan synchronization sufficient
3. **Performance headroom** — <2ms latency enables v0.5.1 features
4. **Future-proof** — Vulkan foundation for multi-adapter (v0.6), stream quality adaptation (v0.5.1)
5. **Decision Engine Level 3 ready** — Runtime metrics (frame drop, GPU temp) feed Vulkan query API

### Core Components

| Component | Purpose | File(s) |
|-----------|---------|---------|
| **VulkanInitializer** | Instance/device creation, extension detection | `src/pipeline/gpu/vulkan_initializer.h/.cpp` |
| **ExternalMemoryBridge** | D3D11↔Vulkan interop (under GpuResourceManager) | `src/pipeline/gpu/external_memory_bridge.h/.cpp` |
| **VulkanRenderPath** | Hot-path render (vkCmdCopyBufferToImage, submit, present) | `src/ui/vulkan_render_path.h/.cpp` |
| **RenderCapabilityDetector** | Vulkan support detection, fallback chain logic | `src/ui/render_capability.h` (extended) |
| **QRhi Window Wrapper** | QOpenGLWidget → Vulkan surface binding | `src/ui/qrhi_window_wrapper.h/.cpp` |
| **ShaderCache** | SPIR-V binary caching (FNV-1a hash, %APPDATA% storage) | `src/ui/shader_cache.h/.cpp` |

---

## 2. GPU Interop Strategy — KHR_external_memory_win32

### D3D11 → Vulkan Zero-Copy Transfer

**Windows Minimum Requirement:** Windows 10 (build 1909+) — NT handle support required.

**Transfer Flow:**

```
Frame N (DXGI):
  1. DXGI::GetFrameMetadataArray() [CPU, ~0.1ms]
  2. AcquireNextFrame() → D3D11 staging texture [GPU, ~0.2ms]
  3. ExternalMemoryBridge::export_d3d11_handle()
       └─ ID3D11Texture2D::CreateSharedHandle()  [NT handle, not legacy GetSharedHandle]
          └─ HANDLE (non-blocking) [CPU, ~0.05ms]

Frame N (Vulkan):
  4. vkCreateImage(VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_WIN32_BIT, HANDLE)
       └─ Image pool: image = pool[frame_idx % N]  [pre-allocated, no per-frame alloc]
  5. vkBindImageMemory() → D3D11 memory borrowed (no allocation)
  6. vkCmdCopyBufferToImage() [GPU, ~0.5ms]
  7. vkQueueSubmit() + barrier: VK_PIPELINE_STAGE_TRANSFER_BIT → VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT
  8. vkQueuePresentKHR() [GPU, ~0.8ms]

Total: ~1.85ms (vs 7.6ms with DwmFlush) ✓
```

### ExternalMemoryBridge Design

**Ownership Model:**
- **GpuResourceManager** owns ExternalMemoryBridge
- **D3D11** owns texture lifecycle (allocation → deallocation)
- **Vulkan images** are borrowed views (no vkDestroyImage ownership, no allocation)

**Image Pool (Hot-Path Optimization):**

```cpp
class ExternalMemoryBridge {
  static const int POOL_SIZE = 3;  // triple buffer
  VkImage image_pool[POOL_SIZE];   // pre-allocated at startup
  
  // Per-frame: reuse pooled image (zero allocation)
  VkImage get_pooled_image(uint32_t frame_idx) {
    return image_pool[frame_idx % POOL_SIZE];
  }
};
```

**Key Functions:**

```cpp
// Export D3D11 handle (NT handle, Win10+ only)
HANDLE export_d3d11_handle(ID3D11Texture2D* staging_texture) {
  ID3D11Device1* device = ...;  // from D3D11 context
  HANDLE nt_handle = nullptr;
  device->CreateSharedHandle(staging_texture, nullptr, DXGI_SHARED_RESOURCE_READ, &nt_handle);
  return nt_handle;  // non-blocking, handle only
}

// Vulkan image creation from D3D11 handle
VkImage create_vulkan_image_from_d3d11(
  HANDLE d3d11_handle,
  VkFormat format, uint32_t width, uint32_t height
) {
  VkExternalMemoryImageCreateInfo ext_img_info = {
    .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
    .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_WIN32_BIT,
  };
  
  VkImageCreateInfo img_info = {
    .pNext = &ext_img_info,
    .imageType = VK_IMAGE_TYPE_2D,
    .format = format,
    .extent = {width, height, 1},
    .mipLevels = 1,
    .arrayLayers = 1,
    .samples = VK_SAMPLE_COUNT_1_BIT,
    .tiling = VK_IMAGE_TILING_OPTIMAL,
    .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
  };
  
  VkImage vk_img = nullptr;
  vkCreateImage(device, &img_info, nullptr, &vk_img);
  
  // Bind D3D11 memory (no allocation)
  VkImportMemoryWin32HandleInfoKHR import_info = {
    .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
    .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_WIN32_BIT,
    .handle = d3d11_handle,
  };
  
  VkMemoryAllocateInfo alloc_info = {
    .pNext = &import_info,
    .allocationSize = ...,  // from D3D11 texture size
    .memoryTypeIndex = ...,
  };
  
  VkDeviceMemory vk_mem = nullptr;
  vkAllocateMemory(device, &alloc_info, nullptr, &vk_mem);
  vkBindImageMemory(device, vk_img, vk_mem, 0);
  
  return vk_img;
}
```

### Cross-Adapter Synchronization (v0.5 scope: iGPU→dGPU)

**Current hardware:** AMD Radeon 780M (display adapter) → NVIDIA RTX 4070 (encode adapter)

**Synchronization strategy:**
- **Barrier:** `VK_PIPELINE_STAGE_TRANSFER_BIT` → `VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT` ensures ordering
- **Fence (future v0.6):** `VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_WIN32` for reverse path

**Reverse path (NVIDIA→AMD) deferred to v0.6** to limit scope.

---

## 3. Component Breakdown & File Structure

### New Files

```
src/pipeline/gpu/
├── vulkan_initializer.h/.cpp       [NEW]
│   • VulkanInitializer class
│   • vkCreateInstance, device selection
│   • Extension validation (VK_KHR_external_memory_win32)
│   • Vendor-specific initialization (AMD, NVIDIA)
│   • Error handling + fallback detection
│
└── external_memory_bridge.h/.cpp   [NEW]
    • ExternalMemoryBridge class (owned by GpuResourceManager)
    • export_d3d11_handle() → CreateSharedHandle (NT handle)
    • create_vulkan_image_from_d3d11()
    • Image pool management (pre-allocate POOL_SIZE=3)
    • Ownership/lifecycle documentation

src/ui/
├── vulkan_render_path.h/.cpp       [NEW]
│   • VulkanRenderPath class
│   • Hot-path: vkCmdCopyBufferToImage, vkQueueSubmit, vkQueuePresentKHR
│   • Inline candidate for <2ms target (minimal overhead)
│   • Thread-safe command buffer pool access (QMutexLocker)
│
├── qrhi_window_wrapper.h/.cpp      [NEW]
│   • QRhi surface binding (Vulkan backend)
│   • QOpenGLWidget → Vulkan native window conversion
│   • Window resize, DPI scaling handling
│   • Fallback: OpenGL rendering path
│
└── shader_cache.h/.cpp             [NEW]
    • SPIR-V binary caching system
    • FNV-1a hash (header-only, no crypto dependency)
    • Cache storage: %APPDATA%\Reji\shader_cache\
    • Hash naming: shader_cache/{hash}.spv
    • Invalidation: on shader.glsl modification or Vulkan SDK version change
```

### Modified Files

```
src/pipeline/capture/
├── gpu_resource_manager.h/.cpp     [MODIFIED]
    • Add ExternalMemoryBridge member
    • Extend initialize() for Vulkan setup
    • Image pool lifetime management

src/ui/
├── render_capability.h             [MODIFIED]
    • Add kVulkan to RenderPath enum
    • Extend CapabilityDetector::detect():
      1. Check VK_KHR_external_memory_win32 extension
         → Not found: fallback to OpenGL stub
      2. Vendor detection (AMD/NVIDIA)
         → Other vendor: fallback to QRhi
      3. AMD 780M + RTX 4070: Direct Vulkan path
    • Device feature checks
│
├── preview_widget.h/.cpp           [MODIFIED]
    • Replace PBO ping-pong with VulkanRenderPath
    • Remove DwmFlush() call
    • Hot-path: direct Vulkan render (VulkanRenderPath::render())
    • Fallback: OpenGL stub selection (if Vulkan init fails)
    • Thread-safety: QMutexLocker for Vulkan command buffer access
│
└── healing_overlay.h/.cpp          [MODIFIED]
    • Add onVulkanInitFailed() slot
    • User notification: "Vulkan desteklenmiyor, OpenGL modunda..."
    • Graceful degradation message

CMakeLists.txt (root)           [MODIFIED]
├── find_package(Vulkan REQUIRED)
├── Add -DREJI_VULKAN_MOCK=ON option (for CI)
├── Vulkan SDK path detection (VULKAN_SDK env var)
└── Link flags, include directories

Cargo.toml                       [MODIFIED]
├── Remove ash crate (C++ Vulkan SDK sufficient, Rust untouched)
├── No spirv crate needed (SPIR-V managed in C++)
└── No other Vulkan changes
```

---

## 4. Hot-Path Data Flow & Performance

### Frame Capture → Present Timeline

**Target:** <2ms total latency (vs 7.6ms current)

```
T=0ms:      DXGI::GetFrameMetadataArray() [CPU, ~0.1ms]
            ├─ Dirty rect enumeration
            └─ Modified regions only

T=0.1ms:    DXGI::AcquireNextFrame() → D3D11 staging texture [GPU, ~0.2ms]
            ├─ Desktop Duplication API
            └─ Texture ready in D3D11 context

T=0.3ms:    ExternalMemoryBridge::export_d3d11_handle() [CPU, ~0.05ms]
            ├─ CreateSharedHandle() → NT HANDLE (Win10+)
            └─ Non-blocking, handle only

T=0.35ms:   get_pooled_image(frame_idx % 3) [CPU, ~0.05ms]
            ├─ Image pool lookup (pre-allocated at startup)
            └─ Zero allocation per-frame (vkCreateImage only at init)

T=0.45ms:   [VulkanRenderPath::render() — Hot-Path]
            ├─ vkCmdBeginRenderPass()
            ├─ vkCmdCopyBufferToImage() → transfer to render target [GPU, ~0.5ms]
            │  └─ Shader: blit + color space conversion (from cache)
            ├─ vkCmdEndRenderPass()
            └─ Inlined for minimal overhead

T=0.95ms:   vkQueueSubmit() [CPU, ~0.1ms]
            ├─ Command buffer submission
            ├─ Barrier: VK_PIPELINE_STAGE_TRANSFER_BIT → VK_PIPELINE_STAGE_COLOR_OUTPUT_BIT
            └─ Cross-adapter sync

T=1.05ms:   vkQueuePresentKHR() [GPU, ~0.8ms]
            ├─ VSynced present (60fps → 16.67ms frame)
            └─ DwmFlush() removed ✓ (no race condition overhead)

T=1.85ms:   Frame complete → next frame starts
────────────────────────────────────────────────────
Total:      ~1.85ms (goal: <2ms) ✅
```

### AGENTS.md Hot-Path Compliance

✅ **No heap allocation per frame** — image pool pre-allocated  
✅ **No JSON parsing per frame** — shader hash pre-computed  
✅ **No system queries per frame** — metrics polled separately (v0.5.1)  
✅ **Fixed-size buffers** — Vulkan command buffer pool  
✅ **Direct Vulkan calls** — no Qt abstraction overhead in render loop  

### Shader Compilation & Caching

**Strategy:** Compile-on-first-run, cache SPIR-V binary for subsequent startups.

**Startup (no cache):**

```cpp
1. shader.glsl read
2. FNV-1a hash: hash = fnv1a(shader.glsl + Vulkan SDK version)
3. Check: %APPDATA%\Reji\shader_cache\{hash}.spv exists?
   → Hit: load binary → vkCreateShaderModule (fast, ~1ms)
   → Miss: compile glsl → SPIR-V (slow, ~50-100ms)
4. Write to cache: %APPDATA%\Reji\shader_cache\{hash}.spv
```

**Subsequent startups:**

```cpp
1. FNV-1a hash computed
2. Check cache → hit (>90% expected)
3. vkCreateShaderModule(binary) → fast (~1ms)
```

**Cache invalidation:**

```
Automatic:
  • shader.glsl modified (timestamp) → new hash → recompile
  • VULKAN_SDK version changed → new hash suffix
  
Manual:
  • CLI flag: --clear-shader-cache
  • Env var: REJI_SHADER_CACHE_CLEAR=1
```

**Hash function:** FNV-1a (Fowler-Noll-Vo), header-only, no crypto dependency

```cpp
uint64_t fnv1a(const std::string& data) {
  uint64_t hash = 14695981039346656037ULL;  // FNV offset basis
  for (unsigned char c : data) {
    hash ^= c;
    hash *= 1099511628211ULL;  // FNV prime
  }
  return hash;
}
```

### Fallback Path (Graceful Degradation)

**Trigger:** Vulkan init fails (VK_ERROR_*, missing extension, unsupported hardware)

```
1. RenderCapabilityDetector::detect() returns kOpenGL
2. VulkanInitializer::initialize() → VK_ERROR_*
3. HealingOverlay::onVulkanInitFailed()
   └─ Notify user: "Vulkan desteklenmiyor, OpenGL uyumlu modunda..."
4. Switch render path: QRhi + OpenGL backend
5. Fallback latency: 7.6ms (acceptable for unsupported hardware)
6. Feature parity: preview streaming works, just slower
```

---

## 5. Testing & Validation Strategy

### Unit Tests (C++ / FFI)

**File: `tests/test_vulkan_interop.cpp`**

```cpp
// VulkanInitializer
TEST(VulkanInitializer, create_instance) {
  VulkanInitializer init;
  ASSERT_TRUE(init.initialize());
  ASSERT_NE(nullptr, init.instance());
}

TEST(VulkanInitializer, detect_vendor) {
  VulkanInitializer init;
  init.initialize();
  uint32_t vendor = init.vendor_id();
  ASSERT_TRUE(vendor == 0x1002 || vendor == 0x10DE || vendor == 0x8086);
}

TEST(VulkanInitializer, check_extension) {
  VulkanInitializer init;
  init.initialize();
  ASSERT_TRUE(init.has_extension("VK_KHR_external_memory_win32"));
}

// ExternalMemoryBridge
TEST(ExternalMemoryBridge, export_d3d11_handle) {
  auto bridge = GpuResourceManager::create_bridge();
  ID3D11Texture2D* staging = ... ;
  HANDLE nt_handle = bridge->export_d3d11_handle(staging);
  ASSERT_NE(nullptr, nt_handle);
}

TEST(ExternalMemoryBridge, create_vulkan_image_from_d3d11) {
  auto bridge = GpuResourceManager::create_bridge();
  VkImage img = bridge->create_vulkan_image_from_d3d11(nt_handle, ...);
  ASSERT_NE(nullptr, img);
}

TEST(ExternalMemoryBridge, image_pool_reuse) {
  auto bridge = GpuResourceManager::create_bridge();
  VkImage img0 = bridge->get_pooled_image(0);
  VkImage img1 = bridge->get_pooled_image(3);  // wrap-around
  ASSERT_EQ(img0, img1);  // same pool entry
}

// RenderCapabilityDetector
TEST(RenderCapabilityDetector, vulkan_supported) {
  auto cap = CapabilityDetector::detect();
  ASSERT_EQ(RenderPath::kVulkan, cap.preferred_path);
}

TEST(RenderCapabilityDetector, fallback_to_opengl) {
  // Mock: no KHR_external_memory_win32
  auto cap = CapabilityDetector::detect_with_mock(REJI_VULKAN_MOCK=ON);
  ASSERT_EQ(RenderPath::kOpenGL, cap.preferred_path);
}

// ShaderCache
TEST(ShaderCache, fnv1a_hash) {
  std::string shader = "void main() {}";
  uint64_t hash = fnv1a(shader);
  ASSERT_NE(0ULL, hash);  // deterministic, non-zero
}

TEST(ShaderCache, write_cache) {
  ShaderCache cache;
  cache.write_cache(hash, spirv_binary);
  ASSERT_TRUE(std::filesystem::exists(cache_path));
}

TEST(ShaderCache, hit_after_write) {
  ShaderCache cache;
  cache.write_cache(hash, spirv_binary);
  std::vector<uint32_t> loaded = cache.read_cache(hash);
  ASSERT_EQ(spirv_binary, loaded);
}

// FFI boundary
TEST(FFI, render_frame_vulkan_path) {
  rj_render_frame();  // hot-path call
  // Vulkan path executed without exception
}
```

**File: `tests/test_shader_cache.cpp`**

```cpp
TEST(ShaderCache, concurrent_writes) {
  std::vector<std::thread> threads;
  for (int i = 0; i < 5; ++i) {
    threads.emplace_back([i]() {
      ShaderCache cache;
      cache.write_cache(hash_i, spirv_binary_i);
    });
  }
  for (auto& t : threads) t.join();
  // No file corruption
}

TEST(ShaderCache, invalidation_on_shader_change) {
  uint64_t hash1 = fnv1a("void main() {}");
  uint64_t hash2 = fnv1a("void main() { gl_FragColor = vec4(1); }");
  ASSERT_NE(hash1, hash2);  // different content → different hash
}

TEST(ShaderCache, sdk_version_suffix) {
  std::string vk_version = vkGetPhysicalDeviceProperties().apiVersion;
  uint64_t hash_with_version = fnv1a(shader + vk_version);
  // Changing Vulkan SDK version → new hash → recompile
}
```

### Integration Tests (Real Hardware)

**AMD 780M (iGPU) + RTX 4070 (dGPU):**

```
✓ DXGI Desktop Duplication → D3D11 staging texture
✓ ExternalMemoryBridge → export_d3d11_handle (NT handle)
✓ vkCreateImage from D3D11 HANDLE → Vulkan image bound
✓ vkCmdCopyBufferToImage → transfer complete
✓ vkQueuePresentKHR → frame visible on display
✓ Latency measurement: <2ms? (profiler timestamp)
✓ Frame continuity: no drops, no tearing (30-min burn test)
✓ Thermal stability: GPU temp stable under sustained load
✓ Fallback: mock Vulkan init failure → OpenGL path seamless
```

### CI Integration (GitHub Actions)

**Problem:** GitHub Actions Windows VM uses SwiftShader (software rasterizer), which lacks KHR_external_memory_win32.

**Solution:** `-DREJI_VULKAN_MOCK=ON` flag for CI builds

```yaml
# .github/workflows/build.yml
jobs:
  build:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build (mock Vulkan)
        run: |
          python scripts/build.py \
            --config Release \
            --cmake-args "-DREJI_VULKAN_MOCK=ON"
      - name: Run tests (mock)
        run: |
          ctest --output-on-failure
      - name: Validation layers (debug)
        run: |
          python scripts/build.py \
            --config Debug \
            --cmake-args "-DREJI_VULKAN_MOCK=ON"
```

**Real Hardware Testing:** Self-hosted runner (future) or manual testing before v0.5 release.

### Performance Profiling (v0.5-beta)

**Tools:**
- NVIDIA NSight Systems (GPU timeline, AMD→NVIDIA transfer profiling)
- AMD Radeon GPU Profiler (AMD GPU memory/BW analysis)
- Windows Performance Analyzer (CPU/GPU trace, DPC latency)
- Vulkan validation layers (debug mode, error/warning logging)

**Metrics:**

| Metric | Target | Method |
|--------|--------|--------|
| Frame latency | <2ms | vkCmdWriteTimestamp + QueryPool |
| GPU utilization | >80% | GPU profiler |
| CPU overhead | <0.5ms | CPU perf counter |
| Shader cache hit | >90% (after 1st run) | Cache stats logging |
| Memory footprint | <100MB | valgrind / MSVC mem profiler |
| 30-min stability | 0 crashes | burn test, log monitoring |

### Validation Layers

**Debug build (`CMAKE_BUILD_TYPE=Debug`):**
```cpp
VK_LAYER_KHRONOS_validation enabled
vkCreateDebugUtilsMessenger → stderr output
All errors halt execution (catch issues early)
```

**Release build (`CMAKE_BUILD_TYPE=Release`):**
```cpp
Validation layers OFF (zero overhead)
Assertions convert to logs only (no halt)
Performance profiling enabled
```

**CI:**
```yaml
-DREJI_VULKAN_MOCK=ON
Validation layers ON in debug CI job
Catch shader compilation errors, API misuse
```

---

## 6. Implementation Phases & Dependencies

### Phase 1: Infrastructure (Weeks 1-2)

**Parallel tasks:**

```
Task 1a: VulkanInitializer class
  ├─ vkCreateInstance, vkEnumeratePhysicalDevices
  ├─ Vendor detection (AMD, NVIDIA, Intel fallback)
  ├─ Extension validation (VK_KHR_external_memory_win32)
  ├─ Error handling + fallback detection
  └─ Unit tests (all paths)

Task 1b: ExternalMemoryBridge (under GpuResourceManager)
  ├─ export_d3d11_handle() using CreateSharedHandle
  ├─ vkCreateImage from D3D11 HANDLE
  ├─ Image pool management (POOL_SIZE=3, pre-allocate)
  ├─ Ownership/lifecycle documentation
  └─ Unit tests (pool reuse, HANDLE validity)

Task 1c: RenderCapabilityDetector extension
  ├─ Vulkan support detection
  ├─ Fallback chain: (1) KHR_external_memory check
  │                  (2) Vendor detection
  │                  (3) AMD 780M + RTX 4070 path
  ├─ RenderPath::kVulkan enum addition
  └─ Unit tests (all fallback paths)

Task 1d: ShaderCache class
  ├─ FNV-1a hash implementation (header-only)
  ├─ %APPDATA%\Reji\shader_cache\ directory management
  ├─ Binary cache read/write
  ├─ Invalidation (timestamp, SDK version)
  └─ Unit tests (hash consistency, concurrent writes)

Task 1e: CMakeLists.txt integration
  ├─ find_package(Vulkan REQUIRED)
  ├─ -DREJI_VULKAN_MOCK=ON option
  ├─ VULKAN_SDK environment detection
  ├─ Include paths, link libraries
  └─ CI mock flag documentation

Blockers: None (fully parallelizable)
Output: Core Vulkan infrastructure, unit tests pass
Success: All 5 tasks independently compilable + tested
```

### Phase 2: Rendering Path (Weeks 3-4)

**Sequential (depends on Phase 1):**

```
Task 2a: VulkanRenderPath class
  ├─ Hot-path: vkCmdCopyBufferToImage (from ExternalMemoryBridge)
  ├─ vkQueueSubmit + barrier (VK_PIPELINE_STAGE_TRANSFER_BIT)
  ├─ vkQueuePresentKHR (to surface)
  ├─ Command buffer pool management
  ├─ Inline candidate annotations
  └─ Unit tests (render path execution)

Task 2b: QRhi Window Wrapper
  ├─ QRhi initialization (Vulkan backend)
  ├─ QOpenGLWidget → Vulkan native window
  ├─ Window resize, DPI scaling
  ├─ Fallback: OpenGL rendering path
  └─ Unit tests (window lifecycle)

Task 2c: preview_widget.cpp refactor
  ├─ Replace PBO ping-pong with VulkanRenderPath
  ├─ Remove DwmFlush() call
  ├─ Hot-path: direct Vulkan render
  ├─ Fallback: OpenGL stub selection
  ├─ Thread-safety: QMutexLocker (Vulkan command buffer)
  └─ Integration tests (render output visible)

Task 2d: HealingOverlay enhancement
  ├─ onVulkanInitFailed() slot
  ├─ User notification: "Vulkan desteklenmiyor, OpenGL modunda..."
  ├─ Graceful degradation message
  └─ Integration tests (message display)

Blockers: Phase 1 complete
Output: Full Vulkan render pipeline, fallback working
Success: 30-minute integration test (AMD 780M + RTX 4070) <2ms latency
```

### Phase 3: Validation & Polish (Weeks 5-6)

**Sequential (depends on Phase 2):**

```
Task 3a: Vulkan validation layers
  ├─ vkCreateDebugUtilsMessenger setup
  ├─ Debug vs Release layer management
  ├─ CI mock flag verification
  └─ Unit tests (error reporting)

Task 3b: Shader compilation testing
  ├─ Cache hit/miss scenarios
  ├─ Concurrent cache writes (race conditions)
  ├─ Invalidation on shader.glsl change
  ├─ Cache size monitoring
  └─ Integration tests (startup time measurements)

Task 3c: Performance profiling
  ├─ Frame latency: target <2ms (NSight/GPU Profiler)
  ├─ GPU utilization (transfer BW, submit overhead)
  ├─ CPU overhead (DXGI, ExternalMemoryBridge calls)
  ├─ 30-minute burn test (thermal stability)
  └─ Performance regression tests (vs v0.4)

Task 3d: CI integration
  ├─ Build.yml: Vulkan SDK detection
  ├─ -DREJI_VULKAN_MOCK=ON for SwiftShader
  ├─ Validation layer reporting (debug CI job)
  ├─ Fallback test: mock Vulkan init failure
  └─ CI passing gates (unit + integration)

Task 3e: Documentation
  ├─ Vulkan dev guide (build, debug, troubleshooting)
  ├─ ExternalMemoryBridge usage notes
  ├─ Shader cache management (clear, invalidation)
  ├─ Performance tuning guide
  └─ Known issues, roadmap (v0.5.1, v0.6)

Blockers: Phase 2 complete
Output: v0.5-beta branch ready
Success: Extensive hardware testing, CI passing, docs complete
```

### Dependency Graph

```
Phase 1 (Infrastructure) ──────┬────────────────────────┐
                              │                        │
                       Phase 2 (Rendering)      Phase 2 (Window)
                              │                        │
                              └────────────┬───────────┘
                                          │
                               Phase 3 (Validation)
                                          │
                                    v0.5-beta ✓
                                          │
                               v0.5 Release Candidate
                                          │
                                    v0.5.1 (Performance)
```

---

## 7. Risk Mitigation

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|-----------|
| Vulkan init fails on Intel iGPU | Medium | High (unsupported users) | RenderCapabilityDetector fallback (QRhi → OpenGL); pre-test on Intel before release |
| Performance target (<2ms) not met | Low | High (feature parity loss) | Image pooling (zero per-frame alloc); shader cache (no per-frame compile); early profiling (Phase 2) |
| Cross-adapter sync race condition | Low | High (frame corruption) | Barrier: VK_PIPELINE_STAGE_TRANSFER_BIT; extensive testing on real hardware (Phase 3) |
| SwiftShader in CI breaks builds | Medium | Low (CI only) | -DREJI_VULKAN_MOCK=ON flag; unit tests still run; real HW testing (self-hosted runner) |
| D3D11 handle export fails | Low | High (initialization fails) | CreateSharedHandle error handling; fallback to OpenGL; logging/notifications |
| Shader cache file corruption | Very Low | Medium (recompile overhead) | Atomic writes (lock file); hash validation; cache version suffix |

---

## 8. Success Criteria (v0.5 Complete)

- ✅ Vulkan instance/device creation stable (all vendor paths)
- ✅ KHR_external_memory_win32 working on real hardware (AMD 780M + RTX 4070)
- ✅ Frame latency <2ms (sustained, 30-min burn test)
- ✅ Shader cache functional (hit rate >90% after first run)
- ✅ Fallback paths graceful (Intel/unsupported → OpenGL with user notification)
- ✅ CI builds passing (mock flag ON, validation layers clean)
- ✅ Unit test suite 100% pass rate
- ✅ Integration tests 100% pass rate (real hardware pre-release)
- ✅ Documentation complete (dev guide, troubleshooting, roadmap)
- ✅ v0.5.1 foundation clean (Vulkan API surface stable for frame pacing, GPU query timing)

---

## 9. v0.5.1 Roadmap (out of scope, preview)

v0.5.1 will build on v0.5's Vulkan foundation:

```
Frame Pacing (DXGI Statistics):
  • vkGetPastPresentationTimingGOOGLE → present timing
  • Frame delivery latency analysis
  • Adaptive present interval

GPU Query Timing (Vulkan):
  • vkCmdWriteTimestamp → per-stage timing
  • Shader execution profiling
  • Transfer BW measurement (D3D11 → Vulkan)

Multi-Monitor Support:
  • DXGI EnumOutputs() enumeration
  • Per-monitor capture selection (UI)
  • Resolution handling per monitor

Preview Quality Selection:
  • Runtime resolution switch (full/half/quarter)
  • CPU/GPU load adaptation
  • User preference override
```

---

## 10. References

- **Khronos Vulkan Registry:** https://registry.khronos.org/vulkan/specs/latest/html/vkspec.html
- **VK_KHR_external_memory_win32:** https://registry.khronos.org/vulkan/specs/latest/man/html/VK_KHR_external_memory_win32.html
- **AGENTS.md (hot-path rules, coding standards)**
- **CONTEXT.md (decision engine levels, architecture decisions)**
- **Windows 10 minimum build:** 1909 (October 2019 Update) — NT handle support
