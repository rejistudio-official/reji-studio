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
#include "../src/pipeline/include/metrics_subsystem.h"

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

// ── apply_collector_metrics (build_sample'ın alan-eşleme seam'i) ────────────
// gpu_load_pct v0.5'te ABI'ye eklenmiş ama build_sample'da eşlenmemişti →
// sample'da hep 0 kalıyor, drainer'ın GpuUsage event'i (ffi.rs
// system_events_for_sample, `> 0` kapısı) üretimde hiç doğmuyordu.

TEST(MetricsSubsystemTest, ApplyCollectorMetricsCarriesGpuLoadPct) {
    // Arrange: collector snapshot'ında gerçek GPU yükü var.
    rj::Metrics latest;
    latest.gpu_load_pct = 70;

    // Act
    RjMetricSample m{};
    rj::apply_collector_metrics(m, latest);

    // Assert: değer sample'a taşınır ve drainer'ın `> 0` kapısını geçer.
    EXPECT_EQ(m.gpu_load_pct, 70u);
    EXPECT_GT(m.gpu_load_pct, 0u);
}

TEST(MetricsSubsystemTest, ApplyCollectorMetricsCopiesAllExtendedFields) {
    // Her alana ayırt edici değer — eşlemede alan atlanır/karışırsa yakalanır
    // (gpu_load_pct'nin başına gelen tam buydu).
    rj::Metrics latest;
    latest.frame_drop_pct   = 11;
    latest.gpu_temp_c       = 62;
    latest.cpu_temp_c       = 47;
    latest.memory_usage_pct = 33;
    latest.cpu_load_pct     = 55;
    latest.gpu_load_pct     = 78;
    latest.network_rtt_ms   = 120;
    latest.network_loss_pct = 3;

    RjMetricSample m{};
    rj::apply_collector_metrics(m, latest);

    EXPECT_EQ(m.frame_drop_pct,   11u);
    EXPECT_EQ(m.gpu_temp_c,       62);
    EXPECT_EQ(m.cpu_temp_c,       47);
    EXPECT_EQ(m.memory_usage_pct, 33u);
    EXPECT_EQ(m.cpu_load_pct,     55u);
    EXPECT_EQ(m.gpu_load_pct,     78u);
    EXPECT_EQ(m.network_rtt_ms,   120u);
    EXPECT_EQ(m.network_loss_pct, 3u);
}
