// Ses wire-format saf yardimcilari birim testi — FLV AAC audio tag header ve
// float32->S16 PCM donusumu. Header-only (yalniz <cstdint>/<array>) —
// reji_pipeline/Rust/Media Foundation link gerekmez, sadece gtest.
#include <gtest/gtest.h>
#include "flv_audio_tag.h"
#include "pcm_convert.h"

using reji::pipeline::audio::flv_aac_audio_header;
using reji::pipeline::audio::AacPacketType;
using reji::pipeline::audio::float_to_s16;

// FLV AudioTagHeader (E.4.2.1): SoundFormat(4)=AAC(10) | SoundRate(2)=3 |
// SoundSize(1)=1(16-bit) | SoundType(1)=1(stereo) = 0xAF, ardindan AACPacketType.
// Sequence header (ASC) -> packet type 0.
TEST(FlvAudioTagTest, AacSequenceHeaderIs_AF_00) {
    auto h = flv_aac_audio_header(AacPacketType::SequenceHeader);
    EXPECT_EQ(h[0], 0xAF);
    EXPECT_EQ(h[1], 0x00);
}

// Ham AAC frame -> packet type 1.
TEST(FlvAudioTagTest, AacRawFrameIs_AF_01) {
    auto h = flv_aac_audio_header(AacPacketType::Raw);
    EXPECT_EQ(h[0], 0xAF);
    EXPECT_EQ(h[1], 0x01);
}

// float32 [-1,1] -> S16 (MF AAC encoder girdisi). Sessizlik 0'da kalir.
TEST(PcmConvertTest, SilenceIsZero) {
    EXPECT_EQ(float_to_s16(0.0f), 0);
}

// Tam olcek: +1.0 -> +32767 (ust sinir), -1.0 -> -32768 (alt sinir).
TEST(PcmConvertTest, FullScalePositiveClampsToMax) {
    EXPECT_EQ(float_to_s16(1.0f), 32767);
}
TEST(PcmConvertTest, FullScaleNegativeIsMin) {
    EXPECT_EQ(float_to_s16(-1.0f), -32768);
}

// Sinir asimi (>1.0 / <-1.0) clamp'lenir, sarmalanmaz.
TEST(PcmConvertTest, OverflowClamps) {
    EXPECT_EQ(float_to_s16(2.0f), 32767);
    EXPECT_EQ(float_to_s16(-2.0f), -32768);
}

// Yari olcek: 0.5 * 32768 = 16384 (yuvarlama).
TEST(PcmConvertTest, HalfScale) {
    EXPECT_EQ(float_to_s16(0.5f), 16384);
}
