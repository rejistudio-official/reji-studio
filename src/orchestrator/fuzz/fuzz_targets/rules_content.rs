//! Hedef 2 — tam kural dosyası içeriği (JSON + TOML + doğrulama katmanı):
//! `parse_rules_content` (bb110c4 ile `RuleEngine::new`'den çıkarıldı).
//! Girdi = ham dosya içeriği (lossy UTF-8).
//!
//! İnvariantlar (RULES_SCHEMA'daki "sessiz false" sınıfını crash'e çevirir):
//! 1. parse hiç panic etmez;
//! 2. ROUNDTRIP — parse → serialize → parse → serialize aynı JSON'u verir
//!    (yükleme kaybı/sapması yok);
//! 3. EVALUATE ASLA PANİC ETMEZ — fuzz'lanmış kurallar + fuzz'lanmış
//!    metrikler + her mod için `RuleEngine::evaluate` her zaman döner;
//! 4. `snapshot_json` çıktısı yeniden ayrıştırılabilir (UI'ın okuduğu biçim).
#![no_main]

mod common;

use libfuzzer_sys::fuzz_target;
use reji_orchestrator::rules::{parse_rules_content, Rule, RuleEngine};

fuzz_target!(|data: &[u8]| {
    common::install_panic_hook();
    let content = String::from_utf8_lossy(data);

    let parsed = match parse_rules_content(&content) {
        Ok(p) => p,
        Err(_) => return, // reddedilen içerik = beklenen sonuç
    };

    // 2. Roundtrip: serialize → parse → serialize sabit nokta olmalı.
    // Bilinen sapma (ilk 15 dk kampanyası, 2026-09-23): serde_json `float_roundtrip`
    // özelliği olmadan uzun ondalık/tamsayı literal'lerini (örn. 82 haneli
    // `fps_limit`) 1 ulp sapmayla ayrıştırır; std::parse doğru yuvarlar. Bu,
    // kural motorunun değil serde_json yapılandırmasının davranışı — float'lar
    // ulp toleransıyla karşılaştırılır, geri kalan her şey bire bir.
    let v1 = serde_json::json!({
        "hysteresis_ms": parsed.hysteresis_ms,
        "rules": parsed.rules,
    });
    let json1 = serde_json::to_string(&v1).expect("kabul edilen kural seti serileştirilebilmeli");
    let again = parse_rules_content(&json1)
        .unwrap_or_else(|e| panic!("ROUNDTRIP: kabul edilen set yeniden parse edilemedi: {e}\n{json1}"));
    let v2 = serde_json::json!({
        "hysteresis_ms": again.hysteresis_ms,
        "rules": again.rules,
    });
    assert!(
        json_equivalent(&v1, &v2),
        "ROUNDTRIP: ikinci geçiş farklı JSON üretti\n  1: {json1}\n  2: {}",
        serde_json::to_string(&v2).unwrap()
    );

    // 3. Evaluate hiç panic etmemeli — kalibre eşik + cooldown yolları dahil.
    let hysteresis = parsed.hysteresis_ms;
    let engine = RuleEngine::new_test(parsed.rules, hysteresis);
    engine.set_calibrated_threshold("cpu_load_pct", 42);
    let modes = ["auto-pilot", "co-pilot", "manual", ""];
    for m in common::metrics_from(data).iter() {
        for mode in modes {
            let _ = engine.evaluate(m, mode);
            let _ = engine.evaluate(m, mode); // hysteresis/last_trigger yolu
        }
    }
    if let Some(first) = engine_first_rule_id(&engine) {
        engine.apply_cooldown(&first, std::time::Duration::from_secs(60));
        for m in common::metrics_from(data).iter() {
            let _ = engine.evaluate(m, "auto-pilot");
        }
    }

    // 4. snapshot_json UI'ın okuduğu biçim — yeniden ayrıştırılabilir olmalı.
    let snap = engine.snapshot_json();
    let _: Vec<Rule> = serde_json::from_str(&snap)
        .unwrap_or_else(|e| panic!("snapshot_json geçerli JSON değil: {e}\n{snap}"));
});

/// Yapısal eşitlik; f64'ler ≤ 4 ulp farkla eşit sayılır (serde_json
/// `float_roundtrip`'siz ayrıştırma sapması), diğer her şey bire bir.
fn json_equivalent(a: &serde_json::Value, b: &serde_json::Value) -> bool {
    use serde_json::Value::*;
    match (a, b) {
        (Number(x), Number(y)) => {
            if x == y {
                return true;
            }
            match (x.as_f64(), y.as_f64()) {
                (Some(p), Some(q)) => {
                    if p.is_nan() || q.is_nan() || p.is_infinite() || q.is_infinite() {
                        return p.to_bits() == q.to_bits();
                    }
                    let (bp, bq) = (p.to_bits() as i64, q.to_bits() as i64);
                    (p.is_sign_negative() == q.is_sign_negative()) && (bp - bq).abs() <= 4
                }
                _ => false,
            }
        }
        (Array(xs), Array(ys)) => {
            xs.len() == ys.len() && xs.iter().zip(ys).all(|(x, y)| json_equivalent(x, y))
        }
        (Object(xs), Object(ys)) => {
            xs.len() == ys.len()
                && xs.iter().all(|(k, x)| ys.get(k).is_some_and(|y| json_equivalent(x, y)))
        }
        _ => a == b,
    }
}

fn engine_first_rule_id(engine: &RuleEngine) -> Option<String> {
    let snap = engine.snapshot_json();
    let rules: Vec<Rule> = serde_json::from_str(&snap).ok()?;
    rules.first().map(|r| r.id.clone())
}
