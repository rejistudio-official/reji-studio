#pragma once
#include <cstdint>
#include <array>

// FLV AAC audio tag header'inin saf (yan-etkisiz) uretimi — RTMP/FLV mux'i icin.
// Header-only: librtmp / Zig / reji_pipeline link gerektirmez, dogrudan birim
// testlenebilir (bkz. tests/test_audio_wire.cpp). Zig FLV muxer bu baytlari
// AUDIODATA gövdesinin onune koyar.
namespace reji::pipeline::audio {

/// FLV AAC AUDIODATA icindeki AACPacketType (E.4.2.2).
enum class AacPacketType : uint8_t {
    SequenceHeader = 0,  ///< AudioSpecificConfig (bir kez, ilk ses paketi)
    Raw            = 1,   ///< ham AAC frame (raw)
};

/// FLV AAC audio tag'inin 2-baytlik basligi:
///   [0] AudioTagHeader = SoundFormat(4)=AAC(10) | SoundRate(2)=3 |
///       SoundSize(1)=1 (16-bit) | SoundType(1)=1 (stereo) = 0xAF.
///       AAC icin Flash bu alt alanlari yok sayar; spec sabit 0xAF onerir.
///   [1] AACPacketType (0 = sequence header, 1 = raw).
inline std::array<uint8_t, 2> flv_aac_audio_header(AacPacketType packet_type) {
    constexpr uint8_t kAacTagHeader = 0xAF;
    return { kAacTagHeader, static_cast<uint8_t>(packet_type) };
}

} // namespace reji::pipeline::audio
