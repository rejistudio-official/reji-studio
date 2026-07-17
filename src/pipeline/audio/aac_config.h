#pragma once
#include <cstdint>
#include <array>
#include <optional>

// AAC-LC yayin yardimcilarinin saf (yan-etkisiz) parcalari — FLV AAC mux'i icin.
// Header-only: Media Foundation / reji_pipeline / Rust link gerektirmez, boylece
// dogrudan birim testlenebilir (bkz. tests/test_aac_config.cpp, bitrate_policy.h
// deseni). MF AAC encoder'in kendisi ayri (COM/SEH) katmanda kalir; burada yalniz
// wire-format bit-paketleme mantigi test-edilebilir tutulur.
namespace reji::pipeline::audio {

/// MPEG-4 "Sampling Frequency Index" (ISO/IEC 14496-3, Table 1.16).
/// AudioSpecificConfig'in 4-bitlik freq alanini besler.
/// @return  desteklenen hiz icin 0..12 indeks; desteklenmeyen hiz icin -1.
inline int sampling_frequency_index(uint32_t sample_rate) {
    switch (sample_rate) {
        case 96000: return 0;
        case 88200: return 1;
        case 64000: return 2;
        case 48000: return 3;
        case 44100: return 4;
        case 32000: return 5;
        case 24000: return 6;
        case 22050: return 7;
        case 16000: return 8;
        case 12000: return 9;
        case 11025: return 10;
        case 8000:  return 11;
        case 7350:  return 12;
        default:    return -1;
    }
}

/// AAC-LC AudioSpecificConfig (ISO/IEC 14496-3, 1.6.2.1) — FLV AAC sequence
/// header'inin (AACPacketType=0) tasidigi 2-baytlik yapi.
///
/// Bit yerlesimi (MSB-first): audioObjectType(5) | samplingFrequencyIndex(4) |
/// channelConfiguration(4) | GASpecificConfig(3, hepsi 0: frameLengthFlag=0,
/// dependsOnCoreCoder=0, extensionFlag=0). audioObjectType daima AAC-LC (2).
///
/// @param sample_rate  desteklenen MPEG-4 ornekleme hizi (bkz. sampling_frequency_index).
/// @param channels     channelConfiguration; gecerli aralik 1..7.
/// @return  2-baytlik ASC; desteklenmeyen hiz/kanal icin std::nullopt.
inline std::optional<std::array<uint8_t, 2>>
make_audio_specific_config(uint32_t sample_rate, uint32_t channels) {
    const int freq = sampling_frequency_index(sample_rate);
    if (freq < 0)                    return std::nullopt;
    if (channels < 1 || channels > 7) return std::nullopt;

    constexpr uint32_t kAacLcObjectType = 2;  // AudioObjectType: AAC-LC
    const uint16_t bits = static_cast<uint16_t>(
        (kAacLcObjectType << 11) |            // 5 bit, [15..11]
        (static_cast<uint32_t>(freq) << 7) |  // 4 bit, [10..7]
        (channels << 3));                     // 4 bit, [6..3]; [2..0] = 0

    return std::array<uint8_t, 2>{
        static_cast<uint8_t>(bits >> 8),
        static_cast<uint8_t>(bits & 0xFF),
    };
}

} // namespace reji::pipeline::audio
