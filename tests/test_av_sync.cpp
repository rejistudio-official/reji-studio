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
