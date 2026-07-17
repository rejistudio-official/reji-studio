// AacEncoder entegrasyon testi — Media Foundation AAC-LC yazilim MFT'si.
// AAC encoder MFT donanim gerektirmez (saf yazilim), bu yuzden headless CI'da
// gercek encode dogrulanabilir: bilinen PCM besle -> AAC frame + dogru ASC.
// aac_encoder.cpp dogrudan derlenir (test_seh_filter deseni); MF link'lenir.
#include <gtest/gtest.h>
#include "aac_encoder.h"

#include <vector>
#include <cstdint>

using reji::pipeline::audio::AacEncoder;

namespace {
// Basit statik sink — uretilen AAC baytlarini toplar (test-yerel durum).
std::vector<uint8_t> g_aac;
int                  g_frames;
void collect_sink(const uint8_t* aac, uint32_t len, int64_t /*pts_us*/, void*) {
    g_aac.insert(g_aac.end(), aac, aac + len);
    ++g_frames;
}
void reset_sink() { g_aac.clear(); g_frames = 0; }
} // namespace

// MF AAC encoder 48kHz/stereo/128kbps ile ayaga kalkar.
TEST(AacEncoderTest, InitSucceedsFor48kStereo) {
    AacEncoder enc;
    EXPECT_TRUE(enc.init({48000u, 2u, 128000u}, &collect_sink, nullptr));
    enc.shutdown();
}

// init sonrasi ASC, aac_config.h ile ayni deterministik degeri verir (0x11 0x90).
TEST(AacEncoderTest, AscMatchesDeterministicConfig) {
    AacEncoder enc;
    ASSERT_TRUE(enc.init({48000u, 2u, 128000u}, &collect_sink, nullptr));
    const auto& asc = enc.audio_specific_config();
    ASSERT_EQ(asc.size(), 2u);
    EXPECT_EQ(asc[0], 0x11);
    EXPECT_EQ(asc[1], 0x90);
    enc.shutdown();
}

// Gercek encode: 1 saniye 48kHz/stereo sessizlik -> en az bir ham AAC frame.
// (Yazilim MFT, donanimsiz calisir; bit ureten yolu bastan sona dogrular.)
TEST(AacEncoderTest, EncodesSilenceToAacFrames) {
    reset_sink();
    AacEncoder enc;
    ASSERT_TRUE(enc.init({48000u, 2u, 128000u}, &collect_sink, nullptr));

    constexpr uint32_t kChunkFrames = 1024;
    std::vector<float> silence(kChunkFrames * 2u, 0.0f);  // stereo interleaved
    int64_t pts_us = 0;
    const int64_t chunk_us = static_cast<int64_t>(kChunkFrames) * 1'000'000 / 48000;
    for (int i = 0; i < 48; ++i) {  // ~1 saniye
        ASSERT_TRUE(enc.encode(silence.data(), kChunkFrames, pts_us));
        pts_us += chunk_us;
    }
    ASSERT_TRUE(enc.drain());

    EXPECT_GT(g_frames, 0);
    EXPECT_GT(g_aac.size(), 0u);
    enc.shutdown();
}
