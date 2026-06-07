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
using VkFormat = int;
using VkResult = int;
const int VK_FORMAT_UNDEFINED = 0;
const int VK_SUCCESS = 0;
#endif

#include <d3d11_1.h>
#include <wrl/client.h>
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

  // Task 6: Zero-copy frame acquisition with cached handle reuse
  // Returns staging and target VkImages for GPU-side operations
  // Hot-path optimized: no heap allocation, no blocking calls
  bool get_frame_images(
    ID3D11Texture2D* tex,
    VkImage* out_staging,
    VkImage* out_target
  );

  void shutdown();

 private:
  VkDevice device_;
  VkPhysicalDevice physical_device_;

  std::vector<VkImage> image_pool_;
  std::vector<VkDeviceMemory> pool_memory_;

  // GL interop için export edilebilir target image pool
  std::vector<VkImage>        gl_target_pool_;
  std::vector<VkDeviceMemory> gl_target_memory_;
  HANDLE                      gl_target_handles_[POOL_SIZE]{};

  VkFormat format_;
  uint32_t width_;
  uint32_t height_;

  // Task 6: Cached state for hot-path optimization
  uint32_t pool_index_;         // Round-robin index (0..POOL_SIZE-1)
  HANDLE cached_d3d11_handle_;  // NT handle (reused, no per-frame alloc)
};

}
