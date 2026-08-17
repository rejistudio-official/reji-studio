// tests/test_frame_repeat_policy.cpp
//
// VFR/CFR Faz 2: kare tekrarı + zaman bazlı yedek IDR saf karar çekirdeği.
//
// Kaynak karar (TASARIM_VFR_CFR_FAZ1.md):
// - K2: her null tick'te tekrar — eşik/kadans/üst sınır YOK; NeedsReinit'te de
//   sürer. Tek önkoşul: tekrar edilecek kopya mevcut olmalı (WGC null tick'inde
//   canlı texture zaten bırakılmış durumda — capture_wgc.cpp:171).
// - K3: savunma katmanı — son GERÇEKLEŞEN IDR'dan >= 2 sn geçtiyse force_idr
//   istenir. Normal akışta (gopLength=120 @60fps) hiç tetiklenmemeli.
//
// Saf/test-edilebilir: yalnız <cstdint> — D3D/NVENC/QPC bilmez; pts'ler
// mikrosaniye olarak çağırandan gelir (pacer epoch'u).
#include <gtest/gtest.h>

#include "frame_repeat_policy.h"

using rj::IdrCadence;
using rj::should_repeat_frame;

namespace {
constexpr int64_t kSecUs = 1'000'000;
}

// ── K2: tekrar kararı matrisi ──────────────────────────────────────────────

TEST(FrameRepeatPolicyTest, RepeatsOnNullTickWhenCopyExists) {
    EXPECT_TRUE(should_repeat_frame(/*have_copy=*/true, /*is_null_tick=*/true));
}

TEST(FrameRepeatPolicyTest, NoRepeatBeforeFirstValidFrame) {
    // İlk geçerli kareden önce kopya yok — null tick bugünkü gibi boş geçer.
    EXPECT_FALSE(should_repeat_frame(/*have_copy=*/false, /*is_null_tick=*/true));
}

TEST(FrameRepeatPolicyTest, NoRepeatOnValidFrameTick) {
    // Geçerli karede tekrar anlamsız — gerçek kare encode edilir.
    EXPECT_FALSE(should_repeat_frame(/*have_copy=*/true, /*is_null_tick=*/false));
    EXPECT_FALSE(should_repeat_frame(/*have_copy=*/false, /*is_null_tick=*/false));
}

// ── K3: zaman bazlı yedek IDR kadansı ─────────────────────────────────────

TEST(IdrCadenceTest, DoesNotFireBeforeFirstObservedKeyframe) {
    // Epoch yok — start_stream zaten IDR istiyor (pipeline.cpp:609); kadans
    // ilk gerçekleşen keyframe'den sonra ölçmeye başlar.
    IdrCadence c(2 * kSecUs);
    EXPECT_FALSE(c.should_force_idr(10 * kSecUs));
}

TEST(IdrCadenceTest, FiresWhenIntervalElapsedSinceLastKeyframe) {
    IdrCadence c(2 * kSecUs);
    c.on_keyframe(0);
    EXPECT_FALSE(c.should_force_idr(2 * kSecUs - 1));  // sınırın hemen altı
    EXPECT_TRUE(c.should_force_idr(2 * kSecUs));       // sınır dahil
}

TEST(IdrCadenceTest, DoesNotDoubleFireWhileRequestPending) {
    // Tetikledikten sonra keyframe gerçekleşene kadar yeniden istemez —
    // request_idr atomic'ini her tick'te sahte beslemek IDR fırtınası olurdu.
    IdrCadence c(2 * kSecUs);
    c.on_keyframe(0);
    ASSERT_TRUE(c.should_force_idr(2 * kSecUs));
    EXPECT_FALSE(c.should_force_idr(2 * kSecUs + 100));
    EXPECT_FALSE(c.should_force_idr(5 * kSecUs));
}

TEST(IdrCadenceTest, RearmsAfterKeyframeObserved) {
    IdrCadence c(2 * kSecUs);
    c.on_keyframe(0);
    ASSERT_TRUE(c.should_force_idr(2 * kSecUs));
    c.on_keyframe(2 * kSecUs + 50'000);                     // IDR gerçekleşti
    EXPECT_FALSE(c.should_force_idr(3 * kSecUs));           // yeni epoch içinde
    EXPECT_TRUE(c.should_force_idr(4 * kSecUs + 50'000));   // yeni epoch doldu
}

TEST(IdrCadenceTest, NormalGopCadenceNeverFires) {
    // Normal akış: keyframe her 2 sn'de gerçekleşiyor (gopLength birincil) —
    // yedek katman hiç devreye girmemeli (idrF=0 beklentisinin saf karşılığı).
    IdrCadence c(2 * kSecUs);
    int64_t t = 0;
    c.on_keyframe(t);
    for (int gop = 0; gop < 10; ++gop) {
        // GOP içi tick'ler: 60 kare, 16.7ms aralık — hep sınırın altında
        for (int f = 1; f < 120; ++f)
            EXPECT_FALSE(c.should_force_idr(t + f * 16'667));
        t += 2 * kSecUs - 10'000;  // sonraki keyframe ~2sn'de gerçekleşir
        c.on_keyframe(t);
    }
}
