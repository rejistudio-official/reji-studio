# Claude Code Talimatı — V8/I1: RuleEngine'i HealingMonitor Döngüsüne Bağla

`.claude/skills/obs-ws-protocol/SKILL.md` ilgisiz; burada ilgili bir skill yok,
genel Rust FFI disiplini için `.claude/skills/ffi-safety-review/SKILL.md`'ye bak
(action_queue zaten var olan bir FFI kuyruğu, yeni fonksiyon eklemiyoruz ama
disiplini koru).

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md`, madde I1 — projenin en eski bekleyen
kritik maddesi. Fable5'in 06.07.2026 taze taramasında da bağımsız olarak
doğrulandı (5.3: "RuleEngine is loaded and hot-reloadable but never evaluated").
Bu talimat yazılırken güncel `master`'a karşı yeniden doğrulandı: `RuleEngine::
evaluate()` metodu gerçekten repo genelinde HİÇBİR yerden çağrılmıyor (grep
sonucu sıfır).

## Önemli mimari netleştirme — bu talimatı yazarken bulundu

V8'in I1'i "self-healing tamamen dead code" diyordu — bu **kısmen yanlış**.
İki paralel mekanizma var:
1. **`HealingMonitor`'un kendi `evaluate_predictive()`/`evaluate_adaptive()`
   metotları** — GERÇEKTEN ÇALIŞIYOR, `HealingEvent` üretip `healing_tx`
   kanalına gönderiyor. Sabit kodlanmış eşikler kullanıyor.
2. **`RuleEngine`** (`rules.rs`, `~/.reji/rules.json`'dan hot-reload) — kullanıcının
   kendi kurallarını tanımlayabildiği, JSON/TOML destekli, gerçekten sofistike
   bir motor — ama `evaluate()`'i hiç çağrılmadığı için TAMAMEN ölü.

Yani "self-healing hiç çalışmıyor" değil, **"kullanıcı-yapılandırılabilir kural
katmanı hiç çalışmıyor, sadece hardcoded katman çalışıyor"**. Bu talimat
`RuleEngine`'i gerçekten bağlıyor.

## Kapsam DIŞI — ayrı bir sorun, karıştırma

`rj_action_approve` (`ffi.rs`) hâlâ stub: `// TODO: Implement Co-Pilot approval`,
her zaman `1` (onaylandı) dönüyor. Bu talimat bunu DÜZELTMİYOR — sadece
`RuleEngine::evaluate()`'in ürettiği aksiyonların kuyruğa girmesini sağlıyor.
AutoPilot modunda (onay gerektirmez) bu yeterli ve anlamlı bir düzeltme;
CoPilot modunun gerçek onay akışı ayrı bir V8 maddesi olarak plana eklenmeli
(bu talimatın 5. adımı).

## Yapılacaklar

### 1. `HealingMonitor::subscribe()`'a `rule_engine` parametresi ekle

`healing.rs`:
```rust
pub struct HealingMonitor {
    // ... mevcut alanlar ...
    rule_engine: Arc<Mutex<Option<RuleEngine>>>,  // YENİ
}

impl HealingMonitor {
    pub fn subscribe(
        system_rx: Receiver<SystemEvent>,
        media_rx: Receiver<MediaEvent>,
        healing_tx: Sender<HealingEvent>,
        mode: HealingMode,
        thresholds: HealingThresholds,
        metric_state: Arc<MetricState>,
        rule_engine: Arc<Mutex<Option<RuleEngine>>>,  // YENİ parametre
    ) -> Self {
        Self {
            // ... mevcut alanlar ...
            rule_engine,
        }
    }
}
```
`use crate::rules::RuleEngine;` importunu ekle.

### 2. `on_periodic()`'e RuleEngine değerlendirmesi ekle

```rust
fn on_periodic(&mut self) {
    self.evaluate_predictive();
    self.evaluate_adaptive();
    self.evaluate_rule_engine();  // YENİ
}

fn evaluate_rule_engine(&self) {
    let Ok(guard) = self.rule_engine.lock() else {
        warn!("rule_engine mutex poisoned, skipping evaluation");
        return;
    };
    let Some(engine) = guard.as_ref() else { return; };

    let mode_str = match self.mode.load() {
        HealingMode::AutoPilot => "autopilot",
        HealingMode::CoPilot   => "copilot",
        HealingMode::Manual    => "manual",
        // enum'da başka varyant varsa (Assist?) kontrol et, eksik kalmasın
    };

    match engine.evaluate(&self.current_metrics, mode_str) {
        Ok(actions) => {
            for action in actions {
                let rj_action = RjAction {
                    id: action.id,
                    action_type: convert_action_type(action.action_type),
                    param1: action.param1,
                    param2: action.param2,
                    canary: 0,  // mevcut RjAction kullanım kalıbını kontrol et,
                                // canary'nin gerçek anlamı/hesaplanışı varsa onu kullan
                };
                if !crate::ffi::enqueue_action(rj_action) {
                    warn!(action_id = action.id, "rule action enqueue failed, kuyruk dolu");
                }
            }
        }
        Err(e) => {
            warn!(error = %e, "rule engine evaluate() failed");
        }
    }
}
```

**`mode_str` değerleri:** `RuleEngine::evaluate()`'in `mode: &str` parametresini
`rules.json`'daki kuralların `modes` alanıyla karşılaştırdığını gördüm
(`rule.modes.contains(&mode.to_string())`) — gerçek `rules.json` şablonunda
(`docs/config/` altında olabilir, kontrol et) hangi string değerlerin
kullanıldığını DOĞRULA, yukarıdaki `"autopilot"`/`"copilot"`/`"manual"`
tahminimi köre körüne kullanma.

### 3. `ActionType` → `RjActionType` dönüşüm fonksiyonu

`rules.rs` veya `healing.rs`'e (hangisi daha uygunsa):
```rust
fn convert_action_type(a: crate::rules::ActionType) -> RjActionType {
    use crate::rules::ActionType as A;
    match a {
        A::BitrateReduce     => RjActionType::BitrateReduce,
        A::BitrateRecover    => RjActionType::BitrateRecover,
        A::ScaleResolution   => RjActionType::ScaleResolution,
        A::RestoreResolution => RjActionType::RestoreResolution,
        A::CapFps            => RjActionType::CapFps,
        A::RestoreFps        => RjActionType::RestoreFps,
        A::LogOnly           => RjActionType::LogOnly,
    }
}
```
(Enum varyantları zaten birebir eşleşiyor — mekanik bir dönüşüm, mantık yok.)

### 4. `ffi.rs`'de çağrı yerini güncelle

`rj_start_monitor_impl()`'deki `HealingMonitor::subscribe(...)` çağrısına
`rule_engine.clone()`'u ekle (state'te zaten `rule_engine: Arc<Mutex<Option<RuleEngine>>>`
alanı var, `FFI_STATE` kurulumunda oluşturuluyor — sadece `HealingMonitor::subscribe`'a
geçirilmemiş, şimdi geçir).

### 5. `docs/FABLE5_BUG_PLAN_V8.md`'ye I33 ekle (rj_action_approve stub'ı)

I1'in "CoPilot onayı sahte" kısmı bu talimatla ÇÖZÜLMÜYOR — ayrı bir madde
olarak takibe alınmalı:
```
| I33 | (I1'den ayrıştırıldı) | rj_action_approve() hâlâ stub — CoPilot modunda gerçek kullanıcı onayı yok, her zaman "1" (onaylandı) dönüyor. AutoPilot etkilenmiyor (onay gerektirmez), ama CoPilot modu yanıltıcı | ffi.rs | Yuksek | Sprint 2 |
```

## Test

- `evaluate_rule_engine()` için birim testi: sahte bir `RuleEngine` (test
  rules.json ile) + sahte `RuleMetrics` (eşiği aşan bir değer) ile, `evaluate()`
  sonrası `enqueue_action()`'ın gerçekten çağrıldığını doğrula (mock/spy ile,
  veya `action_queue`'yu test sonrası `rj_action_dequeue` ile okuyup içeriği
  kontrol ederek — mevcut `test_action_dequeue_*` testlerinin kalıbını izle).
- Hysteresis/mode filtreleme testleri: yanlış mode'da kuralın ATLANDIĞINI,
  hysteresis içindeyken tekrar tetiklenmediğini doğrula (RuleEngine'in kendi
  `last_trigger`/hysteresis mantığı zaten var, sadece gerçekten çağrıldığında
  işe yaradığını kanıtla).
- Regresyon: mevcut `test_healing_monitor_reactive_send` ve diğer tüm
  `healing.rs`/`rules.rs`/`ffi.rs` testleri PASS.

## Doğrulama Checklist

- [ ] `HealingMonitor`'a `rule_engine` alanı + parametre eklendi
- [ ] `evaluate_rule_engine()` yazıldı, `on_periodic()`'ten çağrılıyor
- [ ] `mode_str` değerleri gerçek `rules.json` şablonuyla doğrulandı (tahmin değil)
- [ ] `ActionType → RjActionType` dönüşümü yazıldı
- [ ] `ffi.rs`'deki `HealingMonitor::subscribe(...)` çağrısı güncellendi
- [ ] Yeni birim testleri PASS (enqueue gerçekten oluyor mu, mode/hysteresis filtreleme)
- [ ] Tüm eski testler PASS
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'de I1 `[DÜZELTILDI]` + I33 (approve stub) yeni madde olarak eklendi
- [ ] `docs/SESSION_NOTES.md`'ye özet — "iki paralel mekanizma" netleştirmesi dahil
- [ ] Commit: `feat(healing): V8/I1 — RuleEngine::evaluate()'i HealingMonitor döngüsüne bağla`
- [ ] Push yapma — özet raporla, onay bekle

## Sınır

`rj_action_approve` stub'ı (I33) bu talimatın kapsamı DIŞINDA. CoPilot/Manual
modunun gerçek davranışsal farkı (onay bekleme UI akışı) da kapsam dışı —
sadece AutoPilot'ta aksiyonların artık gerçekten kuyruğa girdiğini garanti
ediyoruz.
