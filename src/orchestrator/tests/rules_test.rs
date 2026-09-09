use reji_orchestrator::rules::{
    eval_condition, resolve_conflicts, Action, ActionType, Rule, RuleEngine, RuleMetrics,
};
use std::collections::HashMap;

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
fn evaluate_skips_broken_rule_and_runs_valid_ones() {
    // P2-1: bozuk kural (bilinmeyen action) tüm tick'i düşürmemeli — atlanmalı,
    // listedeki geçerli kurallar çalışmaya devam etmeli. Bozuk kural kasten
    // İLKTE: eski davranışta (`?` yayılımı) sonraki geçerli kuralı da götürürdü.
    let mut params = HashMap::new();
    params.insert("step_kbps".to_string(), serde_json::json!(500));

    let rules = vec![
        Rule {
            id: "broken".to_string(),
            description: String::new(),
            condition: "cpu_load_pct > 80".to_string(),
            action: "does_not_exist".to_string(),
            params: HashMap::new(),
            modes: vec!["auto-pilot".to_string()],
        },
        Rule {
            id: "valid".to_string(),
            description: String::new(),
            condition: "cpu_load_pct > 80".to_string(),
            action: "bitrate_reduce".to_string(),
            params,
            modes: vec!["auto-pilot".to_string()],
        },
    ];

    let engine = RuleEngine::new_test(rules, 0);
    let metrics = RuleMetrics { cpu_load_pct: 90, ..Default::default() };

    let actions = engine
        .evaluate(&metrics, "auto-pilot")
        .expect("bozuk kural evaluate'i Err'e düşürmemeli");
    assert_eq!(actions.len(), 1, "geçerli kural yine de aksiyon üretmeli");
    assert_eq!(actions[0].rule_id, "valid");
    assert_eq!(actions[0].action_type, ActionType::BitrateReduce);
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
