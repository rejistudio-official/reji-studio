#include "ffi_bridge.h"
#include <cstdio>

// ABI constraints — derleme zamanı güvencesi; Rust const_assert ile eşleşmeli.
static_assert(sizeof(RjMetricSample) == 64, "RjMetricSample ABI mismatch");
static_assert(sizeof(RjAction)       == 20, "RjAction ABI mismatch");
static_assert(sizeof(RjActionEvent)  == 24, "RjActionEvent ABI mismatch");
static_assert(sizeof(RjCommand)      == 24, "RjCommand ABI mismatch");

// RjCommand field offsets
static_assert(offsetof(RjCommand, cmd_type)     ==  0, "RjCommand::cmd_type offset");
static_assert(offsetof(RjCommand, timestamp_us) ==  8, "RjCommand::timestamp_us offset");
static_assert(offsetof(RjCommand, param_u32)    == 16, "RjCommand::param_u32 offset");
static_assert(offsetof(RjCommand, param_f32)    == 20, "RjCommand::param_f32 offset");

// RjAction field offsets
static_assert(offsetof(RjAction, id)          ==  0, "RjAction::id offset");
static_assert(offsetof(RjAction, action_type) ==  4, "RjAction::action_type offset");
static_assert(offsetof(RjAction, param1)      ==  8, "RjAction::param1 offset");
static_assert(offsetof(RjAction, param2)      == 12, "RjAction::param2 offset");
static_assert(offsetof(RjAction, canary)      == 16, "RjAction::canary offset");

// RjActionEvent field offsets (V8/I33 — UI bildirim event'i)
static_assert(offsetof(RjActionEvent, id)               ==  0, "RjActionEvent::id offset");
static_assert(offsetof(RjActionEvent, action_type)      ==  4, "RjActionEvent::action_type offset");
static_assert(offsetof(RjActionEvent, param1)           ==  8, "RjActionEvent::param1 offset");
static_assert(offsetof(RjActionEvent, param2)           == 12, "RjActionEvent::param2 offset");
static_assert(offsetof(RjActionEvent, require_approval) == 16, "RjActionEvent::require_approval offset");
static_assert(offsetof(RjActionEvent, kind)             == 20, "RjActionEvent::kind offset");

// RjMetricSample field offsets
static_assert(offsetof(RjMetricSample, magic_head)       ==  0, "RjMetricSample::magic_head offset");
static_assert(offsetof(RjMetricSample, timestamp_us)     ==  8, "RjMetricSample::timestamp_us offset");
static_assert(offsetof(RjMetricSample, bitrate_kbps)     == 16, "RjMetricSample::bitrate_kbps offset");
static_assert(offsetof(RjMetricSample, fps_actual)       == 20, "RjMetricSample::fps_actual offset");
static_assert(offsetof(RjMetricSample, cpu_percent)      == 24, "RjMetricSample::cpu_percent offset");
static_assert(offsetof(RjMetricSample, frame_drops)      == 28, "RjMetricSample::frame_drops offset");
static_assert(offsetof(RjMetricSample, frame_drop_pct)   == 32, "RjMetricSample::frame_drop_pct offset");
static_assert(offsetof(RjMetricSample, gpu_temp_c)       == 36, "RjMetricSample::gpu_temp_c offset");
static_assert(offsetof(RjMetricSample, cpu_temp_c)       == 38, "RjMetricSample::cpu_temp_c offset");
static_assert(offsetof(RjMetricSample, memory_usage_pct) == 40, "RjMetricSample::memory_usage_pct offset");
static_assert(offsetof(RjMetricSample, cpu_load_pct)     == 44, "RjMetricSample::cpu_load_pct offset");
static_assert(offsetof(RjMetricSample, gpu_load_pct)     == 48, "RjMetricSample::gpu_load_pct offset");
static_assert(offsetof(RjMetricSample, network_rtt_ms)   == 52, "RjMetricSample::network_rtt_ms offset");
static_assert(offsetof(RjMetricSample, network_loss_pct) == 54, "RjMetricSample::network_loss_pct offset");
static_assert(offsetof(RjMetricSample, source_id)        == 55, "RjMetricSample::source_id offset");
static_assert(offsetof(RjMetricSample, magic_tail)       == 56, "RjMetricSample::magic_tail offset");

// RjActionType enum değerleri — Rust ffi.rs:RjActionType #[repr(u32)] ile eşleşmeli.
// cbindgen C++ modunda enum değerleri kaynak sırasıyla 0'dan başlar.
static_assert(static_cast<uint32_t>(BitrateReduce)     == 0u, "RjActionType::BitrateReduce = 0");
static_assert(static_cast<uint32_t>(BitrateRecover)    == 1u, "RjActionType::BitrateRecover = 1");
static_assert(static_cast<uint32_t>(ScaleResolution)   == 2u, "RjActionType::ScaleResolution = 2");
static_assert(static_cast<uint32_t>(RestoreResolution) == 3u, "RjActionType::RestoreResolution = 3");
static_assert(static_cast<uint32_t>(CapFps)            == 4u, "RjActionType::CapFps = 4");
static_assert(static_cast<uint32_t>(RestoreFps)        == 5u, "RjActionType::RestoreFps = 5");
static_assert(static_cast<uint32_t>(LogOnly)           == 6u, "RjActionType::LogOnly = 6");

int main() {
    printf("=== RjMetricSample ABI Check ===\n");
    printf("sizeof(RjMetricSample) = %zu bytes\n", sizeof(RjMetricSample));
    printf("sizeof(RjAction) = %zu bytes\n", sizeof(RjAction));
    printf("sizeof(RjActionEvent) = %zu bytes\n", sizeof(RjActionEvent));
    printf("sizeof(RjCommand) = %zu bytes\n", sizeof(RjCommand));
    printf("sizeof(RjHealingMode) = %zu bytes\n", sizeof(RjHealingMode));
    printf("\n=== RjActionType enum değerleri ===\n");
    printf("BitrateReduce     = %u\n", static_cast<uint32_t>(BitrateReduce));
    printf("BitrateRecover    = %u\n", static_cast<uint32_t>(BitrateRecover));
    printf("ScaleResolution   = %u\n", static_cast<uint32_t>(ScaleResolution));
    printf("RestoreResolution = %u\n", static_cast<uint32_t>(RestoreResolution));
    printf("CapFps            = %u\n", static_cast<uint32_t>(CapFps));
    printf("RestoreFps        = %u\n", static_cast<uint32_t>(RestoreFps));
    printf("LogOnly           = %u\n", static_cast<uint32_t>(LogOnly));
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
    printf("gpu_load_pct offset: %zu\n", offsetof(RjMetricSample, gpu_load_pct));
    printf("network_rtt_ms offset: %zu\n", offsetof(RjMetricSample, network_rtt_ms));
    printf("network_loss_pct offset: %zu\n", offsetof(RjMetricSample, network_loss_pct));
    printf("source_id offset: %zu\n", offsetof(RjMetricSample, source_id));
    printf("magic_tail offset: %zu\n", offsetof(RjMetricSample, magic_tail));

    return 0;
}
