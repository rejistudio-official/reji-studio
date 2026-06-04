#include <gtest/gtest.h>
#include "../src/pipeline/frame_pacing.h"

class DxgiFramePacingTest : public ::testing::Test {
protected:
    DxgiFramePacing pacing_;
};

TEST_F(DxgiFramePacingTest, InitFailsWithNullSwapChain) {
    bool result = pacing_.init(nullptr);
    EXPECT_FALSE(result);
}

TEST_F(DxgiFramePacingTest, PollStatsStructInitialized) {
    DxgiFramePacing::FrameStats stats{};
    EXPECT_EQ(stats.frame_time_ms, 0.0f);
    EXPECT_EQ(stats.gpu_busy_ms, 0.0f);
}
