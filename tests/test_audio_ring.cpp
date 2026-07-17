// audio_ring.h saf birim testi — SPSC (tek uretici/tek tuketici) ses ring'i.
// Header-only (yalniz <atomic>/<cstdint>/<cstring>) — reji_pipeline/MF link
// gerekmez, sadece gtest. Capture thread (uretici) PCM push eder, encode thread
// (tuketici) drain eder. Lock-free SPSC'de "en eski dusur" guvenli olmadigindan
// (head tuketiciye ait) politika: DOLUYKEN EN YENIYI DUSUR + sayac.
#include <gtest/gtest.h>
#include "audio_ring.h"

#include <vector>
#include <cstdint>

using reji::pipeline::audio::AudioRing;

namespace {
// Tuketici yardimcisi: bir chunk'i (varsa) kopyalayip disari alir.
struct Popped {
    bool     ok = false;
    int64_t  pts_us = 0;
    uint32_t frames = 0, channels = 0, sample_rate = 0;
    std::vector<float> samples;
};
Popped pop_one(AudioRing& r) {
    Popped p;
    p.ok = r.consume([&](const float* s, uint32_t frames, uint32_t ch,
                         uint32_t sr, int64_t pts) {
        p.pts_us = pts; p.frames = frames; p.channels = ch; p.sample_rate = sr;
        p.samples.assign(s, s + frames * ch);
    });
    return p;
}
} // namespace

// Bos ring'ten tuketim basarisiz olur.
TEST(AudioRingTest, ConsumeOnEmptyReturnsFalse) {
    AudioRing r;
    EXPECT_FALSE(pop_one(r).ok);
    EXPECT_TRUE(r.empty());
}

// push edilen chunk ayni degerlerle geri okunur.
TEST(AudioRingTest, PushThenConsumeRoundTrips) {
    AudioRing r;
    const float in[4] = { 0.1f, -0.2f, 0.3f, -0.4f };  // 2 cerceve stereo
    ASSERT_TRUE(r.push(in, /*frames*/2, /*ch*/2, /*sr*/48000, /*pts*/1234));

    Popped p = pop_one(r);
    ASSERT_TRUE(p.ok);
    EXPECT_EQ(p.frames, 2u);
    EXPECT_EQ(p.channels, 2u);
    EXPECT_EQ(p.sample_rate, 48000u);
    EXPECT_EQ(p.pts_us, 1234);
    ASSERT_EQ(p.samples.size(), 4u);
    EXPECT_FLOAT_EQ(p.samples[0], 0.1f);
    EXPECT_FLOAT_EQ(p.samples[3], -0.4f);
}

// Coklu push FIFO sirasiyla tuketilir.
TEST(AudioRingTest, FifoOrder) {
    AudioRing r;
    const float a[2] = { 1.0f, 1.0f };
    const float b[2] = { 2.0f, 2.0f };
    ASSERT_TRUE(r.push(a, 1, 2, 48000, 10));
    ASSERT_TRUE(r.push(b, 1, 2, 48000, 20));
    EXPECT_EQ(pop_one(r).pts_us, 10);
    EXPECT_EQ(pop_one(r).pts_us, 20);
    EXPECT_FALSE(pop_one(r).ok);
}

// Ring dolunca yeni push dusurulur + sayac artar; mevcut (eski) veri korunur.
TEST(AudioRingTest, FullDropsNewestAndCounts) {
    AudioRing r;
    const float s[2] = { 0.0f, 0.0f };
    uint32_t accepted = 0;
    for (uint32_t i = 0; i < AudioRing::kCapacity + 5; ++i) {
        if (r.push(s, 1, 2, 48000, static_cast<int64_t>(i))) ++accepted;
    }
    EXPECT_EQ(accepted, AudioRing::kCapacity);
    EXPECT_EQ(r.dropped(), 5u);
    // Ilk (en eski) chunk hala pts=0 — en yeni dusuruldu, eski korundu.
    EXPECT_EQ(pop_one(r).pts_us, 0);
}

// Kapasiteyi asan cerceve sayisi reddedilir (drop sayaci artar), ring bozulmaz.
TEST(AudioRingTest, OversizedChunkRejected) {
    AudioRing r;
    std::vector<float> big(AudioRing::kMaxSamplesPerChunk + 2, 0.0f);
    EXPECT_FALSE(r.push(big.data(), (AudioRing::kMaxSamplesPerChunk + 2) / 2, 2,
                        48000, 1));
    EXPECT_EQ(r.dropped(), 1u);
    EXPECT_TRUE(r.empty());
}
