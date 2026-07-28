// bitrate_policy.h saf birim testi — REDUCE tabani (min_bitrate_kbps) clamp'i.
// Header-only (yalniz <cstdint>/<algorithm>) — reji_pipeline/Rust/GPU link gerekmez.
// Karakterizasyon testi clamp'i yalniz varsayilan (floor < target) senaryosuyla
// dolayli kapsar; asil dal (target < floor) burada dogrudan kilitlenir.
#include <gtest/gtest.h>
#include "bitrate_policy.h"

using rj::reduce_floor_for_target;

// Varsayilan durum: taban (1000) hedefin (6000) altinda — taban degismez,
// REDUCE 6000'den 1000'e kadar kademeli inebilir.
TEST(BitratePolicyTest, FloorBelowTargetIsPreserved) {
    EXPECT_EQ(reduce_floor_for_target(1000u, 6000u), 1000u);
}

// Asil clamp dali (bu degisikligin sebebi): kullanici hedefi tabanin altinda.
// Taban hedefe indirilmezse apply_action'daki max(new,1000) yuzunden REDUCE
// hic ilerleyemezdi. Taban 800'e clamp'lenmeli.
TEST(BitratePolicyTest, TargetBelowFloorClampsFloorToTarget) {
    EXPECT_EQ(reduce_floor_for_target(1000u, 800u), 800u);
}

// Sinir: hedef == taban → taban korunur (clamp no-op).
TEST(BitratePolicyTest, TargetEqualsFloorIsNoop) {
    EXPECT_EQ(reduce_floor_for_target(1000u, 1000u), 1000u);
}

// Uc deger: spinbox alt siniri (500) tabanin cok altinda → taban 500'e iner.
TEST(BitratePolicyTest, MinimumTargetClampsFloor) {
    EXPECT_EQ(reduce_floor_for_target(1000u, 500u), 500u);
}

// Sonuc asla hedefi asmaz (degismez): floor ne olursa olsun <= target.
TEST(BitratePolicyTest, ResultNeverExceedsTarget) {
    for (uint32_t target : {500u, 800u, 1000u, 6000u, 50000u}) {
        for (uint32_t floor : {0u, 500u, 1000u, 8000u}) {
            EXPECT_LE(reduce_floor_for_target(floor, target), target);
        }
    }
}

// ── V10/L11: step_kbps (param1) artik kullaniliyor ──────────────────────────
// Kural motoru profil step'ini param1'e tasir (HP2); apply_action sabit %15
// uygulayip yok sayiyordu — mild(250-300)/high(500-750) ayrimi olu konfigdi.

TEST(BitratePolicyTest, ReduceUsesStepWhenProvided) {
    EXPECT_EQ(rj::reduce_step_bitrate(6000, 750), 6000u - 750u);
    EXPECT_EQ(rj::reduce_step_bitrate(6000, 300), 6000u - 300u);
}

TEST(BitratePolicyTest, ReduceFallsBackToPercentWithoutStep) {
    EXPECT_EQ(rj::reduce_step_bitrate(6000, 0),
              static_cast<uint32_t>(6000 * 0.85f));
    EXPECT_EQ(rj::reduce_step_bitrate(6000, -1),
              static_cast<uint32_t>(6000 * 0.85f));
}

TEST(BitratePolicyTest, ReduceStepLargerThanCurrentYieldsZeroForFloorClamp) {
    // Taban clamp cagirandadir (max ile) — burada 0 doner, negatif tasma yok.
    EXPECT_EQ(rj::reduce_step_bitrate(500, 750), 0u);
}

TEST(BitratePolicyTest, RecoverUsesStepWhenProvided) {
    EXPECT_EQ(rj::recover_step_bitrate(3500, 500), 4000u);
}

TEST(BitratePolicyTest, RecoverFallsBackToPercentWithoutStep) {
    EXPECT_EQ(rj::recover_step_bitrate(3500, 0),
              static_cast<uint32_t>(3500 * 1.15f));
}
