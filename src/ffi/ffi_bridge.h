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

    /* Runtime Adaptation L3 (v0.4) */
    uint32_t frame_drop_pct;           /* [0, 100] — last 30s drop rate */
    int16_t  gpu_temp_c;               /* [-128, 127] °C, 0 = unavailable */
    int16_t  cpu_temp_c;               /* [-128, 127] °C, 0 = unavailable */
    uint32_t memory_usage_pct;         /* [0, 100] */
    uint32_t cpu_load_pct;             /* [0, 100] */
    uint16_t network_rtt_ms;           /* [0, 65535] ms (optional v0.4.1+) */
    uint8_t  network_loss_pct;         /* [0, 100] % (optional v0.4.1+) */
    uint8_t  reserved;                 /* padding */

    uint32_t magic_tail;
} RjMetricSample;

/* Healing Mode (v0.4+) */
typedef enum {
    RJ_MODE_AUTO_PILOT  = 0,   /* All actions auto-execute */
    RJ_MODE_CO_PILOT    = 1,   /* User approval required, 30s timeout → cancel */
    RJ_MODE_ASSIST      = 2,   /* Critical auto, mid/low log-only */
    RJ_MODE_MANUAL      = 3,   /* All actions suppressed */
} RjHealingMode;

/* Rust -> C++: Komut (Legacy, v0.3) */
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

/* Rust -> C++: Adaptation Action (v0.4+) */
typedef enum {
    RJ_ACTION_BITRATE_REDUCE        = 0,
    RJ_ACTION_BITRATE_RECOVER       = 1,
    RJ_ACTION_SCALE_RESOLUTION      = 2,
    RJ_ACTION_RESTORE_RESOLUTION    = 3,
    RJ_ACTION_CAP_FPS               = 4,
    RJ_ACTION_RESTORE_FPS           = 5,
    RJ_ACTION_LOG_ONLY              = 6,
} RjActionType;

typedef struct {
    uint32_t id;                   /* unique action ID */
    RjActionType action_type;
    int32_t param1;                /* step_kbps, fps_limit, scale_factor*1000, etc */
    int32_t param2;                /* reserved */
    uint32_t canary;               /* = RJ_METRIC_MAGIC (0xEEFF1234) */
} RjAction;

/* Rust tarafindan implement edilir */
extern void rj_metrics_push(const RjMetricSample* sample);
extern int  rj_command_drain(RjCommand* out, int max);
extern int  rj_pipeline_status(void);
extern void rj_start_monitor(void);
/* SRT baglanti kopusunu Rust event bus'a iletir; reason UTF-8, null-safe. */
extern void rj_connection_lost(const char* reason);

/* Runtime Adaptation (v0.4+) — Rust -> C++ */
/* Poll latest metrics from pipeline. Returns true if available. */
__declspec(noinline) extern int rj_metrics_poll(RjMetricSample* out);

/* Dequeue next adaptation action (FIFO). Returns true if action available. */
__declspec(noinline) extern int rj_action_dequeue(RjAction* out);

/* Approve pending action (Co-Pilot mode). Returns true if action approved. */
__declspec(noinline) extern int rj_action_approve(uint32_t action_id);

/* Set healing mode. Returns true if successful. */
__declspec(noinline) extern int rj_set_healing_mode(RjHealingMode mode);

/* Get current healing mode. Returns RjHealingMode. */
__declspec(noinline) extern int rj_get_healing_mode(void);

/* Reload rules from file (async). Returns true if reload requested. */
__declspec(noinline) extern int rj_reload_rules(const char* path);

/* Bu dosyadan export edilir */
uint32_t rj_ffi_version(void);

#ifdef __cplusplus
}
#endif