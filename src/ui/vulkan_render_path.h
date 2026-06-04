#pragma once

#ifndef REJI_VULKAN_MOCK
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#else
using VkDevice = void*;
using VkQueue = void*;
using VkFormat = int;
using VkImage = void*;
using VkCommandPool = void*;
using VkCommandBuffer = void*;
using VkResult = int;
const int VK_SUCCESS = 0;
#endif

#include <vector>
#include <cstdint>

namespace reji::ui {

class VulkanRenderPath {
 public:
  VulkanRenderPath(VkDevice device, VkQueue graphics_queue, uint32_t queue_family);
  ~VulkanRenderPath();

  bool initialize(VkFormat swapchain_format, uint32_t width, uint32_t height);

  bool render(VkImage source_image);

  bool submit_and_present();

  void shutdown();

 private:
  bool create_command_pool();
  bool create_command_buffers();

  VkDevice device_;
  VkQueue graphics_queue_;
  uint32_t queue_family_;

  VkCommandPool cmd_pool_;
  std::vector<VkCommandBuffer> cmd_buffers_;
  uint32_t current_cmd_buffer_;

  VkFormat swapchain_format_;
  uint32_t width_;
  uint32_t height_;
};

}
