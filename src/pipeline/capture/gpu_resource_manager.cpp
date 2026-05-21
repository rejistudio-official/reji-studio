#include "gpu_resource_manager.h"
#ifdef RJ_PLATFORM_WINDOWS

#include <cstdio>
#include <intrin.h>   // YieldProcessor

namespace reji {

// ---------------------------------------------------------------------------
// D3D11GpuContext
// ---------------------------------------------------------------------------

bool D3D11GpuContext::init(IDXGIAdapter* adapter) {
    adapter_ = adapter;

    DXGI_ADAPTER_DESC ad = {};
    adapter->GetDesc(&ad);
    wcsncpy_s(desc_, ad.Description, 127);

    constexpr UINT kFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT
#ifdef _DEBUG
                          | D3D11_CREATE_DEVICE_DEBUG
#endif
                          ;

    HRESULT hr = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,  // must be UNKNOWN when adapter is provided
        nullptr, kFlags,
        nullptr, 0,
        D3D11_SDK_VERSION,
        &device_, nullptr, &context_
    );
    if (FAILED(hr)) {
        wprintf(L"[GpuContext] D3D11CreateDevice '%s' failed: 0x%08lX\n", desc_, hr);
        return false;
    }
    wprintf(L"[GpuContext] Ready: %s\n", desc_);
    return true;
}

void D3D11GpuContext::shutdown() {
    context_.Reset();
    device_.Reset();
    adapter_.Reset();
}

// ---------------------------------------------------------------------------
// GpuResourceManager — private helpers
// ---------------------------------------------------------------------------

bool GpuResourceManager::create_same_adapter_staging(uint32_t w, uint32_t h,
                                                      DXGI_FORMAT fmt) {
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width          = w;
    desc.Height         = h;
    desc.MipLevels      = 1;
    desc.ArraySize      = 1;
    desc.Format         = fmt;
    desc.SampleDesc     = { 1, 0 };
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.BindFlags      = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    HRESULT hr = encode_gpu_->d3d_device()->CreateTexture2D(
        &desc, nullptr, &encode_tex_);
    if (FAILED(hr)) {
        printf("[GpuRM] CreateTexture2D (staging) failed: 0x%08lX\n", hr);
        return false;
    }
    return true;
}

bool GpuResourceManager::create_cross_adapter_shared(uint32_t w, uint32_t h,
                                                      DXGI_FORMAT fmt) {
    // Shared texture on display GPU — keyed mutex + NT handle for cross-adapter open.
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width      = w;
    desc.Height     = h;
    desc.MipLevels  = 1;
    desc.ArraySize  = 1;
    desc.Format     = fmt;
    desc.SampleDesc = { 1, 0 };
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags  = D3D11_RESOURCE_MISC_SHARED_NTHANDLE
                    | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

    HRESULT hr = display_gpu_->d3d_device()->CreateTexture2D(
        &desc, nullptr, &shared_tex_display_);
    if (FAILED(hr)) {
        printf("[GpuRM] CreateTexture2D (shared) failed: 0x%08lX\n", hr);
        return false;
    }

    // Export NT shared handle from display GPU.
    Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_res;
    hr = shared_tex_display_.As(&dxgi_res);
    if (FAILED(hr)) { return false; }

    hr = dxgi_res->CreateSharedHandle(
        nullptr,
        DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr,
        &shared_handle_);
    if (FAILED(hr)) {
        printf("[GpuRM] CreateSharedHandle failed: 0x%08lX\n", hr);
        return false;
    }

    // Import on encode (NVIDIA) GPU.
    Microsoft::WRL::ComPtr<ID3D11Device1> encode_dev1;
    hr = encode_gpu_->d3d_device()->QueryInterface(IID_PPV_ARGS(&encode_dev1));
    if (FAILED(hr)) {
        printf("[GpuRM] QI ID3D11Device1 failed: 0x%08lX\n", hr);
        return false;
    }

    hr = encode_dev1->OpenSharedResource1(shared_handle_, IID_PPV_ARGS(&encode_tex_));
    if (FAILED(hr)) {
        printf("[GpuRM] OpenSharedResource1 failed: 0x%08lX\n", hr);
        return false;
    }

    // Keyed mutexes on both sides for cross-adapter exclusion.
    hr = shared_tex_display_.As(&keyed_mutex_display_);
    if (FAILED(hr)) { return false; }

    hr = encode_tex_.As(&keyed_mutex_encode_);
    if (FAILED(hr)) { return false; }

    // GPU event query for tracking CopyResource completion before mutex release.
    D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
    hr = display_gpu_->d3d_device()->CreateQuery(&qd, &copy_fence_);
    if (FAILED(hr)) {
        printf("[GpuRM] CreateQuery failed: 0x%08lX\n", hr);
        return false;
    }

    return true;
}

void GpuResourceManager::wait_display_gpu_idle() {
    auto* ctx = display_gpu_->d3d_context();
    ctx->End(copy_fence_.Get());
    BOOL done = FALSE;
    // GetData with no flush flag; commands were already submitted via Flush() above.
    while (ctx->GetData(copy_fence_.Get(), &done, sizeof(done),
                        D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_FALSE || !done) {
        YieldProcessor();
    }
}

// ---------------------------------------------------------------------------
// GpuResourceManager — public API
// ---------------------------------------------------------------------------

bool GpuResourceManager::init(
    std::shared_ptr<GpuContext> display_gpu,
    std::shared_ptr<GpuContext> encode_gpu,
    uint32_t width, uint32_t height, DXGI_FORMAT format)
{
    display_gpu_ = std::move(display_gpu);
    encode_gpu_  = std::move(encode_gpu);

    same_adapter_ = (display_gpu_->dxgi_adapter() == encode_gpu_->dxgi_adapter());

    bool ok = same_adapter_
        ? create_same_adapter_staging(width, height, format)
        : create_cross_adapter_shared(width, height, format);

    if (!ok) { return false; }

    printf("[GpuRM] Init OK  adapter=%s  %ux%u  fmt=%u\n",
           same_adapter_ ? "same" : "cross",
           width, height, static_cast<unsigned>(format));

    initialized_ = true;
    return true;
}

ID3D11Texture2D* GpuResourceManager::transfer(ID3D11Texture2D* src) {
    if (!initialized_ || !src) { return nullptr; }

    if (same_adapter_) {
        // Both GPUs are the same device; plain CopyResource suffices.
        // D3D11 command ordering ensures NVENC reads after this copy executes.
        encode_gpu_->d3d_context()->CopyResource(encode_tex_.Get(), src);
        return encode_tex_.Get();
    }

    // Cross-adapter path.
    // key=0: display GPU owns; key=1: encode GPU owns.

    // Acquire write access on display side (waits for previous encode cycle).
    HRESULT hr = keyed_mutex_display_->AcquireSync(0, 16 /* ms, one frame budget */);
    if (hr == static_cast<HRESULT>(WAIT_TIMEOUT)) {
        printf("[GpuRM] AcquireSync(display) timeout — frame dropped\n");
        return nullptr;
    }
    if (FAILED(hr)) {
        printf("[GpuRM] AcquireSync(display) failed: 0x%08lX\n", hr);
        return nullptr;
    }

    // GPU-blit: DXGI frame texture (display VRAM) → shared texture (display VRAM).
    // This is a VRAM-to-VRAM copy; no CPU memory involved.
    auto* ctx = display_gpu_->d3d_context();
    ctx->CopyResource(shared_tex_display_.Get(), src);
    ctx->Flush();

    // Wait for GPU to finish before releasing the keyed mutex.
    // encode GPU must only see committed pixels.
    wait_display_gpu_idle();

    // Hand ownership to encode GPU (key 0→1).
    keyed_mutex_display_->ReleaseSync(1);

    // Acquire encode side to assert exclusive access.
    hr = keyed_mutex_encode_->AcquireSync(1, 16);
    if (FAILED(hr)) {
        printf("[GpuRM] AcquireSync(encode) failed: 0x%08lX\n", hr);
        return nullptr;
    }

    // Release encode mutex immediately.
    // Within a single D3D11 device context, NVENC commands submitted after
    // AcquireSync execute after the shared texture write, so no further
    // CPU synchronization is needed for synchronous NVENC use.
    keyed_mutex_encode_->ReleaseSync(0);

    return encode_tex_.Get();
}

void GpuResourceManager::shutdown() {
    initialized_ = false;
    copy_fence_.Reset();
    keyed_mutex_encode_.Reset();
    keyed_mutex_display_.Reset();
    encode_tex_.Reset();
    shared_tex_display_.Reset();
    if (shared_handle_) {
        CloseHandle(shared_handle_);
        shared_handle_ = nullptr;
    }
}

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
