#include "gpu_resource_manager.h"
#ifdef RJ_PLATFORM_WINDOWS

#include <cstdio>
#include "pitch_copy.h"  // V8/I4: satır-pitch güvenli kopya (copy_mapped_rows)

// d3d11_1.h doesn't define SHARED_CROSS_ADAPTER; it arrived in d3d11_2.h (Win8.1 SDK).
#ifndef D3D11_RESOURCE_MISC_SHARED_CROSS_ADAPTER
#define D3D11_RESOURCE_MISC_SHARED_CROSS_ADAPTER 0x20000
#endif

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
        fprintf(stderr, "[GpuRM] CreateTexture2D (staging) failed: 0x%08lX\n", hr);
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
    // SHARED|NTHANDLE: NT handle export/import between AMD display and NVIDIA encode GPU.
    // SHARED_CROSS_ADAPTER (0x20000) was tested but AMD 780M returns E_INVALIDARG on
    // CreateTexture2D — driver does not support cross-adapter D3D11 sharing.
    // KEYEDMUTEX|NTHANDLE also produces E_INVALIDARG on OpenSharedResource1 (NVIDIA side).
    // Current best attempt: SHARED|NTHANDLE — OpenSharedResource1 still returns E_INVALIDARG.
    // Root cause: AMD iGPU + NVIDIA dGPU cross-vendor D3D11 NT handle sharing unsupported
    // on this Optimus/hybrid topology. Next step: CPU staging fallback path.
    desc.MiscFlags  = D3D11_RESOURCE_MISC_SHARED
                   | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;

    fprintf(stderr, "[GpuRM] Cross-adapter: forcing RGBA format (src fmt=%u)\n", static_cast<unsigned>(fmt));

    HRESULT hr = display_gpu_->d3d_device()->CreateTexture2D(
        &desc, nullptr, &shared_tex_display_);
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] CreateTexture2D (shared) failed: 0x%08lX\n", hr);
        return false;
    }

    // Export NT shared handle from display GPU via IDXGIResource1.
    Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_res1;
    hr = shared_tex_display_.As(&dxgi_res1);
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] QI IDXGIResource1 failed: 0x%08lX\n", hr);
        return false;
    }

    hr = dxgi_res1->CreateSharedHandle(
        nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
        nullptr, &shared_handle_);
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] CreateSharedHandle (NT) failed: 0x%08lX\n", hr);
        return false;
    }

    // Import on encode (NVIDIA) GPU using NT handle.
    Microsoft::WRL::ComPtr<ID3D11Device1> encode_dev1;
    hr = encode_gpu_->d3d_device()->QueryInterface(IID_PPV_ARGS(&encode_dev1));
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] QI ID3D11Device1 failed: 0x%08lX\n", hr);
        return false;
    }

    hr = encode_dev1->OpenSharedResource1(shared_handle_, IID_PPV_ARGS(&encode_tex_));
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] OpenSharedResource1 (NT) failed: 0x%08lX\n", hr);
        return false;
    }

    return true;
}

bool GpuResourceManager::create_cpu_fallback_staging(uint32_t width, uint32_t height,
                                                      DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = width;
    desc.Height           = height;
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = format;
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;

    HRESULT hr = display_gpu_->d3d_device()->CreateTexture2D(&desc, nullptr, &cpu_staging_display_);
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] CPU fallback staging (display) failed: 0x%08lX\n", hr);
        return false;
    }

    desc.Usage          = D3D11_USAGE_DYNAMIC;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;

    hr = encode_gpu_->d3d_device()->CreateTexture2D(&desc, nullptr, &cpu_upload_encode_);
    if (FAILED(hr)) {
        fprintf(stderr, "[GpuRM] CPU fallback staging (encode) failed: 0x%08lX\n", hr);
        return false;
    }

    fprintf(stderr, "[GpuRM] CPU fallback staging created: %ux%u\n", width, height);
    return true;
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

    // Detect same-adapter vs. cross-adapter topology via LUID comparison.
    {
        IDXGIAdapter* display_adapter = display_gpu_->dxgi_adapter();
        IDXGIAdapter* encode_adapter  = encode_gpu_->dxgi_adapter();
        DXGI_ADAPTER_DESC display_desc{}, encode_desc{};
        display_adapter->GetDesc(&display_desc);
        encode_adapter->GetDesc(&encode_desc);

        same_adapter_ = (
            display_desc.AdapterLuid.LowPart  == encode_desc.AdapterLuid.LowPart &&
            display_desc.AdapterLuid.HighPart == encode_desc.AdapterLuid.HighPart
        );

        fprintf(stderr, "[GpuRM] same_adapter=%s (display=%ls encode=%ls)\n",
            same_adapter_ ? "true" : "false",
            display_desc.Description,
            encode_desc.Description);
    }

    // C9: same-adapter → encode_gpu_ must share the display device so that
    // encode_tex_ (created on display_gpu_) and CopyResource are on the same D3D11 context.
    if (same_adapter_) {
        encode_gpu_ = display_gpu_;
    }

    width_  = width;
    height_ = height;

    bool ok = same_adapter_
        ? create_same_adapter_staging(width, height, format)
        : create_cross_adapter_shared(width, height, format);

    if (!ok && !same_adapter_) {
        fprintf(stderr, "[GpuRM] Cross-adapter NT handle sharing failed — falling back to CPU staging path\n");
        ok = create_cpu_fallback_staging(width, height, format);
        use_cpu_fallback_ = ok;
    }

    if (!ok) { return false; }

    fprintf(stderr, "[GpuRM] Init OK  adapter=%s  %ux%u  fmt=%u\n",
           same_adapter_ ? "same" : "cross",
           width, height, static_cast<unsigned>(format));

    initialized_ = true;
    return true;
}

ID3D11Texture2D* GpuResourceManager::transfer(ID3D11Texture2D* src) {
    if (!initialized_ || !src) { return nullptr; }

    if (use_cpu_fallback_) {
        // Display GPU'dan CPU'ya map
        display_gpu_->d3d_context()->CopyResource(cpu_staging_display_.Get(), src);
        D3D11_MAPPED_SUBRESOURCE mapped{};
        HRESULT hr = display_gpu_->d3d_context()->Map(
            cpu_staging_display_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
        if (FAILED(hr)) return nullptr;

        // Encode GPU'ya upload
        D3D11_MAPPED_SUBRESOURCE dst_mapped{};
        HRESULT hr2 = encode_gpu_->d3d_context()->Map(
            cpu_upload_encode_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &dst_mapped);
        if (SUCCEEDED(hr2)) {
            // V8/I4: src (display GPU) ve dst (encode GPU) farklı adapter/sürücü
            // → mapped.RowPitch ile dst_mapped.RowPitch eşit olmayabilir. Eski
            // memcpy(mapped.RowPitch * height_) dst daha küçük pitch'liyse overrun,
            // pitch'ler farklıysa satır kayması yapardı. Satır-pitch güvenli kopya:
            copy_mapped_rows(dst_mapped.pData, dst_mapped.RowPitch,
                             mapped.pData,     mapped.RowPitch, height_);
            encode_gpu_->d3d_context()->Unmap(cpu_upload_encode_.Get(), 0);
        }
        display_gpu_->d3d_context()->Unmap(cpu_staging_display_.Get(), 0);
        return hr2 == S_OK ? cpu_upload_encode_.Get() : nullptr;
    }

    if (same_adapter_) {
        // Both GPUs are the same device; plain CopyResource suffices.
        // D3D11 command ordering ensures NVENC reads after this copy executes.
        encode_gpu_->d3d_context()->CopyResource(encode_tex_.Get(), src);
        return encode_tex_.Get();
    }

    // Cross-adapter path: teorik olarak buraya asla ulaşılmamalı (V8/I30 keşfi —
    // AMD/NVIDIA cross-vendor D3D11 NT-handle paylaşımı bu Optimus/hybrid topolojide
    // desteklenmiyor; create_cross_adapter_shared() her zaman başarısız olur →
    // init() (satır ~254-257) her zaman use_cpu_fallback_=true yapar, transfer()
    // yukarıdaki use_cpu_fallback_ dalından asla çıkamaz).
    // V9/J3 FAIL-CLOSED: Buraya ulaşılıyorsa (örn. paylaşımın desteklendiği farklı
    // donanım/sürücü), senkronsuz bir CopyResource GÜVENLİ DEĞİL — encode_gpu_
    // okumaya başlamadan önce display_gpu_ yazmasının bittiği garanti edilmiyor
    // (eski keyed-mutex/copy_fence_ denemesi V8/I30'da ölü kod olarak kaldırıldı,
    // yerine bir şey konmadı). Cross-adapter NVENC (ROADMAP E7) gerçek bir
    // senkronizasyon mekanizması (keyed mutex + fence) tamamlanana dek etkin
    // DEĞİLDİR; bu yol o zamana kadar veri döndürmez. Senkronsuz kopya yapıp
    // bozuk/yarış-koşullu bir kare döndürmektense fail-closed (nullptr) dönüyoruz —
    // çağıran (capture_dxgi.cpp) null'ı zaten zarifçe ele alıyor.
    fprintf(stderr, "[GpuRM] WARNING: cross-adapter path reached — sync missing (V8/I30, ROADMAP E7), failing closed\n");
    return nullptr;
}

void GpuResourceManager::shutdown() {
    initialized_      = false;
    use_cpu_fallback_ = false;
    encode_tex_.Reset();
    shared_tex_display_.Reset();
    cpu_staging_display_.Reset();
    cpu_upload_encode_.Reset();
    cpu_transfer_buf_.clear();
    if (shared_handle_ && shared_handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(shared_handle_);
        shared_handle_ = nullptr;
    }
}

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS



