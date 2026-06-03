#pragma once

#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan.h>
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

  HANDLE export_d3d11_handle(ID3D11Texture2D* staging_texture);

  VkImage create_vulkan_image_from_d3d11(
    HANDLE d3d11_handle,
    VkFormat format,
    uint32_t width,
    uint32_t height
  );

  VkImage get_pooled_image(uint32_t frame_idx);

  bool initialize_image_pool(VkFormat format, uint32_t width, uint32_t height);

  void shutdown();

 private:
  VkDevice device_;
  VkPhysicalDevice physical_device_;

  std::vector<VkImage> image_pool_;
  std::vector<VkDeviceMemory> pool_memory_;

  VkFormat format_;
  uint32_t width_;
  uint32_t height_;
};

}
