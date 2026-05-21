/**
 * reji_pipeline — Medya pipeline çekirdeği
 * Platform: Windows (v0.1), macOS (v0.3), Linux (v0.4)
 */

#include "include/pipeline.h"
#include "ffi_bridge.h"
#include <chrono>
#include <cstdio>

namespace reji {

Pipeline::Pipeline() {
    printf("[Pipeline] Baslatildi\n");
}

Pipeline::~Pipeline() {
    printf("[Pipeline] Kapatildi\n");
}

bool Pipeline::init(const PipelineConfig& cfg) {
    config_ = cfg;
    printf("[Pipeline] Konfigürasyon: %ux%u@%u fps\n",
           cfg.width, cfg.height, cfg.fps);

    rj_start_monitor();

#ifdef RJ_PLATFORM_WINDOWS
    capture_ = std::make_unique<DxgiCapturePipeline>();
    DxgiCapturePipeline::Config cap_cfg;
    cap_cfg.output_index        = cfg.output_index;
    cap_cfg.timeout_ms          = 1000u / cfg.fps;
    cap_cfg.allow_cross_adapter = true;
    if (!capture_->init(cap_cfg)) {
        printf("[Pipeline] DXGI capture init failed — pipeline continues without capture\n");
        capture_.reset();
    }

    if (capture_) {
        encoder_ = std::make_unique<NvencEncoder>();
        NvencEncoder::Config enc_cfg;
        enc_cfg.width        = capture_->width();
        enc_cfg.height       = capture_->height();
        enc_cfg.fps_num      = cfg.fps;
        enc_cfg.fps_den      = 1;
        enc_cfg.bitrate_kbps = 6000;
        if (!encoder_->init(capture_->encode_gpu()->d3d_device(), enc_cfg,
                            packet_cb_)) {
            printf("[Pipeline] NVENC init failed — encoder disabled\n");
            encoder_.reset();
        }
    }
#endif

    return true;
}

void Pipeline::shutdown() {
#ifdef RJ_PLATFORM_WINDOWS
    if (encoder_) { encoder_->flush(); }
    encoder_.reset();
    capture_.reset();
#endif
    printf("[Pipeline] Shutdown\n");
}

#ifdef RJ_PLATFORM_WINDOWS
ID3D11Texture2D* Pipeline::capture_next() {
    return capture_ ? capture_->capture_next() : nullptr;
}

void Pipeline::set_packet_callback(NvencEncoder::PacketCallback cb) {
    packet_cb_ = std::move(cb);
}
#endif

void Pipeline::push_metrics(float cpu_pct, uint32_t bitrate_kbps,
                             float fps, uint32_t frame_drops) {
    using namespace std::chrono;
    uint64_t ts_us = static_cast<uint64_t>(
        duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count()
    );
    RjMetricSample sample = {};
    sample.magic_head   = RJ_METRIC_MAGIC;
    sample.timestamp_us = ts_us;
    sample.bitrate_kbps = bitrate_kbps;
    sample.fps_actual   = fps;
    sample.cpu_percent  = cpu_pct;
    sample.frame_drops  = frame_drops;
    sample.magic_tail   = RJ_METRIC_MAGIC;
    rj_metrics_push(&sample);
}

} // namespace reji