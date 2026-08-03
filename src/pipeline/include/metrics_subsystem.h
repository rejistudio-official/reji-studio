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
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
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

// MetricsCollector snapshot'ı → RjMetricSample genişletilmiş alan eşlemesi.
// Saf/platform-bağımsız seam: build_sample() tek çağıran, birim test doğrudan
// çağırır (PDH/thread gerekmez). Yeni bir Metrics alanı ABI'ye girdiğinde
// eşleme BURAYA da eklenmeli — gpu_load_pct v0.5'te ABI'ye eklenip burada
// eşlenmediği için drainer'ın GpuUsage event'i (ffi.rs
// system_events_for_sample, `> 0` kapısı) üretimde hiç doğmuyordu.
inline void apply_collector_metrics(RjMetricSample& m,
                                    const Metrics& latest) noexcept {
    m.frame_drop_pct   = latest.frame_drop_pct;
    m.gpu_temp_c       = latest.gpu_temp_c;
    m.cpu_temp_c       = latest.cpu_temp_c;
    m.memory_usage_pct = latest.memory_usage_pct;
    m.cpu_load_pct     = latest.cpu_load_pct;
    m.gpu_load_pct     = latest.gpu_load_pct;
    m.network_rtt_ms   = latest.network_rtt_ms;
    m.network_loss_pct = latest.network_loss_pct;
}

class MetricsSubsystem {
public:
    // J8: destructor arka plan poll thread'ini durdurur (RAII güvenlik ağı —
    // Pipeline::shutdown() stop()'u açıkça çağırsa da idempotent).
    ~MetricsSubsystem();

    // MetricsCollector oluşturur VE 1Hz arka plan poll thread'ini başlatır.
    // false → collector kurulamadı. J8: PDH/WMI sorguları AGENTS.md gereği
    // frame thread'inde DEĞİL, bu ayrı thread'de koşar (run_frame yalnız
    // get_latest() snapshot okur).
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

    // J8: arka plan poll thread'ini durdurur (poll_running_=false + join).
    // Pipeline::shutdown() sırasında, CommandRouter::stop() ile aynı desende.
    // Idempotent — birden çok kez çağrılabilir (destructor da çağırır).
    void stop();

    // Bu frame'in başlangıç tick'ini kaydeder (fps ölçümü için, build_sample SONRASI).
    // qpc_freq gerekmez (yalnızca tick saklanır — YAGNI).
    void record_frame_start(int64_t frame_start_ticks) noexcept {
        last_frame_ticks_ = frame_start_ticks;
    }

    // V10/L22: frame_drop_pct beslemesi — on_packet (encode thread) çağırır.
    // Collector'ın 30s pencereli pct hesabı bu iki sayaçtan türer; başka
    // besleme noktası yok. metrics_lock_ kısa tutulur (sayaç artışı).
    void record_frame() noexcept      { if (metrics_) metrics_->record_frame(); }
    void record_frame_drop() noexcept { if (metrics_) metrics_->record_frame_drop(); }

private:
    // J8: arka plan poll döngüsü — while(running){ metrics_->poll(); sleep }.
    // poll() zaten 1Hz self-throttle (POLL_INTERVAL); tick sleep yalnız stop
    // yanıt süresini sınırlar (≤kMetricsPollTickMs).
    void poll_loop();

    CpuMeter                              cpu_;
    std::unique_ptr<rj::MetricsCollector> metrics_;
    int64_t                               last_frame_ticks_ = 0;

    std::thread                           poll_thread_;
    std::atomic<bool>                     poll_running_{false};
};

} // namespace rj
