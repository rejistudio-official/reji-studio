#ifndef REJI_PIPELINE_METRICS_COLLECTOR_H
#define REJI_PIPELINE_METRICS_COLLECTOR_H

#include <cstdint>
#include <deque>
#include <mutex>
#include <chrono>

namespace rj {

// Real-time metrics snapshot
struct Metrics {
  // Frame timing
  uint32_t frame_drop_pct = 0;        // [0, 100] — last 30s drop rate
  uint32_t total_frames = 0;          // cumulative frame count
  uint32_t dropped_frames = 0;        // cumulative drop count

  // Thermal
  int16_t gpu_temp_c = 0;             // [-128, 127] °C, 0 = unavailable
  int16_t cpu_temp_c = 0;             // [-128, 127] °C, 0 = unavailable

  // System load
  uint32_t memory_usage_pct = 0;      // [0, 100]
  uint32_t cpu_load_pct = 0;          // [0, 100]

  // Network (optional, v0.4.1+)
  uint16_t network_rtt_ms = 0;        // [0, 65535] ms
  uint8_t network_loss_pct = 0;       // [0, 100] %

  // Timestamp
  std::chrono::steady_clock::time_point timestamp;
};

class MetricsCollector {
public:
  MetricsCollector();
  ~MetricsCollector();

  // Poll thermal sensors, system load, frame drop rate
  // Called from background thread ~1 Hz
  // Returns true if successful, false if WMI/sensor unavailable
  bool poll();

  // Get latest metrics snapshot (thread-safe)
  Metrics get_latest() const;

  // Record frame drop (called from DXGI capture thread)
  void record_frame_drop();

  // Record successful frame (called from DXGI capture thread)
  void record_frame();

private:
  // Thermal sensor queries (Windows)
  int16_t query_gpu_thermal_wmi();      // fallback: 0°C if unavailable
  int16_t query_cpu_thermal_wmi();      // fallback: 0°C if unavailable
  int16_t query_gpu_temp_amd_adl();     // AMD ADL fallback, returns 0 if unavailable
  int16_t query_gpu_temp_nvidia_nvapi(); // NVIDIA NVAPI fallback, returns 0 if unavailable

  // System load
  uint32_t query_memory_usage_pct();
  uint32_t query_cpu_load_pct();

  // Frame drop calculation (30s rolling window @ 60fps = 1800 frames)
  void calculate_frame_drop_pct();

  // State
  Metrics metrics_;
  mutable std::mutex metrics_lock_;

  // Frame tracking
  std::deque<uint32_t> frame_drop_window_;  // 30s worth of drops
  uint32_t total_frames_ = 0;
  uint32_t total_drops_ = 0;

  // Polling throttle
  std::chrono::steady_clock::time_point last_poll_time_;
  static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(1000);
};

} // namespace rj

#endif // REJI_PIPELINE_METRICS_COLLECTOR_H
