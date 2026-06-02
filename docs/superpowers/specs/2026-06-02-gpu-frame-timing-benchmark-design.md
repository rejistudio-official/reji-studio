# v0.4 GPU Frame Timing Benchmark — Design Spec

**Date:** 2026-06-02  
**Author:** Claude Code  
**Version:** v0.4  
**Status:** Design (pre-implementation)

---

## 1. Overview

**Objective:** Profile PBO ping-pong render path performance and identify bottlenecks (CPU vs GPU bound).

**Success Criteria:**
- Collect frame timing metrics over 5-second window (≥300 frames @ 60fps)
- Calculate p50, p95, max latency for acquire, copy, paintGL phases
- Determine if CPU copy or GPU stall is the bottleneck
- Inform v0.5 strategy: DXGI shared handle vs Vulkan pivot

**Output:** stderr logs + future visual companion analysis

---

## 2. Scope

### In Scope
- Frame timing breakdown: DXGI acquire, CPU copy (staging→buffer), PBO upload, paintGL render
- Percentile statistics: p50, p95, max (microseconds)
- Thread-safe sampling (QMutex frame_mutex)
- Early finalization (shutdown before 5s)
- Integration with preview_widget + capture_dxgi

### Out of Scope
- GPU profiler (NVIDIA NSight, AMD GPUPerfStudio) — use standalone tools if needed
- Frame drop analysis (separate concern, v0.4 later)
- Multi-monitor scaling (v0.4 explicit task)
- Cross-adapter (DXGI shared handle) — v0.5 decision point

---

## 3. Architecture

### 3.1 FrameProfiler Module

**Files:**
- `src/pipeline/include/frame_profiler.h` (new)
- `src/pipeline/frame_profiler.cpp` (new)

**Class Definition:**
```cpp
namespace rj {

struct FrameTiming {
  uint64_t acquire_us;   // DXGI acquire duration
  uint64_t copy_us;      // CPU copy (staging → buffer)
  uint64_t paintgl_us;   // paintGL() total render time
};

class FrameProfiler {
public:
  FrameProfiler();
  ~FrameProfiler();
  
  // Timing marks — each pair marks a phase
  void markAcquireStart();
  void markAcquireEnd();
  void markCopyStart();
  void markCopyEnd();
  void markPaintGLStart();
  void markPaintGLEnd();
  
  // Finalize: calculate p50/p95/max, log to stderr
  // Call after 5s or on shutdown
  void finalize();
  
private:
  struct Sample {
    uint64_t acquire_start_us = 0;
    uint64_t acquire_end_us = 0;
    uint64_t copy_start_us = 0;
    uint64_t copy_end_us = 0;
    uint64_t paintgl_start_us = 0;
    uint64_t paintgl_end_us = 0;
  };
  
  std::vector<FrameTiming> samples;
  Sample current_sample;
  QMutex mutex;
};

} // namespace rj
```

**Key Design Decisions:**
- `uint64_t` microseconds: safe from wraparound (584 years range)
- Separate start/end marks for flexible instrumentation points
- QMutex for thread-safe access (same lock as frame_mutex)
- No-op in Release build (#ifdef ENABLE_FRAME_PROFILING)

---

## 4. Instrumentation Points

### 4.1 DXGI Capture Thread (`src/pipeline/capture/capture_dxgi.cpp`)

```cpp
// Before AcquireNextFrame
profiler->markAcquireStart();

// After AcquireNextFrame (success)
profiler->markAcquireEnd();
```

**Location:** `DxgiCapturePipeline::captureFrame()` or equivalent

### 4.2 CPU Copy (`src/ui/preview_widget.cpp`)

```cpp
void PreviewWidget::uploadFrame(const uint8_t* bgra_data, int width, int height) {
  profiler->markCopyStart();
  
  // glBufferData or memcpy to PBO
  memcpy(pbo_ptr, bgra_data, width * height * 4);
  
  profiler->markCopyEnd();
}
```

**Location:** `uploadFrame()` method

### 4.3 paintGL (`src/ui/preview_widget.cpp`)

```cpp
void PreviewWidget::paintGL() {
  profiler->markPaintGLStart();
  
  DwmFlush();  // NVIDIA Optimus barrier
  // ... shader bind, texture upload, render quad
  glFinish();  // Wait for GPU
  
  profiler->markPaintGLEnd();
}
```

**Location:** At start and end of `paintGL()`

---

## 5. Data Collection & Processing

### 5.1 Sample Structure
Each frame produces one `FrameTiming`:
```cpp
FrameTiming {
  acquire_us = acquire_end - acquire_start
  copy_us = copy_end - copy_start
  paintgl_us = paintgl_end - paintgl_start
}
```

**Memory Usage:** 300 frames × 3×uint64 = ~7.2 KB (safe)

### 5.2 Statistics Calculation

**Percentile P50/P95:**
1. Sort samples by duration (each phase independently)
2. P50 = samples[50% index].duration
3. P95 = samples[95% index].duration
4. Max = samples[-1].duration

**Example:**
```
20 samples: [10, 12, 15, 18, 20, 22, 25, 28, 30, 32, 35, 38, 40, 42, 45, 48, 50, 52, 55, 60]
P50 index = 10 → samples[10] = 35 µs
P95 index = 19 → samples[19] = 60 µs
Max = 60 µs
```

### 5.3 Output Format

```
[FrameProfiler] 300 frames in 5.0s (60.0 fps)
[FrameProfiler] acquire   | p50=  45 µs | p95= 120 µs | max= 250 µs
[FrameProfiler] copy      | p50= 180 µs | p95= 320 µs | max= 450 µs
[FrameProfiler] paintGL   | p50=2100 µs | p95=2800 µs | max=3500 µs
```

---

## 6. Error Handling

| Case | Handling |
|---|---|
| Missing mark pair (acquire_end without start) | Skip that sample, log warning |
| Zero duration (fast GPU, same tick) | Accept as valid |
| Duration > 1,000,000 µs (1s) | Log warning, clamp to 999,999 µs |
| Early finalize (< 10 valid samples) | Log partial: "[FrameProfiler] early finalize: N frames in Xs" |
| Thread safety (concurrent marks) | QMutex protects sample list |

---

## 7. Testing

### 7.1 Unit Tests (`tests/test_frame_profiler.cpp`)

```cpp
TEST(FrameProfilerTest, BasicMarking) {
  FrameProfiler profiler;
  profiler.markAcquireStart();
  std::this_thread::sleep_for(10us);
  profiler.markAcquireEnd();
  
  // Verify duration ~10 µs (allow ±5 µs jitter)
}

TEST(FrameProfilerTest, PercentileCalculation) {
  // Create 100 samples: [1, 2, 3, ..., 100]
  // P50 should be ~50, P95 should be ~95
}

TEST(FrameProfilerTest, MissingMarks) {
  // markAcquireEnd without start → should not crash
}
```

### 7.2 Integration Test

**Manual Steps:**
1. Build Release: `python scripts/build.py --config Release`
2. Run app: `Start-Process reji_app.exe -RedirectStandardError run.log`
3. Wait 10 seconds
4. Check log: `Get-Content run.log | findstr "FrameProfiler"`
5. Verify:
   - ✓ p50/p95/max logged for all 3 phases
   - ✓ No crashes
   - ✓ Timings realistic (copy < paintGL, acquire < copy)

**Expected Output (typical RTX 4070 Laptop):**
```
[FrameProfiler] acquire   | p50=  30 µs | p95=  80 µs | max= 150 µs
[FrameProfiler] copy      | p50= 150 µs | p95= 250 µs | max= 400 µs
[FrameProfiler] paintGL   | p50=1800 µs | p95=2500 µs | max=3200 µs
```

---

## 8. Integration & Dependencies

### 8.1 Linking
- Add to `src/pipeline/CMakeLists.txt`:
  ```cmake
  target_sources(reji_pipeline PRIVATE frame_profiler.cpp)
  ```

### 8.2 Initialization
- In `Pipeline::init()`:
  ```cpp
  profiler_ = std::make_unique<FrameProfiler>();
  ```

### 8.3 Lifetime
- Create in `Pipeline::init()`
- Call `finalize()` on `Pipeline::shutdown()` or after 5 seconds
- Timer: Use `std::chrono` to track elapsed time

### 8.4 Dependencies
- `<chrono>` (C++11 standard)
- `<algorithm>` (std::sort)
- `QMutex` (Qt6)
- `<cstdio>` (fprintf)

---

## 9. Future Extensions

**v0.4 Phase 2:**
- Automatic finalization timer (5s callback)
- Frame drop counter integration
- GPU temperature monitoring (NVIDIA/AMD APIs)

**v0.5:**
- Visual companion: timeline graph (frame timing × time)
- Comparison graph: acquire vs copy vs paintGL
- Automatic bottleneck detection (p95 > threshold → recommend optimization)

**v1.0:**
- Persistent metrics database (SQLite)
- Historical trend analysis

---

## 10. Revision History

| Date | Author | Change |
|---|---|---|
| 2026-06-02 | Claude Code | Initial design — FrameProfiler module, 3-phase breakdown, p50/p95/max stats |

---

## Appendix A: Timing Assumptions

**CPU Copy (staging → buffer):**
- Expected: 100–300 µs (1920×1080 @ 60fps)
- Depends: Memory bandwidth, CPU core frequency

**DXGI Acquire:**
- Expected: 20–100 µs (mostly GPU handoff)
- Depends: Frame availability, driver overhead

**paintGL (render + glFinish):**
- Expected: 1800–3500 µs (full frame @ 60fps)
- Bottleneck likely here (GPU-bound)

---

## Appendix B: Why Not Hardware Profilers?

| Tool | Pro | Con |
|---|---|---|
| NVIDIA NSight | Detailed GPU metrics | Requires NVIDIA GPU, overhead |
| AMD GPU Profiler | Cross-vendor | Windows-only, heavyweight |
| ETW (Windows) | Zero-overhead | Complex setup, learning curve |
| **FrameProfiler (this design)** | Lightweight, portable, app-specific | Limited to app's instrumentation points |

**Decision:** FrameProfiler for quick identification, NSight for deep dive if needed.
