// src/pipeline/metrics_subsystem.cpp
//
// MetricsSubsystem + CpuMeter implementasyonu. RjMetricSample derleme mantığı,
// Pipeline::run_frame()'in eski "4) Metrics push" bloğuyla birebir aynıdır
// (Aşama 2 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "include/metrics_subsystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <algorithm>
#include "seh_filter.h"  // V8/I10: paylaşımlı SEH filtresi

namespace rj {
namespace {

constexpr uint32_t kMetricMagic = RJ_METRIC_MAGIC;

inline int64_t ticks_to_us(int64_t t, int64_t freq) noexcept {
    return (t * 1'000'000LL) / freq;
}
inline uint64_t ft64(const FILETIME& f) noexcept {
    return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
}

// SEH leaf: FFI push  __declspec(noinline), __try içinde yok edilebilir local yok.
__declspec(noinline)
static void seh_metrics_push(const RjMetricSample* s) noexcept {
    SehCapture cap{};
    __try   { rj_metrics_push(s); }
    __except(seh_filter(GetExceptionInformation(), SehSite::MetricsPush, &cap)) {}
    if (cap.fired) seh_report(cap, SehSite::MetricsPush);
}

} // namespace

// ── CpuMeter ────────────────────────────────────────────────────────────────
CpuMeter::CpuMeter() noexcept {
    SYSTEM_INFO si{}; GetSystemInfo(&si);
    ncpus_ = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1u;
}

float CpuMeter::sample() noexcept {
    FILETIME c, e, k, u, now;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return last_;
    GetSystemTimeAsFileTime(&now);
    uint64_t wall = ft64(now), busy = ft64(k) + ft64(u);
    if (prev_wall_) {
        uint64_t dwall = wall - prev_wall_, dbusy = busy - prev_busy_;
        if (dwall) {
            float pct = float(dbusy) / (float(dwall) * float(ncpus_)) * 100.f;
            last_ = pct < 0.f ? 0.f : pct > 100.f ? 100.f : pct;
        }
    }
    prev_wall_ = wall; prev_busy_ = busy;
    return last_;
}

// ── MetricsSubsystem ──────────────────────────────────────────────────────────
bool MetricsSubsystem::init() {
    metrics_ = std::make_unique<rj::MetricsCollector>();
    return metrics_ != nullptr;
}

RjMetricSample MetricsSubsystem::build_sample(uint32_t bitrate_kbps,
                                              uint32_t frame_drops_delta,
                                              int64_t  frame_start_ticks,
                                              int64_t  qpc_freq) noexcept {
    RjMetricSample m{};
    m.magic_head   = kMetricMagic;
    m.magic_tail   = kMetricMagic;
    m.timestamp_us = static_cast<uint64_t>(ticks_to_us(frame_start_ticks, qpc_freq));
    m.bitrate_kbps = bitrate_kbps;
    {
        auto delta = frame_start_ticks - last_frame_ticks_;
        m.fps_actual = (delta > 0)
            ? std::clamp(
                  static_cast<float>(qpc_freq) / static_cast<float>(delta),
                  0.0f, 240.0f)
            : 0.0f;
    }
    m.cpu_percent = cpu_.sample();
    m.frame_drops = frame_drops_delta;

    // v0.4+: Extended metrics from MetricsCollector
    // NOTE: WMI queries run in separate thread; run_frame() only reads snapshot
    if (metrics_) {
        auto latest = metrics_->get_latest();
        m.frame_drop_pct   = latest.frame_drop_pct;
        m.gpu_temp_c       = latest.gpu_temp_c;
        m.cpu_temp_c       = latest.cpu_temp_c;
        m.memory_usage_pct = latest.memory_usage_pct;
        m.cpu_load_pct     = latest.cpu_load_pct;
        m.network_rtt_ms   = latest.network_rtt_ms;
        m.network_loss_pct = latest.network_loss_pct;
    }

    m.source_id = 0;  // video
    return m;
}

void MetricsSubsystem::push(const RjMetricSample& sample) noexcept {
    seh_metrics_push(&sample);
}

void MetricsSubsystem::poll() {
    if (metrics_) metrics_->poll();
}

} // namespace rj

#else  // !_WIN32

namespace rj {
CpuMeter::CpuMeter() noexcept {}
float CpuMeter::sample() noexcept { return 0.f; }
bool MetricsSubsystem::init() { return false; }
RjMetricSample MetricsSubsystem::build_sample(uint32_t, uint32_t,
                                              int64_t, int64_t) noexcept {
    return RjMetricSample{};
}
void MetricsSubsystem::push(const RjMetricSample&) noexcept {}
void MetricsSubsystem::poll() {}
} // namespace rj

#endif // _WIN32
