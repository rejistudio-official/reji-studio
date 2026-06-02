#include "capture_dxgi.h"
#ifdef RJ_PLATFORM_WINDOWS

#include "../include/frame_profiler.h"
#include <cstdio>

namespace reji {

// ---------------------------------------------------------------------------
// DxgiCaptureSession
// ---------------------------------------------------------------------------

bool DxgiCaptureSession::init(ID3D11Device* device, IDXGIAdapter* adapter,
                               const Config& cfg) {
    device_  = device;
    adapter_ = adapter;
    config_  = cfg;

    if (!create_duplication()) { return false; }

    initialized_ = true;
    printf("[DxgiCapture] Session ready  output=%u  %ux%u  fmt=%u\n",
           cfg.output_index, width_, height_,
           static_cast<unsigned>(surface_format_));
    return true;
}

bool DxgiCaptureSession::create_duplication() {
    duplication_.Reset();
    frame_tex_.Reset();
    frame_held_   = false;
    needs_reinit_ = false;

    Microsoft::WRL::ComPtr<IDXGIOutput> output;
    HRESULT hr = adapter_->EnumOutputs(config_.output_index, &output);
    if (FAILED(hr)) {
        printf("[DxgiCapture] EnumOutputs(%u) failed: 0x%08lX\n",
               config_.output_index, hr);
        return false;
    }

    DXGI_OUTPUT_DESC out_desc = {};
    output->GetDesc(&out_desc);
    width_  = static_cast<uint32_t>(out_desc.DesktopCoordinates.right
                                  - out_desc.DesktopCoordinates.left);
    height_ = static_cast<uint32_t>(out_desc.DesktopCoordinates.bottom
                                  - out_desc.DesktopCoordinates.top);

    Microsoft::WRL::ComPtr<IDXGIOutput1> output1;
    hr = output.As(&output1);
    if (FAILED(hr)) {
        printf("[DxgiCapture] IDXGIOutput1 QI failed: 0x%08lX\n", hr);
        return false;
    }

    hr = output1->DuplicateOutput(device_.Get(), &duplication_);
    if (FAILED(hr)) {
        // E_ACCESSDENIED: this adapter does not drive the display.
        // DXGI_ERROR_UNSUPPORTED: running in a remote session without WDDM.
        printf("[DxgiCapture] DuplicateOutput failed: 0x%08lX\n", hr);
        return false;
    }

    DXGI_OUTDUPL_DESC dupl_desc = {};
    duplication_->GetDesc(&dupl_desc);
    surface_format_ = dupl_desc.ModeDesc.Format;

    return true;
}

bool DxgiCaptureSession::reinit() {
    // Ensure any held frame is released before tearing down the session.
    if (frame_held_ && duplication_) {
        duplication_->ReleaseFrame();
        frame_held_ = false;
    }
    frame_tex_.Reset();
    return create_duplication();
}

void DxgiCaptureSession::shutdown() {
    if (frame_held_ && duplication_) {
        duplication_->ReleaseFrame();
        frame_held_ = false;
    }
    frame_tex_.Reset();
    duplication_.Reset();
    device_.Reset();
    adapter_.Reset();
    initialized_  = false;
    needs_reinit_ = false;
}

bool DxgiCaptureSession::acquire(CaptureFrame& out_frame) {
    if (!initialized_ || needs_reinit_) {
        printf("[DxgiCapture] acquire: early-out initialized_=%d needs_reinit_=%d\n",
               (int)initialized_, (int)needs_reinit_);
        return false;
    }

    // Guard against missing release_frame() call from the previous iteration.
    if (frame_held_) {
        duplication_->ReleaseFrame();
        frame_tex_.Reset();
        frame_held_ = false;
    }

    DXGI_OUTDUPL_FRAME_INFO info = {};
    Microsoft::WRL::ComPtr<IDXGIResource> resource;

    if (profiler_) profiler_->markAcquireStart();

    HRESULT hr = duplication_->AcquireNextFrame(config_.timeout_ms, &info, &resource);

    if (profiler_) profiler_->markAcquireEnd();

    if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
        return false;
    }
    if (hr == DXGI_ERROR_ACCESS_LOST  ||
        hr == DXGI_ERROR_DEVICE_REMOVED) {
        // Screen locked, resolution changed, UAC prompt, remote session switch.
        printf("[DxgiCapture] Session lost (0x%08lX); call reinit()\n", hr);
        needs_reinit_ = true;
        return false;
    }
    if (FAILED(hr)) {
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;

    // Cursor-only update: still a valid frame for preview
    // return false;  // removed: preview needs these frames too
    if (info.AccumulatedFrames == 0 && info.LastPresentTime.QuadPart == 0) {
        // No new desktop content � reuse last texture if we have one, else skip.
        if (!frame_tex_) {
            duplication_->ReleaseFrame();
            return false;
        }
        out_frame.texture     = frame_tex_.Get();
        out_frame.width       = width_;
        out_frame.height      = height_;
        out_frame.format      = surface_format_;
        out_frame.present_time = info.LastPresentTime.QuadPart;
        frame_held_           = true;
        return true;
    }

    hr = resource.As(&tex);
    if (FAILED(hr)) {
        printf("[DxgiCapture] resource.As<Texture2D> failed: 0x%08lX\n", hr);
        duplication_->ReleaseFrame();
        frame_held_ = false;
        return false;
    }
    frame_tex_ = tex;

    out_frame.texture     = frame_tex_.Get();
    out_frame.width       = width_;
    out_frame.height      = height_;
    out_frame.format      = surface_format_;
    out_frame.present_time = info.LastPresentTime.QuadPart;
    frame_held_           = true;
    return true;
}

void DxgiCaptureSession::release_frame() {
    if (!frame_held_) { return; }

    // Release the duplication frame back to DXGI.
    // frame_tex_ ref count drops here; actual D3D11 destruction deferred by runtime.
    duplication_->ReleaseFrame();
    frame_tex_.Reset();
    frame_held_ = false;
}

// ---------------------------------------------------------------------------
// DxgiCapturePipeline � GPU scan
// ---------------------------------------------------------------------------

bool DxgiCapturePipeline::scan_gpus(IDXGIFactory1* factory, GpuScan& out) {
    out.count = 0;
    UINT idx = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
    while (factory->EnumAdapters1(idx++, &adapter) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { adapter.Reset(); continue; }
        if (out.count < 8) {
            auto& e = out.entries[out.count];
            wcsncpy_s(e.description, desc.Description, 127);
            e.vendor_id         = desc.VendorId;
            e.dedicated_vram_mb = desc.DedicatedVideoMemory / (1024 * 1024);
            ++out.count;
            wprintf(L"[GpuScan] [%u] %s  vendor=0x%04X  VRAM=%lluMB\n",
                    out.count - 1, e.description, e.vendor_id, e.dedicated_vram_mb);
        }
        adapter.Reset();
    }
    printf("[GpuScan] %u hardware adapter(s) found\n", out.count);
    return out.count > 0;
}

// ---------------------------------------------------------------------------
// DxgiCapturePipeline � adapter discovery
// ---------------------------------------------------------------------------

bool DxgiCapturePipeline::find_display_adapter(IDXGIFactory1* factory,
                                                IDXGIAdapter** out_adapter) {
    UINT idx = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;

    while (factory->EnumAdapters1(idx++, &candidate) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        candidate->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { continue; }  // skip WARP

        // A hardware adapter with the requested output drives the display.
        Microsoft::WRL::ComPtr<IDXGIOutput> test_out;
        if (SUCCEEDED(candidate->EnumOutputs(config_.output_index, &test_out))) {
            wprintf(L"[DxgiCapture] Display adapter: %s\n", desc.Description);
            *out_adapter = candidate.Detach();
            return true;
        }
    }

    printf("[DxgiCapture] No hardware adapter with output %u found\n",
           config_.output_index);
    return false;
}

bool DxgiCapturePipeline::find_encode_adapter(IDXGIFactory1* factory,
                                               IDXGIAdapter*  display_adapter,
                                               IDXGIAdapter** out_adapter) {
    if (!config_.allow_cross_adapter) {
        *out_adapter = display_adapter;
        display_adapter->AddRef();
        return true;
    }

    UINT idx = 0;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> candidate;

    while (factory->EnumAdapters1(idx++, &candidate) != DXGI_ERROR_NOT_FOUND) {
        DXGI_ADAPTER_DESC1 desc;
        candidate->GetDesc1(&desc);

        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) { continue; }
        if (candidate.Get() == display_adapter)       { continue; }

        // NVIDIA VendorId 0x10DE � NVENC lives here.
        if (desc.VendorId == 0x10DE) {
            wprintf(L"[DxgiCapture] Encode adapter: %s (NVIDIA)\n", desc.Description);
            *out_adapter = candidate.Detach();
            return true;
        }
    }

    // No discrete NVIDIA GPU found; fall back to same-adapter path.
    wprintf(L"[DxgiCapture] No NVIDIA adapter found; using display adapter\n");
    *out_adapter = display_adapter;
    display_adapter->AddRef();
    return true;
}

// ---------------------------------------------------------------------------
// DxgiCapturePipeline � lifecycle
// ---------------------------------------------------------------------------

bool DxgiCapturePipeline::init(const Config& cfg) {
    config_ = cfg;

    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        printf("[DxgiCapture] CreateDXGIFactory1 failed: 0x%08lX\n", hr);
        return false;
    }

    scan_gpus(factory.Get(), gpu_scan_);

    Microsoft::WRL::ComPtr<IDXGIAdapter> display_adapter;
    if (!find_display_adapter(factory.Get(), &display_adapter)) { return false; }

    Microsoft::WRL::ComPtr<IDXGIAdapter> encode_adapter;
    if (!find_encode_adapter(factory.Get(), display_adapter.Get(), &encode_adapter)) {
        return false;
    }

    display_ctx_ = std::make_shared<D3D11GpuContext>();
    if (!display_ctx_->init(display_adapter.Get())) { return false; }

    encode_ctx_ = std::make_shared<D3D11GpuContext>();
    if (!encode_ctx_->init(encode_adapter.Get())) { return false; }

    capture_ = std::make_unique<DxgiCaptureSession>();
    DxgiCaptureSession::Config cap_cfg;
    cap_cfg.output_index = cfg.output_index;
    cap_cfg.timeout_ms   = cfg.timeout_ms;
    if (!capture_->init(display_ctx_->d3d_device(),
                        display_ctx_->dxgi_adapter(), cap_cfg)) {
        return false;
    }

    resource_mgr_ = std::make_unique<GpuResourceManager>();
    if (!resource_mgr_->init(
            display_ctx_, encode_ctx_,
            capture_->width(), capture_->height(), capture_->surface_format())) {
        return false;
    }

    initialized_ = true;
    printf("[DxgiCapture] Pipeline ready  %ux%u  cross_adapter=%s\n",
           capture_->width(), capture_->height(),
           resource_mgr_->same_adapter() ? "no" : "yes");
    return true;
}

void DxgiCapturePipeline::shutdown() {
    initialized_ = false;
    resource_mgr_.reset();
    capture_.reset();
    encode_ctx_.reset();
    display_ctx_.reset();
}


void DxgiCapturePipeline::setProfiler(rj::FrameProfiler* profiler) {
    profiler_ = profiler;
    if (capture_) {
        capture_->setProfiler(profiler);
    }
}
// ---------------------------------------------------------------------------
// DxgiCapturePipeline � hot path
// ---------------------------------------------------------------------------

ID3D11Texture2D* DxgiCapturePipeline::capture_next() {
    if (!initialized_) { return nullptr; }

    // Recover from screen lock / resolution change / remote session events.
    if (capture_->needs_reinit()) {
        printf("[DxgiCapture] Reinitializing after session loss\n");
        if (!capture_->reinit()) { return nullptr; }
    }

    CaptureFrame frame;
    if (!capture_->acquire(frame)) {
        return nullptr;  // timeout or session loss � not an error
    }

    // GPU-blit frame to encode GPU (same or cross-adapter).
    // For cross-adapter: CopyResource + keyed-mutex sync; no CPU copy.
    ID3D11Texture2D* encode_tex = resource_mgr_->transfer(frame.texture);

    // CPU preview staging: copy DXGI display texture to staging BEFORE releasing
    // the DXGI frame. Map() will synchronise; no explicit Flush needed here.
    if (preview_staging_) {
        display_ctx_->d3d_context()->CopyResource(preview_staging_.Get(), frame.texture);
        preview_staging_dirty_ = true;
    }

    // Release the DXGI frame now that the GPU copy is submitted.
    // The shared / staging texture holds the data for NVENC.
    capture_->release_frame();

    if (!encode_tex) {
        static int null_count = 0;
        if (++null_count <= 10)
            printf("[DxgiCapture] transfer() returned null (count=%d) tex=%p\n",
                   null_count, frame.texture);
    }

    return encode_tex;  // nullptr if transfer failed
}

bool DxgiCapturePipeline::init_preview_staging() {
    if (!initialized_ || !capture_) { return false; }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = capture_->width();
    desc.Height           = capture_->height();
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = capture_->surface_format();
    desc.SampleDesc.Count = 1;
    desc.Usage            = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags   = D3D11_CPU_ACCESS_READ;
    HRESULT hr = display_ctx_->d3d_device()->CreateTexture2D(&desc, nullptr, &preview_staging_);
    if (FAILED(hr)) {
        printf("[DxgiCapture] init_preview_staging failed: 0x%08lX\n", hr);
        return false;
    }
    printf("[DxgiCapture] Preview staging %ux%u fmt=%u\n",
           capture_->width(), capture_->height(),
           static_cast<unsigned>(capture_->surface_format()));
    return true;
}

bool DxgiCapturePipeline::map_preview_frame(const void** out_data, int* out_pitch) {
    if (!preview_staging_ || !preview_staging_dirty_) { return false; }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = display_ctx_->d3d_context()->Map(
        preview_staging_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
    if (FAILED(hr)) {
        static int fail_count = 0;
        if (++fail_count <= 3)
            printf("[DxgiCapture] map_preview_frame Map failed: 0x%08lX\n", hr);
        return false;
    }
    *out_data              = mapped.pData;
    *out_pitch             = static_cast<int>(mapped.RowPitch);
    preview_mapped_        = true;
    preview_staging_dirty_ = false;
    return true;
}

void DxgiCapturePipeline::unmap_preview_frame() {
    if (!preview_mapped_) { return; }
    display_ctx_->d3d_context()->Unmap(preview_staging_.Get(), 0);
    preview_mapped_ = false;
}


uint32_t DxgiCapturePipeline::width() const {
    return capture_ ? capture_->width() : 0;
}

uint32_t DxgiCapturePipeline::height() const {
    return capture_ ? capture_->height() : 0;
}

DXGI_FORMAT DxgiCapturePipeline::surface_format() const {
    return capture_ ? capture_->surface_format() : DXGI_FORMAT_UNKNOWN;
}

bool DxgiCapturePipeline::is_cross_adapter() const {
    return resource_mgr_ && !resource_mgr_->same_adapter();
}

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
