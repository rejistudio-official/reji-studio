/// Reji Studio Orchestrator — Rust FFI boundary
///
/// This module provides:
/// - Rule engine for runtime adaptation (v0.4+)
/// - Metrics collection and analysis
/// - Action queue management
/// - C++ FFI wrapper

pub mod rule_engine;
pub mod metrics;

pub use rule_engine::{RuleEngine, Action, ActionType, Rule, Metrics as RuleMetrics};
pub use metrics::AdaptationDecider;

/// FFI binding stubs (implemented in ffi_bridge.c)
/// These are called by the Rust side to communicate with C++
#[allow(non_snake_case)]
pub mod ffi {
    use std::os::raw::c_char;

    #[repr(C)]
    pub struct RjMetricSample {
        pub magic_head: u32,
        pub timestamp_us: u64,
        pub bitrate_kbps: u32,
        pub fps_actual: f32,
        pub cpu_percent: f32,
        pub frame_drops: u32,
        pub frame_drop_pct: u32,
        pub gpu_temp_c: i16,
        pub cpu_temp_c: i16,
        pub memory_usage_pct: u32,
        pub cpu_load_pct: u32,
        pub network_rtt_ms: u16,
        pub network_loss_pct: u8,
        pub reserved: u8,
        pub magic_tail: u32,
    }

    #[repr(C)]
    #[derive(Debug, Clone, Copy)]
    pub enum RjHealingMode {
        AutoPilot = 0,
        CoPilot = 1,
        Assist = 2,
        Manual = 3,
    }

    #[repr(C)]
    pub enum RjActionType {
        BitrateReduce = 0,
        BitrateRecover = 1,
        ScaleResolution = 2,
        RestoreResolution = 3,
        CapFps = 4,
        RestoreFps = 5,
        LogOnly = 6,
    }

    #[repr(C)]
    pub struct RjAction {
        pub id: u32,
        pub action_type: RjActionType,
        pub param1: i32,
        pub param2: i32,
        pub timestamp_us: u64,
        pub canary: u32,
    }

    extern "C" {
        pub fn rj_metrics_poll(out: *mut RjMetricSample) -> i32;
        pub fn rj_action_dequeue(out: *mut RjAction) -> i32;
        pub fn rj_action_approve(action_id: u32) -> i32;
        pub fn rj_set_healing_mode(mode: RjHealingMode) -> i32;
        pub fn rj_get_healing_mode() -> i32;
        pub fn rj_reload_rules(path: *const c_char) -> i32;
    }
}
