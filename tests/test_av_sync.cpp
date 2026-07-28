// av_sync.h saf birim testi — A-V drift savunma-derinligi valfi (I10 deseni).
// Header-only (yalniz <cstdint>) — reji_pipeline/Rust/MF link gerekmez, sadece
// gtest. Ses/video pts drift'i esigi asinca uyari tetiklenir; throttle ile spam
// engellenir. "Sync sessizce kotulesirse fark edilsin" garantisini kilitler.
#include <gtest/gtest.h>
#include "av_sync.h"

using reji::pipeline::audio::should_warn_av_drift;
using reji::pipeline::audio::kAvDriftWarnThresholdMs;
using reji::pipeline::audio::kNoPriorWarn;

// Esik altindaki drift (100ms < 200ms) uyari tetiklemez.
TEST(AvSyncTest, WithinThresholdNoWarn) {
    EXPECT_FALSE(should_warn_av_drift(100, /*now*/5000, /*last*/kNoPriorWarn));
}

// Esigi asan ilk drift (onceki uyari yok) uyari tetikler.
TEST(AvSyncTest, ExceedsThresholdFirstTimeWarns) {
    EXPECT_TRUE(should_warn_av_drift(250, /*now*/5000, /*last*/kNoPriorWarn));
}

// Negatif drift de mutlak deger uzerinden esigi asar.
TEST(AvSyncTest, NegativeDriftBeyondThresholdWarns) {
    EXPECT_TRUE(should_warn_av_drift(-250, /*now*/5000, /*last*/kNoPriorWarn));
}

// Tam esikte (200ms) tetiklenmez — yalniz kesin buyuk.
TEST(AvSyncTest, ExactlyAtThresholdNoWarn) {
    EXPECT_FALSE(should_warn_av_drift(kAvDriftWarnThresholdMs, 5000, kNoPriorWarn));
}

// Esik asili ama son uyaridan bu yana throttle penceresi dolmadi -> susar.
TEST(AvSyncTest, ThrottledWithinWindow) {
    EXPECT_FALSE(should_warn_av_drift(250, /*now*/5500, /*last*/5000));
}

// Throttle penceresi dolduktan sonra tekrar uyarir.
TEST(AvSyncTest, WarnsAgainAfterThrottleWindow) {
    EXPECT_TRUE(should_warn_av_drift(250, /*now*/6000, /*last*/5000));
}

// ── V10/L12: ses pts epoch rebase ───────────────────────────────────────────
// WASAPI pts QPC-mutlak (boot'tan beri), video pts pacer-origin'e goreli;
// muxer tek first_pts_us epoch'u paylasir — tabanlar esitlenmezse ilk gelen
// akis epoch'u belirler, digeri ts=0'a yapisir ya da saatler kayar.
using reji::pipeline::audio::rebase_audio_pts;

TEST(AvSyncTest, RebaseSubtractsOrigin) {
    // Boot'tan beri 2 saat, origin 2 saatin 5 sn oncesi → 5 sn goreli pts.
    const int64_t two_hours_us = 2LL * 3600 * 1'000'000;
    EXPECT_EQ(rebase_audio_pts(two_hours_us, two_hours_us - 5'000'000),
              5'000'000);
}

TEST(AvSyncTest, RebaseClampsPreOriginTimestampToZero) {
    // Cihaz zaman damgasi origin'den eski (ilk paket yarisi) → 0'a clamp.
    EXPECT_EQ(rebase_audio_pts(1'000'000, 2'000'000), 0);
}

TEST(AvSyncTest, RebaseZeroOriginIsIdentity) {
    EXPECT_EQ(rebase_audio_pts(42, 0), 42);
}
