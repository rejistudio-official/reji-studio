#include "render_capability.h"

#ifndef REJI_VULKAN_MOCK
#include "../pipeline/gpu/vulkan_initializer.h"
using namespace rj::pipeline::gpu;
#endif

#include <cstdio>

namespace reji {

RenderProfile CapabilityDetector::detect() noexcept {
#ifdef REJI_VULKAN_MOCK
  fprintf(stderr, "[CapabilityDetector] Mock mode: OpenGL fallback\n");
  fflush(stderr);
  return {RenderPath::kOpenGL, "OpenGL (mocked)", 0x0000, false};
#else
  VulkanInitializer vk_init;
  if (!vk_init.initialize()) {
    fprintf(stderr, "[CapabilityDetector] Vulkan init failed, falling back to OpenGL\n");
    fflush(stderr);
    return {RenderPath::kOpenGL, "OpenGL", 0x0000, false};
  }

  if (!vk_init.has_extension(VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME)) {
    fprintf(stderr, "[CapabilityDetector] KHR_external_memory not supported\n");
    fflush(stderr);
    vk_init.shutdown();
    return {RenderPath::kQRhi, "QRhi", vk_init.vendor_id(), false};
  }

  VulkanVendor vendor = vk_init.vendor();
  uint32_t vendor_id = vk_init.vendor_id();

  if (vendor == VulkanVendor::kAMD || vendor == VulkanVendor::kNVIDIA) {
    fprintf(stderr, "[CapabilityDetector] Using Vulkan (vendor: 0x%04x)\n", vendor_id);
    fflush(stderr);
    vk_init.shutdown();
    return {RenderPath::kVulkan, "Vulkan", vendor_id, true};
  } else if (vendor == VulkanVendor::kIntel) {
    fprintf(stderr, "[CapabilityDetector] Intel GPU, using QRhi fallback\n");
    fflush(stderr);
    vk_init.shutdown();
    return {RenderPath::kQRhi, "QRhi (Intel)", vendor_id, true};
  } else {
    fprintf(stderr, "[CapabilityDetector] Unknown vendor, using OpenGL\n");
    fflush(stderr);
    vk_init.shutdown();
    return {RenderPath::kOpenGL, "OpenGL", vendor_id, true};
  }
#endif
}

RenderProfile CapabilityDetector::detect_with_mock(bool mock_vulkan) noexcept {
  if (mock_vulkan) {
    return {RenderPath::kOpenGL, "OpenGL (mocked)", 0x0000, false};
  }
  return detect();
}

}
