# Kök Düzeltme Sonrası Yeni Belirti: Kutu Artık Uygulama Açılışında Beliriyor (START Gerekmeden)

**Kaynak:** Kullanıcının canlı doğrulaması. 7665cb0'ın Rust-tarafı
deficit filtresi ("current=0 → bilinmeyen, üretme") uygulandıktan
sonra, kutu artık START'a hiç basılmadan, yalnızca uygulama açık
tutulduğunda birkaç dakika içinde beliriyor.

Bu, filtrenin "current=0" durumunu doğru ele aldığını ama başka bir
başlangıç senkronizasyon boşluğunu yakalamadığını gösteriyor.

**Kullanıcının hipotezi (doğrulanmamış, kontrol edilsin):** Önceki
ekran görüntülerinde uygulama açılışında durum çubuğu hep "3500 kbps"
gösteriyordu (muhtemelen kalıcı/varsayılan bir ayar). Az önce
uygulanan profil ise 6000 kbps hedefliyordu. Eğer current_bitrate
ve original_bitrate, rj_update_bitrate_state'e farklı zamanlarda
farklı kaynaklardan (biri eski kalıcı ayar, diğeri yeni profil
config'i) besleniyorsa — ikisi de sıfır olmadığından "bilinmeyen"
filtresine takılmaz, ama aralarındaki fark gerçek bir encoder
durumunu değil, yalnızca başlangıç senkronizasyon boşluğunu yansıtır
→ sahte "açık" (0<current<original) yine oluşur.

---

## Görevin Özü (Faz 0 — kod yazmadan önce doğrula)

1. rj_update_bitrate_state'in tüm çağrı noktalarını (7665cb0'da
   listelenen: frame-cmd uygulama, predictive/WS komutu, pipeline
   init) çağrılma sırasına göre izle — uygulama açılışında hangisi
   önce, hangisi sonra çalışıyor?
2. Her çağrı noktasının current/original için hangi kaynaktan
   değer okuduğunu tespit et — ikisi aynı config nesnesinden mi
   (bitrate_kbps.store ile aynı yer), yoksa biri kalıcı QSettings'ten
   diğeri profil-uygulama akışından mı geliyor?
3. Kullanıcının hipotezini (3500 vs 6000 kbps kaynaklı tutarsızlık)
   doğrula veya çürüt — gerçek başlangıç değerlerini logla/izle.
4. Eğer gerçekten bir senkronizasyon boşluğu varsa (örn. original
   profil uygulamasıyla güncellenirken current eski kalıcı değeri
   koruyor, ya da tam tersi), bunun ne zaman düzelip düzelmediğini
   izle — bir sonraki gerçek frame-cmd/encoder-init çağrısı ikisini
   senkronlar mı, yoksa kalıcı bir tutarsızlık mı oluşur?

Faz 0 çıktısı: Kesin senkronizasyon boşluğu noktası (varsa) +
önerilen düzeltme. Onaya sun.

## Düzeltme Yönü (Faz 0 bulgusuna göre, öneri)

Muhtemelen: uygulama başlangıcında (encoder henüz gerçekten
başlamamışken) current/original ikisi de aynı, tek bir
config kaynağından (o anki gerçek yapılandırılmış bitrate) set
edilmeli — iki farklı zaman/kaynaktan beslenmemeli. Eğer profil
uygulama akışı yalnızca original'ı güncelleyip current'ı
güncellemiyorsa, bu iki değerin birlikte güncellenmesi gerekebilir
(aynı config değişikliği ikisini de etkiliyorsa).

---

## Sabit Kurallar

- Kod yazmadan önce Faz 0'ı tamamla, kesin senkronizasyon noktasını
  kanıtla — tahmin üzerine düzeltme yazma.
- feat/v10-sprint1 dalı üzerine ek düzeltme commit'i — dal henüz
  merge edilmedi.
- Düzeltme sonrası kullanıcının üç senaryoyu yeniden gözlemlemesi
  gerekecek: (1) uygulama açık, START'a hiç basmadan birkaç dakika,
  (2) START ile yayında, (3) gerçek bitrate düşüşünde recovery'nin
  hâlâ çalıştığı.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bu dördüncü ardışık merge-öncesi bulgu — raporun sonunda bu
  desenin (mutlu-yol testlerinin başlangıç durumu/edge-case'leri
  kaçırması) bir sonraki sprint için genel bir ders olarak not
  düşülmesi faydalı olur.
