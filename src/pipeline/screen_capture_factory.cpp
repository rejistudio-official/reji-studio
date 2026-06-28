// screen_capture_factory.cpp — compiled as C++20 (WinRT ApiInformation)
// Implements rj::IScreenCapture::create(): WGC preferred, DXGI fallback.
#include "include/i_screen_capture.h"

#ifdef RJ_PLATFORM_WINDOWS
#include "capture/capture_wgc.h"
#include "capture/capture_dxgi_screen.h"
#endif

namespace rj {

std::unique_ptr<IScreenCapture> IScreenCapture::create() {
#ifdef RJ_PLATFORM_WINDOWS
    if (reji::WgcScreenCapture::is_supported())
        return std::make_unique<reji::WgcScreenCapture>();
    else
        return std::make_unique<reji::DxgiScreenCapture>();
#else
    return nullptr;
#endif
}

} // namespace rj
