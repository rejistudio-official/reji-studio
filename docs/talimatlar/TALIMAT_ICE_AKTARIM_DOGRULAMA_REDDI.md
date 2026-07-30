# TALİMAT: Kural Seti İçe Aktarım — Geçerli Dosyanın Yanlışlıkla Reddedilmesi

**Kaynak:** Kullanıcının canlı GUI testi (kopyalama-hatası düzeltmesinden
sonra, commit c99f1b6). İlerleme var — hata artık "Doğrulama için
kopyalanamadı" değil, içerik doğrulama aşamasına taşındı: "Seçilen
dosya geçersiz — mevcut kurallar korunuyor. Beklenen format: geçerli
JSON, her kuralda 'id', 'condition', 'action' alanları."

**Kritik anomali:** Reddedilen dosya, uygulamanın kendisinin Dışa
Aktar ile ürettiği bir dosya — yani içeriğinin geçerli olması garanti
olmalı (motorun o anki çalışan kural setinden geliyor). Buna rağmen
içe aktarım reddediyor.

---

## Görevin Özü

Kod yazmadan önce (Faz 0):

1. Dışa aktarılan gerçek dosyanın şemasını incele — rules.json'ın
   (veya dışa aktarılan kopyanın) tam yapısını çıkar. Düz bir dizi mi
   ([{...}, {...}]), yoksa sarmalanmış bir obje mi ({"rules": [...],
   "hysteresis_ms": ..., "default_mode": ...})? Önceki turlarda
   (RuleFileJson, Özellik #5) ikinci ihtimal daha olasıydı — teyit et.
2. İçe aktarımın doğrulama mantığını bul — "id/condition/action"
   alanlarını kontrol eden kodu izle. Bu kontrol dosyanın doğru
   seviyesinde mi çalışıyor (sarmalayıcı objenin içindeki rules
   dizisine mi bakıyor, yoksa yanlışlıkla en üst seviyede düz bir dizi
   mi arıyor)?
3. Bu doğrulamanın rj_reload_rules'tan farklı, ayrı bir ön-kontrol
   olup olmadığını netleştir — eğer öyleyse, bu ön-kontrol muhtemelen
   yanlış şema varsayıyor ve gereksiz/hatalı. rj_reload_rules'ın
   kendisi zaten (hot-reload turunda kanıtlanmış) güvenilir bir
   doğrulama yapıyor — mümkünse bu ön-kontrolü kaldırıp doğrudan
   rj_reload_rules'a güvenmek daha sağlam bir çözüm olabilir mi,
   değerlendir.
4. Gerçek dışa aktarılan dosyayı (veya benzerini) bu doğrulama
   mantığından elle/kod-izleme yoluyla geçirip tam olarak hangi
   koşulda false/reddedildiğini kanıtla.

Faz 0 çıktısı: Şema uyuşmazlığının kesin noktası + önerilen
düzeltme (muhtemelen doğrulama kodunun doğru seviyeye bakması, ya da
gereksiz bir ön-kontrolün kaldırılması). Onaya sun — düzeltme küçükse
aynı turda uygulanabilir.

---

## Sabit Kurallar

- Kod yazmadan önce Faz 0'ı tamamla, bulguyu raporla.
- Bu, içe aktarımın hâlâ tamamen kullanılamaz olduğu anlamına geliyor
  (geçerli dosyalar bile reddediliyor) — öncelik yüksek.
- Düzeltme küçükse onay sonrası aynı turda uygulanabilir — CLAUDE.md
  Bölüm 8b'ye göre dal kararı ver.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Düzeltmeden sonra kullanıcının round-trip senaryosunu (dışa aktar →
  aynı dosyayı içe aktar) yeniden denemesi gerekecek — raporda hatırlat.
