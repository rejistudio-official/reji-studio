// tests/test_metrics_collector.cpp
//
// V10/L22: frame_drop_pct diriltme testi. record_frame/record_frame_drop
// (on_packet beslemesi) → poll() → 30s pencereli pct hesabı. Metrik ölüyken
// üç profil kuralından ikisi (frame_drop_mild/high) hiç tetiklenmiyor,
// frame_drop_recovery koşulsuz tetikleniyordu — pct artık gerçek veri taşır.
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "../src/pipeline/include/metrics_collector.h"

TEST(MetricsCollectorTest, FrameDropPctFromRecordedCounters) {
    // Arrange: 100 send denemesi, 10'u drop (pct beklentisi %10).
    // record_frame her deneme için çağrılır (drop dahil), record_frame_drop ek.
    rj::MetricsCollector mc;
    for (int i = 0; i < 100; ++i) mc.record_frame();
    for (int i = 0; i < 10;  ++i) mc.record_frame_drop();

    // Act: poll 1Hz self-throttle — pencerenin işlenmesi için aralığı geç.
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(mc.poll());

    // Assert
    EXPECT_EQ(mc.get_latest().frame_drop_pct, 10u);
}

TEST(MetricsCollectorTest, FrameDropPctZeroWhenIdle) {
    // Boşta (hiç frame kaydı yok) pct 0 kalmalı — payda 0 → 0 sözleşmesi.
    // Sprint 2 kuralı: boşta senaryo ayrı doğrulanır (Sprint 1 dersi).
    rj::MetricsCollector mc;

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(mc.poll());

    EXPECT_EQ(mc.get_latest().frame_drop_pct, 0u);
}

TEST(MetricsCollectorTest, FrameDropPctZeroWithHealthyStream) {
    // Aktif-yayın, drop'suz: 100 frame, 0 drop → pct 0.
    rj::MetricsCollector mc;
    for (int i = 0; i < 100; ++i) mc.record_frame();

    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_TRUE(mc.poll());

    EXPECT_EQ(mc.get_latest().frame_drop_pct, 0u);
}
