// src/pipeline/frame_pacer.cpp
//
// FramePacer implementasyonu. Davranış, Pipeline::run_frame()'in eski "5) Frame
// pacing" bloğu ve pts_us hesabıyla birebir aynıdır (Aşama 1 saf çıkarma —
// baseline_metrics.txt ile doğrulanır).
#include "include/frame_pacer.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace rj {
namespace {

constexpr int64_t kResyncFrames = 4;   // catch-up spiral guard

inline int64_t qpc_ticks() noexcept {
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart;
}
inline int64_t ticks_to_us(int64_t t, int64_t freq) noexcept {
    return (t * 1'000'000LL) / freq;
}

} // namespace

bool FramePacer::init(uint32_t fps) {
    if (fps == 0) return false;
    LARGE_INTEGER freq{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) return false;
    qpc_freq_      = freq.QuadPart;
    frame_ticks_   = qpc_freq_ / static_cast<int64_t>(fps);
    pts_origin_    = qpc_ticks();
    next_deadline_ = qpc_ticks() + frame_ticks_;
    return true;
}

int64_t FramePacer::pts_us(int64_t frame_start_ticks) const noexcept {
    return ticks_to_us(frame_start_ticks - pts_origin_, qpc_freq_);
}

void FramePacer::pace() noexcept {
    next_deadline_ += frame_ticks_;
    int64_t now    = qpc_ticks();
    int64_t remain = next_deadline_ - now;

    if (remain < -frame_ticks_ * kResyncFrames) {
        next_deadline_ = now + frame_ticks_;  // resync — prevent catch-up spiral
    } else if (remain > 0) {
        int64_t remain_us = ticks_to_us(remain, qpc_freq_);
        if (remain_us > 1500)
            Sleep(static_cast<DWORD>((remain_us - 1000) / 1000));
        while (qpc_ticks() < next_deadline_)
            YieldProcessor();
    }
}

} // namespace rj

#else  // !_WIN32

namespace rj {
bool    FramePacer::init(uint32_t)                 { return false; }
int64_t FramePacer::pts_us(int64_t) const noexcept { return 0; }
void    FramePacer::pace() noexcept                {}
} // namespace rj

#endif // _WIN32
