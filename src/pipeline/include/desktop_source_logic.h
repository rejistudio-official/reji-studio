// src/pipeline/include/desktop_source_logic.h
//
// ExistingDesktopSource'un saf (D3D11/GPU'suz, header-only) çekirdeği:
//  - map_captured_frame(): CapturedFrame → SourceFrame alan eşlemesi.
//  - NullStreakTracker: null-frame streak → NeedsReinit eşiği
//    (CaptureSubsystem::handle_null_frame()'deki 60-kare davranışının
//    ISource::state() karşılığı — eşik sabiti birebir aynı tutulur).
//
// Kurtarma KARARI burada verilmez: kaynak yalnızca sinyal üretir, reinit
// kararı üst katmandadır (RecoveryCoordinator deseni, bkz. i_source.h).
// tests/test_desktop_source_logic.cpp bu davranışları kilitler.
#pragma once
#include <cstdint>

#include "i_screen_capture.h"
#include "i_source.h"

namespace rj {

// WGC frame pool piksel formatı: Direct3D11CaptureFramePool,
// DirectXPixelFormat::B8G8R8A8UIntNormalized ile kurulur (capture_wgc.cpp)
// = DXGI_FORMAT_B8G8R8A8_UNORM. i_source.h d3d11.h bağımlılığı almadığı
// için ham değer kullanılır.
inline constexpr uint32_t kWgcFramePoolFormat = 87;

// CaptureSubsystem::kNullStreakReinit ile birebir aynı eşik — iki sayaç
// sessizce ayrışmasın diye test kilidi var.
inline constexpr int kNullStreakReinitThreshold = 60;

// CapturedFrame → SourceFrame alan eşlemesi (saf).
//  format: kaynak-düzeyi sabit (DXGI: surface_format(), WGC: kWgcFramePoolFormat).
//  fallback_timestamp_us: capture timestamp doldurmadıysa (DXGI yolu, 0 kalır)
//  kullanılacak acquire-anı QPC zamanı; WGC'nin SystemRelativeTime değeri korunur.
inline SourceFrame map_captured_frame(const CapturedFrame& in, uint32_t format,
                                      uint64_t fallback_timestamp_us) noexcept {
    SourceFrame out{};
    // HandleType enumerator kümeleri birebir (D3D11/DmaBuf/IOSurface/CpuBuffer).
    out.type         = static_cast<SourceFrame::HandleType>(in.type);
    out.handle       = in.handle;
    out.width        = in.width;
    out.height       = in.height;
    out.format       = format;
    out.timestamp_us = in.timestamp_us != 0 ? in.timestamp_us
                                            : fallback_timestamp_us;
    return out;
}

// Null-frame streak sayacı — tek thread (frame thread) varsayımı,
// CaptureSubsystem::null_streak_ ile aynı.
class NullStreakTracker {
public:
    // Geçerli kare → streak sıfırlanır; null kare → ++streak.
    void on_frame(bool has_frame) noexcept {
        if (has_frame) streak_ = 0;
        else           ++streak_;
    }

    // init()/reinit sonrası temiz başlangıç.
    void reset() noexcept { streak_ = 0; }

    // Eşiğe ulaşıldı mı — NeedsReinit sinyali. Null'lar sürdükçe sinyal kalır;
    // geçerli kare gelirse (on_frame(true)) kendiliğinden temizlenir.
    bool needs_reinit() const noexcept {
        return streak_ >= kNullStreakReinitThreshold;
    }

    int streak() const noexcept { return streak_; }

private:
    int streak_ = 0;
};

} // namespace rj
