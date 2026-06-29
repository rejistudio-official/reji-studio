// capture_wgc.cpp — compiled in C++20 mode (see CMakeLists.txt)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "capture_wgc.h"
#ifdef RJ_PLATFORM_WINDOWS

// D3D11 <-> WinRT IDirect3DDevice bridge (CreateDirect3D11DeviceFromDXGIDevice)
#include <Windows.Graphics.DirectX.Direct3D11.Interop.h>

#include <dxgi.h>
#include <cstdio>

namespace reji {

using Microsoft::WRL::ComPtr;
namespace wgc  = winrt::Windows::Graphics::Capture;
namespace wgdx = winrt::Windows::Graphics::DirectX;

// ---------------------------------------------------------------------------
// Convert ID3D11Device → WinRT IDirect3DDevice (required by CreateFreeThreaded)
// ---------------------------------------------------------------------------
static wgdx::Direct3D11::IDirect3DDevice make_winrt_device(ID3D11Device* device)
{
    ComPtr<IDXGIDevice> dxgi_dev;
    device->QueryInterface(__uuidof(IDXGIDevice),
                           reinterpret_cast<void**>(dxgi_dev.GetAddressOf()));

    winrt::com_ptr<::IInspectable> insp;
    winrt::check_hresult(
        ::CreateDirect3D11DeviceFromDXGIDevice(
            dxgi_dev.Get(),
            reinterpret_cast<::IInspectable**>(winrt::put_abi(insp))));

    return insp.as<wgdx::Direct3D11::IDirect3DDevice>();
}

// ---------------------------------------------------------------------------
// WgcScreenCapture::init
// ---------------------------------------------------------------------------
bool WgcScreenCapture::init(const Config& cfg)
{
    // Find NVIDIA encode adapter (vendor 0x10DE)
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                  reinterpret_cast<void**>(factory.GetAddressOf())))) {
        fprintf(stderr, "[WgcCapture] CreateDXGIFactory1 failed\n");
        return false;
    }

    ComPtr<IDXGIAdapter> encode_adapter;
    for (UINT i = 0; ; ++i) {
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(factory->EnumAdapters(i, &adapter))) break;
        DXGI_ADAPTER_DESC desc = {};
        adapter->GetDesc(&desc);
        if (desc.VendorId == 0x10DE) {
            fprintf(stderr, "[WgcCapture] NVIDIA adapter: %ls\n", desc.Description);
            encode_adapter = adapter;
            break;
        }
    }
    if (!encode_adapter) {
        fprintf(stderr, "[WgcCapture] NVIDIA adapter not found — using default\n");
        factory->EnumAdapters(0, &encode_adapter);
    }

    // Create D3D11 device on NVIDIA adapter.
    // BGRA_SUPPORT  — required by WGC / DXGI surface interop
    // VIDEO_SUPPORT — required by NVENC to open an encode session on this device
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr = D3D11CreateDevice(
        encode_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,
        nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        &fl, 1,
        D3D11_SDK_VERSION,
        device_.GetAddressOf(), nullptr, nullptr);

    if (FAILED(hr)) {
        fprintf(stderr, "[WgcCapture] D3D11CreateDevice failed: 0x%08lX\n", hr);
        return false;
    }

    // Find the output_index-th desktop-attached output across all adapters
    HMONITOR target_mon = nullptr;
    uint32_t global_idx = 0;
    for (UINT ai = 0; !target_mon; ++ai) {
        ComPtr<IDXGIAdapter> adapter;
        if (FAILED(factory->EnumAdapters(ai, &adapter))) break;
        for (UINT oi = 0; ; ++oi) {
            ComPtr<IDXGIOutput> output;
            if (FAILED(adapter->EnumOutputs(oi, &output))) break;
            DXGI_OUTPUT_DESC desc = {};
            output->GetDesc(&desc);
            if (!desc.AttachedToDesktop) continue;
            if (global_idx == cfg.output_index) {
                target_mon = desc.Monitor;
                width_  = static_cast<uint32_t>(
                    desc.DesktopCoordinates.right - desc.DesktopCoordinates.left);
                height_ = static_cast<uint32_t>(
                    desc.DesktopCoordinates.bottom - desc.DesktopCoordinates.top);
                break;
            }
            ++global_idx;
        }
    }

    if (!target_mon) {
        fprintf(stderr, "[WgcCapture] Monitor %u not found\n", cfg.output_index);
        return false;
    }

    return init_capture(target_mon);
}

// ---------------------------------------------------------------------------
// WgcScreenCapture::init_capture
// ---------------------------------------------------------------------------
bool WgcScreenCapture::init_capture(HMONITOR monitor)
{
    // IGraphicsCaptureItemInterop lives on the WinRT activation factory
    auto factory_obj = winrt::get_activation_factory<
        wgc::GraphicsCaptureItem,
        IGraphicsCaptureItemInterop>();

    wgc::GraphicsCaptureItem item{ nullptr };
    HRESULT hr = factory_obj->CreateForMonitor(
        monitor,
        winrt::guid_of<wgc::GraphicsCaptureItem>(),
        winrt::put_abi(item));

    if (FAILED(hr) || !item) {
        fprintf(stderr, "[WgcCapture] CreateForMonitor failed: 0x%08lX\n", hr);
        return false;
    }

    auto winrt_device = make_winrt_device(device_.Get());

    winrt::Windows::Graphics::SizeInt32 size{
        static_cast<int32_t>(width_),
        static_cast<int32_t>(height_)
    };

    // CreateFreeThreaded: no DispatcherQueue / message pump needed
    frame_pool_ = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
        winrt_device,
        wgdx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
        2,
        size);

    session_ = frame_pool_.CreateCaptureSession(item);
    session_.IsCursorCaptureEnabled(false);
    session_.StartCapture();

    fprintf(stderr, "[WgcCapture] Session started  %ux%u\n", width_, height_);
    return true;
}

// ---------------------------------------------------------------------------
// WgcScreenCapture::next_frame
//
// Returns borrowed pointer — valid until next next_frame() or shutdown().
// Returns handle==nullptr when no frame is available yet.
// ---------------------------------------------------------------------------
rj::CapturedFrame WgcScreenCapture::next_frame()
{
    std::lock_guard<std::mutex> lk(frame_mutex_);

    // Release previous holdings before fetching the next frame
    last_tex_.Reset();
    last_frame_ = nullptr;

    static int frame_count = 0;
    last_frame_ = frame_pool_.TryGetNextFrame();
    if (!last_frame_) return {};
    if (++frame_count <= 5)
        fprintf(stderr, "[WgcCapture] frame #%d arrived\n", frame_count);

    // Unwrap the WinRT IDirect3DSurface to ID3D11Texture2D via the DXGI interop
    // interface defined in Windows.Graphics.DirectX.Direct3D11.Interop.h.
    auto surface = last_frame_.Surface();
    ComPtr<Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess> access;
    winrt::get_unknown(surface)->QueryInterface(
        __uuidof(Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess),
        reinterpret_cast<void**>(access.GetAddressOf()));

    if (!access) {
        fprintf(stderr, "[WgcCapture] IDirect3DDxgiInterfaceAccess QI failed\n");
        last_frame_ = nullptr;
        return {};
    }

    HRESULT hr = access->GetInterface(
        __uuidof(ID3D11Texture2D),
        reinterpret_cast<void**>(last_tex_.GetAddressOf()));

    if (FAILED(hr) || !last_tex_) {
        fprintf(stderr, "[WgcCapture] GetInterface(ID3D11Texture2D) failed: 0x%08lX\n", hr);
        last_frame_ = nullptr;
        return {};
    }

    // SystemRelativeTime() is a TimeSpan (100ns ticks) — convert to microseconds
    uint64_t ts = static_cast<uint64_t>(
        last_frame_.SystemRelativeTime().count() / 10);

    rj::CapturedFrame out{};
    out.type         = rj::CapturedFrame::HandleType::D3D11;
    out.handle       = last_tex_.Get();
    out.width        = width_;
    out.height       = height_;
    out.timestamp_us = ts;
    return out;
}

// ---------------------------------------------------------------------------
// WgcScreenCapture::shutdown
// ---------------------------------------------------------------------------
void WgcScreenCapture::shutdown()
{
    {
        std::lock_guard<std::mutex> lk(frame_mutex_);
        last_tex_.Reset();
        last_frame_ = nullptr;

        if (session_) {
            session_.Close();
            session_ = nullptr;
        }
        if (frame_pool_) {
            frame_pool_.Close();
            frame_pool_ = nullptr;
        }
    }
    device_.Reset();
    fprintf(stderr, "[WgcCapture] Shutdown\n");
}

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
