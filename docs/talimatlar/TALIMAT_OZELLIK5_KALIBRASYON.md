# TALİMAT: Özellik #5 — Kalibre Edilmiş Temel Çizgi (Statik Eşikler Yerine)

**Kaynak:** `docs/ROADMAP.md`, "Gelecek Özellikler — Sinerjik Değerlendirme",
madde 5 (madde 3'ün — SQLite healing-log — üzerine, artık kalıcılık
altyapısı hazır).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

Bu, altı özelliğin **en mimari-müdahaleci olanı**. J9/J10/Healing Plumbing
serisinde tekrar tekrar aynı bug sınıfıyla karşılaştık: `rules.json`'daki
kör sabit eşikler (`gpu_temp_c < 70` gibi), bir metrik kaynağı stub
çıkınca (0 dönünce) tüm mantığı bozuyor. Bu özellik, sistemin donanıma
özgü normal aralığı öğrenip eşikleri buna göre kalibre etmesini hedefliyor.

**Bu özellik üç önceki özellikle doğrudan etkileşiyor — hiçbiri
görmezden gelinemez:**

1. **Özellik #1 (CoPilot açıklaması):** Kullanıcıya "eşik 85°C" gösteriyoruz.
   Kalibrasyon bu eşiği dinamik hale getirirse, açıklama **kalibre
   edilmiş** değeri göstermeli — `rules.json`'daki ham değeri değil.
   Aksi halde açıklama yanlış/bayat bilgi gösterir, tam da Özellik #1'in
   önlemeye çalıştığı güven aşınmasını yeniden yaratır.
2. **Healing Plumbing (termal guard):** `gpu_temp_c==0` iken
   `gpu_thermal_restore` kuralının atlanması kararını hatırla. Kalibrasyon
   **bu korumayı geçersiz kılmamalı** — bir metrik sürekli sabit/stub ise,
   kalibrasyon bunu "öğrenilmiş normal değer" sanıp sessizce yanlış bir
   temel çizgi öğrenmemeli. Bu, kalibrasyonun kendisinin yeniden
   ürettiği bir tehlike olabilir — dikkatle tasarlanmalı.
3. **Özellik #3 (SQLite healing-log):** Bu log yalnızca **kural
   tetiklendiğinde** yazıyor — kalibrasyonun ihtiyaç duyduğu "normal,
   anomali-öncesi" davranış örneklemesi için yeterli olmayabilir (Faz
   0'da netleştirilecek).

**Öneri (Faz 1'de kesinleşecek):** Bu özelliği **MVP kapsamında dar
başlat** — tüm 8 metriği aynı anda kalibre etmeye çalışmak yerine, 1-2
metrikle (örn. yalnızca gerçek/stub-olmayan metrikler) modeli kanıtla,
sonra genişlet. Bu, talimatın öngördüğü değil, Faz 1'de önerilecek bir
kapsam daraltması — Faz 0 bulgusuna göre değerlendirilsin.

---

## Faz 0 — Mevcut Mimariyi Çıkar (kod yazmadan, zorunlu)

1. **`MetricState`'in geçmiş tutup tutmadığını** kontrol et (I14) — şu an
   yalnızca en güncel snapshot mu tutuluyor, yoksa bir pencere/geçmiş var
   mı? Kalibrasyon, "ilk N dakika" boyunca ham örnekleri toplamak için
   muhtemelen yeni bir rolling-window buffer'a ihtiyaç duyacak — bunun
   `MetricState`'e mi yoksa ayrı bir modüle mi ait olması gerektiğini
   değerlendir.
2. **`healing_log`'un (Özellik #3) kalibrasyon için yeterli veri kaynağı
   olup olmadığını** netleştir — yalnızca kural tetiklendiğinde
   yazıyorsa, "normal" davranışı örneklemek için yetersizdir (kurallar
   zaten anomali durumunda tetikleniyor). Muhtemelen ayrı, periyodik bir
   ham-örnekleme mekanizması gerekiyor.
3. **Hangi metriklerin gerçek, hangilerinin stub olduğunu** teyit et
   (Healing Plumbing'den hatırla: GPU-termal stub, RAM gerçek — diğer
   6 metriğin güncel durumunu kontrol et). Yalnızca gerçek metrikler
   kalibre edilebilir; stub olanlar için kalibrasyon anlamsızdır ve
   **açıkça "kalibre edilemez" olarak işaretlenmeli**, sessizce
   atlanmamalı (kullanıcı görünürlüğü — Healing Plumbing'in dersini
   burada da uygula).
4. **`rules.json` şemasını incele** — eşikler şu an mutlak değerler
   (`gpu_temp_c > 85`). Kalibrasyon bu değerleri nasıl etkileyecek: (a)
   `rules.json`'a göreli bir eşik formatı eklemek (şema değişikliği,
   kullanıcı-görünür) mü, yoksa (b) `RuleEngine::evaluate`'in mutlak
   değeri şeffaf bir kalibrasyon ofseti ile ayarlaması mı (şema değişmez,
   ama "eşik" kavramının anlamı örtük değişir)? İkisinin de kullanıcı
   deneyimi ve Özellik #1'in açıklama doğruluğu üzerindeki etkisini
   değerlendir.
5. **Kalıcılık:** Kalibre edilmiş temel çizginin oturumlar arası
   saklanıp saklanmayacağı — `paths.rs` deseniyle mi (I21), yoksa
   `healing_log`'un kendisinden mi türetilecek? Donanım/ortam değişirse
   (farklı dock, farklı yük) bayat kalibrasyon riski nasıl ele alınacak.

6. **(Tasarım İlkesi kontrolü)** `docs/ROADMAP.md`'deki "Tasarım İlkesi —
   Tek Merkezi Boru Hattı, Çok Tüketici" notunu oku. Bu özelliğin kendisi
   zaten bu boru hattının bir tüketicisi (Faz 0 madde 1-2), ama ayrıca:
   eğer `rules.json` şemasını değiştirme kararı verirsen (madde 4'teki
   soru), bunun Farklılaşma Stratejisi sütun 3'ünü (paylaşılabilir kural
   setleri) nasıl etkilediğini bir paragrafla değerlendir — bir kural
   seti paylaşılırken donanıma özgü kalibrasyon verisi de mi taşınmalı,
   yoksa yalnızca kural mantığı mı? Bu, şimdi karar verilecek bir şey
   değil, yalnızca ileride çelişki yaratmayacak şekilde kayda geçirilsin.

**Faz 0 çıktısı:** Yukarıdaki altı sorunun cevabı + önerilen MVP kapsamı
(hangi metrik(ler) ile başlanacağı) + üç özellik-etkileşiminin nasıl ele
alınacağına dair ilk değerlendirme + kural-paylaşımı çapraz-etkisi notu.
Onaya sun — bu özellik için Faz 0 raporu diğerlerinden daha kapsamlı
olabilir, acele etme.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

Faz 0 bulgusuna göre şekillenecek, ama karar noktaları:

1. **MVP kapsamı:** Hangi metrik(ler) ile başlanacak (öneri: yalnız
   gerçek/stub-olmayan metrikler, örn. `MemoryUsagePct`, `FrameDropPct`).
2. **Kalibrasyon algoritması:** Basit olmalı — örn. ilk N dakika
   örneklerinin ortalama+standart sapması, eşik = ortalama + k×sigma.
   Karmaşık istatistiksel modeller (ML vb.) bu turun kapsamı dışında
   (YAGNI).
3. **Kalibrasyon penceresi:** Kaç dakika, o süre boyunca hangi eşikler
   kullanılacak (statik varsayılanlar mı, kalibrasyon bitene kadar
   kural devre dışı mı)?
4. **Stub-metrik davranışı (kritik, atlanmasın):** Sabit/değişmeyen bir
   metrik tespit edilirse (örn. N örneğin hepsi aynı değer), kalibrasyon
   o metrik için **iptal edilmeli** ve statik varsayılana (veya Healing
   Plumbing'in termal guard'ına benzer bir "atla" davranışına) düşülmeli
   — kullanıcıya bu durumun görünür olması (log veya UI notu).
5. **Özellik #1 entegrasyonu:** Açıklama gösteriminin kalibre edilmiş
   eşiği yansıtması için gereken değişiklik (muhtemelen küçük — açıklama
   zaten `threshold_value`'yu event'ten okuyor, kaynağı `rules.json`'dan
   kalibre edilmiş değere kaydırmak yeterli olabilir).
6. **Kalıcılık formatı ve bayatlık politikası.**

Tasarımı onaya sun — bu özellik için normalden daha ayrıntılı bir onay
turu bekleniyor, kapsamı büyük.

## Faz 2 — İmplementasyon (küçük commit'ler, MVP kapsamında)

Faz 1 onayına göre uyarlanacak. Genel ilke: MVP'yi bitirip kullanıcı
gözlemiyle doğrulamadan tüm metriklere genişletme — bu talimatın kapsamı
yalnızca MVP.

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim testleri: kalibrasyon algoritmasının sentetik veri setleriyle
  doğru ortalama/eşik ürettiğini doğrula; sabit-değer (stub) senaryosunda
  kalibrasyonun doğru şekilde iptal edildiğini doğrula.
- Entegrasyon: kalibre edilmiş eşiğin gerçekten `RuleEngine::evaluate`'e
  ve Özellik #1'in açıklamasına doğru yansıdığını doğrula.
- Regresyon: mevcut healing/rules testleri PASS kalmalı; kalibrasyon
  penceresi sırasında sistemin statik varsayılanlarla güvenli çalıştığını
  doğrula.
- **GUI/gerçek donanım gözlemi kullanıcıda kalacak** — kalibrasyonun
  gerçek bir laptop üzerinde makul değerler öğrendiğinin gözlemi zor
  simüle edilir, dürüstçe belirt.
- Kullanıcıya görünen davranış değişikliği: eşikler artık dinamik —
  bunu ROADMAP'te ve kullanıcıya sunulan raporda açıkça listele.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0/1 bulguları bu talimatın varsayımlarıyla çelişirse (özellikle
  MVP kapsamı veya üç özellik-etkileşiminin karmaşıklığı beklenenden
  büyükse) dur, kapsamı yeniden değerlendirip raporla — bu özellik,
  aşırı iddialı bir ilk turdan çok, dikkatli bir MVP'yi hak ediyor.
