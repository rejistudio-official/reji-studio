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
