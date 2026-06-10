#include "frame_profiler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace rj {

namespace {

inline uint64_t now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::high_resolution_clock::now().time_since_epoch())
      .count();
}

template <typename T>
T percentile(const std::vector<T>& sorted_vec, float p) {
  if (sorted_vec.empty()) return 0;
  size_t idx = static_cast<size_t>(std::ceil(sorted_vec.size() * (p / 100.0f))) - 1;
  if (idx >= sorted_vec.size()) idx = sorted_vec.size() - 1;
  return sorted_vec[idx];
}

}  // anonymous namespace

FrameProfiler::FrameProfiler() = default;

FrameProfiler::~FrameProfiler() {
  if (!finalized_) {
    finalize();
  }
}

void FrameProfiler::markAcquireStart() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.acquire_start_us = now_us();
}

void FrameProfiler::markAcquireEnd() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.acquire_end_us = now_us();
}

void FrameProfiler::markCopyStart() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.copy_start_us = now_us();
}

void FrameProfiler::markCopyEnd() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.copy_end_us = now_us();
}

void FrameProfiler::markPaintGLStart() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.paintgl_start_us = now_us();
}

void FrameProfiler::markPaintGLEnd() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_sample_.paintgl_end_us = now_us();

  // After paintGL ends, finalize the frame and store it
  if (current_sample_.acquire_start_us > 0 &&
      current_sample_.acquire_end_us > current_sample_.acquire_start_us &&
      current_sample_.copy_start_us > 0 &&
      current_sample_.copy_end_us > current_sample_.copy_start_us &&
      current_sample_.paintgl_start_us > 0 &&
      current_sample_.paintgl_end_us > current_sample_.paintgl_start_us) {
    FrameTiming timing{
        current_sample_.acquire_end_us - current_sample_.acquire_start_us,
        current_sample_.copy_end_us - current_sample_.copy_start_us,
        current_sample_.paintgl_end_us - current_sample_.paintgl_start_us};
    samples_[head_ % MAX_SAMPLES] = timing;
    ++head_;
    count_ = std::min(count_ + 1, MAX_SAMPLES);
    current_sample_ = Sample();  // Reset
  }
}

size_t FrameProfiler::sampleCount() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

void FrameProfiler::finalize() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (finalized_) return;  // Idempotent

  if (count_ == 0) {
    fprintf(stderr, "[FrameProfiler] No samples collected\n");
    finalized_ = true;
    return;
  }

  // Sort each phase independently for percentile calculation
  std::vector<uint64_t> acquire_times, copy_times, paintgl_times;
  for (size_t k = 0; k < count_; ++k) {
    acquire_times.push_back(samples_[k].acquire_us);
    copy_times.push_back(samples_[k].copy_us);
    paintgl_times.push_back(samples_[k].paintgl_us);
  }

  std::sort(acquire_times.begin(), acquire_times.end());
  std::sort(copy_times.begin(), copy_times.end());
  std::sort(paintgl_times.begin(), paintgl_times.end());

  // Calculate percentiles
  uint64_t acq_p50 = percentile(acquire_times, 50.0f);
  uint64_t acq_p95 = percentile(acquire_times, 95.0f);
  uint64_t acq_max = acquire_times.back();

  uint64_t copy_p50 = percentile(copy_times, 50.0f);
  uint64_t copy_p95 = percentile(copy_times, 95.0f);
  uint64_t copy_max = copy_times.back();

  uint64_t paint_p50 = percentile(paintgl_times, 50.0f);
  uint64_t paint_p95 = percentile(paintgl_times, 95.0f);
  uint64_t paint_max = paintgl_times.back();

  // Log results
  fprintf(stderr, "\n[FrameProfiler] ========== RESULTS ==========\n");
  fprintf(stderr, "[FrameProfiler] %zu frames collected\n", count_);
  fprintf(stderr, "[FrameProfiler] acquire   | p50=%6llu µs | p95=%6llu µs | max=%6llu µs\n",
          acq_p50, acq_p95, acq_max);
  fprintf(stderr, "[FrameProfiler] copy      | p50=%6llu µs | p95=%6llu µs | max=%6llu µs\n",
          copy_p50, copy_p95, copy_max);
  fprintf(stderr, "[FrameProfiler] paintGL   | p50=%6llu µs | p95=%6llu µs | max=%6llu µs\n",
          paint_p50, paint_p95, paint_max);
  fprintf(stderr, "[FrameProfiler] =============================\n\n");

  finalized_ = true;
}

}  // namespace rj
