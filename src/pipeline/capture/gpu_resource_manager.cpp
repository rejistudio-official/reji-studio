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

    HRESULT hr = display_gpu_->d3d_device()->CreateTexture2D(
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
    // AMD cross-adapter shared texture requires RGBA format, not BGRA
    desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = { 1, 0 };
    desc.Usage      = D3D11_USAGE_DEFAULT;
    desc.BindFlags  = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    desc.MiscFlags  = D3D11_RESOURCE_MISC_SHARED;

    printf("[GpuRM] Cross-adapter: forcing RGBA format (src fmt=%u)\n", static_cast<unsigned>(fmt));

    HRESULT hr = display_gpu_->d3d_device()->CreateTexture2D(
        &desc, nullptr, &shared_tex_display_);
    if (FAILED(hr)) {
        printf("[GpuRM] CreateTexture2D (shared) failed: 0x%08lX\n", hr);
        return false;
    }

    // Export legacy shared handle from display GPU.
    Microsoft::WRL::ComPtr<IDXGIResource> dxgi_res;
    hr = shared_tex_display_.As(&dxgi_res);
    if (FAILED(hr)) { return false; }

    hr = dxgi_res->GetSharedHandle(&shared_handle_);
    if (FAILED(hr)) {
        printf("[GpuRM] GetSharedHandle failed: 0x%08lX\n", hr);
        return false;
    }

    // Import on encode (NVIDIA) GPU using legacy handle.
    hr = encode_gpu_->d3d_device()->OpenSharedResource(
        shared_handle_, IID_PPV_ARGS(&encode_tex_));
    if (FAILED(hr)) {
        printf("[GpuRM] OpenSharedResource failed: 0x%08lX\n", hr);
        return false;
    }

    return true;
}

void GpuResourceManager::wait_display_gpu_idle() {
    if (!copy_fence_) return;  // B11: cross-adapter path only; same-adapter has no fence
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

    // Fill GPU info from adapter descriptors.
    auto fill_info = [](GpuInfo& info, GpuContext* gpu) {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> a1;
        if (gpu->dxgi_adapter() &&
            SUCCEEDED(gpu->dxgi_adapter()->QueryInterface(IID_PPV_ARGS(&a1)))) {
            DXGI_ADAPTER_DESC1 d{};
            a1->GetDesc1(&d);
            wcsncpy_s(info.description, d.Description, 127);
            info.vendor_id         = d.VendorId;
            info.dedicated_vram_mb = d.DedicatedVideoMemory / (1024 * 1024);
            info.valid             = true;
        }
    };
    fill_info(display_info_, display_gpu_.get());
    fill_info(encode_info_,  encode_gpu_.get());
    wprintf(L"[GpuRM] Display: %s  vendor=0x%04X  VRAM=%lluMB\n",
            display_info_.description, display_info_.vendor_id,
            display_info_.dedicated_vram_mb);
    wprintf(L"[GpuRM] Encode:  %s  vendor=0x%04X  VRAM=%lluMB\n",
            encode_info_.description, encode_info_.vendor_id,
            encode_info_.dedicated_vram_mb);

    // Preview path: always use display GPU staging (bypass cross-adapter shared texture)
    // TODO(cross-adapter): same_adapter_ = false yapılmadan önce:
    // 1. D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX flag'i doğrula
    // 2. IDXGIKeyedMutex::AcquireSync(0)/ReleaseSync(1) implement et
    // 3. VkWin32KeyedMutexAcquireReleaseInfoKHR ekle
    // 4. copy_fence_ (D3D11Query) oluştur
    // Referans: docs/FABLE5_BUG_PLAN.md B6, B11
    same_adapter_ = true;

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

    // Cross-adapter path: simple CopyResource + Flush.
    auto* ctx = display_gpu_->d3d_context();
    ctx->CopyResource(shared_tex_display_.Get(), src);
    ctx->Flush();
    wait_display_gpu_idle();
    return encode_tex_.Get();
}

void GpuResourceManager::shutdown() {
    initialized_ = false;
    copy_fence_.Reset();
    keyed_mutex_encode_.Reset();
    keyed_mutex_display_.Reset();
    encode_tex_.Reset();
    shared_tex_display_.Reset();
    shared_handle_ = nullptr;
}

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS



