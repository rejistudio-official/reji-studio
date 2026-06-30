use reji_orchestrator::rules::{
    eval_condition, resolve_conflicts, Action, ActionType, Rule, RuleEngine, RuleMetrics,
};
use std::collections::HashMap;

// Integration test binary'si C++ pipeline'ı link edemez.
// ffi.rs, cfg(not(test)) ile extern "C" { fn rj_ws_command } tanımlar;
// bu stub linker'ın sembolü çözmesini sağlar.
#[no_mangle]
pub unsafe extern "C" fn rj_ws_command(_handle: u64, _cmd: i32) {}

#[test]
fn test_or_condition() {
    let metrics = RuleMetrics { cpu_load_pct: 90, gpu_load_pct: 10, ..Default::default() };
    assert!(eval_condition("cpu_load_pct > 80 || gpu_load_pct > 85", &metrics));
}

#[test]
fn test_and_condition() {
    let metrics = RuleMetrics { cpu_load_pct: 90, frame_drop_pct: 2, ..Default::default() };
    assert!(eval_condition("cpu_load_pct > 80 && frame_drop_pct > 1", &metrics));
}

#[test]
fn test_mixed_or_and() {
    // || daha düşük önceliğe sahip: "(cpu>80 && mem>50) || gpu>85"
    // cpu=50 && mem=30 → false; gpu=90 > 85 → true; OR → true
    let metrics = RuleMetrics {
        cpu_load_pct: 50,
        gpu_load_pct: 90,
        memory_usage_pct: 30,
        ..Default::default()
    };
    assert!(eval_condition(
        "cpu_load_pct > 80 && memory_usage_pct > 50 || gpu_load_pct > 85",
        &metrics
    ));
}

#[test]
fn test_hysteresis_blocks_rapid_retrigger() {
    let mut params = HashMap::new();
    params.insert("step_kbps".to_string(), serde_json::json!(500));

    let rules = vec![Rule {
        id: "cpu_high".to_string(),
        description: String::new(),
        condition: "cpu_load_pct > 80".to_string(),
        action: "bitrate_reduce".to_string(),
        params,
        modes: vec!["auto-pilot".to_string()],
    }];

    // 5 saniyelik hysteresis — art arda iki evaluate() çağrısında ikincisi bloklanmalı
    let engine = RuleEngine::new_test(rules, 5_000);
    let metrics = RuleMetrics { cpu_load_pct: 90, ..Default::default() };

    let first = engine.evaluate(&metrics, "auto-pilot").unwrap();
    assert_eq!(first.len(), 1, "ilk evaluate kuralı tetiklemeli");

    let second = engine.evaluate(&metrics, "auto-pilot").unwrap();
    assert_eq!(second.len(), 0, "hysteresis süresi dolmadan ikinci evaluate bloklanmalı");
}

#[test]
fn test_conflict_resolution_reduce_wins() {
    let actions = vec![
        Action { action_type: ActionType::BitrateReduce, ..Default::default() },
        Action { action_type: ActionType::BitrateRecover, ..Default::default() },
    ];
    let resolved = resolve_conflicts(actions);
    assert_eq!(resolved.len(), 1);
    assert_eq!(resolved[0].action_type, ActionType::BitrateReduce);
}
