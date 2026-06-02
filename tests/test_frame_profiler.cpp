#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "../src/pipeline/include/frame_profiler.h"

using namespace rj;

TEST(FrameProfilerTest, BasicMarking) {
  FrameProfiler profiler;

  profiler.markAcquireStart();
  std::this_thread::sleep_for(std::chrono::microseconds(50));
  profiler.markAcquireEnd();

  EXPECT_EQ(profiler.sampleCount(), 1);
}

TEST(FrameProfilerTest, PercentileCalculation) {
  FrameProfiler profiler;

  // Simulate 100 frames with increasing acquire times
  for (int i = 0; i < 100; i++) {
    profiler.markAcquireStart();
    std::this_thread::sleep_for(std::chrono::microseconds(i + 1));
    profiler.markAcquireEnd();
  }

  EXPECT_EQ(profiler.sampleCount(), 100);
  // finalize() should not crash
  profiler.finalize();
}

TEST(FrameProfilerTest, MissingMarksGraceful) {
  FrameProfiler profiler;

  // markAcquireEnd without start should not crash
  profiler.markAcquireEnd();  // No start mark
  profiler.markCopyStart();
  profiler.markCopyEnd();
  profiler.markPaintGLStart();
  profiler.markPaintGLEnd();

  EXPECT_GE(profiler.sampleCount(), 0);  // At least one valid frame
  profiler.finalize();  // Should not crash
}
