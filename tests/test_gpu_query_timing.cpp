#include <gtest/gtest.h>
#include "../src/pipeline/gpu_query_timing.h"

class GpuQueryTimingTest : public ::testing::Test {
protected:
    GpuQueryTiming timing_;
};

TEST_F(GpuQueryTimingTest, InitFailsWithNullDevice) {
    bool result = timing_.init(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
    EXPECT_FALSE(result);
}

TEST_F(GpuQueryTimingTest, QueryResultStructInitialized) {
    GpuQueryTiming::QueryResult result{};
    EXPECT_EQ(result.copy_duration_ms, 0.0f);
    EXPECT_EQ(result.render_duration_ms, 0.0f);
}
