# TALİMAT: Faz 5 (Zig Global State) — Faz 3 Bağımlılık Netleştirmesi (Yalnızca Faz 0)

**Kaynak:** `docs/ROADMAP.md`'de bulunan Faz 5 ("Zig Global State Tam
Çözümü", 4 alt madde, hepsi açık) — son alt maddesi ("Çoklu ISource/
ITransport senaryosunda test") Faz 3'ün (Çoklu Kaynak Mimarisi) bir
önkoşulu olabileceğini düşündürüyor.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

**Bu talimat yalnızca Faz 0 — hiçbir kod yazılmayacak, hiçbir
implementasyon kararı verilmeyecek.** Amaç tek bir soruyu kesin olarak
cevaplamak: **Faz 3'e başlamadan önce Faz 5'in (tamamının ya da bir
kısmının) bitmesi gerekiyor mu, yoksa ikisi bağımsız/paralel ele
alınabilir mi?**

**Önemli nüans — baştan uyarı:** "Global state" otomatik olarak kötü
değil. Bazı kaynaklar (Vulkan instance, physical/logical device gibi)
meşru şekilde paylaşılan/tekil olmalı — çoklu kaynak mimarisinde bile
tek bir Vulkan cihazı yeterli, her kaynak için ayrı bir cihaz açmak
yanlış olur. Sorun yalnızca kaynak-özel olması gereken ama global
kalan state'te (örn. bir capture akışına özel image pool, slot
sayacı). Bu talimat bu ayrımı netleştirmeden "hepsi kötü, hepsini
instance-level yap" sonucuna atlamayacak.

---

## Faz 0 — Kapsamlı Araştırma (kod yazmadan)

### Bölüm A — Zig tarafındaki global state'in tam envanteri

1. `external_memory_bridge.zig`'deki tüm global/static değişkenleri
   listele — her biri için: ne tutuyor, kim okuyor/yazıyor, J4'ün
   `pool_index_` düzeltmesi bunu kapsıyor mu kapsamıyor mu.
2. `vulkan_initializer.zig`'deki tüm global/static değişkenleri
   listele — aynı analiz.
3. Her global için sınıflandır: (a) meşru paylaşılan kaynak
   (Vulkan instance/device gibi, çoklu kaynak olsa bile tek olmalı) vs
   (b) kaynak-özel olması gereken ama şu an global olan state
   (image pool, slot sayacı, bir capture akışına özgü buffer gibi).

### Bölüm B — J4'ün gerçek kapsamı

1. J4'ün static slot'u pool_index_'e taşıma düzeltmesinin tam
   olarak neyi çözdüğünü yeniden incele — bu, tek bir
   ExternalMemoryBridge örneğinin kendi içindeki reentrancy
   sorununu mu çözdü (örn. aynı örneğin farklı thread'lerden çağrılması),
   yoksa iki ayrı ExternalMemoryBridge C++ nesnesinin aynı anda var
   olabilmesini de mi sağladı?
2. Eğer J4 yalnızca tek-örnek reentrancy'sini çözdüyse (muhtemel), iki
   ayrı bridge örneği oluşturulmaya çalışılırsa şu an ne olur — kod
   incelemesiyle izle (çakışma/veri bozulması riski var mı, yoksa
   ikinci örnek oluşturma zaten mimari olarak engellenmiş mi).

### Bölüm C — Faz 3'ün gerçek eşzamanlılık ihtiyacı

1. ROADMAP.md'de Faz 3 (ISource) için var olan tüm notları/taslakları
   çıkar — tasarımın gerçek eşzamanlı çoklu kaynak (örn. webcam +
   ekran görüntüsü aynı anda kompozit edilen bir sahne) mi, yoksa
   sıralı/tek-aktif-kaynak (bir seferde yalnız bir kaynak aktif,
   sahne geçişinde değişir) mi hedeflediğini belirle.
2. Eğer tasarım notları yetersizse, GUI Gözlem Turu'nun bulgusunu
   (CUT/FADE'in "ikinci içerik" beklediği, gerçek sahne kompozisyonu
   olmadığı) referans alarak mantıklı bir çıkarım yap — bir "sahne"
   kavramı genelde birden fazla kaynağın aynı anda render edildiği
   bir kompozisyon ima eder (yalnızca sıralı geçiş değil).

### Bölüm D — Sonuç ve Öneri

Yukarıdakilere dayanarak üç olası sonuçtan birine var:

1. Faz 5 tam önkoşul: Bölüm A'daki (b) kategorisindeki state'ler
   gerçekten Faz 3'ün ihtiyaç duyacağı eşzamanlılığı engelliyor —
   Faz 3'e başlamadan önce en azından bunların instance-level'a
   taşınması gerekiyor.
2. Faz 5 kısmi önkoşul: Yalnızca bazı global'ler (örn.
   external_memory_bridge.zig'deki, vulkan_initializer.zig'deki
   değil) gerçekten engelleyici — Faz 5'in tam kapsamı değil, dar bir
   alt kümesi önkoşul.
3. Bağımsız/paralel: Faz 3'ün ilk aşamaları (örn. temel sahne
   kompozisyon mimarisini kurmak) gerçek eşzamanlı çoklu-kaynak
   gerektirmeden ilerleyebilir — Faz 5 ayrı, paralel bir iş olarak ele
   alınabilir, Faz 3'ü bloklamaz.

Faz 0 çıktısı: Yukarıdaki dört bölümün bulguları + üç sonuçtan
hangisinin geçerli olduğuna dair gerekçeli bir öneri. Onaya sun — bu
talimat burada biter, implementasyon kararı (Faz 5'e mi Faz 3'e mi
önce başlanacağı) bu rapordan sonra kullanıcıyla birlikte verilecek.

---

## Sabit Kurallar

- Kod değişikliği yok, bu tamamen bir bağımlılık analizi turu.
- "Kod incelemesiyle doğrulandı" dışında bir dürüstlük sınıfı
  gerekmiyor.
- Emin olunmayan bir noktada (örn. Faz 3'ün tasarım niyeti hiç
  belgelenmemişse) tahmin etmek yerine "belgelenmemiş, varsayım/çıkarım"
  olarak açıkça işaretle.
- Bu talimatın ruhu: büyük bir mimari sıralama sorusunu, iki fazı da
  gereksiz yere büyütmeden veya küçültmeden, dürüstçe cevaplamak.
