#pragma once

#include <dxgi1_2.h>
#include <cstdint>

class DxgiFramePacing {
public:
    struct FrameStats {
        uint32_t present_count = 0;
        uint32_t present_refresh_count = 0;
        uint32_t sync_refresh_count = 0;
        float frame_time_ms = 0.0f;
        float gpu_busy_ms = 0.0f;
        bool gpu_stall = false;  // GPU stall detected (frame time > 5ms)

        // Vulkan GPU timestamps (filled by GpuQueryTiming)
        float copy_gpu_time_ms = 0.0f;
        float render_gpu_time_ms = 0.0f;
        uint64_t timestamp_us = 0;  // Wallclock microseconds
    };

    DxgiFramePacing() = default;
    ~DxgiFramePacing() = default;

    // Initialize with DXGI swap chain
    bool init(IDXGISwapChain1* swap_chain);

    // Poll frame statistics (non-blocking)
    // Returns true if new stats available, false if no update
    bool poll_frame_stats(FrameStats* out_stats);

    // Shutdown
    void shutdown();

private:
    IDXGISwapChain1* swap_chain_ = nullptr;
    uint32_t last_present_count_ = 0;
    uint64_t last_qpc_time_ns_ = 0;

    // Cache for rolling average
    static constexpr int ROLLING_WINDOW = 30;
    float frame_times_[ROLLING_WINDOW]{};
    int frame_index_ = 0;

    float compute_rolling_average() const;
};
