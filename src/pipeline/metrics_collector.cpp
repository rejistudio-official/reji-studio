#include "include/metrics_collector.h"
#include <algorithm>
#include <numeric>
#include <cmath>

// TODO: WMI implementation (v0.4.1+)
// #ifdef _WIN32
// #include <wbemidl.h>
// #pragma comment(lib, "wbemuuid.lib")
// #pragma comment(lib, "ole32.lib")
// #pragma comment(lib, "oleaut32.lib")
// #endif

namespace rj {

MetricsCollector::MetricsCollector()
    : last_poll_time_(std::chrono::steady_clock::now()) {
  metrics_.timestamp = last_poll_time_;
}

MetricsCollector::~MetricsCollector() = default;

bool MetricsCollector::poll() {
  auto now = std::chrono::steady_clock::now();
  auto elapsed = now - last_poll_time_;

  // Throttle polling to ~1 Hz
  if (elapsed < POLL_INTERVAL) {
    return true;
  }

  {
    std::lock_guard lock(metrics_lock_);

    // Query thermal sensors (fallback chain)
    metrics_.gpu_temp_c = query_gpu_thermal_wmi();
    if (metrics_.gpu_temp_c == 0) {
      metrics_.gpu_temp_c = query_gpu_temp_amd_adl();
    }
    if (metrics_.gpu_temp_c == 0) {
      metrics_.gpu_temp_c = query_gpu_temp_nvidia_nvapi();
    }

    metrics_.cpu_temp_c = query_cpu_thermal_wmi();

    // Query system load
    metrics_.memory_usage_pct = query_memory_usage_pct();
    metrics_.cpu_load_pct = query_cpu_load_pct();

    // Calculate frame drop rate
    calculate_frame_drop_pct();

    metrics_.timestamp = now;
  }

  last_poll_time_ = now;
  return true;
}

Metrics MetricsCollector::get_latest() const {
  std::lock_guard lock(metrics_lock_);
  return metrics_;
}

void MetricsCollector::record_frame_drop() {
  {
    std::lock_guard lock(metrics_lock_);
    total_drops_++;
  }
}

void MetricsCollector::record_frame() {
  {
    std::lock_guard lock(metrics_lock_);
    total_frames_++;
  }
}

int16_t MetricsCollector::query_gpu_thermal_wmi() {
  // WMI query for GPU temperature via Win32_OperatingSystem
  // Fallback: return 0°C (unavailable)
  // TODO: Implement WMI query in v0.4.1
  return 0;
}

int16_t MetricsCollector::query_cpu_thermal_wmi() {
  // WMI query for CPU temperature via Win32_PerfFormattedData_PerfProc_Process
  // Fallback: return 0°C (unavailable)
  // TODO: Implement WMI query in v0.4.1
  return 0;
}

int16_t MetricsCollector::query_gpu_temp_amd_adl() {
  // AMD ADL (AMD Display Library) GPU temperature query
  // Fallback: return 0°C (unavailable or ADL not installed)
  // TODO: Implement ADL query in v0.4.1
  return 0;
}

int16_t MetricsCollector::query_gpu_temp_nvidia_nvapi() {
  // NVIDIA NVAPI GPU temperature query
  // Fallback: return 0°C (unavailable or NVAPI not installed)
  // TODO: Implement NVAPI query in v0.4.1
  return 0;
}

uint32_t MetricsCollector::query_memory_usage_pct() {
  // Query system memory usage percentage
  // TODO: Implement via WMI or native Windows API
  return 0;
}

uint32_t MetricsCollector::query_cpu_load_pct() {
  // Query CPU load percentage (all cores average or max)
  // TODO: Implement via Performance Counters (PDH) or WMI
  return 0;
}

void MetricsCollector::calculate_frame_drop_pct() {
  // Maintain 30s rolling window @ 60fps = 1800 frames
  static const size_t MAX_WINDOW_FRAMES = 1800;

  // Add current drop count to window
  frame_drop_window_.push_back(total_drops_);

  // Keep window size bounded
  while (frame_drop_window_.size() > MAX_WINDOW_FRAMES) {
    frame_drop_window_.pop_front();
  }

  // Calculate drop percentage over window
  if (!frame_drop_window_.empty()) {
    uint32_t total_window_drops =
        std::accumulate(frame_drop_window_.begin(), frame_drop_window_.end(), 0u);
    float drop_rate =
        (total_window_drops / static_cast<float>(frame_drop_window_.size())) * 100.0f;
    metrics_.frame_drop_pct = static_cast<uint32_t>(std::clamp(drop_rate, 0.0f, 100.0f));
  } else {
    metrics_.frame_drop_pct = 0;
  }

  metrics_.total_frames = total_frames_;
  metrics_.dropped_frames = total_drops_;
}

} // namespace rj
