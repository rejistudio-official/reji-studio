#include "frame_pacing.h"
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <algorithm>

bool DxgiFramePacing::init(IDXGISwapChain1* swap_chain) {
    if (!swap_chain) {
        fprintf(stderr, "[DxgiFramePacing] Invalid swap chain\n");
        return false;
    }

    try {
        swap_chain_ = swap_chain;
        last_present_count_ = 0;

        LARGE_INTEGER qpc;
        if (!QueryPerformanceCounter(&qpc)) {
            fprintf(stderr, "[DxgiFramePacing] QueryPerformanceCounter failed\n");
            return false;
        }
        last_qpc_time_ns_ = qpc.QuadPart;

        std::memset(frame_times_, 0, sizeof(frame_times_));

        fprintf(stderr, "[DxgiFramePacing] Initialized with swap chain\n");
        return true;
    } catch (...) {
        fprintf(stderr, "[DxgiFramePacing] Exception during init\n");
        return false;
    }
}

bool DxgiFramePacing::poll_frame_stats(FrameStats* out_stats) {
    if (!swap_chain_ || !out_stats) {
        return false;
    }

    try {
        DXGI_FRAME_STATISTICS frame_stats{};
        HRESULT hr = swap_chain_->GetFrameStatistics(&frame_stats);
        if (FAILED(hr)) {
            return false;  // No new stats available
        }

        // Check if present count changed
        if (frame_stats.PresentCount == last_present_count_) {
            return false;  // No new frame
        }

        LARGE_INTEGER qpc;
        QueryPerformanceCounter(&qpc);
        uint64_t current_qpc_ns = qpc.QuadPart;

        // Calculate frame time (QPC ticks to milliseconds)
        // Note: Requires QueryPerformanceFrequency() for accurate conversion
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        float frame_time_ms = (float)(current_qpc_ns - last_qpc_time_ns_) / (float)freq.QuadPart * 1000.0f;

        // Rolling average frame time
        frame_times_[frame_index_] = frame_time_ms;
        frame_index_ = (frame_index_ + 1) % ROLLING_WINDOW;
        float avg_frame_time = compute_rolling_average();

        // Detect GPU stall (>5ms frame time indicates VSYNC waiting or GPU stall)
        bool gpu_stall = avg_frame_time > 5.0f;

        out_stats->present_count = frame_stats.PresentCount;
        out_stats->present_refresh_count = frame_stats.PresentRefreshCount;
        out_stats->sync_refresh_count = frame_stats.SyncRefreshCount;
        out_stats->frame_time_ms = frame_time_ms;
        out_stats->gpu_busy_ms = frame_time_ms;  // Placeholder: will be filled by GPU timestamps
        out_stats->gpu_stall = gpu_stall;
        out_stats->timestamp_us = current_qpc_ns / 1000;

        last_present_count_ = frame_stats.PresentCount;
        last_qpc_time_ns_ = current_qpc_ns;

        return true;
    } catch (...) {
        fprintf(stderr, "[DxgiFramePacing] Exception during poll_frame_stats\n");
        return false;
    }
}

void DxgiFramePacing::shutdown() {
    try {
        swap_chain_ = nullptr;
        fprintf(stderr, "[DxgiFramePacing] Shutdown complete\n");
    } catch (...) {
        fprintf(stderr, "[DxgiFramePacing] Exception during shutdown\n");
    }
}

float DxgiFramePacing::compute_rolling_average() const {
    float sum = 0.0f;
    for (int i = 0; i < ROLLING_WINDOW; i++) {
        sum += frame_times_[i];
    }
    return sum / ROLLING_WINDOW;
}
