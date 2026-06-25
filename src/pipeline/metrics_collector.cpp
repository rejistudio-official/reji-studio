#include "include/metrics_collector.h"
#include <algorithm>
#include <cmath>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

namespace rj {

MetricsCollector::MetricsCollector()
    : last_poll_time_(std::chrono::steady_clock::now()) {
  metrics_.timestamp = last_poll_time_;

#ifdef _WIN32
  HQUERY q = nullptr;
  if (PdhOpenQuery(nullptr, 0, &q) == ERROR_SUCCESS) {
    pdh_query_ = q;

    HCOUNTER cpu_ctr = nullptr;
    PdhAddEnglishCounterA(q, "\\Processor(_Total)\\% Processor Time",
                          0, &cpu_ctr);
    pdh_cpu_ctr_ = cpu_ctr;

    HCOUNTER gpu_ctr = nullptr;
    PdhAddEnglishCounterA(q,
        "\\GPU Engine(*engtype_3D)\\Utilization Percentage",
        0, &gpu_ctr);
    pdh_gpu_ctr_ = gpu_ctr;

    // İlk sample — delta counter'lar ikinci çağrıda anlamlı değer üretir.
    PdhCollectQueryData(q);
  } else {
    fprintf(stderr, "[Metrics] PdhOpenQuery failed — load counters unavailable\n");
  }
#endif
}

MetricsCollector::~MetricsCollector() {
#ifdef _WIN32
  if (pdh_query_) {
    PdhCloseQuery(static_cast<HQUERY>(pdh_query_));
    pdh_query_ = nullptr;
  }
#endif
}

bool MetricsCollector::poll() {
  auto now = std::chrono::steady_clock::now();
  if (now - last_poll_time_ < POLL_INTERVAL) return true;

#ifdef _WIN32
  // Tüm PDH counter'ları tek seferde güncelle — query_* fonksiyonları
  // sadece bu snapshot'ı okur, tekrar collect çağırmaz.
  if (pdh_query_) PdhCollectQueryData(static_cast<HQUERY>(pdh_query_));
#endif

  {
    std::lock_guard lock(metrics_lock_);

    metrics_.gpu_temp_c = query_gpu_thermal_wmi();
    if (metrics_.gpu_temp_c == 0) metrics_.gpu_temp_c = query_gpu_temp_amd_adl();
    if (metrics_.gpu_temp_c == 0) metrics_.gpu_temp_c = query_gpu_temp_nvidia_nvapi();
    metrics_.cpu_temp_c = query_cpu_thermal_wmi();

    metrics_.memory_usage_pct = query_memory_usage_pct();
    metrics_.cpu_load_pct     = query_cpu_load_pct();
    metrics_.gpu_load_pct     = query_gpu_load_pct();

    calculate_frame_drop_pct();
    metrics_.timestamp = now;

    fprintf(stderr, "[Metrics] cpu=%u%% mem=%u%% gpu=%u%% drop=%u%%\n",
            metrics_.cpu_load_pct, metrics_.memory_usage_pct,
            metrics_.gpu_load_pct, metrics_.frame_drop_pct);
  }

  last_poll_time_ = now;
  return true;
}

Metrics MetricsCollector::get_latest() const {
  std::lock_guard lock(metrics_lock_);
  return metrics_;
}

void MetricsCollector::record_frame_drop() {
  std::lock_guard lock(metrics_lock_);
  total_drops_++;
}

void MetricsCollector::record_frame() {
  std::lock_guard lock(metrics_lock_);
  total_frames_++;
}

// ---------------------------------------------------------------------------
// Thermal — stub fallback chain (WMI / ADL / NVAPI TBD)
// ---------------------------------------------------------------------------

int16_t MetricsCollector::query_gpu_thermal_wmi()       { return 0; }
int16_t MetricsCollector::query_cpu_thermal_wmi()       { return 0; }
int16_t MetricsCollector::query_gpu_temp_amd_adl()      { return 0; }
int16_t MetricsCollector::query_gpu_temp_nvidia_nvapi() { return 0; }

// ---------------------------------------------------------------------------
// System load
// ---------------------------------------------------------------------------

uint32_t MetricsCollector::query_memory_usage_pct() {
#ifdef _WIN32
  MEMORYSTATUSEX ms{};
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms))
    return static_cast<uint32_t>(ms.dwMemoryLoad);
#endif
  return 0;
}

uint32_t MetricsCollector::query_cpu_load_pct() {
#ifdef _WIN32
  if (!pdh_cpu_ctr_) return 0;
  PDH_FMT_COUNTERVALUE val{};
  if (PdhGetFormattedCounterValue(static_cast<HCOUNTER>(pdh_cpu_ctr_),
                                   PDH_FMT_DOUBLE, nullptr, &val) == ERROR_SUCCESS) {
    return static_cast<uint32_t>(std::clamp(val.doubleValue, 0.0, 100.0));
  }
#endif
  return 0;
}

uint32_t MetricsCollector::query_gpu_load_pct() {
#ifdef _WIN32
  if (!pdh_gpu_ctr_) return 0;

  // İlk çağrı: gereken buffer boyutunu ve instance sayısını öğren.
  DWORD buf_bytes = 0, count = 0;
  PdhGetFormattedCounterArrayA(static_cast<HCOUNTER>(pdh_gpu_ctr_),
                                PDH_FMT_DOUBLE, &buf_bytes, &count, nullptr);
  if (buf_bytes == 0 || count == 0) return 0;

  std::vector<BYTE> buf(buf_bytes);
  auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_A*>(buf.data());
  PDH_STATUS st = PdhGetFormattedCounterArrayA(
      static_cast<HCOUNTER>(pdh_gpu_ctr_),
      PDH_FMT_DOUBLE, &buf_bytes, &count, items);
  if (st != ERROR_SUCCESS) return 0;

  // Her 3D engine instance'ının max değerini al.
  double max_val = 0.0;
  for (DWORD i = 0; i < count; ++i)
    max_val = std::max(max_val, items[i].FmtValue.doubleValue);

  return static_cast<uint32_t>(std::clamp(max_val, 0.0, 100.0));
#endif
  return 0;
}

// ---------------------------------------------------------------------------
// Frame drop — 30s rolling window
// ---------------------------------------------------------------------------

void MetricsCollector::calculate_frame_drop_pct() {
  uint32_t delta_drops  = total_drops_  - prev_total_drops_;
  uint32_t delta_frames = total_frames_ - prev_total_frames_;
  prev_total_drops_  = total_drops_;
  prev_total_frames_ = total_frames_;

  drop_ring_[ring_head_  % WINDOW] = delta_drops;
  frame_ring_[ring_head_ % WINDOW] = delta_frames;
  ++ring_head_;
  ring_count_ = std::min(ring_count_ + 1, WINDOW);

  uint32_t total_drops = 0, total_frames = 0;
  for (size_t i = 0; i < ring_count_; ++i) {
    total_drops  += drop_ring_[i];
    total_frames += frame_ring_[i];
  }

  metrics_.frame_drop_pct  = total_frames > 0 ? (total_drops * 100u / total_frames) : 0u;
  metrics_.total_frames    = total_frames_;
  metrics_.dropped_frames  = total_drops_;
}

} // namespace rj
