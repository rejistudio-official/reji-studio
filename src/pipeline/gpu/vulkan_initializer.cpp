#include "vulkan_initializer.h"

#include <cstdio>

namespace rj::pipeline::gpu {

VulkanInitializer* VulkanInitializer::get() {
  static VulkanInitializer s_instance;
  return &s_instance;
}

bool VulkanInitializer::initialize() {
  if (initialized_) return true;

  if (!vulkan_init_initialize()) {
    fprintf(stderr, "[Vulkan] Zig init failed\n");
    return false;
  }

  instance_              = vulkan_init_instance();
  physical_device_       = vulkan_init_physical_device();
  device_                = vulkan_init_device();
  graphics_queue_        = vulkan_init_graphics_queue();
  graphics_queue_family_ = vulkan_init_graphics_queue_family();
  vendor_id_             = vulkan_init_vendor_id();
  use_keyed_mutex_       = vulkan_init_use_keyed_mutex();
  initialized_           = true;

  fprintf(stderr, "[Vulkan] Zig init OK, vendor=0x%04X\n", vendor_id_);
  return true;
}

bool VulkanInitializer::has_extension(const std::string& name) const {
  return vulkan_init_has_extension(name.c_str());
}

VulkanVendor VulkanInitializer::vendor() const {
  switch (vendor_id_) {
    case 0x1002: return VulkanVendor::kAMD;
    case 0x10DE: return VulkanVendor::kNVIDIA;
    case 0x8086: return VulkanVendor::kIntel;
    default: return VulkanVendor::kUnknown;
  }
}

void VulkanInitializer::shutdown() {
  if (!initialized_) return;
  vulkan_init_shutdown();
  initialized_ = false;
}

VulkanInitializer::~VulkanInitializer() {
  shutdown();
}

}
