#include "ffi_bridge.h"
#include <cstdio>

// Quick check to print struct sizes
int main() {
    printf("=== RjMetricSample ABI Check ===\n");
    printf("sizeof(RjMetricSample) = %zu bytes\n", sizeof(RjMetricSample));
    printf("sizeof(RjAction) = %zu bytes\n", sizeof(RjAction));
    printf("sizeof(RjHealingMode) = %zu bytes\n", sizeof(RjHealingMode));
    printf("\n=== Offsets ===\n");

    RjMetricSample sample = {};

    printf("magic_head offset: %zu\n", offsetof(RjMetricSample, magic_head));
    printf("timestamp_us offset: %zu\n", offsetof(RjMetricSample, timestamp_us));
    printf("bitrate_kbps offset: %zu\n", offsetof(RjMetricSample, bitrate_kbps));
    printf("fps_actual offset: %zu\n", offsetof(RjMetricSample, fps_actual));
    printf("cpu_percent offset: %zu\n", offsetof(RjMetricSample, cpu_percent));
    printf("frame_drops offset: %zu\n", offsetof(RjMetricSample, frame_drops));
    printf("frame_drop_pct offset: %zu\n", offsetof(RjMetricSample, frame_drop_pct));
    printf("gpu_temp_c offset: %zu\n", offsetof(RjMetricSample, gpu_temp_c));
    printf("cpu_temp_c offset: %zu\n", offsetof(RjMetricSample, cpu_temp_c));
    printf("memory_usage_pct offset: %zu\n", offsetof(RjMetricSample, memory_usage_pct));
    printf("cpu_load_pct offset: %zu\n", offsetof(RjMetricSample, cpu_load_pct));
    printf("network_rtt_ms offset: %zu\n", offsetof(RjMetricSample, network_rtt_ms));
    printf("network_loss_pct offset: %zu\n", offsetof(RjMetricSample, network_loss_pct));
    printf("reserved offset: %zu\n", offsetof(RjMetricSample, reserved));
    printf("magic_tail offset: %zu\n", offsetof(RjMetricSample, magic_tail));

    return 0;
}
