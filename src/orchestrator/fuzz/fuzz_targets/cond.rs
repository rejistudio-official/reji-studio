//! Hedef 1 — koşul ifadesi parser'ı (`rules.rs`): `eval_condition`,
//! `explanation_for` ve kalibre türevleri. Girdi = ham koşul dizgesi
//! (lossy UTF-8). İnvariant: hiçbir girdi panic/stack overflow üretmez.
#![no_main]

mod common;

use libfuzzer_sys::fuzz_target;
use reji_orchestrator::rules::{
    eval_condition, eval_condition_calibrated, explanation_for, explanation_for_calibrated,
    CalibrationTable,
};

fuzz_target!(|data: &[u8]| {
    common::install_panic_hook();
    let cond = String::from_utf8_lossy(data);

    let mut calib = CalibrationTable::new();
    calib.insert("cpu_load_pct".to_string(), 42);
    calib.insert("gpu_temp_c".to_string(), -5);

    for m in common::metrics_from(data).iter() {
        let _ = eval_condition(&cond, m);
        let _ = explanation_for(&cond, m);
        let _ = eval_condition_calibrated(&cond, m, &calib);
        let _ = explanation_for_calibrated(&cond, m, &calib);
    }
});
