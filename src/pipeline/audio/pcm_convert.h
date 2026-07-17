#pragma once
#include <cstdint>
#include <cmath>
#include <algorithm>

// float32 -> S16 PCM donusumunun saf (yan-etkisiz) parcasi. WASAPI capture
// float32 verir (wasapi_capture.cpp scratch_), Media Foundation AAC encoder ise
// 16-bit PCM ister. Header-only: MF/reji_pipeline link gerektirmez, dogrudan
// birim testlenebilir (bkz. tests/test_audio_wire.cpp).
namespace reji::pipeline::audio {

/// Tek bir float32 ornegini [-1.0, 1.0] araligindan S16'ya cevirir.
/// Tam olcek 32768 ile carpilir, en yakina yuvarlanir ve [-32768, 32767]
/// araligina clamp'lenir (sinir asiminda sarmalanma degil, doygunluk).
inline int16_t float_to_s16(float sample) {
    const long v = std::lroundf(sample * 32768.0f);
    const long clamped = (std::max)(-32768L, (std::min)(32767L, v));
    return static_cast<int16_t>(clamped);
}

} // namespace reji::pipeline::audio
