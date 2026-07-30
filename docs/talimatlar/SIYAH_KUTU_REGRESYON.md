# Sprint 1 Sonrası Yeni Görsel Regresyon: Sahneler Panelinde Periyodik Siyah Kutu

**Kaynak:** Kullanıcının canlı GUI doğrulaması (L1/L5 düzeltmelerinin
merge-öncesi testi sırasında fark edildi). Profil uygulama akışı
başarıyla çalışıyor ("Profil uygulandı: Stabilite (6000 kbps · 30
fps)" durum çubuğunda doğru görünüyor) — bu ayrı, yeni bir sorun.

**Gözlem:**
- Konum: Sahneler panelinin başlığı ("Sahneler") ile ilk sahne
  girdisi ("Sahne 1") arasında.
- Davranış: Her ~5 saniyede bir siyah bir kutu beliriyor.
- Etkileşim: Tıklama/üzerine gelme hiçbir şey yapmıyor — yalnızca
  görsel.
- Zamanlama: Bu, önceki turlarda hiç yoktu — Sprint 1
  düzeltmelerinden (L1-L6, özellikle L5'in init-akışı birleştirmesi)
  sonra ortaya çıktı.

---

## Görevin Özü

1. Şüpheli birinci aday — L5'in init birleştirmesi: wireUpPipeline/
   startFrameThread'e taşınan kurulum kodunu incele — Sahneler
   panelinin (muhtemelen QListWidget + başlık) doldurulması/render
   edilmesiyle ilgili bir sıralama değişikliği olabilir mi? Özellikle:
   ctor'da önceden farklı bir sırada çalışan bir şey, artık
   initPipeline içinde farklı bir zamanlama ile mi tetikleniyor?
2. ~5 saniyelik periyot ipucu: Kod tabanında ~5000ms/5s civarında
   çalışan bir QTimer ara (metrics refresh, sahne önizleme thumbnail
   güncellemesi, WS durum yenilemesi vb.). Bu, Sprint 1'de dokunulan
   bir alanla (L2'nin WS broadcast/source_id filtrelemesi, ya da
   başka bir periyodik UI güncellemesi) çakışıyor mu?
3. Sahneler panelinin render zincirini izle: Panel başlığı ile liste
   arasında boyanan bir şey var mı (örn. bir thumbnail/preview
   widget'ı, henüz doldurulmamış bir QPixmap)? Siyah bir kutu,
   genelde ya boyanmamış/sıfırlanmış bir buffer'ı ya da yanlış
   boyutlu bir widget'ı işaret eder.
4. Sprint 1'in altı commit'ini (L1-L6) tek tek git diff ile bu
   panelle ilgili herhangi bir dosyaya (main_window.cpp/.h,
   sahne/scene ile ilgili UI kodu) dokunup dokunmadığını kontrol et —
   şüpheyi daraltmak için.

Faz 0 çıktısı: Kök neden + hangi Sprint 1 commit'inin bunu
tetiklediği. Onaya sun.

---

## Sabit Kurallar

- feat/v10-sprint1 dalı üzerine ek düzeltme commit'i olarak ekle —
  dal henüz merge edilmedi.
- Bu görsel bir regresyon, işlevsel bir kırılma bildirilmedi — yine de
  merge öncesi kapatılmalı (temiz bir baseline hedefi).
- Düzeltme sonrası kullanıcının GUI'de birkaç dakika gözlemleyip
  siyah kutunun bir daha çıkmadığını teyit etmesi gerekecek.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Kök neden bulunamazsa (nadiren olur), dürüstçe "tam neden
  belirlenemedi, X hipotezi en olası ama doğrulanamadı" diye raporla
  — tahmin etmeden "düzeltildi" deme.
