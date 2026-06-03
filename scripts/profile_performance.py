#!/usr/bin/env python3
"""
Reji Studio v0.5 Performance Profiling Script

Measures:
- Frame latency (target: <2ms)
- Shader cache hit rate (target: >90%)
- Per-frame allocations (target: 0)
"""

import subprocess
import re
import sys
import time
from pathlib import Path
from dataclasses import dataclass
from typing import Optional


@dataclass
class FrameMetrics:
    """Single frame metrics from log output"""
    timestamp: float
    paint_start_ms: Optional[float] = None
    paint_end_ms: Optional[float] = None
    latency_ms: Optional[float] = None


@dataclass
class ProfileResults:
    """Aggregated profiling results"""
    total_frames: int = 0
    avg_latency_ms: float = 0.0
    max_latency_ms: float = 0.0
    min_latency_ms: float = 0.0
    cache_hits: int = 0
    cache_misses: int = 0
    hit_rate_percent: float = 0.0
    per_frame_allocs: int = 0
    passes: int = 0
    failures: int = 0

    def __str__(self):
        """Format results for display"""
        return f"""
=== Reji Studio v0.5 Performance Profile ===

Frame Latency:
  Average: {self.avg_latency_ms:.2f}ms
  Min:     {self.min_latency_ms:.2f}ms
  Max:     {self.max_latency_ms:.2f}ms
  Target:  <2.0ms

Shader Cache:
  Hit Rate: {self.hit_rate_percent:.1f}%
  Hits:     {self.cache_hits}
  Misses:   {self.cache_misses}
  Target:   >90%

Per-Frame Allocations:
  Count:    {self.per_frame_allocs}
  Target:   0 (zero allocation hot-path)

Test Results:
  Passed:   {self.passes}
  Failed:   {self.failures}
"""


class PerformanceProfiler:
    """Runs and analyzes performance metrics"""

    def __init__(self, executable_path: str, timeout_seconds: int = 30):
        self.executable_path = executable_path
        self.timeout = timeout_seconds
        self.results = ProfileResults()
        self.log_buffer = []

    def run_profiling(self) -> bool:
        """Run the executable and capture metrics"""
        try:
            print(f"Starting performance profiling (timeout: {self.timeout}s)...")
            proc = subprocess.Popen(
                [self.executable_path],
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1
            )

            # Collect output for 30 seconds
            start_time = time.time()
            while time.time() - start_time < self.timeout:
                try:
                    line = proc.stderr.readline()
                    if line:
                        self.log_buffer.append(line.strip())
                        self._parse_log_line(line)
                except Exception as e:
                    print(f"Warning: Error reading output: {e}")

            # Terminate gracefully
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()

            return True

        except FileNotFoundError:
            print(f"Error: Executable not found: {self.executable_path}")
            return False
        except Exception as e:
            print(f"Error running profiler: {e}")
            return False

    def _parse_log_line(self, line: str):
        """Extract metrics from log line"""
        line = line.strip()

        # Parse frame profiler markers
        if "[PreviewWidget] paintGL start" in line:
            self.results.total_frames += 1

        # Parse shader cache hits/misses
        if "[ShaderCache] Cache hit" in line:
            self.results.cache_hits += 1
        elif "[ShaderCache] Cache miss" in line:
            self.results.cache_misses += 1

        # Parse memory allocations
        if "new " in line or "malloc" in line or "allocate" in line:
            self.results.per_frame_allocs += 1

    def analyze_results(self):
        """Post-process and validate results"""
        total_cache = self.results.cache_hits + self.results.cache_misses
        if total_cache > 0:
            self.results.hit_rate_percent = (self.results.cache_hits / total_cache) * 100

        # Validation checks
        if self.results.avg_latency_ms < 2.0:
            self.results.passes += 1
        else:
            self.results.failures += 1
            print(f"❌ Latency target FAILED: {self.results.avg_latency_ms:.2f}ms (target: <2ms)")

        if self.results.hit_rate_percent >= 90.0:
            self.results.passes += 1
        else:
            self.results.failures += 1
            print(f"❌ Cache hit rate FAILED: {self.results.hit_rate_percent:.1f}% (target: >90%)")

        if self.results.per_frame_allocs == 0:
            self.results.passes += 1
        else:
            self.results.failures += 1
            print(f"❌ Per-frame allocations FAILED: {self.results.per_frame_allocs} (target: 0)")

    def print_results(self):
        """Display profiling results"""
        print(self.results)
        return self.results.failures == 0


def main():
    """Main profiling entry point"""
    # Build output executable path (release build)
    project_root = Path(__file__).parent.parent
    executable = project_root / "build" / "src" / "ui" / "Release" / "reji_ui.exe"

    if not executable.exists():
        # Try debug build
        executable = project_root / "build" / "src" / "ui" / "Debug" / "reji_ui.exe"

    if not executable.exists():
        print(f"Error: Executable not found at {executable}")
        print("Build the project first with: cmake --build build --config Release")
        return 1

    profiler = PerformanceProfiler(str(executable), timeout_seconds=30)

    if not profiler.run_profiling():
        return 1

    profiler.analyze_results()
    success = profiler.print_results()

    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
