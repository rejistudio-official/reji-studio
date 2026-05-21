#pragma once
#include <cstdint>
#include <functional>
#include <memory>

#ifdef RJ_PLATFORM_WINDOWS
#include "capture_dxgi.h"
#include "encode_nvenc.h"
#endif

namespace reji {

struct PipelineConfig {
    uint32_t width        = 1920;
    uint32_t height       = 1080;
    uint32_t fps          = 60;
    uint32_t output_index = 0;    ///< which monitor to capture
};

class Pipeline {
public:
    Pipeline();
    ~Pipeline();

    bool init(const PipelineConfig& cfg);
    void shutdown();

    /// Anlık telemetriyi Rust HealingMonitor'a gönderir (non-blocking).
    void push_metrics(float cpu_pct, uint32_t bitrate_kbps,
                      float fps, uint32_t frame_drops);

#ifdef RJ_PLATFORM_WINDOWS
    /// Capture next desktop frame and transfer to encode GPU.
    /// Returns NVENC-ready texture, nullptr on timeout or error.
    ID3D11Texture2D* capture_next();

    DxgiCapturePipeline* capture_pipeline() { return capture_.get(); }
    NvencEncoder*        encoder()          { return encoder_.get(); }

    /// Set callback invoked with each compressed NAL packet.
    /// Must be called before init() or will be ignored.
    void set_packet_callback(NvencEncoder::PacketCallback cb);
#endif

private:
    PipelineConfig config_;

#ifdef RJ_PLATFORM_WINDOWS
    std::unique_ptr<DxgiCapturePipeline> capture_;
    std::unique_ptr<NvencEncoder>        encoder_;
    NvencEncoder::PacketCallback         packet_cb_;
#endif
};

} // namespace reji