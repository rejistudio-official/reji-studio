#pragma once
#ifdef RJ_PLATFORM_WINDOWS

// -----------------------------------------------------------------------
// capture_wgc.h — Windows Graphics Capture implementation of IScreenCapture
//
// This header (and the corresponding .cpp) is compiled in C++20 mode;
// do NOT include it from C++17 translation units.
// -----------------------------------------------------------------------

#include "../include/i_screen_capture.h"
#include <d3d11.h>
#include <mutex>
#include <wrl/client.h>

// C++/WinRT projection (requires C++20 or /await)
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Graphics.DirectX.Direct3D11.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Metadata.h>

// IGraphicsCaptureItemInterop — CreateForMonitor interop entry point
#include <Windows.Graphics.Capture.Interop.h>

namespace reji {

class WgcScreenCapture : public rj::IScreenCapture {
public:
    static bool is_supported() noexcept {
        return winrt::Windows::Foundation::Metadata::ApiInformation::IsTypePresent(
            L"Windows.Graphics.Capture.GraphicsCaptureSession");
    }

    bool              init(const Config&) override;
    rj::CapturedFrame next_frame()        override;
    uint32_t          width()  const      override { return width_;  }
    uint32_t          height() const      override { return height_; }
    void              shutdown()          override;
    ID3D11Device*     d3d_device()  const noexcept override { return device_.Get(); }

private:
    bool init_capture(HMONITOR monitor);

    Microsoft::WRL::ComPtr<ID3D11Device>    device_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> last_tex_;

    winrt::Windows::Graphics::Capture::GraphicsCaptureSession    session_{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool frame_pool_{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame    last_frame_{ nullptr };

    uint32_t width_  = 0;
    uint32_t height_ = 0;

    std::mutex frame_mutex_;
};

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
