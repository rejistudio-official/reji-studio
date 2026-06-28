#pragma once
#ifdef RJ_PLATFORM_WINDOWS

// ---------------------------------------------------------------------------
// capture_dxgi_screen.h — IScreenCapture wrapper around DxgiCapturePipeline
//
// Allows the IScreenCapture::create() factory to return a DXGI-backed object
// while preserving typed access to DxgiCapturePipeline (encode_gpu,
// shared_texture, map_preview_frame, etc.) via the pipeline() accessor.
// ---------------------------------------------------------------------------

#include "../include/i_screen_capture.h"
#include "capture_dxgi.h"
#include <memory>

namespace reji {

class DxgiScreenCapture : public rj::IScreenCapture {
public:
    DxgiScreenCapture() = default;
    ~DxgiScreenCapture() { shutdown(); }

    DxgiScreenCapture(const DxgiScreenCapture&)            = delete;
    DxgiScreenCapture& operator=(const DxgiScreenCapture&) = delete;

    bool init(const rj::IScreenCapture::Config& cfg) override {
        DxgiCapturePipeline::Config dcfg;
        dcfg.output_index        = cfg.output_index;
        dcfg.timeout_ms          = cfg.timeout_ms;
        dcfg.allow_cross_adapter = cfg.allow_cross_adapter;
        pipeline_ = std::make_unique<DxgiCapturePipeline>();
        if (!pipeline_->init(dcfg)) {
            pipeline_.reset();
            return false;
        }
        return true;
    }

    rj::CapturedFrame next_frame() override {
        rj::CapturedFrame f{};
        if (!pipeline_) return f;
        f.handle = pipeline_->capture_next();
        f.type   = rj::CapturedFrame::HandleType::D3D11;
        f.width  = pipeline_->width();
        f.height = pipeline_->height();
        return f;
    }

    uint32_t width()  const override { return pipeline_ ? pipeline_->width()  : 0; }
    uint32_t height() const override { return pipeline_ ? pipeline_->height() : 0; }

    void shutdown() override {
        if (pipeline_) { pipeline_->shutdown(); pipeline_.reset(); }
    }

    DxgiCapturePipeline* pipeline() const { return pipeline_.get(); }

private:
    std::unique_ptr<DxgiCapturePipeline> pipeline_;
};

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
