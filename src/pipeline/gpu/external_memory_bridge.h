#pragma once

#ifndef REJI_VULKAN_MOCK
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#else
using VkDevice = void*;
using VkPhysicalDevice = void*;
using VkImage = void*;
using VkDeviceMemory = void*;
using VkSemaphore = void*;
using VkFormat = int;
using VkResult = int;
using VkDeviceSize = uint64_t;
const int VK_FORMAT_UNDEFINED = 0;
const int VK_SUCCESS = 0;
#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE nullptr
#endif
#endif

#include <d3d11_1.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <vector>

namespace rj::pipeline::gpu {

class ExternalMemoryBridge {
 public:
  static constexpr int POOL_SIZE = 3;

  ExternalMemoryBridge(VkDevice device, VkPhysicalDevice physical_device);
  ~ExternalMemoryBridge();

  // Late device binding — Vulkan device may not be ready at construction time.
  // Callers (e.g. set_d3d11_frame_callback) can re-bind the logical/physical
  // device pair once VulkanInitializer reports a valid handle.
  void set_device(VkDevice device, VkPhysicalDevice phys_device);

  HANDLE export_d3d11_handle(ID3D11Texture2D* staging_texture);

  VkImage create_vulkan_image_from_d3d11(
    HANDLE d3d11_handle,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t pool_idx
  );

  VkImage get_pooled_image(uint32_t frame_idx);

  bool initialize_image_pool(VkFormat format, uint32_t width, uint32_t height);

  // GL interop için export edilebilir target image pool
  bool initialize_gl_target_pool(VkFormat format, uint32_t width, uint32_t height);
  HANDLE get_gl_target_handle(uint32_t idx) const;

  // G6: GL memory import için exact VkMemoryRequirements::size (w*h*4 yaklaşımı spec ihlali)
  VkDeviceSize gl_target_size(uint32_t slot) const;

  // Task 6: Zero-copy frame acquisition with cached handle reuse
  // Returns staging and target VkImages for GPU-side operations
  // Hot-path optimized: no heap allocation, no blocking calls
  bool get_frame_images(
    ID3D11Texture2D* tex,
    VkImage* out_staging,
    VkImage* out_target
  );

  // B5/C7: GL/Vulkan semaphore sync — 3-slot binary semaphore pool (prevents re-signal)
  bool        create_gl_sync_semaphore();
  HANDLE      get_gl_sync_semaphore_handle(uint32_t slot) const;
  VkSemaphore get_gl_sync_semaphore(uint32_t slot) const;

  // B6: Return the VkDeviceMemory imported from D3D11 for the given VkImage.
  // Used to build VkWin32KeyedMutexAcquireReleaseInfoKHR in execute_copy().
  VkDeviceMemory get_staging_memory_for_image(VkImage img) const;

  // H2: Return the VkDeviceMemory imported from the shared D3D11 texture (slot 0).
  // Keyed mutex must protect this memory — not the per-frame staging pool entry.
  VkDeviceMemory get_shared_texture_memory() const;

  // B16: Register GL-side memory object cleanup hook.
  // Must be called from the GL thread before shutdown() to delete imported GL memory
  // objects before their NT handles are closed.
  using PFN_glDeleteMemoryObjects = void(*)(int, const unsigned int*);
  void set_gl_memory_objects(PFN_glDeleteMemoryObjects pfn,
                             std::vector<unsigned int> objs) {
    pfn_delete_memory_objects_ = pfn;
    gl_memory_objects_         = std::move(objs);
  }

  void shutdown();

 private:
  VkDevice         device_          = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;

  std::vector<VkImage> image_pool_;
  std::vector<VkDeviceMemory> pool_memory_;

  // GL interop için export edilebilir target image pool
  std::vector<VkImage>           gl_target_pool_;
  std::vector<VkDeviceMemory>    gl_target_memory_;
  HANDLE                         gl_target_handles_[POOL_SIZE]{};
  std::array<VkDeviceSize, 3>    gl_target_sizes_{};

  // B5/C7: GL/Vulkan semaphore sync — 3-slot pool (round-robin, no re-signal)
  VkSemaphore gl_sync_sem_pool_[3]    = {};
  HANDLE      gl_sync_sem_handles_[3] = {};

  // B16: GL-side memory object cleanup (call before NT handle close)
  PFN_glDeleteMemoryObjects pfn_delete_memory_objects_ = nullptr;
  std::vector<unsigned int> gl_memory_objects_;

  VkFormat format_  = VK_FORMAT_UNDEFINED;
  uint32_t width_   = 0;
  uint32_t height_  = 0;

  // Task 6: Cached state for hot-path optimization
  uint32_t         pool_index_            = 0;       // Round-robin index (0..POOL_SIZE-1)
  HANDLE           cached_d3d11_handle_   = nullptr; // NT handle (reused, no per-frame alloc)
  ID3D11Texture2D* cached_texture_ptr_    = nullptr; // E5: texture identity — stale-handle guard

  // G9: double-shutdown race guard
  std::atomic<bool> shutdown_called_{false};
};

}

// ── Zig static lib C ABI ─────────────────────────────────────────────────────
// ext_bridge_zig.lib içindeki fonksiyonlar — C++ tarafından doğrudan çağrılabilir.
extern "C" {
    bool ext_bridge_init(VkDevice, VkPhysicalDevice);
    void ext_bridge_set_device(VkDevice, VkPhysicalDevice);
    void ext_bridge_shutdown();
    bool ext_bridge_get_frame_images(
        void* tex, uint32_t slot,
        VkImage* staging, VkImage* target);
    VkImage ext_bridge_get_pooled_image(uint32_t frame_idx);
    bool ext_bridge_init_gl_target_pool(
        VkFormat, uint32_t w, uint32_t h);
    void* ext_bridge_get_gl_target_handle(uint32_t slot);
    VkImage ext_bridge_get_gl_target_image(uint32_t slot);
    uint64_t ext_bridge_gl_target_size(uint32_t slot);
    bool ext_bridge_create_gl_sync_semaphores();
    void* ext_bridge_get_gl_sync_handle(uint32_t slot);
    VkSemaphore ext_bridge_get_staging_semaphore(uint32_t slot);
    VkDeviceMemory ext_bridge_get_staging_memory(VkImage img);
    void ext_bridge_set_gl_memory_objects(
        void* pfn, const uint32_t* objs, uint32_t count);
}
