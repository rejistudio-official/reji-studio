// aac_config.h saf birim testi — AAC-LC AudioSpecificConfig (ASC) uretimi.
// Header-only (yalniz <cstdint>/<array>/<optional>) — reji_pipeline/Rust/MF link
// gerekmez. FLV AAC sequence header'i icin gereken 2-baytlik ASC'yi ve MPEG-4
// sampling-frequency-index eslemesini deterministik olarak kilitler.
#include <gtest/gtest.h>
#include "aac_config.h"

using reji::pipeline::audio::sampling_frequency_index;
using reji::pipeline::audio::make_audio_specific_config;

// WASAPI capture 48 kHz ile yakalar (pipeline.cpp acfg.sample_rate=48000).
// MPEG-4 tablosunda 48000 -> index 3.
TEST(AacConfigTest, SamplingFrequencyIndex48kIs3) {
    EXPECT_EQ(sampling_frequency_index(48000u), 3);
}

TEST(AacConfigTest, SamplingFrequencyIndex44kIs4) {
    EXPECT_EQ(sampling_frequency_index(44100u), 4);
}

TEST(AacConfigTest, SamplingFrequencyIndexUnsupportedIsNegative) {
    EXPECT_EQ(sampling_frequency_index(9999u), -1);
}

// AudioSpecificConfig (ISO/IEC 14496-3): objType(5)=AAC-LC(2), freqIdx(4),
// chanCfg(4), GASpecificConfig(3)=0. Reji capture konfigurasyonu 48kHz/stereo:
// 00010 0011 0010 000 -> 0001 0001 1001 0000 = {0x11, 0x90}.
TEST(AacConfigTest, AscForDefault48kStereo) {
    auto asc = make_audio_specific_config(48000u, 2u);
    ASSERT_TRUE(asc.has_value());
    EXPECT_EQ((*asc)[0], 0x11);
    EXPECT_EQ((*asc)[1], 0x90);
}

// Bilinen referans deger: 44.1kHz/stereo AAC-LC ASC = {0x12, 0x10}.
TEST(AacConfigTest, AscFor44kStereoMatchesKnownValue) {
    auto asc = make_audio_specific_config(44100u, 2u);
    ASSERT_TRUE(asc.has_value());
    EXPECT_EQ((*asc)[0], 0x12);
    EXPECT_EQ((*asc)[1], 0x10);
}

// Mono kanal konfigurasyonu: chanCfg=1 -> 48kHz mono = {0x11, 0x88}.
TEST(AacConfigTest, AscFor48kMono) {
    auto asc = make_audio_specific_config(48000u, 1u);
    ASSERT_TRUE(asc.has_value());
    EXPECT_EQ((*asc)[0], 0x11);
    EXPECT_EQ((*asc)[1], 0x88);
}

// Desteklenmeyen ornekleme hizi -> ASC uretilemez (nullopt).
TEST(AacConfigTest, AscUnsupportedRateIsNullopt) {
    EXPECT_FALSE(make_audio_specific_config(9999u, 2u).has_value());
}
