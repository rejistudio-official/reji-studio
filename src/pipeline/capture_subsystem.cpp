// src/pipeline/capture_subsystem.cpp
//
// CaptureSubsystem implementasyonu. Windows'a özel (audio/encode_subsystem gibi
// yalnızca WIN32 altında derlenir). Davranış, Pipeline'ın eski capture/capture_dxgi_
// koduyla (init / run_frame adım-2 / null-streak / handle_device_lost reinit)
// birebir aynıdır (Aşama 8 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "capture_subsystem.h"

#include <cstdio>                  // fprintf ([WgcStaging] preview log)
#include "capture_dxgi_screen.h"   // reji::DxgiScreenCapture (dynamic_cast + pipeline())
#include "capture_dxgi.h"          // reji::DxgiCapturePipeline (capture_next())

namespace rj {

bool CaptureSubsystem::init(const Config& cfg) {
    capture_      = rj::IScreenCapture::create();
    capture_dxgi_ = nullptr;
    if (!capture_) {
        return false;
    }
    if (!capture_->init(cfg)) {
        capture_.reset();
        return false;
    }
    // Cache typed DXGI pipeline for encode-specific ops (WGC path'te null kalır).
    auto* dsc = dynamic_cast<reji::DxgiScreenCapture*>(capture_.get());
    capture_dxgi_ = dsc ? dsc->pipeline() : nullptr;
    return true;
}

rj::CapturedFrame CaptureSubsystem::next_frame() {
    rj::CapturedFrame frame{};
    if (capture_dxgi_) {
        // DXGI: typed capture_next() — texture handle'a yazılır (dims kullanılmaz).
        frame.handle = capture_dxgi_->capture_next();
    } else if (capture_) {
        // WGC: next_frame() ile CapturedFrame (handle + dims) döner.
        frame = capture_->next_frame();
    }
    if (frame.handle) null_streak_ = 0;   // geçerli frame → streak sıfırlanır
    return frame;
}

bool CaptureSubsystem::handle_null_frame() noexcept {
    if (++null_streak_ >= kNullStreakReinit) {
        null_streak_ = 0;
        return true;   // reinit gerekli — orkestratör handle_device_lost() çağırır
    }
    return false;
}

void CaptureSubsystem::emit_wgc_preview(const PreviewCallback& preview_cb,
                                        ID3D11Texture2D* tex,
                                        uint32_t frame_w, uint32_t frame_h) {
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

void CaptureSubsystem::shutdown() noexcept {
    // wgc_staging_tex_ kasıtlı olarak reset EDİLMEZ — eski davranış: capture.reset()
    // staging texture'a dokunmazdı (yalnızca çözünürlük değişiminde/yıkımda reset).
    capture_dxgi_ = nullptr;
    capture_.reset();
}

} // namespace rj
