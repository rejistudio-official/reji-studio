# TALİMAT: Kural Seti İçe Aktarım — "Doğrulama İçin Kopyalanamadı" Hatası Araştırması

**Kaynak:** Kullanıcının canlı GUI testi — Kural Seti Paylaşımı'nın
(Farklılaşma Stratejisi Sütun 3) içe aktarım akışında, hem geçerli hem
geçersiz dosyalarla aynı hata çıkıyor: "Doğrulama için kopyalanamadı:
<dosya yolu>". Bu, beklenen "geçersiz dosya reddedilir, geçerli dosya
kabul edilir" davranışı değil — ilk adımın (geçici konuma kopyalama)
kendisi, dosya içeriğine hiç bakılmadan başarısız oluyor.

**Kritik kanıt:** Uygulamanın kendisinin ürettiği, kesinlikle geçerli
bir dosya (Dışa Aktar ile az önce kaydedilen reji-rules-2026-07-20.json)
bile içe aktarılırken aynı hatayı veriyor. Bu, içerik doğrulamasıyla
ilgisi olmayan, dosya-sistemi/kopyalama seviyesinde bir sorun olduğunu
gösteriyor.

**Test ortamı detayı:** Test klasörünün adında boşluk var
("Test Klasörü") — bu tesadüf olabilir ama olası bir ipucu, araştırılsın.

**Durum çubuğu tutarsızlığı:** Diyalog "Doğrulama için kopyalanamadı"
derken, durum çubuğu "İÇE AKTARIM REDDEDİLDİ — geçersiz dosya (JSON/alan
hatası?)" diyor — ikisi farklı hata sınıflarını işaret ediyor, bu da
hata mesajının doğru kaynaktan gelmediğini gösterebilir.

---

## Görevin Özü

Kod yazmadan önce (Faz 0), sırayla:

1. Kural Seti Paylaşımı talimatının (arşivlenmiş) içe aktarım tasarımını
   yeniden oku — geçici konuma kopyalama adımının tam olarak hangi
   Qt API'siyle (QFile::copy, QTemporaryFile vb.) ve hangi hedef
   yolla (QDir::temp() + "reji_import_XXXXXX.json") yapıldığını bul.
2. Bu kopyalama çağrısının hata durumunda ne döndürdüğünü izle —
   QFile::copy() başarısız olursa gerçek sebebi (izin, yol, disk)
   raporluyor mu, yoksa jenerik bir "kopyalanamadı" mesajına mı
   düşüyor? Eğer gerçek sebep (QFile::error()/errorString()) hiç
   loglanmıyorsa, bu bilgiyi eklemek teşhisi kolaylaştırır.
3. Boşluklu yol ihtimalini test et: Kaynak dosya yolunda boşluk
   olan bir senaryoyu (örn. C:\Test Klasörü\dosya.json) kod
   incelemesiyle veya mümkünse gerçek bir kopyalama denemesiyle izle —
   QFile::copy Qt'de boşlukları doğal olarak destekler, ama eğer
   kod bir yerde manuel path-string birleştirmesi/kabuk çağrısı
   yapıyorsa (örn. system() veya benzeri) bu sorun kaynağı olabilir.
4. Hedef (geçici) dizinin var olup olmadığını/yazılabilir olduğunu
   kontrol et — QDir::temp()'in döndürdüğü yol her zaman var olan
   bir dizin midir, yoksa alt bir klasör önceden oluşturulması mı
   gerekiyordu ve bu adım atlanmış olabilir mi?
5. Durum çubuğu mesajının kaynağını bul — "İÇE AKTARIM REDDEDİLDİ
   — geçersiz dosya (JSON/alan hatası?)" mesajının, gerçek hatayı
   (kopyalama başarısızlığı) doğru yansıtmadığını, yanlış bir hata
   koduna/dala düştüğünü teyit et.

Faz 0 çıktısı: Kopyalama başarısızlığının kök nedeni + durum
çubuğu/diyalog mesaj tutarsızlığının kaynağı. Onaya sun — düzeltme
küçükse aynı turda uygulanabilir.

---

## Sabit Kurallar

- Kod yazmadan önce Faz 0'ı tamamla, bulguyu raporla.
- Bu, Kural Seti Paylaşımı özelliğinin tüm içe aktarım işlevselliğini
  bloke eden bir regresyon — öncelik yüksek (yalnızca edge-case
  senaryoları değil, geçerli dosyalar da etkileniyor).
- Düzeltme küçükse (örn. eksik dizin oluşturma, yanlış hata dalı),
  onay sonrası aynı turda uygulanabilir — CLAUDE.md Bölüm 8b'ye göre
  dal kararı ver.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık
  olsun.
- Düzeltmeden sonra kullanıcının orijinal 8 senaryoyu (bu talimatın
  kaynağı olan test turu) yeniden denemesi gerekecek — bunu raporda
  hatırlat.
