#pragma once
#include <cstdint>
#include <string>

#ifndef REJI_VULKAN_MOCK
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#else
// Mock mode: no Vulkan includes needed
#endif

namespace reji {

enum class RenderPath {
    kPbo,          ///< Ping-pong PBO CPU→GPU upload (all non-NVIDIA GPUs)
    kNvDxInterop,  ///< WGL_NV_DX_INTEROP zero-copy (NVIDIA; stub until v0.3)
    kVulkan,       ///< Vulkan KHR_external_memory_win32 (v0.5+)
    kOpenGL,       ///< OpenGL fallback (v0.5+)
    kQRhi,         ///< Qt6 QRhi fallback (v0.5+)
};

struct RenderProfile {
    RenderPath path;
    const char* name;
    uint32_t vendor_id;
    bool supports_khr_external_memory;
};

struct CapabilityDetector {
    static constexpr uint32_t kVendorAmd = 0x1002;
    static constexpr uint32_t kVendorNvidia = 0x10DE;
    static constexpr uint32_t kVendorIntel = 0x8086;

    static RenderProfile detect() noexcept;
    static RenderProfile detect_with_mock(bool mock_vulkan) noexcept;

 private:
    static RenderProfile detect_vulkan_support() noexcept;
};

} // namespace reji
