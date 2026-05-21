#pragma once
#include <cstdint>
#include <memory>

namespace rj {

/// DXGI capture → NVENC encode → SRT transport pipeline controller.
/// Thread safety: run_frame() is single-threaded; start/stop_stream()
/// may be called from another thread.
/// All public methods return bool (void return is prohibited per project rules).
class Pipeline {
public:
    struct Config {
        uint32_t width         = 1920;
        uint32_t height        = 1080;
        uint32_t fps           = 60;
        uint32_t bitrate_kbps  = 6000;
        bool     audio_enabled = false;
        bool     loopback      = true;
        char     srt_host[256] = {};
        uint16_t srt_port      = 4200;
    };

    Pipeline();
    ~Pipeline();
    Pipeline(const Pipeline&)            = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    /// Init all subsystems: COM, QPC, DxgiCapturePipeline, NvencEncoder,
    /// WasapiCapture (optional), SrtOutput, rj_start_monitor.
    bool init(const Config& cfg);

    /// Activate SRT packet forwarding and audio capture.
    /// Idempotent: returns true if already streaming.
    bool start_stream();

    /// Deactivate SRT packet forwarding and audio capture.
    /// Idempotent: returns true if already stopped.
    bool stop_stream();

    /// True when both initialized and actively streaming.
    bool is_running() const;

    /// Process one frame: drain commands, capture, encode, push metrics, pace.
    /// Single-thread assumption — do not call concurrently.
    bool run_frame();

    /// Graceful teardown of all subsystems. SEH-protected.
    bool shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rj
