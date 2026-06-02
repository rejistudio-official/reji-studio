#pragma once
#include <cstdint>

namespace reji {

enum class RenderPath {
    kPbo,          ///< Ping-pong PBO CPU→GPU upload (all non-NVIDIA GPUs)
    kNvDxInterop,  ///< WGL_NV_DX_INTEROP zero-copy (NVIDIA; stub until v0.3)
};

struct RenderProfile {
    RenderPath  path;
    const char* name;  ///< human-readable label for logging
};

/// Detects the appropriate GL render path from the display adapter's PCI vendor ID.
struct CapabilityDetector {
    static constexpr uint32_t kVendorNvidia = 0x10DE;

    static RenderProfile detect(uint32_t vendor_id) noexcept {
        if (vendor_id == kVendorNvidia)
            return { RenderPath::kNvDxInterop, "NV_DX_INTEROP" };
        return { RenderPath::kPbo, "PBO" };
    }
};

} // namespace reji
