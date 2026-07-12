#ifndef REJI_PIPELINE_METRICS_COLLECTOR_H
#define REJI_PIPELINE_METRICS_COLLECTOR_H

#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <vector>

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
  uint32_t gpu_load_pct = 0;          // [0, 100] — GPU 3D engine max utilisation

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
  uint32_t query_gpu_load_pct();

  // Frame drop calculation (30s rolling window @ 1Hz = 30 polls)
  void calculate_frame_drop_pct();

  // State
  Metrics metrics_;
  mutable std::mutex metrics_lock_;

  // Frame tracking — sabit ring buffer (heap allocation yok)
  static constexpr size_t WINDOW = 30;
  std::array<uint32_t, WINDOW> drop_ring_{};
  std::array<uint32_t, WINDOW> frame_ring_{};
  size_t ring_head_ = 0;
  size_t ring_count_ = 0;
  uint32_t total_frames_ = 0;
  uint32_t total_drops_ = 0;
  uint32_t prev_total_frames_ = 0;
  uint32_t prev_total_drops_ = 0;

  // PDH query state (Windows only — void* avoids including pdh.h in header)
  void* pdh_query_   = nullptr;   // HQUERY
  void* pdh_cpu_ctr_ = nullptr;   // HCOUNTER  \\Processor(_Total)\\% Processor Time
  void* pdh_gpu_ctr_ = nullptr;   // HCOUNTER  \\GPU Engine(*engtype_3D)\\Utilization Percentage

  // V8/I16: PdhGetFormattedCounterArray çıktı buffer'ı — üye olarak tutulur,
  // yalnız büyüdükçe resize edilir (1Hz poll'de her seferinde alloc yapılmaz).
  std::vector<unsigned char> gpu_pdh_buf_;

  // Polling throttle
  std::chrono::steady_clock::time_point last_poll_time_;
  static constexpr auto POLL_INTERVAL = std::chrono::milliseconds(1000);
};

} // namespace rj

#endif // REJI_PIPELINE_METRICS_COLLECTOR_H
