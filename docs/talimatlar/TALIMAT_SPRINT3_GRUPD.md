# TALİMAT: Sprint 3-4 / Grup D — I15 (Hot-Path Metrics Push) + I18 (Wasapi Katman İhlali)

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I15, I18), Sprint 3-4 Faz 0 ön-triyajı (onaylı)
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Grup A ve Grup B tamamlanıp push edildi. Bu, Sprint 3-4'ün **en riskli**
grubu — Faz 0 ön-triyajında ikisi de "(a) orta" risk sınıflandırıldı, ama
"orta" burada Grup A/B'den (trivial/sertleştirme) daha ciddi bir eşik: I15
gerçek zamanlı encode/preview akışının çalıştığı hot-path'e dokunuyor, I18
self-healing'in girdi sinyallerinden birine (`rj_connection_lost`) dokunuyor.
İkisinde de **davranışsal eşdeğerlik** (refactor öncesi/sonrası aynı sonucu
üretmesi) davranışsal iyileştirmeden daha kritik.

- **I15 — `rj_metrics_push` hot-path'te alloc/format/broadcast:**
  `ffi.rs:467` — her push çağrısında `unsafe { *sample }` deref + `Mutex`
  kilidi + `write!` ile JSON formatlama + `clone` + broadcast **inline**
  çalışıyor. Bu, capture/encode thread'inin (veya push'u tetikleyen thread'in)
  her metrik örneğinde gereksiz iş yapması demek. Var olan 16ms'lik arka
  plan drainer'ı (I14'te haritalanmıştı) şu an JSON/broadcast işini
  **yapmıyor** — I15'in hedefi bu işi drainer'a taşımak.
- **I18 — wasapi katman ihlali:** `wasapi_capture.cpp:368/402/567` —
  capture katmanı `::rj_connection_lost` (×2) ve `::rj_metrics_push`'ı
  **doğrudan** FFI üzerinden çağırıyor, aradaki soyutlama katmanını
  atlıyor (Faz 2'deki `ITransport` soyutlaması veya `MetricsSubsystem`'in
  diğer alt sistemlerde izlediği desenin aksine).

---

## Faz 0-tamamlayıcı — Kesin Haritalama (implementasyondan önce, zorunlu)

Ön-triyaj bu iki maddeyi doğruladı ama tam çözüm tasarımı için yeterli
derinlikte değildi. Faz 1'e geçmeden önce şunlar netleşmeli:

**I15 için:**
1. `rj_metrics_push`'ın **tüm çağıranlarını** find-references ile bul
   (MetricsSubsystem, wasapi, srt_output — I18 ile kesişim burada).
   Her çağıranın push'tan beklediği **senkron** bir garanti var mı (örn.
   çağıran, push'un JSON broadcast'i bitirmesini bekliyor mu, yoksa
   yalnız veriyi teslim edip devam mı ediyor)?
2. Drainer'ın (16ms) şu an tam olarak neyi tükettiğini (ring'den ne
   okuyor, `MetricState`'e ne yazıyor) I14'teki bulgudan teyit et — I15'in
   hedefi bu drainer'a JSON/broadcast sorumluluğunu eklemek mi, yoksa
   ayrı bir mekanizma mı gerekiyor?
3. Mutex'in neyi koruduğunu belirle (paylaşılan broadcast kanalı mı,
   yoksa `MetricSample` state'i mi) — taşıma sonrası bu kilidin hot-path'te
   hâlâ gerekip gerekmediğini değerlendir.

**I18 için:**
1. `wasapi_capture.cpp`'nin 3 çağrı noktasının (368/402/567) her birinde
   **hangi koşulda** tetiklendiğini belirle (cihaz kaybı, buffer hatası,
   periyodik metrik mi?).
2. Projede zaten var olan soyutlama desenlerini incele (Faz 2'deki
   `ITransport`, ya da `OutputSubsystem`'in `ITransport` kullanma şekli)
   — I18'in çözümü bu desenle tutarlı olmalı, yeni bir mimari icat etme.
3. `rj_connection_lost` çağrısının self-healing/recovery zincirine (
   `MediaEvent::SourceDisconnected` → reconnect tetikleme, I10 tartışmasında
   haritalanmıştı) bağlı olduğunu unutma — soyutlama katmanı eklerken bu
   sinyalin **zamanlaması ve içeriği** birebir korunmalı, aksi halde
   recovery davranışını sessizce bozabilirsin (I19 deseninin tersi: burada
   çalışan bir mekanizmayı bozma riski var).

**Çıktı:** Bu haritalamanın sonucu (özellikle "senkron garanti var mı",
"hangi soyutlama deseni uygun") onaya sunulacak, sonra Faz 1 tasarımına
geçilecek.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

**I15 tasarımı:**
- Hot-path'te yalnız ne kalmalı: muhtemelen `unsafe { *sample }` deref +
  ring'e (veya benzer lock-free yapıya) minimal kopyalama. JSON
  formatlama/clone/broadcast drainer'ın 16ms döngüsüne taşınmalı.
- Eğer bazı çağıranlar senkron garanti bekliyorsa (Faz 0-tamamlayıcı
  bulgusuna göre), bu değişikliğin onlar için görünür bir davranış
  değişikliği olup olmadığını belirt — sessizce asenkronlaştırma riskli
  olabilir.
- Performans iddiası **ölçümle** desteklenecek (Faz 3'te detaylandırılıyor)
  — "daha hızlı olmalı" yeterli değil.

**I18 tasarımı:**
- Faz 0-tamamlayıcının bulduğu mevcut deseni (örn. `ITransport` tarzı arayüz)
  wasapi capture'a uyarla — örn. bir `ICaptureEventSink` (veya projeye uygun
  isim) arayüzü, wasapi bu arayüze bağımlı olur, gerçek FFI çağrısı üst
  katmanda yapılır.
- **Davranışsal eşdeğerlik şartı:** Yeni soyutlama üzerinden giden
  `rj_connection_lost`/`rj_metrics_push` çağrılarının eski koddakiyle
  aynı parametrelerle, aynı sırayla, aynı koşullarda tetiklendiği
  kanıtlanmalı — bu bir "iyileştirme" değil "yapısal temizlik" görevi,
  self-healing davranışı bir bit bile değişmemeli.

## Faz 2 — İmplementasyon (küçük commit'ler)

Önerilen sıra (Faz 0-tamamlayıcı/Faz 1 bulgusuna göre uyarlanabilir):
1. I18 önce (daha yalıtık, I15'in senkron-garanti sorusunu etkilemez) —
   soyutlama arayüzü + wasapi'nin buna geçirilmesi + davranışsal
   eşdeğerlik testi.
2. I15 — hot-path'in inceltilmesi + drainer'a taşınan iş + gerekiyorsa
   Mutex'in kaldırılması/değiştirilmesi.
3. Dokümantasyon: V8 planı güncellemesi, SESSION_NOTES.

(Sıra tersine çevrilebilir eğer Faz 0-tamamlayıcı I15'in I18'den bağımsız
olduğunu ve önce yapılmasının daha güvenli olduğunu gösterirse — gerekçeyle
öner.)

## Faz 3 — Test ve Dürüstlük Sınırları (bu grupta özellikle sıkı)

- **I15 performans iddiası:** Değişiklik öncesi/sonrası ölçüm
  (mikro-benchmark veya mevcut test altyapısında ölçülebilir bir gösterge —
  örn. push çağrısının süresi, hot-path'teki alloc sayısı). "İyileşti"
  iddiası sayısal veriyle raporlanacak, sezgiyle değil.
- **I15/I18 regresyon:** `PipelineCharacterization` testi (davranış
  taban çizgisini koruma amaçlı, daha önceki serilerde bu rol için
  kullanıldı) mutlaka PASS olmalı — bu grupta özellikle kritik, çünkü
  hem hot-path hem recovery sinyali dokunuluyor.
- **I18 davranışsal eşdeğerlik testi:** Mümkünse wasapi'nin 3 tetikleme
  koşulunu (cihaz kaybı, buffer hatası, periyodik metrik) simüle eden
  bir test — yeni soyutlama üzerinden giden çağrının eskisiyle aynı
  sonucu ürettiğini doğrula. Simülasyon pratik değilse kod
  incelemesiyle (çağrı imzası/sırası birebir eşleşme) doğrula ve
  bunu açıkça belirt.
- Kullanıcıya görünen davranış değişikliği **beklenmiyor** (bu bir iç
  refactor) — ama I15'te asenkron hale gelen bir yol varsa (Faz 0-tamamlayıcı
  bulgusu), bunu açıkça listele; beklenmedik bir gecikme/gözlem farkı
  ortaya çıkarsa raporla.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; Grup D tamamlanıp doğrulanınca push öncesi
  onay (Sprint 3-4'ün alt-grup bazlı push disiplini).
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık — bu grupta özellikle I18'in
  davranışsal eşdeğerlik iddiası için önemli.
- Faz 0-tamamlayıcı veya Faz 1 bulguları bu talimatın varsayımlarıyla
  çelişirse implementasyona geçmeden dur, raporla (kurulu proje deseni).
- Bu grup, diğerlerinden farklı olarak **"davranış hiç değişmemeli"**
  hedefi taşıyor — I8/I10/I33'teki gibi bilinçli davranış değişikliği
  değil, sessiz regresyon riski en yüksek grup. Şüphe anında dur.
