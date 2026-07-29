// src/pipeline/existing_desktop_source.cpp
//
// ExistingDesktopSource implementasyonu. Windows'a özel (capture_subsystem.cpp
// gibi yalnızca WIN32 altında derlenir). Davranış CaptureSubsystem::init /
// next_frame / handle_null_frame / shutdown akışıyla birebir eşlenir; saf
// çekirdek (alan eşlemesi + streak) desktop_source_logic.h'de test edilir.
#include "existing_desktop_source.h"

#include <cstdio>                  // fprintf (WGC staging tanı logu)
#include <windows.h>               // QueryPerformanceCounter/Frequency

#include "capture_dxgi_screen.h"   // reji::DxgiScreenCapture (dynamic_cast + pipeline())
#include "capture_dxgi.h"          // reji::DxgiCapturePipeline (surface_format, encode_gpu)

namespace rj {

namespace {

// Acquire-anı QPC → mikrosaniye. Bölme-önce-çarpma overflow'suz
// (metrics_subsystem ticks_to_us kalıbı).
uint64_t qpc_now_us(uint64_t freq) noexcept {
    if (freq == 0) return 0;
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const uint64_t ticks = static_cast<uint64_t>(c.QuadPart);
    return (ticks / freq) * 1'000'000ULL + (ticks % freq) * 1'000'000ULL / freq;
}

} // namespace

ExistingDesktopSource::ExistingDesktopSource(const Config& cfg) : cfg_(cfg) {}

ExistingDesktopSource::ExistingDesktopSource(
    const Config& cfg, std::unique_ptr<IScreenCapture> capture_for_test)
    : cfg_(cfg), injected_(std::move(capture_for_test)) {}

ExistingDesktopSource::~ExistingDesktopSource() { shutdown(); }

bool ExistingDesktopSource::init() {
    capture_ = injected_ ? std::move(injected_) : IScreenCapture::create();
    dxgi_    = nullptr;
    if (!capture_) {
        return false;
    }
    if (!capture_->init(cfg_)) {
        capture_.reset();
        return false;
    }
    // Typed DXGI pipeline cache (CaptureSubsystem::init kalıbı; WGC'de null).
    auto* dsc = dynamic_cast<reji::DxgiScreenCapture*>(capture_.get());
    dxgi_ = dsc ? dsc->pipeline() : nullptr;

    // Format kaynak-düzeyi sabittir: DXGI'de duplication yüzeyinden, WGC'de
    // frame pool kuruluş formatından (capture_wgc.cpp B8G8R8A8).
    format_ = dxgi_ ? static_cast<uint32_t>(dxgi_->surface_format())
                    : kWgcFramePoolFormat;

    LARGE_INTEGER f{};
    qpc_freq_ = QueryPerformanceFrequency(&f) ? static_cast<uint64_t>(f.QuadPart) : 0;

    streak_.reset();
    return true;
}

SourceFrame ExistingDesktopSource::next_frame() {
    if (!capture_) return SourceFrame{};
    // DXGI yolunda DxgiScreenCapture::next_frame() capture_next()'i sarar ve
    // dims doldurur; WGC aynen döner — CaptureSubsystem'in iki dalıyla eşdeğer.
    const CapturedFrame frame = capture_->next_frame();
    streak_.on_frame(frame.handle != nullptr);
    return map_captured_frame(frame, format_, qpc_now_us(qpc_freq_));
}

SourceMetadata ExistingDesktopSource::metadata() const {
    SourceMetadata md{};
    if (!capture_) return md;
    md.width  = capture_->width();
    md.height = capture_->height();
    md.format = format_;
    // Karelerin yaşadığı cihaz (i_source.h): WGC kendi cihazı (d3d_device
    // override'ı), DXGI encode-GPU cihazı — DxgiScreenCapture d3d_device()
    // override etmediği için encode_gpu() üzerinden alınır (pipeline.cpp
    // encode_device seçimiyle aynı sıra).
    md.device = (dxgi_ && dxgi_->encode_gpu()) ? dxgi_->encode_gpu()->d3d_device()
                                               : capture_->d3d_device();
    return md;
}

SourceState ExistingDesktopSource::state() const noexcept {
    if (!capture_) return SourceState::Uninitialized;
    return streak_.needs_reinit() ? SourceState::NeedsReinit
                                  : SourceState::Running;
}

void ExistingDesktopSource::emit_wgc_preview(const PreviewCallback& preview_cb,
                                             ID3D11Texture2D* tex,
                                             uint32_t frame_w, uint32_t frame_h) {
    // CaptureSubsystem::emit_wgc_preview'dan BİREBİR taşındı (wiring Faz 0
    // karar 1) — davranış paritesi için mantık değiştirilmedi.
    // Resolution change: reset staging texture if dimensions no longer match
    if (wgc_staging_tex_) {
        D3D11_TEXTURE2D_DESC existing{};
        wgc_staging_tex_->GetDesc(&existing);
        D3D11_TEXTURE2D_DESC current{};
        tex->GetDesc(&current);
        if (existing.Width != current.Width || existing.Height != current.Height) {
            wgc_staging_tex_.Reset();
        }
    }
    // NVIDIA device'da staging texture oluştur (bir kez)
    if (!wgc_staging_tex_) {
        D3D11_TEXTURE2D_DESC desc{};
        tex->GetDesc(&desc);
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;
        ID3D11Device* dev = nullptr;
        tex->GetDevice(&dev);
        if (dev) {
            dev->CreateTexture2D(&desc, nullptr, &wgc_staging_tex_);
            dev->Release();
        }
    }
    // GPU → staging copy
    if (wgc_staging_tex_) {
        ID3D11Device* dev = nullptr;
        tex->GetDevice(&dev);
        if (dev) {
            ID3D11DeviceContext* ctx = nullptr;
            dev->GetImmediateContext(&ctx);
            ctx->CopyResource(wgc_staging_tex_.Get(), tex);
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(wgc_staging_tex_.Get(), 0,
                                   D3D11_MAP_READ, 0, &mapped))) {
                static int wgc_prev_cnt = 0;
                if (++wgc_prev_cnt <= 3)
                    fprintf(stderr, "[WgcStaging] preview frame #%d %ux%u pitch=%u\n",
                            wgc_prev_cnt, frame_w, frame_h,
                            (unsigned)mapped.RowPitch);
                preview_cb(mapped.pData,
                           static_cast<int>(frame_w),
                           static_cast<int>(frame_h),
                           static_cast<int>(mapped.RowPitch));
                ctx->Unmap(wgc_staging_tex_.Get(), 0);
            }
            ctx->Release();
            dev->Release();
        }
    }
}

void ExistingDesktopSource::shutdown() {
    // RAII teardown — CaptureSubsystem::shutdown ile aynı model.
    // wgc_staging_tex_ BİLEREK reset edilmez (parite): yalnız çözünürlük
    // değişiminde (emit_wgc_preview) veya yıkımda serbest bırakılır.
    dxgi_ = nullptr;
    capture_.reset();
}

} // namespace rj
