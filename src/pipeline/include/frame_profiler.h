#ifndef REJI_PIPELINE_FRAME_PROFILER_H
#define REJI_PIPELINE_FRAME_PROFILER_H

#include <cstdint>
#include <vector>
#include <QMutex>

namespace rj {

struct FrameTiming {
  uint64_t acquire_us;   // DXGI acquire duration (microseconds)
  uint64_t copy_us;      // CPU copy duration (staging → buffer)
  uint64_t paintgl_us;   // paintGL() render duration
};

class FrameProfiler {
public:
  FrameProfiler();
  ~FrameProfiler();

  // Timing marks — thread-safe
  void markAcquireStart();
  void markAcquireEnd();
  void markCopyStart();
  void markCopyEnd();
  void markPaintGLStart();
  void markPaintGLEnd();

  // Finalize: calculate p50/p95/max, log to stderr
  // Safe to call multiple times (idempotent after first call)
  void finalize();

  // Query current sample count (for testing)
  size_t sampleCount() const;

private:
  struct Sample {
    uint64_t acquire_start_us = 0;
    uint64_t acquire_end_us = 0;
    uint64_t copy_start_us = 0;
    uint64_t copy_end_us = 0;
    uint64_t paintgl_start_us = 0;
    uint64_t paintgl_end_us = 0;
  };

  std::vector<FrameTiming> samples_;
  Sample current_sample_;
  mutable QMutex mutex_;
  bool finalized_ = false;
  size_t frame_count_ = 0;
};

} // namespace rj

#endif // REJI_PIPELINE_FRAME_PROFILER_H
