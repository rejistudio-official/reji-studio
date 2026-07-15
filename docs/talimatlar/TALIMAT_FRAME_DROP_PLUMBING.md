# TALİMAT: frame_drop_pct Plumbing Düzeltmesi

**Kaynak:** Özellik #5'in Faz 0'ında bulunan, `rules.json`'da en çok
referans verilen metriğin üç kuralının (`frame_drop_mild`,
`frame_drop_high`, `frame_drop_recovery`) üretimde tamamen ölü olduğu
bulgusu.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Bu, Özellik #5'in `memory_usage_pct` için yaptığı düzeltmenin (commit 1,
`system_events_for_sample()`) **birebir aynı desende ikinci uygulaması**.
Talimat küçük tutuldu çünkü çözüm şablonu zaten kanıtlanmış — ama bir
**kritik farkı** var, aşağıda vurgulanıyor.

**Sorun:** `metrics_collector.cpp`'de gerçek veri (30s rolling window)
üretiliyor ve `MetricSample`'a konuyor, ama `healing.rs::handle_media`
`current_metrics.frame_drop_pct`'i hiç set etmiyor — yalnızca
`trend.recent_frame_drops` sayacını artırıyor. Sonuç:
`RuleMetrics.frame_drop_pct` üretimde daima 0, üç kural hiç
tetiklenmiyor.

**Paralel not:** `frame_drop_pct` zaten I14'ten beri `MetricState`
üzerinden UI durum barına (fps/bitrate/drop%) doğru akıyor — bu,
`MetricState`/UI tüketicisi ile `current_metrics`/`RuleEngine` tüketicisi
arasındaki **farklı bir yol** olduğu için, UI'da doğru görünmesi bu
bug'ın var olmadığı anlamına gelmiyordu. Tam da `memory_usage_pct`'te
gördüğümüz "bir tüketiciye ulaşıyor, diğerine ulaşmıyor" deseni.

---

## Faz 0 — Doğrulama (kod yazmadan, zorunlu ama kısa tutulabilir)

1. `healing.rs::handle_media`'da `FrameDropped`/ilgili event'in
   `current_metrics.frame_drop_pct`'i gerçekten set etmediğini teyit et
   (bayat olmadığından emin ol — Özellik #5'ten bu yana kod değişmiş
   olabilir).
2. **Kritik fark — `memory_usage_pct`'teki `> 0` guard'ını buraya
   kopyalama:** `memory_usage_pct` için `> 0` guard'ı mantıklıydı çünkü
   gerçek bellek kullanımının 0 olması pratikte imkânsız (0 = stub/veri
   yok sinyaliydi). **`frame_drop_pct` için bu varsayım YANLIŞ** — %0
   kare düşüşü tamamen geçerli, gerçek ve arzu edilen bir durum (sistem
   sağlıklı çalışıyor demek). `> 0` guard'ı buraya kopyalanırsa, sistem
   sağlıklıyken üretilen doğru "%0" event'leri sessizce filtrelenir —
   bu, "0 = veri yok" konvansiyonunun **yanlış bir metriğe uygulanması**
   olur. Bu ayrımı Faz 0 raporunda açıkça doğrula ve doğru guard'ı
   (muhtemelen guard'sız, veya yalnızca `NaN`/negatif gibi gerçekten
   geçersiz değerleri filtreleyen bir kontrol) belirle.
3. Üç kuralın (`frame_drop_mild`, `frame_drop_high`, `frame_drop_recovery`)
   `rules.json.template`'teki tam koşullarını incele — düzeltme
   sonrası üçünün de doğru tetiklenebilir hale geldiğini teyit edecek
   şekilde test tasarla.
4. Bu metriğin `RjMetricId` enum'unda (Özellik #1) zaten yer aldığını
   teyit et (`FrameDropPct=0`) — yeni bir metrik tipi eklemek gerekmez,
   yalnızca veri akışı düzeltiliyor.

**Faz 0 çıktısı:** Doğru guard/filtre kararı + üç kuralın düzeltme
sonrası davranışı. Onaya sun (kısa olabilir, bu küçük bir düzeltme).

## Faz 1 — Tasarım (kısa onay yeterli olabilir)

`memory_usage_pct` deseninin mirror'ı: `handle_media`'ya (veya drainer'a,
Faz 0 bulgusuna göre) `current_metrics.frame_drop_pct`'i güncelleyen bir
satır/event ekle. Guard kararını (madde 2) burada uygula.

## Faz 2 — İmplementasyon

Küçük, muhtemelen tek commit: düzeltme + testler (üç kuralın artık
tetiklenebildiğini doğrulayan sentetik senaryo).

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim/entegrasyon: sentetik `frame_drop_pct` değerleriyle üç kuralın
  (mild/high/recovery eşiklerinde) doğru tetiklendiğini doğrula —
  özellikle **%0 durumunun da doğru işlendiğini** (yanlışlıkla
  filtrelenmediğini) test et.
- Regresyon: mevcut `rules`/`healing` testleri + `PipelineCharacterization`
  PASS kalmalı.
- Kullanıcıya görünen davranış değişikliği: `frame_drop_mild/high/recovery`
  kuralları artık gerçekten tetiklenebilir hale geliyor — önceden hiç
  çalışmıyorlardı. Bunu açıkça listele, bu bir davranış değişikliği.
- Not (kapsam dışı, kayıtlı kalsın): `frame_drop_pct`'in kalibrasyona
  (Özellik #5) genişletilmesi ayrı bir gelecek iş — bu turun kapsamında
  değil.

## Sabit Kurallar (hatırlatma)

- Küçük commit; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Faz 0'da guard kararı (madde 2) talimatın önerisinden farklı çıkarsa
  (örn. gerçekten bir filtre gerektiği ortaya çıkarsa) gerekçeyle
  birlikte raporla, körü körüne "guard yok" kararını uygulama.
