/// Runtime Adaptation Rule Engine (v0.4+)
/// Evaluates metrics against JSON/TOML rules to generate adaptation actions.

use serde::{Deserialize, Serialize};
use std::error::Error;
use std::fs;
use std::path::Path;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Rule {
    pub id: String,
    pub description: Option<String>,
    pub condition: String,  // e.g., "frame_drop_pct > 10 && hysteresis_ms > 10000"
    pub action: String,     // e.g., "bitrate_reduce", "scale_resolution"
    pub params: std::collections::HashMap<String, serde_json::Value>,
    pub modes: Option<Vec<String>>,  // ["auto-pilot", "co-pilot", "assist"]
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RulesConfig {
    pub metadata: Option<std::collections::HashMap<String, serde_json::Value>>,
    pub rules: Vec<Rule>,
    pub hysteresis_ms: Option<u64>,
    pub default_mode: Option<String>,
}

#[derive(Debug, Clone)]
pub enum ActionType {
    BitrateReduce { step_kbps: i32 },
    BitrateRecover { step_kbps: i32 },
    ScaleResolution { scale_factor: f32 },
    RestoreResolution,
    CapFps { fps_limit: u32 },
    RestoreFps,
    LogOnly { message: String },
}

#[derive(Debug, Clone)]
pub struct Action {
    pub id: u32,
    pub action_type: ActionType,
    pub timestamp_us: u64,
    pub require_approval: bool,
    pub log_only: bool,
    pub rule_id: String,
}

#[derive(Debug)]
pub struct Metrics {
    pub frame_drop_pct: u32,
    pub gpu_temp_c: i16,
    pub cpu_temp_c: i16,
    pub memory_usage_pct: u32,
    pub cpu_load_pct: u32,
    pub network_rtt_ms: u16,
    pub network_loss_pct: u8,
}

pub struct RuleEngine {
    rules: Vec<Rule>,
    hysteresis_ms: u64,
    config_path: Option<String>,
}

impl RuleEngine {
    /// Create a new RuleEngine (empty, no rules)
    pub fn new() -> Self {
        Self {
            rules: Vec::new(),
            hysteresis_ms: 10000,  // default 10 seconds
            config_path: None,
        }
    }

    /// Load rules from JSON file
    pub fn from_json_file<P: AsRef<Path>>(path: P) -> Result<Self, Box<dyn Error>> {
        let path = path.as_ref();
        let content = fs::read_to_string(path)?;
        let config: RulesConfig = serde_json::from_str(&content)?;

        let mut engine = Self::new();
        engine.rules = config.rules;
        engine.hysteresis_ms = config.hysteresis_ms.unwrap_or(10000);
        engine.config_path = Some(path.to_string_lossy().to_string());

        Ok(engine)
    }

    /// Load rules from TOML file
    pub fn from_toml_file<P: AsRef<Path>>(path: P) -> Result<Self, Box<dyn Error>> {
        let path = path.as_ref();
        let content = fs::read_to_string(path)?;
        let config: RulesConfig = toml::from_str(&content)?;

        let mut engine = Self::new();
        engine.rules = config.rules;
        engine.hysteresis_ms = config.hysteresis_ms.unwrap_or(10000);
        engine.config_path = Some(path.to_string_lossy().to_string());

        Ok(engine)
    }

    /// Load rules from JSON string
    pub fn from_json_string(json: &str) -> Result<Self, Box<dyn Error>> {
        let config: RulesConfig = serde_json::from_str(json)?;

        let mut engine = Self::new();
        engine.rules = config.rules;
        engine.hysteresis_ms = config.hysteresis_ms.unwrap_or(10000);

        Ok(engine)
    }

    /// Evaluate rules against metrics
    /// Returns list of triggered actions
    pub fn evaluate(&self, metrics: &Metrics) -> Vec<Action> {
        let mut actions = Vec::new();
        let mut action_id = 1u32;

        for rule in &self.rules {
            if self.eval_condition(&rule.condition, metrics) {
                if let Some(action) = self.create_action(&rule, action_id) {
                    actions.push(action);
                    action_id += 1;
                }
            }
        }

        actions
    }

    /// Evaluate condition string against metrics
    /// Supports: &&, ||, >, <, >=, <=, == operators
    /// Examples: "frame_drop_pct > 10 && gpu_temp_c > 85"
    ///           "frame_drop_pct < 5 || cpu_load_pct < 50"
    fn eval_condition(&self, condition: &str, metrics: &Metrics) -> bool {
        // Split by logical operators (|| and &&)
        if let Some(pos) = condition.find("||") {
            let left = &condition[..pos];
            let right = &condition[pos + 2..];
            return self.eval_condition(left, metrics) || self.eval_condition(right, metrics);
        }

        if let Some(pos) = condition.find("&&") {
            let left = &condition[..pos];
            let right = &condition[pos + 2..];
            return self.eval_condition(left, metrics) && self.eval_condition(right, metrics);
        }

        // Single comparison: "metric op value"
        self.eval_single_comparison(condition, metrics)
    }

    /// Evaluate a single comparison: "frame_drop_pct > 10"
    fn eval_single_comparison(&self, condition: &str, metrics: &Metrics) -> bool {
        let cond = condition.trim();

        // Try each operator in order (longest first to avoid ">=" matching ">")
        for (op, metric_name) in &[
            (">=", "frame_drop_pct"),
            ("<=", "frame_drop_pct"),
            (">", "frame_drop_pct"),
            ("<", "frame_drop_pct"),
            (">=", "gpu_temp_c"),
            ("<=", "gpu_temp_c"),
            (">", "gpu_temp_c"),
            ("<", "gpu_temp_c"),
            (">=", "cpu_temp_c"),
            ("<=", "cpu_temp_c"),
            (">", "cpu_temp_c"),
            ("<", "cpu_temp_c"),
            (">=", "cpu_load_pct"),
            ("<=", "cpu_load_pct"),
            (">", "cpu_load_pct"),
            ("<", "cpu_load_pct"),
            (">=", "memory_usage_pct"),
            ("<=", "memory_usage_pct"),
            (">", "memory_usage_pct"),
            ("<", "memory_usage_pct"),
        ] {
            let pattern = format!("{}{}",metric_name, op);
            if cond.contains(&pattern) {
                if let Some(pos) = cond.find(&pattern) {
                    let value_str = cond[pos + pattern.len()..].trim();

                    // Parse the threshold value
                    if let Ok(threshold) = value_str.parse::<i32>() {
                        return self.compare_metric(
                            *metric_name,
                            threshold,
                            *op,
                            metrics,
                        );
                    }
                }
            }
        }

        // Condition not recognized
        false
    }

    /// Compare a metric against a threshold using the given operator
    fn compare_metric(
        &self,
        metric_name: &str,
        threshold: i32,
        operator: &str,
        metrics: &Metrics,
    ) -> bool {
        let metric_value = match metric_name {
            "frame_drop_pct" => metrics.frame_drop_pct as i32,
            "gpu_temp_c" => metrics.gpu_temp_c as i32,
            "cpu_temp_c" => metrics.cpu_temp_c as i32,
            "cpu_load_pct" => metrics.cpu_load_pct as i32,
            "memory_usage_pct" => metrics.memory_usage_pct as i32,
            "network_rtt_ms" => metrics.network_rtt_ms as i32,
            "network_loss_pct" => metrics.network_loss_pct as i32,
            _ => return false,
        };

        match operator {
            ">" => metric_value > threshold,
            "<" => metric_value < threshold,
            ">=" => metric_value >= threshold,
            "<=" => metric_value <= threshold,
            "==" => metric_value == threshold,
            _ => false,
        }
    }

    /// Create action from rule
    fn create_action(&self, rule: &Rule, action_id: u32) -> Option<Action> {
        let action_type = match rule.action.as_str() {
            "bitrate_reduce" => {
                let step = rule
                    .params
                    .get("step_kbps")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(500) as i32;
                ActionType::BitrateReduce { step_kbps: step }
            }
            "bitrate_recover" => {
                let step = rule
                    .params
                    .get("step_kbps")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(250) as i32;
                ActionType::BitrateRecover { step_kbps: step }
            }
            "scale_resolution" => {
                let scale = rule
                    .params
                    .get("scale_factor")
                    .and_then(|v| v.as_f64())
                    .unwrap_or(0.5) as f32;
                ActionType::ScaleResolution {
                    scale_factor: scale,
                }
            }
            "restore_resolution" => ActionType::RestoreResolution,
            "cap_fps" => {
                let fps = rule
                    .params
                    .get("fps_limit")
                    .and_then(|v| v.as_i64())
                    .unwrap_or(30) as u32;
                ActionType::CapFps { fps_limit: fps }
            }
            "restore_fps" => ActionType::RestoreFps,
            "log_only" => {
                let msg = rule
                    .params
                    .get("message")
                    .and_then(|v| v.as_str())
                    .unwrap_or("")
                    .to_string();
                ActionType::LogOnly { message: msg }
            }
            _ => return None,
        };

        Some(Action {
            id: action_id,
            action_type,
            timestamp_us: 0,  // TODO: get current timestamp
            require_approval: false,
            log_only: false,
            rule_id: rule.id.clone(),
        })
    }

    /// Hot-reload rules from file (if configured)
    pub fn hot_reload(&mut self) -> Result<(), Box<dyn Error>> {
        if let Some(ref path) = self.config_path {
            let content = fs::read_to_string(path)?;
            let config: RulesConfig = serde_json::from_str(&content)?;

            self.rules = config.rules;
            self.hysteresis_ms = config.hysteresis_ms.unwrap_or(10000);
        }

        Ok(())
    }

    /// Get hysteresis duration in milliseconds
    pub fn hysteresis_ms(&self) -> u64 {
        self.hysteresis_ms
    }

    /// Set hysteresis duration
    pub fn set_hysteresis_ms(&mut self, ms: u64) {
        self.hysteresis_ms = ms;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_eval_frame_drop_gt() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 12,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct > 10", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct > 15", &metrics));
    }

    #[test]
    fn test_eval_gpu_temp_gt() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 2,
            gpu_temp_c: 87,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("gpu_temp_c > 85", &metrics));
        assert!(!engine.eval_condition("gpu_temp_c > 90", &metrics));
    }

    #[test]
    fn test_eval_frame_drop_lt() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 3,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct < 5", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct < 2", &metrics));
    }

    #[test]
    fn test_create_action_bitrate_reduce() {
        let json = r#"
        {
          "rules": [
            {
              "id": "test_reduce",
              "action": "bitrate_reduce",
              "condition": "frame_drop_pct > 10",
              "params": {"step_kbps": 500},
              "modes": ["auto-pilot"]
            }
          ]
        }
        "#;

        let engine = RuleEngine::from_json_string(json).unwrap();
        let metrics = Metrics {
            frame_drop_pct: 12,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        let actions = engine.evaluate(&metrics);
        assert_eq!(actions.len(), 1);
        assert_eq!(actions[0].rule_id, "test_reduce");
    }

    #[test]
    fn test_eval_condition_with_and() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 12,
            gpu_temp_c: 87,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct > 10 && gpu_temp_c > 85", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct > 10 && gpu_temp_c > 90", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct > 15 && gpu_temp_c > 85", &metrics));
    }

    #[test]
    fn test_eval_condition_with_or() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 12,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct > 10 || gpu_temp_c > 85", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct > 15 || gpu_temp_c > 85", &metrics));
    }

    #[test]
    fn test_eval_condition_gte() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 10,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct >= 10", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct >= 11", &metrics));
    }

    #[test]
    fn test_eval_condition_lte() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 5,
            gpu_temp_c: 65,
            cpu_temp_c: 55,
            memory_usage_pct: 50,
            cpu_load_pct: 60,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        assert!(engine.eval_condition("frame_drop_pct <= 5", &metrics));
        assert!(!engine.eval_condition("frame_drop_pct <= 4", &metrics));
    }

    #[test]
    fn test_eval_condition_complex() {
        let engine = RuleEngine::new();
        let metrics = Metrics {
            frame_drop_pct: 12,
            gpu_temp_c: 87,
            cpu_temp_c: 70,
            memory_usage_pct: 85,
            cpu_load_pct: 95,
            network_rtt_ms: 20,
            network_loss_pct: 0,
        };

        // Multiple conditions
        assert!(engine.eval_condition(
            "frame_drop_pct > 10 && gpu_temp_c > 85 && cpu_load_pct > 90",
            &metrics
        ));

        // Mixed && and ||
        assert!(engine.eval_condition(
            "frame_drop_pct < 5 || gpu_temp_c > 85 && cpu_load_pct > 90",
            &metrics
        ));
    }
}
