#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define RJ_FFI_VERSION 0x00010000  /* 1.0.0 */

#define RJ_METRIC_MAGIC 0xEEFF1234u

/* C++ -> Rust: Telemetri */
typedef struct {
    uint32_t magic_head;
    uint64_t timestamp_us;
    uint32_t bitrate_kbps;
    float    fps_actual;
    float    cpu_percent;
    uint32_t frame_drops;
    uint32_t magic_tail;
} RjMetricSample;

/* Rust -> C++: Komut */
typedef enum {
    RJ_CMD_SCENE_SWITCH  = 0,
    RJ_CMD_BITRATE_SET   = 1,
    RJ_CMD_PREVIEW_FPS   = 2,
} RjCommandType;

typedef struct {
    uint32_t cmd_type;
    uint64_t timestamp_us;
    uint32_t param_u32;
    float    param_f32;
} RjCommand;

/* Rust tarafindan implement edilir */
extern void rj_metrics_push(const RjMetricSample* sample);
extern int  rj_command_drain(RjCommand* out, int max);
extern int  rj_pipeline_status(void);
extern void rj_start_monitor(void);
/* SRT baglanti kopusunu Rust event bus'a iletir; reason UTF-8, null-safe. */
extern void rj_connection_lost(const char* reason);

/* Bu dosyadan export edilir */
uint32_t rj_ffi_version(void);

#ifdef __cplusplus
}
#endif