# Faz 0 Araştırma: Reduce Döngüsünün Tetikleyicisi Gerçek mi?

**Kaynak:** Kullanıcının canlı doğrulaması. Kutunun görünürlük mantığı
artık DOĞRU görünüyor — yalnızca gerçek bir açık varken (current <
original) beliriyor, kademeli iyileşme (3500→4025→4600→5300→6000)
belgelenmiş ×1.15 healing adımlarıyla birebir örtüşüyor. Bu, muhtemelen
self-healing sisteminin bu kurulumda İLK KEZ gerçekten tasarlandığı
gibi çalıştığını gösteriyor (önceki mode-adı uyuşmazlığı yüzünden
daha önce hiç tetiklenmemişti).

**Asıl soru budur:** 6000'den 3500'e düşüşü tetikleyen koşul gerçek
mi (gerçek CPU/GPU yükü, gerçek frame drop), yoksa hâlâ sahte bir
tetikleyici mi? Kullanıcı testi sırasında masaüstü durağandı/boşta
görünüyordu — GPU/CPU'yu zorlayacak aktif bir yük yoktu.

**Bu, kod değişikliği İSTEMEYEN bir teşhis turu — yalnızca gözlem.**

---

## Görevin Özü

1. frame_drop_policy.h düzeltmesinin gerçek kapsamını netleştir:
   Düzeltme yalnızca "null-frame'i drop sayma" mı yaptı, yoksa
   record_frame_drop'un çağrılma sıklığını/koşulunu da etkiledi mi?
   Önceki turda not düşülen "frame_drop_pct ölü metrik — record_frame/
   record_frame_drop hiç çağrılmıyor" bulgusunu bu düzeltmeye karşı
   yeniden değerlendir — düzeltme sonrası frame_drop_pct gerçekten
   hesaplanıyor mu, yoksa hâlâ sabit/0'da mı donuk?
2. Stabilite profilinin diğer reduce kurallarını kontrol et —
   gpu_load_high >80 → −500, cpu_load_high >75 → cap 30 gibi.
   Test sırasında bu metriklerin gerçek değerlerini (loglardan veya
   mevcut bir debug çıktısından) incele — GPU/CPU yükü gerçekten
   80'in üzerinde miydi, yoksa bu ölçüm de hatalı/yanlış ölçekli mi?
3. Hangi kuralın gerçekte tetiklendiğini kesinleştir: Healing
   log'u (varsa, healing_log.rs/SQLite) veya event history'yi
   incele — reduce aksiyonunun hangi kural ID'siyle (high_cpu_reduce,
   high_frame_drop, vb.) tetiklendiğini bul.
4. Donanım bağlamını hesaba kat: Bu, hibrit-GPU bir laptop —
   dbglog/metrics'in doğru GPU'yu (encode-GPU, dGPU) mu yoksa iGPU'yu
   mu ölçtüğünü kontrol et; yanlış adaptörün ölçülmesi de sahte yüksek
   yük gösterebilir.

Faz 0 çıktısı: Tetikleyicinin gerçek mi sahte mi olduğu, hangi
kuralın/metriğin sorumlu olduğu. Bunu onaya sun — kod değişikliği
gerekip gerekmediğine o zaman birlikte karar verilecek.

---

## Sabit Kurallar

- Bu turda kod DEĞİŞTİRME — yalnızca teşhis/gözlem. Eğer gerçek
  bir hata bulunursa, düzeltmeyi ayrı bir onay turunda başlat.
- feat/v10-sprint1 dalı hâlâ merge edilmedi — bu araştırma da onun
  bir parçası olarak devam ediyor.
- Bulgunun "gerçek sistem yükü" mü yoksa "hâlâ hatalı metrik" mi
  olduğunu net bir şekilde ayır — belirsizse "kesinleşmedi, ek gözlem
  gerekiyor" diye dürüstçe işaretle.
