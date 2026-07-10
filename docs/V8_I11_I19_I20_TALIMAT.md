# Claude Code Talimatı — V8/I11+I19+I20: Healing Mode Semantiği + Action Queue Tutarlılığı

`.claude/skills/ffi-safety-review/SKILL.md` yüklü (enum/ABI tutarlılığı kuralları
doğrudan ilgili).

**Kaynak:** V8 I11, I19, I20 — hepsi `healing.rs` bölgesinde, I1 çalışmasının
tazeliğinde ele alınıyor.

## Bu talimatı yazarken bulunan YENİ bir bulgu (kritik)

`evaluate_rule_engine()` (I1'de eklendi) **I20'nin bug'ını miras almış** —
`self.mode.load()` (donmuş, constructor'da set edilen alan) okuyor,
`evaluate_predictive()`'in kullandığı doğru kalıp olan
`crate::ffi::HEALING_MODE.load(Ordering::Relaxed)` (canlı, global atomic) değil.
Yani I1'in kendisi taze bir I20 örneği ekledi — bu talimat onu da kapsıyor.

## I19'un gerçek yönü netleşti — "3'e küçült" DEĞİL, "Rust'ı 4'e genişlet"

`settings_dialog.cpp` incelendi — UI **4 anlamlı farklı seçenek** sunuyor:
```cpp
d_->combo_mode->addItem(tr("Assist (Kritik otomatik, diğerleri log)"), ...);
d_->combo_mode->addItem(tr("Manual (Tüm adaptasyon kapalı)"), ...);
```
`Assist` hâlâ kritik aksiyonları otomatik yapıyor, `Manual` HİÇBİR şeyi otomatik
yapmıyor — bunlar gerçekten farklı davranışlar. Ama Rust'ın `HealingMode` enum'u
(`healing.rs`) sadece 3 varyant: `AutoPilot`/`CoPilot`/`ManualAssist` — `Assist`
ve `Manual`'ı **tek varyanta sıkıştırıyor**. Sonuç: `Manual` seçen bir kullanıcı
muhtemelen sessizce `Assist` davranışı alıyor (hâlâ kritik aksiyonlar otomatik
çalışıyor) — **gerçek, kullanıcıya görünen bir fonksiyonel hata**, sadece
kozmetik/teknik borç değil.

## Yapılacaklar

### 1. `HealingMode` enum'unu 4 varyanta genişlet

```rust
// healing.rs
pub enum HealingMode {
    AutoPilot,
    CoPilot,
    Assist,   // YENİ — kritik aksiyonlar otomatik, diğerleri log
    Manual,   // YENİ — hiçbir otomatik aksiyon yok
}
```
`ManualAssist` adını kullanan TÜM yerleri bul (`self.mode`, testler, match
kolları) ve güncelle. Bu bir ABI kırılması değil (sadece Rust-içi enum,
FFI sınırında zaten `u32` olarak geçiyor) ama **her `match HealingMode::...`
ifadesinin yeni varyantı ele aldığından emin ol** (derleyici zaten
exhaustive-match kontrolüyle bunu zorlayacak — derleme hatalarını takip et).

### 2. `u32` → `HealingMode` dönüşümünü tek yerden yap

Şu an `HEALING_MODE` (global `AtomicU32`, 0-3) ile `HealingMode` enum'u arasında
DOĞRUDAN bir dönüşüm fonksiyonu yok — `self.mode` sadece `subscribe()`'a
parametre olarak geçiyor, sonra hiç güncellenmiyor (I20'nin tam kendisi).
Ekle:
```rust
impl HealingMode {
    fn from_raw(v: u32) -> Self {
        match v {
            0 => HealingMode::AutoPilot,
            1 => HealingMode::CoPilot,
            2 => HealingMode::Assist,
            3 => HealingMode::Manual,
            _ => HealingMode::CoPilot,  // güvenli varsayılan, rj_set_healing_mode
                                         // zaten >3'ü reddediyor ama savunmacı olsun
        }
    }
}
```

### 3. `evaluate_adaptive()` VE `evaluate_rule_engine()`'i canlı moda bağla

İkisi de artık `self.mode.load()` yerine:
```rust
let mode = HealingMode::from_raw(crate::ffi::HEALING_MODE.load(Ordering::Relaxed));
```
kullanmalı. `self.mode: AtomicCell<HealingMode>` alanının kendisi artık gerekli
mi kontrol et — eğer hiçbir yerde okunmuyorsa (sadece `subscribe()`'da set
ediliyorsa) tamamen KALDIR, ölü state bırakma (I30'daki gibi bir "dead field"
durumuna düşürme).

**`evaluate_adaptive()`'in davranış değişikliği:** Artık `Assist` modunda da
çalışacak (önceden sadece `AutoPilot`'ta çalışıyordu, `ManualAssist` her ikisini
kapsadığı için `Manual` de yanlışlıkla dahildi). Yeni mantık:
```rust
fn evaluate_adaptive(&self) {
    let mode = HealingMode::from_raw(crate::ffi::HEALING_MODE.load(Ordering::Relaxed));
    match mode {
        HealingMode::AutoPilot => { /* tüm adaptif aksiyonlar */ }
        HealingMode::Assist => { /* SADECE kritik olanlar — UI metnine göre */ }
        HealingMode::CoPilot | HealingMode::Manual => return,
    }
    // ... mevcut gövde ...
}
```
**"Kritik" aksiyonun tanımını netleştir** — `rules.rs`'teki `Action::is_critical`
alanı var mı (V8'in I29 taslağında "require_approval/log_only/is_critical"
alanlarından bahsedilmişti)? Varsa `Assist` modunda sadece `is_critical=true`
aksiyonları çalıştır, gerisini logla. Yoksa bu netleştirmeyi bana raporla,
ben karar vereyim (bu, UI metninin verdiği söz — "kritik otomatik, diğerleri
log" — ile kodun gerçekte ne yaptığı arasında bir sözleşme, hafife alma).

### 4. `collect_rule_actions()`'ın `mode_str` eşlemesini güncelle

```rust
let mode_str = match mode {
    HealingMode::AutoPilot => "auto-pilot",
    HealingMode::CoPilot   => "co-pilot",
    HealingMode::Assist    => "assist",
    HealingMode::Manual    => "manual",  // rules.json.template'te bu string
                                          // gerçekten var mı, yoksa Manual modda
                                          // RuleEngine'i hiç çağırmamak mı daha
                                          // doğru (zaten "tüm adaptasyon kapalı"
                                          // sözü veriliyor)? Şablonu kontrol et.
};
```
**Manual modda `RuleEngine::evaluate()`'i hiç çağırmamak** muhtemelen daha
doğru — "tüm adaptasyon kapalı" sözü, kuralların değerlendirilip sonra
görmezden gelinmesinden çok, hiç değerlendirilmemesini gerektirir. Karar senin,
gerekçesini SESSION_NOTES'a yaz.

### 5. I11 — Çift Consumer Race

Önce araştır: `command_router.cpp`'nin `action_thread_main()` (100ms poll) ile
`main_window.cpp`'nin kendi poll'u (200ms) — **hangisi gerçekten
`HealingOverlay`'e (CoPilot onay UI'sı) bağlı?** Muhtemelen UI-taraflı poll
(Qt widget güncellemesi gerektiği için). Eğer öyleyse:
- C++ `action_thread_main()`'in action-queue tüketimi muhtemelen **artık gereksiz**
  (I1 öncesi kuyruk zaten hep boştu, şimdi doluyor ama UI zaten kendi başına
  tüketiyor) — kaldırılabilir.
- Eğer ikisinin de gerçek, farklı bir amacı varsa (örn. biri log'lama biri UI
  güncelleme, ikisi de aynı öğeyi TÜKETMEDEN sadece PEEK ediyorsa çakışma
  olmayabilir — `rj_action_dequeue`'nun gerçekten "tüket" mi "gözetle" mi
  olduğunu kontrol et) bulguyu bana raporla, birlikte karar verelim.

**Varsayılan karar (araştırma başka bir şey göstermezse):** Gereksiz olan
tüketiciyi kaldır, `enqueue_action` → tek tüketici → `HealingOverlay` net
zincirini kur.

## Test

- Yeni `HealingMode` varyantlarını kapsayan testler: `from_raw()` round-trip,
  `evaluate_adaptive()`'in `Assist` modunda sadece kritik aksiyonları
  çalıştırdığını (veya net kararına göre ne yapıyorsa onu) doğrulayan test.
- `collect_rule_actions()`'ın Manual modda (kararına göre) hiç aksiyon
  üretmediğini veya doğru `mode_str` ile çağrıldığını doğrulayan test.
- I11 düzeltmesi kod değişikliği içeriyorsa: manuel GUI testi (CoPilot modunda
  bir aksiyon tetikleyip `HealingOverlay`'de gerçekten göründüğünü doğrula —
  bu muhtemelen sizin elinizde, otonom yapılamaz, ne gerektiğini net belirtin).
- Regresyon: mevcut tüm `healing.rs`/`rules.rs` testleri PASS.

## Doğrulama Checklist

- [ ] `HealingMode` 4 varyanta genişletildi, tüm `match` kolları güncellendi
- [ ] `from_raw()` eklendi, tek dönüşüm noktası
- [ ] `evaluate_adaptive()` + `evaluate_rule_engine()` canlı `HEALING_MODE` okuyor
- [ ] `self.mode` alanı (artık gereksizse) kaldırıldı
- [ ] "Kritik aksiyon" tanımı netleştirildi (kod var/yok, karar + gerekçe)
- [ ] Manual modda RuleEngine çağrılıp çağrılmayacağı kararlaştırıldı + gerekçeli
- [ ] I11 araştırması yapıldı, sonuca göre gereksiz tüketici kaldırıldı VEYA
      neden ikisinin de gerekli olduğu raporlandı
- [ ] Yeni testler PASS, regresyon yok
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'de I11/I19/I20 işaretlendi
- [ ] `docs/SESSION_NOTES.md`'ye özet — özellikle "Manual artık gerçekten
      Assist'ten farklı davranıyor" gibi kullanıcıya görünen davranış
      değişikliğini AÇIKÇA belirt (bu bir bugfix ama kullanıcı gözlemleyebilir)
- [ ] Commit(ler) — mantıksal olarak bölünebilir (enum genişletme + mode
      okuma düzeltmesi bir commit, I11 ayrı bir commit olabilir)
- [ ] Push yapma — özet raporla, onay bekle

## Sınır

`rj_action_approve` stub'ı (I33) bu talimatın kapsamı DIŞINDA — CoPilot'ın
onay UI akışı ayrı bir iş, burada sadece hangi aksiyonların kuyruğa GİRDİĞİ
ve hangi tüketicinin onları OKUDUĞU ele alınıyor.
