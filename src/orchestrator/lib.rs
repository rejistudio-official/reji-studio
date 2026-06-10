/// Reji Studio Orchestrator — Rust FFI boundary
///
/// Bu dosya referans tiplerini ve test yardımcılarını içerir.
/// Canonical FFI implementasyonu: src/orchestrator/src/ffi.rs

pub mod rule_engine;
pub mod metrics;

pub use rule_engine::{RuleEngine, Action, ActionType, Rule, Metrics as RuleMetrics};
pub use metrics::AdaptationDecider;

/// FFI tip bildirimleri — implementasyon src/orchestrator/src/ffi.rs'te
#[allow(non_snake_case)]
pub mod ffi {
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
        CoPilot   = 1,
        Assist    = 2,
        Manual    = 3,
    }

    #[repr(C)]
    pub enum RjActionType {
        BitrateReduce     = 0,
        BitrateRecover    = 1,
        ScaleResolution   = 2,
        RestoreResolution = 3,
        CapFps            = 4,
        RestoreFps        = 5,
        LogOnly           = 6,
    }

    #[repr(C)]
    pub struct RjAction {
        pub id:          u32,
        pub action_type: RjActionType,
        pub param1:      i32,
        pub param2:      i32,
        pub canary:      u32,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::rule_engine::{RuleEngine, Metrics as RuleMetrics};

    #[test]
    fn test_healing_mode_filtering() {
        let metrics = RuleMetrics {
            frame_drop_pct:    20,
            gpu_temp_c:        90,
            cpu_temp_c:        75,
            memory_usage_pct:  85,
            cpu_load_pct:      70,
            network_rtt_ms:   100,
            network_loss_pct:   5,
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

        let mut decider = AdaptationDecider::new(100);
        let (approved, _pending) = decider.filter_by_mode(actions);

        assert_eq!(approved.len(), 2, "AutoPilot should approve all actions");
    }
}
