# TALİMAT: Özellik #3 — SQLite Healing-Log

**Kaynak:** `docs/ROADMAP.md`, "Gelecek Özellikler — Sinerjik Değerlendirme",
madde 3 (madde 5'in — kalibre edilmiş eşikler — ön koşulu).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Özellik #1 (yerel GUI açıklaması) ve Özellik #2 Aşama A (WS'e yayın) ile
her healing kararının **anlık** olarak neden verildiği artık görünür.
Bu özellik aynı bilgiyi **kalıcı** hale getiriyor — her healing kararını
(hangi kural, hangi metrik değeri, hangi aksiyon, ne zaman, sonuç) SQLite'a
kaydetmek. J10/Healing Plumbing turlarında "bu kasıtlı mı tutarsız mı"
sorusunu yalnızca kod okuyarak çözmek zorunda kalmıştık — kalıcı bir log,
bu tür soruları bir SQL sorgusuna indirger.

**Mimari fırsat:** Özellik #1 ve #2, healing'in tüm üretim noktalarını
(`rules.rs::create_action`, `healing.rs::RoutedAction`, `ffi.rs`'teki
`enqueue_ui_event`/`enqueue_pending`/`sweep_expired_pending`/
`clear_pending_on_mode_change`, `rj_set_healing_mode`) zaten haritaladı ve
bu noktalara ikişer fan-out (UI queue, VendorEvent) ekledi. Bu özellik
**üçüncü bir fan-out** — aynı noktalara, yeni bir keşif turu gerekmeden.

---

## Faz 0 — Mevcut Mimariyi ve Kısıtları Çıkar (kod yazmadan, zorunlu)

1. **Üretim noktalarını teyit et:** Özellik #1/#2'nin haritaladığı
   noktaların güncel `master`'a karşı hâlâ doğru olduğunu doğrula (bayat
   olabilir, iki özellik de bu noktalara zaten dokundu — kod değişmiş
   olabilir).
2. **SQLite bağımlılığı:** `rusqlite` (veya benzeri) crate'in projede
   zaten kullanılıp kullanılmadığını kontrol et. Yeni bir bağımlılıksa,
   I8'deki `sha2`/`base64`/`getrandom` eklerinde uygulanan gerekçelendirme
   standardını uygula (minimal, tek-amaçlı, denetlenmiş).
3. **Dosya konumu:** I21'de kurulan `paths.rs` modülünü (`%LOCALAPPDATA%\
   reji-studio\`) incele — log veritabanı dosyası bu modülü genişleterek
   aynı dizine mi konmalı (tutarlılık, "yeni mekanizma icat etme")?
4. **Threading/hot-path riski (kritik, J8 dersi):** Healing kararları
   `RuleEngine`'in ~1s periyodik tick'inde üretiliyor — bu, frame thread
   kadar sık değil ama yine de blocking bir disk I/O'nun bu tick'i
   geciktirip geciktirmeyeceği değerlendirilmeli. SQLite yazma senkron
   mu olmalı (basit ama riskli) yoksa J8'deki gibi ayrı bir yazma
   thread'i/kanalı mı gerekiyor (I15'in 16ms drainer deseni de bir
   emsal)?
5. **Şema tasarımı için ileriye dönük not:** Madde 5 (kalibrasyon) bu
   veriyi tüketecek — şemanın hangi sorguları (zaman aralığına göre
   metrik dağılımı gibi) desteklemesi gerektiğini şimdiden düşün, ama
   madde 5'in tasarımını bu turda yapma (kapsam sınırı, aşağıda).
6. **Retention/büyüme riski:** Sınırsız büyüyen bir log dosyası gerçek
   bir disk-alanı riski taşır — mevcut projede benzer bir "sınırsız
   büyüme" endişesi var mı (örn. log dosyaları için bir emsal, I21'de
   kurulmuş olabilir) kontrol et.

**Faz 0 çıktısı:** Bağımlılık kararı + dosya konumu + threading modeli +
şema taslağı + retention yaklaşımı. Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

**Kapsam sınırı (net, aşılmasın):** Bu turun hedefi yalnızca **yazma**
— healing kararlarının güvenilir şekilde kaydedilmesi. Şunlar bu turun
kapsamı DIŞINDA:
- Madde 5'in kalibrasyon mantığı (bu veriyi nasıl kullanacağı ayrı bir
  tasarım).
- Bu log'u sorgulamak için yeni bir UI/WS yüzeyi (istenirse ayrı özellik).
- Eski/mevcut healing geçmişinin geriye dönük import edilmesi (yok
  zaten, gerek yok).

Tasarımda netleşecekler:
1. **Şema:** timestamp, rule_id, metric_id, current_value,
   threshold_value, action_type, outcome (applied/pending/rejected/
   invalidated), mode. Faz 0 bulgusuna göre ek/eksik alan değerlendir.
2. **Yazma modeli:** Senkron mu (basit, ama tick'i geciktirme riski) yoksa
   batch/async mı (J8/I15 deseninin bir uzantısı — periyodik flush)?
   Öneri: J8'in kurduğu emsali izle (ayrı, hafif bir yazma mekanizması),
   ama gerçek tick süresi ölçülmeden karar verilmesin — Faz 0'da mümkünse
   senkron yazmanın gerçek maliyetini (tek `INSERT`'in tipik SSD'de kaç
   ms sürdüğü) tahmin et, belki senkron yeterli kalacak kadar ucuzdur
   (aşırı mühendislik riski — J8'in kendisi de "gerçekten gerekli mi"
   sorusunu sormuştu, burada da sor).
3. **Retention:** Basit bir üst sınır (örn. son N gün veya son N kayıt,
   periyodik `DELETE`) — karmaşık bir rotasyon sistemi değil.
4. **Hata toleransı:** SQLite yazma başarısız olursa (disk dolu, izin
   sorunu) healing'in kendisi asla etkilenmemeli — I10'un "yut ama sesli
   logla" ilkesi burada da geçerli.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler)

Örnek iskelet (Faz 1 onayına göre uyarlanacak):
1. Bağımlılık + şema + migration (ilk kurulum).
2. Yazma mekanizması (senkron veya batch, Faz 1 kararına göre) +
   üretim noktalarına fan-out (üçüncü, mevcut ikisinin yanına).
3. Retention/pruning mantığı.
4. Dokümantasyon: `ROADMAP.md` madde 3 → "implemente edildi",
   `SESSION_NOTES.md`, gerekirse yeni bir `docs/HEALING_LOG_SCHEMA.md`
   (şema referansı, madde 5 için hazır bir başlangıç noktası olsun).

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim testleri: healing kararının doğru şema ile kaydedildiğini
  doğrula (sentetik kural + metrik).
- Yazma hatası senaryosu (disk/izin simülasyonu mümkünse) → healing
  akışının bozulmadığını doğrula.
- Retention testi: eski kayıtların gerçekten temizlendiğini doğrula.
- Performans notu: senkron yazma seçildiyse, tick süresine eklenen
  gecikmeyi ölç ve raporla (sezgiyle değil, ölçümle — K2/J8 disiplini).
- Regresyon: mevcut healing/rules testleri PASS kalmalı.
- Kullanıcıya görünen davranış değişikliği yok (arka planda log
  tutuluyor) — bunu açıkça belirt.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları bu talimatın varsayımlarıyla çelişirse (özellikle
  threading modeli veya bağımlılık gerekliliği konusunda) dur, raporla.
- Bu tur, madde 5'in (kalibrasyon) önkoşulu — ama madde 5'in kendisi
  değil. Kapsamı bilinçli olarak dar tut.
