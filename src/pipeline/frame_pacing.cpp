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

    swap_chain_ = swap_chain;
    last_present_count_ = 0;

    LARGE_INTEGER qpc;
    if (!QueryPerformanceCounter(&qpc)) {
        fprintf(stderr, "[DxgiFramePacing] QueryPerformanceCounter failed\n");
        fflush(stderr);
        return false;
    }
    last_qpc_time_ns_ = qpc.QuadPart;

    if (!QueryPerformanceFrequency(&qpc_freq_)) {
        fprintf(stderr, "[DxgiFramePacing] QueryPerformanceFrequency failed\n");
        fflush(stderr);
        return false;
    }

    std::memset(frame_times_, 0, sizeof(frame_times_));

    fprintf(stderr, "[DxgiFramePacing] Initialized with swap chain\n");
    fflush(stderr);
    return true;
}

bool DxgiFramePacing::poll_frame_stats(FrameStats* out_stats) {
    if (!swap_chain_ || !out_stats) {
        return false;
    }

    // QPC frequency'i bir kez cache'le (thread-safe magic statics, C++11+)
    // Hot-path: her frame'de QueryPerformanceFrequency syscall'u yapma
    static const double qpc_ticks_per_ms = []() {
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        return static_cast<double>(freq.QuadPart) / 1000.0;
    }();

    // GetFrameStatistics non-blocking — swap chain'in internal cache'inden okur
    // AGENTS.md: hot-path'te blocking call yasak (bu çağrı blocking değil)
    DXGI_FRAME_STATISTICS frame_stats{};
    HRESULT hr = swap_chain_->GetFrameStatistics(&frame_stats);
    if (FAILED(hr)) {
        return false;  // İstatistik henüz hazır değil (normal, retry-safe)
    }

    // Yeni bir frame present edilmiş mi?
    if (frame_stats.PresentCount == last_present_count_) {
        return false;  // Yeni frame yok, çağıran bunu polling için kullanır
    }

    // QPC ile frame time ölçümü
    // Not: last_qpc_time_ns_ isim tarihsel; gerçekte raw QPC ticks saklanıyor
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    const uint64_t current_qpc = qpc.QuadPart;

    // Frame time (ms) — cached frequency ile division
    const float frame_time_ms = static_cast<float>(
        static_cast<double>(current_qpc - last_qpc_time_ns_) / qpc_ticks_per_ms);

    // Rolling 30-frame ortalama (VSYNC kararlılığı tespiti)
    frame_times_[frame_index_] = frame_time_ms;
    frame_index_ = (frame_index_ + 1) % ROLLING_WINDOW;
    const float avg_frame_time = compute_rolling_average();

    // GPU stall tespiti: ortalama frame time > 5ms ise
    // (VSYNC bekleme veya GPU pipeline stall işareti)
    const bool gpu_stall = avg_frame_time > 5.0f;

    // Out parametre doldur
    out_stats->present_count         = frame_stats.PresentCount;
    out_stats->present_refresh_count = frame_stats.PresentRefreshCount;
    out_stats->sync_refresh_count    = frame_stats.SyncRefreshCount;
    out_stats->frame_time_ms         = frame_time_ms;
    out_stats->gpu_busy_ms           = frame_time_ms;  // Placeholder: GpuQueryTiming dolduracak
    out_stats->gpu_stall             = gpu_stall;
    out_stats->timestamp_us          = current_qpc * 1'000'000ULL / qpc_freq_.QuadPart;

    // State güncelle
    last_present_count_ = frame_stats.PresentCount;
    last_qpc_time_ns_   = current_qpc;

    return true;
}

void DxgiFramePacing::shutdown() {
    swap_chain_ = nullptr;
    fprintf(stderr, "[DxgiFramePacing] Shutdown complete\n");
    fflush(stderr);
}

float DxgiFramePacing::compute_rolling_average() const {
    float sum = 0.0f;
    for (int i = 0; i < ROLLING_WINDOW; i++) {
        sum += frame_times_[i];
    }
    return sum / ROLLING_WINDOW;
}
