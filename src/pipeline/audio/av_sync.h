#pragma once
#include <cstdint>

// A-V senkron drift'i icin savunma-derinligi valfi (I10 deseni: esik + throttle).
// MVP'de otomatik duzeltme dayatilmaz; amac "sync sessizce kotulesirse fark
// edilsin" garantisi. Header-only (yan-etkisiz saf karar) — reji_pipeline/MF
// link gerekmez, dogrudan birim testlenebilir (bkz. tests/test_av_sync.cpp).
// Cagri noktasi (sink/mux) bu true dondugunde bir log uyarisi dusurur
// (OutputDebugStringA/dbglog), WASAPI'deki 500ms-sessizlik uyarisi desenine paralel.
namespace reji::pipeline::audio {

/// Uyari esigi: ses/video pts farki bu degeri (ms) KESIN asarsa drift raporlanir.
constexpr int64_t kAvDriftWarnThresholdMs = 200;

/// Throttle penceresi (ms): ardisik uyarilar arasi asgari sure — log spam'ini onler.
constexpr int64_t kAvDriftWarnPeriodMs = 1000;

/// last_warn_ms icin "henuz hic uyari verilmedi" sentinel'i.
constexpr int64_t kNoPriorWarn = INT64_MIN;

/// A-V drift uyarisi simdi verilmeli mi?
/// @param drift_ms      ses_pts - video_pts (ms); isaret onemsiz, mutlak deger kullanilir.
/// @param now_ms        simdiki monotonik zaman (ms).
/// @param last_warn_ms  son uyarinin zamani (ms) veya kNoPriorWarn.
/// @return  |drift| esigi asiyor VE (ilk kez VEYA throttle penceresi doldu) ise true.
inline bool should_warn_av_drift(int64_t drift_ms, int64_t now_ms,
                                 int64_t last_warn_ms) {
    const int64_t magnitude = drift_ms < 0 ? -drift_ms : drift_ms;
    if (magnitude <= kAvDriftWarnThresholdMs) return false;
    if (last_warn_ms == kNoPriorWarn)         return true;
    return (now_ms - last_warn_ms) >= kAvDriftWarnPeriodMs;
}

} // namespace reji::pipeline::audio
