#pragma once

#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan.h>
#else
// Stub Vulkan types for mock mode (CI without physical GPU)
using VkInstance = void*;
using VkPhysicalDevice = void*;
using VkDevice = void*;
using VkQueue = void*;
#endif

#include <string>

namespace rj::pipeline::gpu {

enum class VulkanVendor {
  kAMD = 0x1002,
  kNVIDIA = 0x10DE,
  kIntel = 0x8086,
  kUnknown = 0x0000,
};

class VulkanInitializer {
 public:
  VulkanInitializer() = default;
  ~VulkanInitializer();

  // Singleton accessor (renamed from instance() to avoid conflict with instance_ member)
  static VulkanInitializer* get();

  bool initialize();

  VkInstance instance() const { return instance_; }
  VkPhysicalDevice physical_device() const { return physical_device_; }
  VkDevice device() const { return device_; }
  uint32_t vendor_id() const { return vendor_id_; }
  VulkanVendor vendor() const;

  bool has_extension(const std::string& ext_name) const;

  uint32_t graphics_queue_family() const { return graphics_queue_family_; }
  VkQueue graphics_queue() const { return graphics_queue_; }

  bool use_keyed_mutex() const { return use_keyed_mutex_; }

  void shutdown();

 private:
  VkInstance instance_ = nullptr;
  VkPhysicalDevice physical_device_ = nullptr;
  VkDevice device_ = nullptr;
  uint32_t vendor_id_ = 0x0000;
  uint32_t graphics_queue_family_ = 0;
  VkQueue graphics_queue_ = nullptr;
  bool use_keyed_mutex_ = false;  ///< D11: true only when VK_KHR_win32_keyed_mutex is available
  bool initialized_ = false;
};

}

// ── Zig export bildirimleri (C ABI köprüsü) ──────────────────────────────────
extern "C" {
    bool             vulkan_init_initialize();
    void             vulkan_init_shutdown();
    VkInstance       vulkan_init_instance();
    VkPhysicalDevice vulkan_init_physical_device();
    VkDevice         vulkan_init_device();
    VkQueue          vulkan_init_graphics_queue();
    uint32_t         vulkan_init_graphics_queue_family();
    uint32_t         vulkan_init_vendor_id();
    bool             vulkan_init_use_keyed_mutex();
    bool             vulkan_init_has_extension(const char* name);
}
