// src/pipeline/include/metrics_subsystem.h
//
// MetricsSubsystem — metrik toplama/derleme alt sistemi (Aşama 2'de Pipeline::Impl'den
// çıkarıldı). Sorumluluklar:
//   - CpuMeter ile process CPU kullanımı
//   - MetricsCollector (genişletilmiş WMI/PDH metrikleri) sahipliği
//   - RjMetricSample derleme + SEH-korumalı FFI push
//   - fps ölçümü için frame'ler arası aralık (last_frame_ticks)
//
// NOT: frame_drops Impl'de atomic kalır (capture null / encode err / on_packet
// birden çok noktadan yazar). build_sample() delta'yı PARAMETRE olarak alır,
// kendi içinde tutmaz.
#pragma once
#include <cstdint>
#include <memory>
#include "metrics_collector.h"
#include "../ffi/ffi_bridge.h"   // RjMetricSample

namespace rj {

// CPU kullanım ölçer — process kernel+user zamanını duvar saatine oranlar.
// (Windows: GetProcessTimes / GetSystemTimeAsFileTime; implementasyon .cpp'de.)
class CpuMeter {
public:
    CpuMeter() noexcept;
    float sample() noexcept;
private:
    uint64_t prev_wall_ = 0, prev_busy_ = 0;
    uint32_t ncpus_     = 1;
    float    last_      = 0.f;
};

class MetricsSubsystem {
public:
    // MetricsCollector oluşturur. false → collector kurulamadı.
    bool init();

    // RjMetricSample'ı doldurur. fps, last_frame_ticks_ ile frame_start arasındaki
    // aralıktan hesaplanır. build_sample() last_frame_ticks_'i GÜNCELLEMEZ —
    // bunun için record_frame_start() build_sample SONRASI çağrılmalıdır.
    RjMetricSample build_sample(uint32_t bitrate_kbps,
                                uint32_t frame_drops_delta,
                                int64_t  frame_start_ticks,
                                int64_t  qpc_freq) noexcept;

    // seh_metrics_push sarmalayıcısı (SEH-korumalı FFI push).
    void push(const RjMetricSample& sample) noexcept;

    // MetricsCollector::poll() — WMI/PDH sorgu döngüsünü ilerletir.
    void poll();

    // Bu frame'in başlangıç tick'ini kaydeder (fps ölçümü için, build_sample SONRASI).
    // qpc_freq gerekmez (yalnızca tick saklanır — YAGNI).
    void record_frame_start(int64_t frame_start_ticks) noexcept {
        last_frame_ticks_ = frame_start_ticks;
    }

private:
    CpuMeter                              cpu_;
    std::unique_ptr<rj::MetricsCollector> metrics_;
    int64_t                               last_frame_ticks_ = 0;
};

} // namespace rj
