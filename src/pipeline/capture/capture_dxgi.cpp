#include "capture_dxgi.h"
#include "../include/reji_constants.h"  // J7: paylaşımlı keyed-mutex anahtarları
#ifdef RJ_PLATFORM_WINDOWS

#include "../include/frame_profiler.h"
#include <cassert>
#include <cstdio>
#include <intrin.h>   // YieldProcessor

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

    // Tüm output'ları listele (debug)
    for (UINT i = 0; ; ++i) {
        Microsoft::WRL::ComPtr<IDXGIOutput> dbg_out;
        if (FAILED(adapter_->EnumOutputs(i, &dbg_out))) break;
        DXGI_OUTPUT_DESC d = {};
        dbg_out->GetDesc(&d);
        fprintf(stderr, "[DxgiCapture] output[%u] DeviceName=%ls AttachedToDesktop=%d\n",
                i, d.DeviceName, d.AttachedToDesktop);
    }

    // DXGI_ERROR_NOT_CURRENTLY_AVAILABLE (0x887A0022) geçici hatadır —
    // başka process capture ediyorsa veya masaüstü henüz hazır değilse çıkar.
    // 3 deneme, 100ms aralıkla.
    hr = E_FAIL;
    for (int attempt = 0; attempt < 3 && FAILED(hr); ++attempt) {
        if (attempt > 0) Sleep(100);
        hr = output1->DuplicateOutput(device_.Get(), &duplication_);
    }
    if (FAILED(hr)) {
        printf("[DxgiCapture] DuplicateOutput failed after 3 attempts: 0x%08lX\n", hr);
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
    assert(!frame_held_ && "acquire() called while frame held");

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

    // D7: cursor-only frame — no pixel content, release immediately to avoid stale surface
    if (info.AccumulatedFrames == 0) {
        duplication_->ReleaseFrame();
        frame_held_ = false;
        return false;
    }

    Microsoft::WRL::ComPtr<ID3D11Texture2D> tex;

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
    ensure_preview_staging();  // D10a: lazy alloc/free each frame
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

    // v0.5.1: Copy to dual textures (GPU-side shared + CPU-side staging)
    // B6: Keyed mutex guarantees D3D11 write completes before Vulkan reads (key=0→write→key=1)
    // 1.1: AMD fallback — use_keyed_mutex_=false skips AcquireSync to avoid deadlock
    if (shared_texture_) {
        if (use_keyed_mutex_ && keyed_mutex_shared_) {
            HRESULT hr = keyed_mutex_shared_->AcquireSync(rj::constants::kKeyedMutexKeyD3D11, 16);  // 16ms timeout
            if (hr != S_OK) {
                fprintf(stderr, "[Capture] KeyedMutex timeout/fail: 0x%08X\n", hr);
                fflush(stderr);
                capture_->release_frame();
                return nullptr;
            }
            display_ctx_->d3d_context()->CopyResource(shared_texture_.Get(), frame.texture);
            keyed_mutex_shared_->ReleaseSync(rj::constants::kKeyedMutexKeyVulkan);  // hand off to Vulkan (key=1)
        } else {
            // AMD fallback: keyed mutex yok — dogrudan kopyala, tamamlanma icin fence bekle.
            display_ctx_->d3d_context()->CopyResource(shared_texture_.Get(), frame.texture);
            // Flush: pending komutları GPU komut kuyruğuna gönder.
            display_ctx_->d3d_context()->Flush();
            // Completion wait: GpuResourceManager::wait_display_gpu_idle() ile aynı pattern.
            // D3D11 Event query End()+GetData() ile GPU'nun CopyResource'u bitirmesini bekle.
            if (amd_copy_fence_) {
                auto* ctx = display_ctx_->d3d_context();
                ctx->End(amd_copy_fence_.Get());
                // J6: bounded spin. Sınırsız döngü TDR/GPU-hang'de frame thread'i
                // kalıcı dondururdu (GetData device-removed'da S_FALSE dışı HRESULT
                // döner, eski koşul bunu hiç yakalamıyordu). Eskalasyon: kısa yoğun
                // spin → SwitchToThread → ~100ms üst sınırda pes et. 100ms bilinçli
                // olarak keyed-mutex yolundaki 16ms'den farklı: bu yol yalnız
                // preview'i gate'ler (encode değil), düşen kare maliyeti daha düşük.
                constexpr ULONGLONG kAmdCopyTimeoutMs = 100;
                constexpr int       kBusySpinIters    = 4096;
                const ULONGLONG start = GetTickCount64();
                BOOL done = FALSE;
                int  iter = 0;
                for (;;) {
                    HRESULT gd = ctx->GetData(amd_copy_fence_.Get(), &done, sizeof(done),
                                              D3D11_ASYNC_GETDATA_DONOTFLUSH);
                    if (gd == S_OK && done) { break; }  // kopya tamamlandı
                    if (FAILED(gd)) {
                        // TDR / device-removed: 100ms beklemenin anlamı yok, hemen pes et.
                        fprintf(stderr,
                                "[DxgiCapture] amd_copy_fence GetData failed: 0x%08lX "
                                "(device lost?) — dropping frame\n",
                                static_cast<unsigned long>(gd));
                        fflush(stderr);
                        capture_->release_frame();
                        return nullptr;
                    }
                    // gd == S_FALSE: hâlâ bekliyor. Önce yoğun spin, sonra CPU'yu bırak.
                    if (iter++ < kBusySpinIters) {
                        YieldProcessor();
                    } else {
                        SwitchToThread();
                        if (GetTickCount64() - start >= kAmdCopyTimeoutMs) {
                            fprintf(stderr,
                                    "[DxgiCapture] amd_copy_fence spin timeout (%llums) "
                                    "— dropping frame\n",
                                    static_cast<unsigned long long>(kAmdCopyTimeoutMs));
                            fflush(stderr);
                            capture_->release_frame();
                            return nullptr;
                        }
                    }
                }
            }
        }
    }
    if (staging_texture_) {
        display_ctx_->d3d_context()->CopyResource(staging_texture_.Get(), frame.texture);
        preview_staging_dirty_ = true;
    }

    // Legacy preview_staging_ (deprecated in v0.5.1, kept for compatibility)
    if (preview_staging_) {
        display_ctx_->d3d_context()->CopyResource(preview_staging_.Get(), frame.texture);
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

void DxgiCapturePipeline::set_preview_requested(bool enabled) {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    preview_cb_ = enabled;
}

void DxgiCapturePipeline::ensure_preview_staging() {
    std::lock_guard<std::mutex> lk(cb_mutex_);
    if (preview_cb_ && !preview_staging_) init_preview_staging();
    else if (!preview_cb_ && preview_staging_) preview_staging_.Reset();
}

bool DxgiCapturePipeline::init_preview_staging() {
    if (shared_texture_) return true; // zaten oluşturuldu
    if (!initialized_ || !capture_) { return false; }

    // v0.5.1: Create two textures for GPU/CPU paths
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width            = capture_->width();
    desc.Height           = capture_->height();
    desc.MipLevels        = 1;
    desc.ArraySize        = 1;
    desc.Format           = capture_->surface_format();
    desc.SampleDesc.Count = 1;

    // 1. GPU-side shared texture (for Vulkan external memory export)
    desc.Usage          = D3D11_USAGE_DEFAULT;
    desc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    desc.MiscFlags      = D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    HRESULT hr = display_ctx_->d3d_device()->CreateTexture2D(&desc, nullptr, &shared_texture_);
    if (FAILED(hr)) {
        printf("[DxgiCapture] shared_texture_ creation failed: 0x%08lX\n", hr);
        return false;
    }

    // B6: Obtain keyed mutex interface for D3D11↔Vulkan sync
    hr = shared_texture_->QueryInterface(__uuidof(IDXGIKeyedMutex),
        reinterpret_cast<void**>(keyed_mutex_shared_.GetAddressOf()));
    if (FAILED(hr)) {
        fprintf(stderr, "[DxgiCapture] KeyedMutex QueryInterface failed: 0x%08lX\n", hr);
        fflush(stderr);
        // Non-fatal: proceed without keyed mutex sync
    } else {
        fprintf(stderr, "[DxgiCapture] KeyedMutex acquired OK\n");
        fflush(stderr);
    }

    // 2. CPU-side staging texture (for Map/Unmap preview)
    desc.Usage          = D3D11_USAGE_STAGING;
    desc.BindFlags      = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags      = 0;  // No shared flags (CPU-only)
    hr = display_ctx_->d3d_device()->CreateTexture2D(&desc, nullptr, &staging_texture_);
    if (FAILED(hr)) {
        printf("[DxgiCapture] staging_texture_ creation failed: 0x%08lX\n", hr);
        shared_texture_.Reset();
        return false;
    }

    // AMD fallback: D3D11 Event query for spin-waiting after CopyResource + Flush.
    // Same pattern as GpuResourceManager::wait_display_gpu_idle().
    D3D11_QUERY_DESC qd{};
    qd.Query = D3D11_QUERY_EVENT;
    HRESULT qhr = display_ctx_->d3d_device()->CreateQuery(&qd, &amd_copy_fence_);
    if (FAILED(qhr)) {
        fprintf(stderr, "[DxgiCapture] amd_copy_fence_ CreateQuery failed: 0x%08lX (AMD sync degraded)\n", qhr);
        fflush(stderr);
    }

    printf("[DxgiCapture] Dual textures initialized: %ux%u fmt=%u\n",
           capture_->width(), capture_->height(),
           static_cast<unsigned>(capture_->surface_format()));
    return true;
}

bool DxgiCapturePipeline::map_preview_frame(const void** out_data, int* out_pitch) {
    // v0.5.1: Use staging_texture_ (CPU-side, MiscFlags=0)
    if (!staging_texture_ || !preview_staging_dirty_) { return false; }
    D3D11_MAPPED_SUBRESOURCE mapped{};
    HRESULT hr = display_ctx_->d3d_context()->Map(
        staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped);
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
    // v0.5.1: Unmap staging_texture_ (CPU-side)
    display_ctx_->d3d_context()->Unmap(staging_texture_.Get(), 0);
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
