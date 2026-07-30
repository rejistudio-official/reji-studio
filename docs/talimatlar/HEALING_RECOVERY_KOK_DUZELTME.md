# Düzeltme Yanlış Boyutu Hedefledi — Kök Nedene İn

**Kaynak:** Kullanıcının canlı doğrulaması. UI gate (yalnızca
stream_active_ false iken banner'ı bastırma) beklenmedik sonuç
verdi: START'a basınca (yayın aktifken) siyah kutu 5-7s'de bir
çıkıyor; STOP'a basınca (yayın durunca) çıkmıyor — önceki
davranışın tam tersi.

**Bunun anlamı:** Asıl sorun "yayın var mı yok mu" boyutunda değilmiş.
bitrate_recover kuralı, bitrate zaten hedef/orijinal değerdeyken
bile (kurtarılacak hiçbir şey yokken) hysteresis periyodunda sürekli
doğru değerlendirilip aksiyon üretiyor — bu, yayın aktifken de aynen
geçerli. UI gate yalnızca görünürlüğü boşta'dan yayın-aktife
kaydırdı, kök nedene dokunmadı. Şimdi asıl önemli senaryoda (gerçek
yayın sırasında) sahte healing banner'ları çıkıyor — bu, boşta
durumdan daha kötü bir kullanıcı deneyimi (yanlış "bir şey düzeltiliyor"
izlenimi verir).

---

## Görevin Özü

Önceki turda Sprint 2'ye ertelenen "Rust tarafında kök çözüm"
(o turda seçenek 2 olarak sunulmuştu) şimdi uygulanmalı — UI gate
yeterli değil, kapsamı yanlış boyuttaydı.

1. Kök nedeni doğrula: bitrate_recover kuralının (veya benzer
   recovery kurallarının) koşulu, "gerçekten düşürülmüş bir bitrate
   var mı" diye kontrol etmiyor — yalnızca metrik eşiğine (frame_drop_pct
   < 3 gibi) bakıyor. Bitrate zaten orijinal/hedef değerdeyken bile
   koşul doğru olduğundan aksiyon üretiliyor.
2. Düzeltme yönü: Motor tarafında (Rust), recovery aksiyonu
   üretilmeden önce "aktüatör durumu gerçekten recover edilebilir bir
   durumda mı" kontrolü eklenmeli — örneğin mevcut bitrate zaten
   profilin/config'in orijinal değerindeyse (current_bitrate >=
   original_bitrate gibi), bitrate_recover aksiyonu hiç
   üretilmesin.
3. Bu değişikliğin kapsamını dikkatle sınırla — yalnızca "kurtarılacak
   bir şey yokken aksiyon üretme" mantığı eklensin, mevcut kural
   değerlendirme/hysteresis/diğer aksiyon tiplerinin davranışına
   dokunma.
4. Önceki UI gate'i (stream_active bazlı bastırma) kaldır veya
   koru — kök neden düzeltilirse UI gate zaten tetiklenmez hale
   gelir, ama savunma katmanı olarak kalması zararsız olabilir; hangisi
   daha temiz olduğuna karar ver ve gerekçele.

## Test Planı

- Yeni birim testi: bitrate zaten orijinal/hedef değerdeyken
  bitrate_recover aksiyonunun ÜRETİLMEDİĞİNİ kilitleyen test.
- Regresyon: gerçek bir bitrate düşüşü sonrası recovery'nin hâlâ
  doğru şekilde tetiklendiğini doğrulayan test (asıl işlevsellik
  korunmalı).
- Mevcut HealingOverlayTest'in hâlâ PASS olduğunu doğrula.

## Sabit Kurallar

- feat/v10-sprint1 dalı üzerine ek düzeltme commit'i — dal henüz
  merge edilmedi.
- Bu, önceki turun "Sprint 2'ye not düşüldü" kararının yanlış
  olduğunu gösteriyor — kapsamı şimdi genişletmek doğru, ertelemek
  değil (kullanıcı canlı yayın sırasında yanlış bilgi görüyor).
- Düzeltme sonrası kullanıcının hem boşta hem yayın sırasında
  gözlemlemesi gerekecek — ikisinde de kutu çıkmamalı.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
