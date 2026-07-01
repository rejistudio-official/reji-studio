// src/pipeline/include/frame_pacer.h
//
// FramePacer — QPC tabanlı frame zamanlama alt sistemi.
// Pipeline::Impl'den Aşama 1'de çıkarıldı. Tek sorumluluğu:
//   - pts_us(): capture zaman damgasını pts_origin'e göre mikrosaniyeye çevir
//   - pace(): mutlak deadline'a göre Sleep + spin-wait; catch-up spiral guard'ı
// qpc_freq() salt-okunur getter'ı Metrics'in fps/timestamp hesabı için paylaşılır
// (QPC frekansı donanım sabiti — salt-okunur paylaşım güvenli).
#pragma once
#include <cstdint>

namespace rj {

class FramePacer {
public:
    // QPC frekansını ve ilk deadline'ı kurar. fps==0 veya QPC yoksa false döner.
    bool init(uint32_t fps);

    // capture frame_start tick'ini pts_origin'e göre mikrosaniyeye çevirir.
    int64_t pts_us(int64_t frame_start_ticks) const noexcept;

    // Sonraki mutlak deadline'a kadar bekler (Sleep + YieldProcessor spin);
    // kResyncFrames kadar geride kalınırsa deadline'ı resync eder.
    void pace() noexcept;

    // Metrics'in timestamp_us / fps_actual hesabı için QPC frekansı (Hz).
    int64_t qpc_freq() const noexcept { return qpc_freq_; }

private:
    int64_t qpc_freq_      = 1;
    int64_t frame_ticks_   = 0;
    int64_t next_deadline_ = 0;
    int64_t pts_origin_    = 0;
};

} // namespace rj
