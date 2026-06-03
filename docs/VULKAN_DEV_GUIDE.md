# Reji Studio v0.5 Vulkan Implementation Guide

## Overview

Reji Studio v0.5 implements a zero-copy GPU interop pipeline using Vulkan KHR_external_memory_win32 for direct D3D11→Vulkan texture sharing. This guide covers architecture, building, profiling, and extending the implementation.

## Architecture

### Core Components

#### 1. **VulkanInitializer** (`src/pipeline/gpu/vulkan_initializer.h/cpp`)
- Initializes Vulkan instance, physical device, and logical device
- Detects GPU vendor (AMD=0x1002, NVIDIA=0x10DE, Intel=0x8086)
- Sets up validation layers in Debug builds
- Creates debug messenger for error reporting

**Key Functions:**
```cpp
bool initialize();           // Full initialization pipeline
bool create_instance();      // VkInstance with validation layers
bool select_device();        // Physical device selection with queue detection
bool create_device();        // Logical device with KHR_external_memory extension
void detect_vendor();        // Vendor heuristic from device name
bool check_required_extensions();  // Validates KHR_external_memory_win32 support
```

#### 2. **ExternalMemoryBridge** (`src/pipeline/gpu/external_memory_bridge.h/cpp`)
- Manages D3D11↔Vulkan texture sharing via NT handles
- Pre-allocates image pool (POOL_SIZE=3) for zero per-frame allocation
- Implements CreateSharedHandle export (Win10+ only)

**Zero-Allocation Design:**
```cpp
VkImage get_pooled_image(int frame_idx);  // Modulo pool lookup: idx % POOL_SIZE
void initialize_image_pool();               // Pre-allocate 3 images at startup
```

#### 3. **ShaderCache** (`src/ui/shader_cache.h/cpp`)
- FNV-1a header-only hash for shader source
- Binary SPIR-V caching at `%APPDATA%\Reji\shader_cache\`
- No dependency on external crypto libraries

**Hit Rate Pattern:**
- First compilation: 0% hit (cache miss, compile, write)
- Subsequent identical shaders: 100% hit (read from cache)
- Typical session: >80-90% hit rate with 20-30 unique shaders

#### 4. **RenderCapabilityDetector** (`src/ui/render_capability.h/cpp`)
- Detects best render path: Vulkan → QRhi (Intel) → OpenGL
- Returns early in mock mode (CI testing)
- Graceful degradation on Vulkan init failure

**Fallback Chain:**
1. Vulkan with KHR_external_memory_win32 (AMD/NVIDIA)
2. QRhi (Intel - lacks external memory support)
3. OpenGL (universal fallback)

#### 5. **HealingOverlay** (`src/ui/healing_overlay.h/cpp`)
- User notifications for render path selection
- Slot `onVulkanInitFailed()` displays graceful degradation message
- Shows "Vulkan desteklenmiyor, OpenGL uyumlu modunda..." on failure

## Build Configuration

### Full Build (Hardware with Vulkan SDK)

```bash
# Set Vulkan SDK environment (or install via LunarG)
$env:VULKAN_SDK = "C:\VulkanSDK\1.3.x"

# Configure and build
cmake -B build -G "Visual Studio 18 2026" -DREJI_VULKAN_MOCK=OFF
cmake --build build --config Release
```

**Requirements:**
- Vulkan SDK 1.3+ (https://www.lunarg.com/vulkan-sdk/)
- Windows 10+ (for VK_KHR_EXTERNAL_MEMORY_WIN32)
- AMD Radeon 780M+ or NVIDIA RTX 4070+

### CI Build (Mock Mode - No GPU Required)

```bash
# Mock mode: Vulkan stubs, no SDK needed
cmake -B build -G "Visual Studio 18 2026" -DREJI_VULKAN_MOCK=ON
cmake --build build --config Debug
ctest --build-config Debug -V
```

**Benefits:**
- Builds and tests without physical GPU
- Validates code paths without Vulkan linking
- Used in GitHub Actions CI/CD

## Performance Profiling

### Using the Profiling Script

```bash
# Build release executable first
cmake --build build --config Release

# Run profiling (30-second collection)
python scripts/profile_performance.py
```

**Output Metrics:**
```
Frame Latency:
  Average: 1.45ms ✓ (target: <2ms)
  Min:     1.20ms
  Max:     1.89ms

Shader Cache:
  Hit Rate: 91.3% ✓ (target: >90%)
  Hits:     73
  Misses:   7

Per-Frame Allocations:
  Count: 0 ✓ (target: 0)
```

### Manual Performance Testing

1. **Latency Measurement:**
   - Look for `[PreviewWidget] paintGL start/end` in stderr
   - Calculate delta between marks
   - Expected: <2ms for 60fps (16.67ms frame time budget)

2. **Shader Cache Hit Rate:**
   - Search stderr for `[ShaderCache] Cache hit` and `Cache miss`
   - Formula: `hits / (hits + misses)`
   - Target: >90% after warm-up (first 2-3 unique shaders)

3. **Allocation Profiling:**
   - Build with `_CrtCheckMemory()` guards in paintGL()
   - Verify no `new`/`malloc` calls in hot-path
   - Use Windows Task Manager: watch memory delta per frame

## Hot-Path Rules (from AGENTS.md)

**Enforce in paintGL():**
- ❌ No heap allocation
- ❌ No JSON parsing
- ❌ No WMI queries
- ❌ No string formatting
- ❌ No lock contention
- ✓ Pre-allocated buffers (ping-pong PBOs, image pool)
- ✓ Lock-free frame delivery via Qt::QueuedConnection

**Validation:**
```cpp
// Good: pre-allocated PBO ping-pong
glBindBuffer(GL_PIXEL_UNPACK_BUFFER, pbo_[pbo_idx_]);
glBufferData(GL_PIXEL_UNPACK_BUFFER, size, bgra_data, GL_STREAM_DRAW);

// Bad: would trigger allocation
std::vector<uint8_t> temp(size);  // ❌ Dynamic allocation
```

## Extending the Implementation

### Adding a New Render Path

1. **Add enum to `RenderPath`:**
   ```cpp
   enum class RenderPath { kVulkan, kQRhi, kOpenGL, kYourPath };
   ```

2. **Update `CapabilityDetector::detect()`:**
   ```cpp
   if (vendor == VulkanVendor::kYourVendor) {
       return {RenderPath::kYourPath, "Your Backend", vendor_id, true};
   }
   ```

3. **Create renderer in `src/ui/your_render_path.h/cpp`:**
   ```cpp
   class YourRenderPath {
       void initialize(VkInstance, VkDevice, VkQueue);
       void render(VkImage source);
       void submit_and_present();
   };
   ```

4. **Hook into PreviewWidget:**
   ```cpp
   void PreviewWidget::selectRenderPath(uint32_t vendor_id) {
       if (profile.path == RenderPath::kYourPath) {
           your_renderer_ = std::make_unique<YourRenderPath>();
       }
   }
   ```

### Adding Validation Layer Messages

Edit `vulkan_initializer.cpp` `debug_callback()`:

```cpp
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(...) {
  // Filter messages by severity and type
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    fprintf(stderr, "[Vulkan] %s\n", pCallbackData->pMessage);
  }
  return VK_FALSE;
}
```

### Benchmarking with Real Hardware

**Test System:** AMD Ryzen 5800X3D + NVIDIA RTX 4070

```bash
# Build for profiling
cmake -B build -G "Visual Studio 18 2026" -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 1. Cold start (initial shader compilations)
python scripts/profile_performance.py

# 2. Warm cache (repeat run)
python scripts/profile_performance.py
# Expected: higher hit rate on warm run

# 3. Stress test (100 frame consecutive renders)
# Manual: Run application, monitor Windows Task Manager for memory stability
```

## Troubleshooting

### "Could not find Vulkan"
```
cmake: error: Could NOT find Vulkan
```
**Solution:** Install Vulkan SDK or use mock mode:
```bash
cmake -B build -DREJI_VULKAN_MOCK=ON
```

### "vkCreateWin32SurfaceKHR failed"
```
[QRhiWindowWrapper] vkCreateWin32SurfaceKHR failed: -4
```
**Cause:** Vulkan instance lacks VK_KHR_surface extension
**Solution:** Check vendor support in CapabilityDetector fallback chain

### "KHR_external_memory not supported"
```
[CapabilityDetector] KHR_external_memory not supported
```
**Cause:** GPU or driver lacks KHR_external_memory_win32
**Solution:** System automatically falls back to QRhi/OpenGL

### Cache Write Failures
```
[ShaderCache] Failed to create directory: Eri??im engellendi
```
**Cause:** Permission issues with AppData directory
**Solution:** Run as administrator or check NTFS permissions

## Testing Checklist

- [ ] `ctest --build-config Debug -V` passes all tests
- [ ] `python scripts/profile_performance.py` shows <2ms latency
- [ ] `./build/tests/Debug/test_shader_cache.exe` reports >90% hit rate
- [ ] GitHub Actions CI passes on mock mode
- [ ] Manual testing: application starts without Vulkan errors
- [ ] Graceful degradation: HealingOverlay shows fallback message
- [ ] Performance: no visual stuttering at 60fps

## References

- **Vulkan Spec:** https://registry.khronos.org/vulkan/specs/1.3/
- **KHR External Memory:** https://www.khronos.org/registry/vulkan/extensions/KHR/VK_KHR_external_memory_win32.txt
- **AGENTS.md:** Hot-path rules and decision engine (Levels 1-3)
- **CONTEXT.md:** Performance targets and goals
- **v0.5 Spec:** `docs/superpowers/specs/2026-06-03-vulkan-pivot-v05.md`

## Version History

- **v0.5** (2026-06): Vulkan KHR_external_memory_win32, zero-copy GPU interop
- **v0.4:** PBO ping-pong, DwmFlush latency optimization (7.6ms → 4.2ms)
- **v0.3:** DXGI capture, hardware encoding
- **v0.2:** Software fallback pipeline
