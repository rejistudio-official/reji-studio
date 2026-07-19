# TALİMAT: Kural Motoru Görünürlüğü — GUI'den Görüntüleme/Düzenleme

**Kaynak:** Ayarlar UX Madde 6, Bölüm C — P3 (en yüksek değer, en büyük
maliyet — kendi Faz 0/1'ini hak eden büyük iş).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kapsam Uyarısı

Şu an `rules.json`'daki kurallar (`RuleEngine`'in kalbi — hangi metrik
hangi eşiği aşınca hangi aksiyon) GUI'den tamamen **kör kutu.**
Kullanıcı yalnızca "Kuralları Düzenle..." ile harici bir metin
editöründe JSON'u elle değiştirebiliyor (Ayarlar Zenginleştirme #1) —
kuralları **görüntülemek** için bile GUI içinde hiçbir yol yok.

**Bu, Ses Ayarları'na benzer bir "büyük çıkabilir" riski taşıyor** —
Faz 0'da gerçek boyutu netleşmeden implementasyona geçilmeyecek.

**Mimari ilke (Ses Ayarları'ndan taşınan ders):** "Kutu önce" — önce
FFI'nın hangi veriyi hangi formatta taşıyacağı netleşsin, sonra UI
tasarlansın. Tersten gitmek (önce UI hayal et, sonra FFI'ya sıkıştır)
Ses işinde risk yaratmıştı, burada da aynı hataya düşme.

---

## Faz 0 — Kapsam Belirleme (kod yazmadan, zorunlu ve kapsamlı)

1. **`rules.json`'ın gerçek şemasını** (`RuleFileJson`, Özellik #5'ten
   hatırlanacak — `rules`, `hysteresis_ms`, `default_mode`) tam olarak
   çıkar — her kural neyi taşıyor (`id`, `condition`, `action`,
   `require_approval` vb.)?
2. **Okuma FFI'ı hiç var mı?** `rj_get_rules` gibi bir fonksiyon var mı,
   yoksa sıfırdan mı yazılacak? (Ayarlar Araştırması'nda "hiç yok, grep
   boş" bulunmuştu — bunu güncel `master`'a karşı yeniden doğrula.)
3. **Serileştirme stratejisi:** Kuralların FFI üzerinden C++ tarafına
   nasıl taşınacağı — sabit boyutlu bir `#[repr(C)]` struct dizisi mi
   (kural sayısı/alan boyutları değişken olduğundan zorlayıcı olabilir),
   yoksa JSON string'i olduğu gibi mi geçirilecek (daha basit, ama
   `ffi-safety-review`'in string-sınırda-reddet ilkesiyle gerilir —
   Özellik #1'de bu yüzden yapılandırılmış alanlar tercih edilmişti)?
   **Bu, Faz 0'ın en kritik sorusu.**
4. **Görüntüleme mi, düzenleme mi?** İki farklı büyüklük:
   - **(a) Yalnızca görüntüleme** (salt-okunur bir liste/tablo — kural
     adı, koşul, aksiyon, son ne zaman tetiklendiği belki) — orta
     boyutlu, yazma yolu gerekmez, güvenlik riski yok.
   - **(b) GUI içi düzenleme** (kural ekleme/silme/değiştirme, sonra
     `rules.json`'a yazma) — çok daha büyük, `rj_reload_rules`'ın
     yalnızca JSON yapısını doğruladığını (Sütun 3 Faz 0'ında bulunmuştu)
     hatırla — GUI'den üretilen JSON'un kural sözdizimini de
     doğrulaması gerekir, yeni bir doğrulama katmanı demek.
5. **Gerçek boyut tahmini:** (a)/(b) ayrımına göre küçük/orta/büyük
   sınıflandır — Ses Ayarları deneyiminden biliyoruz ki bu, tek bir
   talimata sığmayabilir.

**Faz 0 çıktısı:** Şema + FFI durumu + serileştirme stratejisi önerisi +
(a)/(b) kapsam kararı önerisi (muhtemelen **yalnızca (a) — görüntüleme
— MVP olarak önerilir**, düzenleme ayrı bir gelecek tura bırakılabilir,
tıpkı Ses Ayarları'nın SRT'yi ertelemesi gibi). Onaya sun — bu talimatın
en kritik adımı, acele edilmesin.

## Faz 1 — Tasarım (yalnızca Faz 0 kapsamı netleştikten sonra)

Faz 0 sonucuna göre şekillenecek. Muhtemelen (öneri, dayatma değil):

1. **MVP: yalnızca görüntüleme.** Settings dialog'un yeni bir sekmesine
   (kategori düzeni zaten sekmeli — "Kurallar" gibi bir 6. sekme
   eklenebilir) salt-okunur bir liste/tablo (`QTableWidget` veya
   benzeri) — her satır bir kural, kolonlar: ad, koşul, aksiyon.
2. **Yenileme:** Dialog açıldığında bir kerelik okuma (WS/auth görünürlüğü
   deseniyle tutarlı — YAGNI, canlı güncelleme gereksiz).
3. **Düzenleme (varsa, ayrı bir gelecek tur olarak not düşülür, bu
   talimatın kapsamına girmez).**

Tasarımı onaya sun, implementasyondan önce.

## Faz 2-3 (Faz 1 onayından sonra tanımlanacak)

Bu talimatın kapsamı Faz 0'da netleşmeden Faz 2-3 planlanmayacak.

## Sabit Kurallar

- CLAUDE.md Bölüm 8b'ye göre dal kararı ver — yeni FFI kesin var, bu
  muhtemelen dal gerektirir (kendi değerlendirmeni yap, körü körüne
  varsayma).
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- **Bu talimatın en önemli kuralı (Ses Ayarları'ndan miras):** Faz 0'da
  kapsamın büyük çıkması bir başarısızlık değil, doğru bir tespit.
  Küçük bir "görüntüleme ekle" işi bekleyip büyük bir "tam düzenleyici
  inşa et" işine rastgele girişme — dur, gerçek boyutu raporla, MVP'yi
  (muhtemelen yalnız görüntüleme) netleştir.
