/// Adaptation Decider — Hysteresis, mode filtering, action management (v0.4+)

use std::collections::HashMap;
use std::time::{Duration, Instant};

use crate::rule_engine::{Action, ActionType};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum HealingMode {
    AutoPilot,
    CoPilot,
    Assist,
    Manual,
}

impl HealingMode {
    pub fn from_u32(val: u32) -> Self {
        match val {
            0 => HealingMode::AutoPilot,
            1 => HealingMode::CoPilot,
            2 => HealingMode::Assist,
            3 => HealingMode::Manual,
            _ => HealingMode::AutoPilot,
        }
    }
}

#[derive(Debug, Clone)]
pub struct PendingAction {
    pub action: Action,
    pub require_approval: bool,
    pub created_at: Instant,
}

pub struct AdaptationDecider {
    last_action_time: Instant,
    hysteresis_min_ms: u64,
    current_mode: HealingMode,
    pending_actions: HashMap<u32, PendingAction>,
}

impl AdaptationDecider {
    pub fn new(hysteresis_min_ms: u64) -> Self {
        Self {
            last_action_time: Instant::now(),
            hysteresis_min_ms,
            current_mode: HealingMode::AutoPilot,
            pending_actions: HashMap::new(),
        }
    }

    /// Check if enough time has passed since last action (hysteresis)
    pub fn should_adapt(&self) -> bool {
        let elapsed = self.last_action_time.elapsed();
        elapsed >= Duration::from_millis(self.hysteresis_min_ms)
    }

    /// Record that an action was taken
    pub fn record_action(&mut self) {
        self.last_action_time = Instant::now();
    }

    /// Set the current healing mode
    pub fn set_mode(&mut self, mode: HealingMode) {
        self.current_mode = mode;
    }

    /// Get the current healing mode
    pub fn current_mode(&self) -> HealingMode {
        self.current_mode
    }

    /// Filter actions based on current healing mode
    /// Returns (approved_actions, pending_actions_for_approval)
    pub fn filter_by_mode(&mut self, actions: Vec<Action>) -> (Vec<Action>, Vec<PendingAction>) {
        let mut approved = Vec::new();
        let mut pending = Vec::new();

        match self.current_mode {
            HealingMode::AutoPilot => {
                // All actions auto-execute
                approved.extend(actions);
            }
            HealingMode::CoPilot => {
                // Critical actions auto-execute, others require approval
                for action in actions {
                    if is_critical_action(&action) {
                        approved.push(action);
                    } else {
                        pending.push(PendingAction {
                            action,
                            require_approval: true,
                            created_at: Instant::now(),
                        });
                    }
                }
            }
            HealingMode::Assist => {
                // Critical actions auto-execute, others log-only
                for mut action in actions {
                    if is_critical_action(&action) {
                        approved.push(action);
                    } else {
                        action.log_only = true;
                        approved.push(action);
                    }
                }
            }
            HealingMode::Manual => {
                // All actions suppressed (no-op)
                // Actions are discarded
            }
        }

        (approved, pending)
    }

    /// Approve a pending action (Co-Pilot mode)
    pub fn approve_action(&mut self, action_id: u32) -> Option<Action> {
        if let Some(pending) = self.pending_actions.remove(&action_id) {
            Some(pending.action)
        } else {
            None
        }
    }

    /// Reject a pending action (Co-Pilot mode)
    pub fn reject_action(&mut self, action_id: u32) -> bool {
        self.pending_actions.remove(&action_id).is_some()
    }

    /// Check for timeout on pending actions (30s default)
    /// Returns actions that timed out (should be cancelled)
    pub fn check_timeout(&mut self, timeout_ms: u64) -> Vec<u32> {
        let mut timed_out = Vec::new();

        for (&id, action) in &self.pending_actions {
            if action.created_at.elapsed() > Duration::from_millis(timeout_ms) {
                timed_out.push(id);
            }
        }

        // Remove timed out actions
        for id in &timed_out {
            self.pending_actions.remove(id);
        }

        timed_out
    }

    /// Store a pending action (for Co-Pilot approval)
    pub fn store_pending(&mut self, action: Action) {
        self.pending_actions.insert(
            action.id,
            PendingAction {
                action,
                require_approval: true,
                created_at: Instant::now(),
            },
        );
    }

    /// Get all pending actions
    pub fn pending_actions(&self) -> Vec<&PendingAction> {
        self.pending_actions.values().collect()
    }
}

/// Determine if an action is critical (should auto-execute even in some modes)
fn is_critical_action(action: &Action) -> bool {
    matches!(
        action.action_type,
        ActionType::BitrateReduce { .. } | ActionType::ScaleResolution { .. }
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_hysteresis_timing() {
        let mut decider = AdaptationDecider::new(100);  // 100ms

        // Initially should adapt
        assert!(decider.should_adapt());

        // Record action
        decider.record_action();

        // Immediately after should not adapt
        assert!(!decider.should_adapt());

        // Wait for hysteresis to pass
        std::thread::sleep(Duration::from_millis(110));

        // Now should adapt again
        assert!(decider.should_adapt());
    }

    #[test]
    fn test_mode_filtering_auto_pilot() {
        let mut decider = AdaptationDecider::new(1000);
        decider.set_mode(HealingMode::AutoPilot);

        let action = Action {
            id: 1,
            action_type: ActionType::BitrateReduce {
                step_kbps: 500,
            },
            timestamp_us: 0,
            require_approval: false,
            log_only: false,
            rule_id: "test".to_string(),
        };

        let (approved, pending) = decider.filter_by_mode(vec![action]);

        assert_eq!(approved.len(), 1);
        assert_eq!(pending.len(), 0);
    }

    #[test]
    fn test_mode_filtering_co_pilot() {
        let mut decider = AdaptationDecider::new(1000);
        decider.set_mode(HealingMode::CoPilot);

        let critical_action = Action {
            id: 1,
            action_type: ActionType::BitrateReduce {
                step_kbps: 500,
            },
            timestamp_us: 0,
            require_approval: false,
            log_only: false,
            rule_id: "critical".to_string(),
        };

        let log_action = Action {
            id: 2,
            action_type: ActionType::LogOnly {
                message: "test log".to_string(),
            },
            timestamp_us: 0,
            require_approval: false,
            log_only: false,
            rule_id: "log".to_string(),
        };

        let (approved, pending) = decider.filter_by_mode(vec![critical_action, log_action]);

        assert_eq!(approved.len(), 1);  // critical auto-approves
        assert_eq!(pending.len(), 1);   // log requires approval
    }

    #[test]
    fn test_mode_filtering_manual() {
        let mut decider = AdaptationDecider::new(1000);
        decider.set_mode(HealingMode::Manual);

        let action = Action {
            id: 1,
            action_type: ActionType::BitrateReduce {
                step_kbps: 500,
            },
            timestamp_us: 0,
            require_approval: false,
            log_only: false,
            rule_id: "test".to_string(),
        };

        let (approved, pending) = decider.filter_by_mode(vec![action]);

        assert_eq!(approved.len(), 0);
        assert_eq!(pending.len(), 0);  // all suppressed
    }

    #[test]
    fn test_pending_action_timeout() {
        let mut decider = AdaptationDecider::new(1000);
        decider.set_mode(HealingMode::CoPilot);

        let action = Action {
            id: 1,
            action_type: ActionType::BitrateReduce {
                step_kbps: 500,
            },
            timestamp_us: 0,
            require_approval: false,
            log_only: false,
            rule_id: "test".to_string(),
        };

        decider.store_pending(action);

        // Should not timeout immediately
        let timed_out = decider.check_timeout(100);
        assert!(timed_out.is_empty());

        // Wait for timeout
        std::thread::sleep(Duration::from_millis(110));

        // Now should timeout
        let timed_out = decider.check_timeout(100);
        assert_eq!(timed_out.len(), 1);
        assert_eq!(timed_out[0], 1);
    }
}
