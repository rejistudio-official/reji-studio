#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include "frame_profiler.h"
#include "metrics_collector.h"
#include "../ffi/ffi_bridge.h"  // RjAction

// Forward declarations for Vulkan types (external memory interop)
#ifndef REJI_VULKAN_MOCK
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkImage_T;
using VkDevice = VkDevice_T*;
using VkPhysicalDevice = VkPhysicalDevice_T*;
using VkImage = VkImage_T*;
#else
using VkDevice = void*;
using VkPhysicalDevice = void*;
using VkImage = void*;
#endif

// Forward declaration for GL interop bridge
namespace rj::pipeline::gpu {
    class ExternalMemoryBridge;
}

namespace rj {

/// DXGI capture → NVENC encode → SRT transport pipeline controller.
/// Thread safety: run_frame() is single-threaded; start/stop_stream()
/// may be called from another thread.
/// All public methods return bool (void return is prohibited per project rules).
class Pipeline {
public:
    struct Config {
        uint32_t width             = 1920;
        uint32_t height            = 1080;
        uint32_t fps               = 60;
        uint32_t bitrate_kbps          = 6000;
        uint32_t min_bitrate_kbps      = 1000;
        uint32_t original_bitrate_kbps = 0;    // init() tarafından set edilir, recovery ceiling
        bool     audio_enabled     = false;
        bool     loopback          = true;
        char     srt_host[256]     = {};
        uint16_t srt_port          = 4200;
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

    /// D3D11 zero-copy frame callback — receives staging texture handle for GPU-side operations.
    /// Called from run_frame thread; implementation must not block.
    using D3D11FrameCallback = std::function<void(void* staging_texture, uint32_t width, uint32_t height)>;
    bool set_d3d11_frame_callback(D3D11FrameCallback cb);

    /// WebSocket scene command callback — fired for cmd=3 (scene_cut) and cmd=4 (scene_fade).
    /// Called from run_frame() via ws_command_queue drain; use QMetaObject::invokeMethod for UI.
    using SceneCommandCallback = std::function<void(int cmd)>;
    bool set_scene_command_callback(SceneCommandCallback cb);

    /// Dispatches cmd=3 (scene_cut) / cmd=4 (scene_fade) to the registered callback.
    /// Called from run_frame() on the frame thread.
    void invoke_scene_cmd_(int cmd) noexcept;

    /// Late Vulkan device binding — updates ExternalMemoryBridge with real device handles.
    /// Call after VulkanInitializer::initialize() succeeds. Safe to call multiple times.
    bool notify_vulkan_ready(VkDevice device, VkPhysicalDevice phys_device);

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

    /// v0.5.1: Get last frame images (staging VkImage + target VkImage).
    /// Called from run_frame thread; returns false if images unavailable.
    bool get_last_frame_images(VkImage* out_staging, VkImage* out_target);

    /// v0.5.2: Get external memory bridge for GL interop NT handle access.
    /// Returns nullptr before init() or if bridge creation failed.
    rj::pipeline::gpu::ExternalMemoryBridge* get_external_memory_bridge() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::unique_ptr<rj::FrameProfiler> profiler_;

    /// v0.4+: Action processing thread main loop — polls rj_action_dequeue().
    void action_processor_main();

    /// v0.4+: Apply a single action (bitrate/resolution/fps change).
    bool apply_action(const RjAction& action);

    std::once_flag shutdown_once_;
};

} // namespace rj
