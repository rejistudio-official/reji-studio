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

use std::sync::OnceLock;
use crossbeam::queue::ArrayQueue;

// v0.4+: Global action queue (64 capacity, lock-free)
static ACTION_QUEUE: OnceLock<ArrayQueue<ffi::RjAction>> = OnceLock::new();

fn action_queue() -> &'static ArrayQueue<ffi::RjAction> {
    ACTION_QUEUE.get_or_init(|| ArrayQueue::new(64))
}

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

// v0.4+: Convert Rust Action to FFI RjAction format
pub fn action_to_ffi(action: &Action) -> ffi::RjAction {
    use std::time::{SystemTime, UNIX_EPOCH};

    let timestamp_us = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0);

    let (action_type, param1) = match &action.action_type {
        ActionType::BitrateReduce { step_kbps } => (ffi::RjActionType::BitrateReduce, *step_kbps),
        ActionType::BitrateRecover { step_kbps } => (ffi::RjActionType::BitrateRecover, *step_kbps),
        ActionType::ScaleResolution { scale_factor } => {
            let param = (*scale_factor * 1000.0) as i32;
            (ffi::RjActionType::ScaleResolution, param)
        }
        ActionType::RestoreResolution => (ffi::RjActionType::RestoreResolution, 1000),
        ActionType::CapFps { fps_limit } => (ffi::RjActionType::CapFps, *fps_limit as i32),
        ActionType::RestoreFps => (ffi::RjActionType::RestoreFps, 60),
        ActionType::LogOnly { .. } => (ffi::RjActionType::LogOnly, 0),
    };

    ffi::RjAction {
        id: action.id,
        action_type,
        param1,
        param2: 0,
        canary: 0xEEFF1234,
    }
}

// v0.4+: Public function to enqueue actions from rule engine
pub fn enqueue_action(action: &Action) -> bool {
    let ffi_action = action_to_ffi(action);
    action_queue().push(ffi_action).is_ok()
}

// v0.4+: FFI implementation — Poll latest metrics from C++
#[no_mangle]
pub extern "C" fn rj_metrics_poll(_out: *mut ffi::RjMetricSample) -> i32 {
    // TODO: Implement metrics polling from C++ pipeline
    // For now, return 0 (no metrics available)
    // This would be called from Rust to get current metrics for rule evaluation
    0
}

// v0.4+: FFI implementation — Dequeue next action from Rust to C++
#[no_mangle]
pub extern "C" fn rj_action_dequeue(out: *mut ffi::RjAction) -> i32 {
    if out.is_null() {
        return 0;
    }

    match action_queue().pop() {
        Some(action) => {
            unsafe {
                *out = action;
            }
            1  // true: action available
        }
        None => 0,  // false: queue empty
    }
}

// v0.4+: FFI implementation stubs for other action/healing mode functions
#[no_mangle]
pub extern "C" fn rj_action_approve(_action_id: u32) -> i32 {
    // TODO: Implement Co-Pilot approval mechanism
    1
}

#[no_mangle]
pub extern "C" fn rj_set_healing_mode(_mode: ffi::RjHealingMode) -> i32 {
    // TODO: Implement healing mode setting
    1
}

#[no_mangle]
pub extern "C" fn rj_get_healing_mode() -> i32 {
    // TODO: Implement healing mode getting — default to AutoPilot
    0  // RJ_MODE_AUTO_PILOT
}

#[no_mangle]
pub extern "C" fn rj_reload_rules(_path: *const u8) -> i32 {
    // TODO: Implement hot-reload of rules
    1
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rule_engine::{RuleEngine, Metrics as RuleMetrics};

    #[test]
    fn test_action_queue_integration() {
        // Create sample metrics that should trigger rules
        let metrics = RuleMetrics {
            frame_drop_pct: 15,      // High frame drop
            gpu_temp_c: 85,          // High GPU temp
            cpu_temp_c: 70,
            memory_usage_pct: 75,
            cpu_load_pct: 60,
            network_rtt_ms: 50,
            network_loss_pct: 2,
        };

        // Create a simple rule engine with a frame drop reduction rule
        let rule_json = r#"
        {
            "rules": [
                {
                    "id": "high_frame_drop",
                    "description": "Reduce bitrate when frame drops exceed 10%",
                    "condition": "frame_drop_pct > 10",
                    "action": "bitrate_reduce",
                    "params": {"step_kbps": 500}
                }
            ],
            "hysteresis_ms": 5000
        }
        "#;

        let engine = RuleEngine::from_json_string(rule_json).expect("Failed to load rules");
        let actions = engine.evaluate(&metrics);

        assert!(!actions.is_empty(), "Rule should be triggered with frame_drop_pct=15");

        // Verify first action is bitrate reduction
        let action = &actions[0];
        assert!(matches!(action.action_type, ActionType::BitrateReduce { .. }));

        // Enqueue the action
        let success = enqueue_action(action);
        assert!(success, "Action should be enqueued successfully");

        // Dequeue the action via FFI
        let mut ffi_action = ffi::RjAction {
            id: 0,
            action_type: ffi::RjActionType::LogOnly,
            param1: 0,
            param2: 0,
            canary: 0,
        };

        let result = rj_action_dequeue(&mut ffi_action);
        assert_eq!(result, 1, "Action should be available in queue");
        assert_eq!(ffi_action.id, 1, "Action ID should match");
        assert!(matches!(ffi_action.action_type, ffi::RjActionType::BitrateReduce));

        // Queue should now be empty
        let result = rj_action_dequeue(&mut ffi_action);
        assert_eq!(result, 0, "Queue should be empty");
    }

    #[test]
    fn test_healing_mode_filtering() {
        let metrics = RuleMetrics {
            frame_drop_pct: 20,
            gpu_temp_c: 90,
            cpu_temp_c: 75,
            memory_usage_pct: 85,
            cpu_load_pct: 70,
            network_rtt_ms: 100,
            network_loss_pct: 5,
        };

        let rule_json = r#"
        {
            "rules": [
                {
                    "id": "critical_high_temp",
                    "condition": "gpu_temp_c > 85",
                    "action": "scale_resolution",
                    "params": {"scale_factor": 0.75}
                },
                {
                    "id": "medium_frame_drop",
                    "condition": "frame_drop_pct > 15",
                    "action": "bitrate_reduce",
                    "params": {"step_kbps": 500}
                }
            ]
        }
        "#;

        let engine = RuleEngine::from_json_string(rule_json).expect("Failed to load rules");
        let actions = engine.evaluate(&metrics);

        assert_eq!(actions.len(), 2, "Both rules should be triggered");

        // Test filtering with AdaptationDecider
        let mut decider = AdaptationDecider::new(100);
        let (approved, _pending) = decider.filter_by_mode(actions);

        // In AutoPilot mode, all actions should be approved
        assert_eq!(approved.len(), 2, "AutoPilot should approve all actions");
    }
}
