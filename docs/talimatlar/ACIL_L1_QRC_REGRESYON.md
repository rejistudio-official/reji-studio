# ACİL: L1 Düzeltmesi Sonrası Profil Uygulama Regresyonu (Merge Öncesi Bulundu)

**Kaynak:** Kullanıcının canlı GUI doğrulaması (merge/push öncesi
istenen doğrulama adımı). L5 düzeltmesi çalışıyor — profil önerisi
diyaloğu artık ilk açılışta doğru görünüyor. Ama "Uygula"ya basınca
yeni bir hata çıktı:

```
Doğrulama için kopyalanamadı: :/config/profiles/stability.json
(No such file or directory)
```

**Bu, L1 düzeltmesinin (rj_validate_rules + QSaveFile atomik yazım)
bir regresyonu görünüyor.** ":/" öneki Qt kaynak sistemi (qrc) yolu —
gerçek dosya sistemi API'siyle açılamaz, yalnızca QFile gibi
Qt-farkında API'lerle. "No such file or directory" hatası, kaynağın
artık ham dosya sistemi API'siyle okunmaya çalışıldığını gösteriyor.

**Bağlam:** applyProfile, writeValidatedRules'ı importRules ile
paylaşıyor (c99f1b6'da bilerek birlikte düzeltilmişti — o düzeltme
kaynağı QFile ile okuyup qrc uyumluluğunu koruyordu, "qrc kaynakları
dahil çalışır" diye raporlanmıştı). L1'in yeniden yazımı bu
uyumluluğu muhtemelen kaybetti.

---

## Görevin Özü

1. Kök nedeni bul: L1'in yeni writeValidatedRules/rj_validate_rules
   akışının kaynak dosyayı hangi API ile okuduğunu izle. c99f1b6'nın
   "kaynak QFile ile okunup içerik QTemporaryFile'a yazılıyor" deseni
   korunmuş mu, yoksa L1 bunu değiştirip qrc-farkında olmayan bir yola
   mı geçirmiş?
2. Regresyon kapsamını netleştir: Yalnızca applyProfile mi
   etkileniyor (qrc kaynaklarını kullanan tek çağıran), yoksa normal
   dosya-sistemi içe aktarımı da mı bozuldu? (İkincisi olası değil ama
   doğrula.)
3. Düzeltme: Kaynak okumanın QFile (qrc + normal dosya sistemi
   ikisini de destekleyen) üzerinden yapıldığından emin ol. Bu,
   c99f1b6'nın kazanımını geri kazandırmalı, L1'in atomik-yazma/
   yan-etkisiz-doğrulama iyileştirmelerini kaybetmeden.

## Test Planı (kritik — bu regresyon otomatik testlerden kaçmıştı)

- Yeni bir birim/entegrasyon testi ekle: qrc kaynağından (gömülü
  profil dosyası) writeValidatedRules/applyProfile akışının
  başarıyla çalıştığını doğrulayan. Bu senaryo daha önce test
  edilmemiş olmalı — regresyon bunu kanıtlıyor.
- Normal dosya sistemi içe aktarımının hâlâ çalıştığını doğrulayan
  regresyon testi de ekle (L1'in diğer testleri muhtemelen bunu zaten
  kapsıyor, teyit et).

## Sabit Kurallar

- Bu, feat/v10-sprint1 dalındaki mevcut commit'lere (özellikle L1'in
  c528b7c'si) ek bir düzeltme commit'i olmalı — dal henüz merge
  edilmedi, bu iyi bir zamanlama.
- Düzeltme sonrası kullanıcının GUI'de yeniden denemesi gerekecek:
  profil önerisi diyaloğunda "Uygula" butonu.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bu regresyonun otomatik testlerden nasıl kaçtığını (hangi test
  boşluğu) kısaca not düş — gelecekte tekrarını önlemek için.
