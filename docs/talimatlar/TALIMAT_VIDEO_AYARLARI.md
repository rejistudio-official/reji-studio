# TALİMAT: Video Ayarları — Bitrate/Çözünürlük/FPS Manuel Kontrolü

**Kaynak:** Ayarlar Araştırma Turu, öncelik #2 — "Config alanları zaten
mevcut; yalnızca UI + init'e bağlama gerekiyor. En iyi maliyet/değer
oranı, 'Video' kategorisinin boşluğunu doldurur."
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Etkileşim Uyarısı

Settings dialog'da şu an "Video" kategorisi tamamen boş (Ayarlar
Araştırması Bölüm C'nin bulgusu). Bitrate/çözünürlük/FPS için config
alanlarının zaten var olduğu, yalnız UI'a bağlanmadığı tahmin ediliyordu
— bu Faz 0'da doğrulanmalı.

**Kritik etkileşim — atlanmasın:** Bu üç ayar, self-healing sisteminin
**referans/tavan** olarak kullandığı değerlerle doğrudan ilişkili:
- **Çözünürlük:** I23'ün NVENC `maxEncodeWidth/Height` kararı ve HP1/HP2'nin
  (Healing Plumbing) `scale_factor` mekanizması, **init anındaki
  çözünürlüğü tavan olarak** kullanıyor — healing yalnızca bu tavanın
  altına ölçekleyebiliyor. Kullanıcının çözünürlüğü değiştirmesi, bu
  tavanı da güncellemeli, yoksa healing'in referans noktası bayat kalır.
- **Bitrate:** `RJ_ACTION_BITRATE_REDUCE`/`RECOVER` aksiyonları mevcut
  bitrate'i yüzdesel olarak değiştiriyor (J10'da netleşen `0.85×`/`1.15×`
  kademeli politika). Kullanıcının "hedef bitrate" ayarı, bu kademeli
  ayarlamanın **taban değeri** olmalı — healing'in "recover" ile
  ulaşmaya çalıştığı üst sınır.
- **FPS:** Muhtemelen daha izole (healing'in FPS'e doğrudan müdahalesi
  şu an sınırlı, `frame_pacer.cpp` içinde), ama yine de doğrulanmalı.

Bu üçünü self-healing'in referans noktalarından **kopuk** bir ayar
olarak eklemek, HP1/HP2/I23'ün özenle kurduğu "tavan" mantığını bozar
— Faz 0 bu etkileşimi netleştirmeden tasarıma geçilmeyecek.

---

## Faz 0 — Doğrulama (kod yazmadan, zorunlu)

1. **Mevcut config alanlarını bul:** `SrtOutput::Config`/`RtmpTransport`
   gibi yerlerde bitrate; encoder init'te çözünürlük/FPS — bunlar şu an
   nereden geliyor (hardcoded sabit mi, `rules.json`'da bir varsayılan
   mı, yoksa gerçekten hiç yeri yok mu)?
2. **Healing referans noktalarını izle** (kritik):
   - I23/NVENC `maxEncodeWidth/Height`'ın tam olarak nerede set edildiğini
     bul — kullanıcı ayarının bu değeri güncelleyip güncellemeyeceğini
     netleştir.
   - HP1/HP2'nin `scale_factor` hesaplamasının "orijinal boyut" olarak
     neyi kullandığını (encoder init dims mi, yoksa ayrı bir sabit mi)
     teyit et.
   - Bitrate healing'in (`RuleEngine`) "taban" olarak hangi değeri
     kullandığını izle.
3. **Canlı değişim mümkün mü?** Yayın akarken bu ayarları değiştirmek
   (encoder'ı yeniden yapılandırmak) mı, yoksa yalnızca bir sonraki
   yayın başlangıcında mı etkili olacağını belirle — NVENC'in canlı
   reconfigure'u ne kadar güvenilir (J9'daki DRC semantiği tartışmasını
   hatırla).
4. Mevcut Settings dialog alanlarının (SRT host/port gibi) UI/persistence
   desenini incele (tutarlılık için).

**Faz 0 çıktısı:** Config alanlarının güncel durumu + healing referans
noktalarıyla tam etkileşim haritası + canlı-değişim mümkünlüğü. Onaya
sun — bu talimatın en kritik adımı.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

Faz 0 bulgusuna göre, ama olası eksenler:

1. **UI alanları:** Bitrate (kbps, spinbox), Çözünürlük (genişlik×yükseklik,
   iki spinbox veya yaygın ön-ayar dropdown'u + "özel" seçeneği), FPS
   (dropdown: 30/60/144 gibi yaygın değerler + varsa özel).
2. **Uygulama zamanlaması:** Faz 0'ın bulgusuna göre — muhtemelen
   "yayın başlamadan önce" uygulanır (basit, güvenli), canlı reconfigure
   ayrı ve daha riskli bir iş olarak ertelenebilir (YAGNI, J9'un DRC
   belirsizliğini hatırla).
3. **Healing referans noktası güncelleme:** Kullanıcı bir değer
   girdiğinde, bu değerin I23/HP1/HP2'nin kullandığı "tavan/taban"
   değerleri de güncellemesi gerekiyor — Faz 0'da bulunan tam noktalara
   bağlanacak, yeni bir paralel referans sistemi icat edilmeyecek.
4. **Doğrulama/sınırlar:** Makul aralıklar (örn. bitrate 500-50000 kbps,
   çözünürlük NVENC'in desteklediği aralık) — aşırı değerlerin encoder
   init'ini bozmaması için.
5. **Persistence:** `QSettings`, mevcut desenle tutarlı.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler, CLAUDE.md Bölüm 8b'ye uy)

Faz 1 onayına göre. Muhtemelen: UI alanları + persistence (1 commit),
encoder/healing referans noktası wiring (1 commit, en riskli kısım),
testler + docs (1 commit) — 2-commit eşiği aşılırsa dal aç.

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim/entegrasyon: kullanıcı ayarının encoder init'e ve healing'in
  referans noktalarına doğru yansıdığını doğrula.
- Regresyon: mevcut healing/rules testleri (özellikle HP1/HP2, I23,
  Özellik #5 kalibrasyon) PASS kalmalı — bu değişiklik onların referans
  aldığı değerlere dokunuyor, regresyon riski gerçek.
- **GUI görsel doğrulaması kullanıcıda kalacak** — gerçek bir yayının
  ayarlanan bitrate/çözünürlük/FPS ile başladığını gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: Video kategorisi artık dolu;
  healing'in davranışı (hangi değerlere göre ölçeklediği) kullanıcı
  ayarına bağlı hale geliyor — bunu açıkça belirt, healing'in "tavan"
  kavramının artık sabit değil kullanıcı-tanımlı olduğunu vurgula.

## Sabit Kurallar

- Küçük commit'ler; Bölüm 8b'ye göre dal kararı ver.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Faz 0'da healing etkileşimi beklenenden karmaşık çıkarsa (örn. birden
  fazla, tutarsız referans noktası varsa) dur, raporla — bu talimatın
  en riskli yanı bu etkileşim, aceleye getirilmesin.
