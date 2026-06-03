#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include "frame_profiler.h"
#include "metrics_collector.h"
#include "../ffi/ffi_bridge.h"  // RjAction

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

    /// Set a callback to receive preview frames (CPU copy, BGRA, called from run_frame thread).
    /// Set to nullptr to disable. Not thread-safe — call before init() or from same thread.
    using PreviewCallback = std::function<void(const void* bgra, int width, int height, int row_pitch)>;
    bool set_preview_callback(PreviewCallback cb);

    /// Process one frame: drain commands, capture, encode, push metrics, pace.
    /// Single-thread assumption — do not call concurrently.
    bool run_frame();

    /// Graceful teardown of all subsystems. SEH-protected.
    bool shutdown();

    /// vendor_id of the display adapter found during init (e.g. 0x10DE = NVIDIA).
    /// Returns 0 if not yet initialized or no adapter found.
    uint32_t display_vendor_id() const;

    /// Accessor for the frame profiler (initialized during init).
    /// Returns nullptr before init() or if profiler creation failed.
    rj::FrameProfiler* profiler() { return profiler_.get(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<rj::FrameProfiler> profiler_;

    /// v0.4+: Action processing thread main loop — polls rj_action_dequeue().
    void action_processor_main();

    /// v0.4+: Apply a single action (bitrate/resolution/fps change).
    bool apply_action(const rj::RjAction& action);
};

} // namespace rj
