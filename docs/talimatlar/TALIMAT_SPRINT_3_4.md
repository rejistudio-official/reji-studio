# TALİMAT: Sprint 3-4 — I15-I18 (Performans/Mimari) + I21-I26 (Temizlik) + I34 (Inert Checkbox)

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md`, CONTEXT.md, Linear REJ-14
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

V8 planının Sprint 1-2'si tamamen kapandı (I1-I14, I19/I20, I33+I11, I8, I9/I10
dahil). Bu talimat, plana hiç dokunulmamış son iki sprint'i + I33 sırasında
bulunan bir yan-madde'yi kapsıyor:

- **Sprint 3 (I15-I18):** Performans/mimari tutarlılık — henüz maddelerin
  içeriği bu talimatı yazan taraf için netleşmemiş, **Faz 0'ın ilk işi
  bunları tam olarak okuyup anlamak.**
- **Sprint 4 (I21-I26):** Temizlik — genelde düşük risk/düşük efor
  maddeler olması beklenir, ama varsayma.
- **I34:** `chk_source_auto` checkbox'ının inert olduğu (source-switch
  aksiyonu pipeline'da yok) — I33 sırasında bulundu, V8'e küçük bir
  temizlik maddesi olarak eklenmesi önerilmişti.

**Bu talimat öncekilerden farklı bir yapıda:** Sprint 1-2'deki maddeler
(I8, I9/I10, I14, I33) tek tek derin Faz 0→1→2→3 döngüsü gerektiren,
birbirinden bağımsız veya az sayıda karmaşık madde grubuydu. Sprint 3-4
ise **çok sayıda, muhtemelen küçük ve birbirinden bağımsız** maddelerden
oluşuyor. Bu yüzden süreç iki katmanlı olacak:

1. **Ön-triyaj (tüm maddeler için, hafif):** Her maddenin gerçek boyutunu,
   riskini ve bağımlılığını hızlıca değerlendir.
2. **Gruplama:** Triyaja göre maddeleri "birlikte tek pakette çözülebilir
   küçük/bağımsız" ve "kendi Faz 0/1 turunu hak eden büyük/riskli" olarak
   ikiye ayır.

Bu, önceki maddelerde defalarca gördüğümüz "küçük görünen madde aslında
derin" deseninin (I14, I10 gibi) bu sprint'te de çıkabileceği varsayımıyla
tasarlandı — ön-triyaj bunu erken yakalamak için var.

---

## Faz 0 — Ön-Triyaj (kod yazmadan, zorunlu)

Her madde (I15, I16, I17, I18, I21, I22, I23, I24, I25, I26, I34) için:

1. `FABLE5_BUG_PLAN_V8.md`'deki tanımını oku.
2. İlgili kod konumunu/konumlarını find-references ile bul.
3. Güncel `master`'a karşı hâlâ geçerli mi doğrula (I2/I3/I8/I9/I10/I14
   deseni — bazıları çoktan çözülmüş, yanlış konumlanmış, veya çürütülmüş
   olabilir; özellikle I22'nin bayat `RjMetricSample` beklentisiyle ilgili
   olduğu daha önce fark edilmişti — check-abi.ps1 notundan hatırlanmalı).
4. Kabaca sınıflandır: **(a) gerçekten küçük/bağımsız/düşük-risk**,
   **(b) küçük görünüyor ama incelemede büyüdü** (I14/I10 deseni —
   bulursan hemen işaretle, bu maddeyi Sprint'ten ayrı ele al),
   **(c) zaten geçersiz/çürütülmüş/farklı konumda** (I2/I3/I29/I31 deseni).

**Faz 0 çıktısı:** 11 maddenin (I15-I18, I21-I26, I34) her biri için tek
satırlık sınıflandırma + kısa gerekçe. Bunu tablo halinde onaya sun.
Herhangi bir madde (b) sınıfına düşerse, o maddeyi bu talimattan çıkarıp
ayrı bir talimat/tur önerisi yap — Sprint 3-4'ün geri kalanı bundan
etkilenmesin.

## Faz 1 — Gruplama ve Yaklaşım Onayı

Faz 0 sonucuna göre:

- **(a) sınıfı maddeler** birlikte, mantıklı alt-gruplar halinde tek
  seferde ele alınabilir (örn. "tüm Sprint 4 temizlik maddeleri bir arada",
  "I15-I18 performans maddeleri kendi içinde 2-3 alt gruba ayrılabilir" —
  Faz 0 bulgusuna göre sen öner).
- **(c) sınıfı maddeler** için: V8 planında kapatma gerekçesi yaz (I29/I31
  formatı — "çürütüldü", tek satır), implementasyon gerekmez.
- **(b) sınıfı maddeler** için: kapsam dışına al, ayrı talimat öner.
- Her alt-grup için beklenen commit sayısı/sırası ve risk seviyesini
  belirt.

Bu onaya sun, sonra implementasyona geç.

## Faz 2 — İmplementasyon (gruplara göre küçük commit'ler)

Faz 1'de onaylanan gruplama sırasına göre ilerle. Genel kurallar:
- Her commit tek bir maddeye veya sıkı ilişkili küçük madde kümesine
  karşılık gelsin — büyük "hepsini birden" commit'lerinden kaçın.
- Bir madde implementasyon sırasında beklenenden büyük çıkarsa (I10/I14
  deseni), dur, raporla, o maddeyi ayrı ele al — sprint'in geri kalanını
  bloklama.
- Dokümantasyon (V8 planı güncellemesi, SESSION_NOTES, ilgili skill
  notları) her alt-grup tamamlandığında güncellensin, sona bırakılmasın
  (sprint uzun sürebilir, ara kayıp riski azalsın).

## Faz 3 — Test ve Dürüstlük Sınırları

- Her madde için uygun test seviyesini (birim/entegrasyon/kod incelemesi)
  Faz 0/1'deki sınıflandırmaya göre belirle — küçük temizlik maddeleri
  için "kod incelemesiyle doğrulandı" yeterli olabilir, performans
  maddeleri ölçülebilir sonuç gerektirebilir (benchmark/profil verisi).
- Performans maddelerinde (I15-I18) iyileştirme iddiası varsa, öncesi/
  sonrası ölçümle destekle — "daha hızlı olmalı" değil "şu ölçümde X'ten
  Y'ye düştü" formatında raporla.
- Regresyon: her alt-grup sonrası ctest/rust test paketi kontrolü.

## Faz 4 — Kapanış Dokümantasyonu

- `FABLE5_BUG_PLAN_V8.md`: Sprint 3-4 durumunu güncelle. Bu, **V8 planının
  I1-I34 arası tüm maddelerinin kapandığı** anlamına gelir (Faz 0'da (b)
  sınıfına düşenler hariç) — CONTEXT.md'de büyük bir kilometre taşı olarak
  not edilmeli.
- `docs/SESSION_NOTES.md`, ilgili skill notları, talimatı arşivle.
- **Dış senkron kaynakları** (Notion, Todoist, Linear REJ-14): sprint
  kapanışını yansıt. Bu, kullanıcı tarafından ayrıca istenmedikçe otomatik
  yapılmaz — bu talimat yalnızca Claude Code'un kod tarafını kapsıyor.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; her alt-grup tamamlanınca push öncesi onay
  (Sprint 1-2'deki gibi "seri tamamlanınca topluca" yerine, sprint uzun
  olabileceğinden **alt-grup bazlı** push döngüsü öner — Faz 1'de netleş).
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları varsayımlarla çelişirse implementasyona geçmeden dur,
  raporla (I2/I3/I8/I9/I10/I14 deseni) — bu sprint'te özellikle "küçük
  sanılan madde büyüdü" senaryosuna karşı tetikte ol.
