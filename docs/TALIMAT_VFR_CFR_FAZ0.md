# TALİMAT: VFR/CFR Tasarım Sorusu — Faz 0 Araştırma (Kod Yazma)

**Kaynak:** 2026-07-31 RTMP darboğaz turunda ortaya çıktı. WGC yalnız
değişen kareyi teslim eder → Reji fiilen **VFR** (değişken kare hızı)
üretiyor; OBS son kareyi tekrarlayarak **CFR** (sabit kare hızı) üretir.
Canlı yayın platformları genelde CFR bekler.

**Gözlem verisi:** 25 FPS'lik bir test videosunda `null≈15/saniye`
görüldü — yani WGC saniyede ~45 kare teslim etti, 60 değil. Bu, o
koşulda doğru davranış (masaüstünde gerçekten 25 değişiklik var),
ama hatta giden akışın kare hızı da o oranda düşük oldu.

**Bu talimat kod yazmaz — yalnızca sorunun gerçek olup olmadığını ve
büyüklüğünü belirler.**

---

## Cevaplanması Gereken Ana Soru

> Reji'nin ürettiği akış, gerçek yayın platformlarında (Twitch/YouTube)
> sorun yaratır mı — yoksa bu teorik bir endişe mi?

Bu, **bilinmeyen bir risk.** Faz 0'ın işi onu ya çürütmek ya da
büyüklüğünü ölçmek.

---

## Bölüm A — Mevcut Davranışı Kesinleştir (kod incelemesi)

1. **PTS üretimi:** `FramePacer` ve `pipeline.cpp`'deki pacing mantığını
   incele. Kare teslim edilmediğinde (null) ne oluyor — PTS ilerliyor
   mu, duruyor mu? Encoder'a giden PTS dizisi düzgün artan mı, yoksa
   boşluklu mu?
2. **NVENC'e ne gidiyor:** Null iterasyonda `encode_frame()` hiç
   çağrılmıyor (doğrulanmalı) — yani encoder gerçekten daha az kare mi
   alıyor, yoksa bir yerde tekrar mı üretiliyor?
3. **FLV/RTMP timestamp'leri:** `rtmp_transport.zig`'deki tag
   timestamp'leri PTS'ten mi türetiliyor? Kare atlandığında timestamp
   akışında boşluk oluşuyor mu?
4. **SPS/PPS ve GOP:** Keyframe aralığı kare-sayısı bazlı mı, zaman
   bazlı mı? VFR'de kare-sayısı bazlı bir GOP, zaman ekseninde
   düzensiz keyframe demektir (platformlar bunu sevmez).

## Bölüm B — Platform Beklentilerini Araştır (web araması)

1. Twitch ve YouTube'un **ingest gereksinimleri** ne diyor — CFR
   zorunlu mu, VFR kabul ediliyor mu? Resmi dokümantasyondan alıntıla.
2. VFR gönderildiğinde tipik olarak ne olur — "low FPS" uyarısı,
   stutter, süre kayması, transcoder reddi? Gerçek vaka/rapor var mı?
3. OBS bunu nasıl çözüyor — son kareyi tekrar mı gönderiyor, yoksa
   encoder'a mı bırakıyor? (OBS kaynak kodu veya dokümantasyonundan)

## Bölüm C — Düzeltme Seçeneklerini Boyutlandır (kod yazma yok)

Eğer sorun gerçekse, olası yönler ve maliyetleri:

1. **Son texture'ı yeniden encode et** (null iterasyonda). Dikkat:
   WGC texture'ı **borrowed** — sonraki `next_frame()`'e kadar geçerli;
   saklamak için kopya gerekebilir. Bitrate maliyeti artar (statik
   içerikte bile sürekli kare gönderilir).
2. **Encoder-seviyesi çözüm:** NVENC'in kendi frame-duplication /
   `frameIntervalP` ayarları bu işi yapabilir mi?
3. **PTS-tabanlı çözüm:** Kare tekrarlamadan, yalnızca timestamp'leri
   CFR'ye uydurmak yeterli mi? (Muhtemelen değil ama değerlendirilsin.)
4. **Hiçbir şey yapma:** Eğer platformlar VFR'yi sorunsuz kabul
   ediyorsa, bu geçerli bir sonuçtur — ve bant genişliği avantajı
   (statik içerikte daha az veri) bir **özellik** olarak
   konumlandırılabilir.

---

## Faz 0 Çıktısı

Şu üçünden birine net bir sonuç:

- **Sorun gerçek ve ciddi** → düzeltme tasarımı için Faz 1 gerekir,
  önerilen yön + tahmini boyut.
- **Sorun gerçek ama küçük** → kayda geç, önceliklendir, şimdi
  düzeltme yapma.
- **Sorun yok / çürütüldü** → gerekçesiyle kapat, ROADMAP ve Todoist
  kaydını güncelle.

**Emin olunamayan noktalar** "doğrulanmadı" diye açıkça işaretlensin —
özellikle gerçek platform davranışı, yerel testle kesinleştirilemez.

---

## Sabit Kurallar

- Kod değiştirme, bu tamamen bir araştırma turu.
- Web araması yaparken **resmi kaynakları** tercih et (Twitch/YouTube
  ingest dokümanları, OBS kaynak kodu) — forum söylentisi değil.
- Bulguları "kod incelemesiyle doğrulandı" / "web kaynağından" /
  "çıkarım, doğrulanmadı" diye ayır.
- Bu tur bir **gerçek yayın testi** gerektirebilir (Twitch'e kısa bir
  test yayını) — gerekiyorsa bunu belirt, kullanıcı yapacak.
