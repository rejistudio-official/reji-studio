# TALİMAT: GUI Kullanıcı Gözlemleri — CUT/FADE, Sahne Değişimi, Mod/CoPilot Wiring Doğrulaması

**Kaynak:** Kullanıcının çalışan uygulamada yaptığı canlı gözlem (ekran
görüntüleriyle). Beş gözlemden ikisi araştırılacak ciddi aday, ikisi
doğrulanacak (muhtemelen beklenen davranış), biri ayrı bir UX konusu
(bu talimatın kapsamı dışı).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Kullanıcı testi sırasında beş gözlem yaptı:
1. ✅ Metrikler canlı güncelleniyor — **doğru çalışıyor, bu talimatın kapsamı dışı.**
2. ⚠️ CUT/FADE butonları hiçbir şey yapmıyor.
3. ⚠️ Yandaki sahne listesinden sahne değiştirmenin görünür bir etkisi yok.
4. ❓ Healing modu değiştirmenin (Settings) yalnızca açıklama metnini
   değiştirdiği, başka görsel etkisi olmadığı.
5. ❓ CoPilot Aksiyon Ayarları checkbox'larının (Bitrate otomatik/
   Çözünürlük düşür/FPS sınırla) açıp kapatmanın görünür bir etkisi
   olmadığı.
6. **(Kapsam dışı, ayrı konu)** Ayarlar penceresinin "temel seviyede"
   kalması, geliştirilmesi gerektiği — bu bir UX/özellik-eksikliği
   yorumu, bug değil. Bu talimatta ele alınmayacak.

**Kritik çerçeve — 4 ve 5 için baştan bir uyarı:** Bu ikisinin
"beklenen davranış" olma ihtimali yüksek, çünkü mod/checkbox
değişikliklerinin etkisi **anlık değil, bir sonraki healing aksiyonu
tetiklendiğinde** görünür olmalı (mod → aksiyonun otomatik mi onay-
bekleyen mi olacağı; checkbox → hangi kategorinin auto-onaylı olduğu).
**Ama bunu varsaymak yasak** — I19'da tam bu türden bir "muhtemelen
çalışıyordur" varsayımı yanlış çıkmıştı (sinyal hiç Rust'a ulaşmıyordu).
Her ikisi de find-references ile gerçekten doğrulanacak, körü körüne
"muhtemelen tasarım gereği" denip kapatılmayacak.

---

## Madde 2 — CUT/FADE butonları

### Faz 0 (zorunlu)
1. `main_window.cpp` (veya ilgili UI dosyası) içinde CUT ve FADE
   butonlarının signal/slot bağlantısını bul — bir slot'a bağlı mı,
   yoksa hiç bağlanmamış mı?
2. Eğer bağlıysa, bağlı olduğu fonksiyonun gerçekte ne yaptığını izle
   (find-references) — bir FFI çağrısına mı gidiyor, yoksa yalnızca
   UI-yerel bir no-op mu?
3. Eğer bir FFI çağrısına gidiyorsa, Rust tarafında (`ffi.rs` veya
   ilgili modül) bu çağrının gerçek bir sahne geçişi/kompozisyon
   mantığına bağlı olup olmadığını izle.
4. **Olası sonuç:** Bu butonların baştan beri (ilk UI iskeletinden)
   hiç implemente edilmemiş stub'lar olduğu ortaya çıkabilir — bu
   durumda "bug" değil "eksik özellik" olarak sınıflandır, farklı bir
   önceliklendirme gerektirir.

**Faz 0 çıktısı:** Butonların gerçek durumu (bağlı-ama-boş / bağlı-
ve-kırık / hiç-bağlı-değil) + varsa asıl kırılma noktası. Onaya sun.

## Madde 3 — Sahne değişiminin görsel etkisi yok

### Faz 0 (zorunlu)
1. Sahne listesindeki bir öğeye tıklamanın hangi slot'u tetiklediğini
   bul.
2. Bunun `rj_user_event_scene_switch` (önceki bir Faz 0 araştırmasında
   "yalnızca `current_scene_idx` atomic'ini yazıyor" bulunmuştu — bunu
   güncel `master`'a karşı yeniden doğrula, bayat olabilir) ile ilişkisini
   kur.
3. **Asıl soru:** `current_scene_idx`'in güncellenmesi, gerçek preview/
   output kompozisyonuna (hangi kaynakların render edildiği) hiç bağlı
   mı? Bu zinciri capture/render tarafına kadar izle.
4. Eğer sahne kompozisyon mimarisi (hangi kaynak hangi sahnede
   görünür) hiç implemente edilmemişse — bu, `ROADMAP.md`'nin Faz 3
   (ISource, Çoklu Kaynak Mimarisi) ile doğrudan örtüşebilir. Eğer
   öyleyse, bunun "henüz Faz 3 gelmedi, sahne değişimi şu an yalnızca
   isim/index takibi yapıyor, gerçek kompozisyon yok" şeklinde bilinen
   bir sınırlama olduğunu netleştir — bug değil, henüz yapılmamış iş.

**Faz 0 çıktısı:** Sahne değişiminin gerçek etki alanı (yalnız state mi,
kompozisyona bağlı mı) + Faz 3 ile ilişkisi. Onaya sun — sonuç "bu Faz
3'ü bekliyor" ise, bu talimatın kapsamında düzeltme yapılmayacak,
yalnızca doğru şekilde belgelenip kapatılacak.

## Madde 4 — Healing modu değişiminin görsel etkisi

### Faz 0 (zorunlu, "muhtemelen tasarım gereği" varsayımını doğrulamadan kabul etme)
1. `rj_set_healing_mode`'un Settings dialog'daki mod seçiciye
   gerçekten bağlı olduğunu (I19'un düzelttiği wiring'in hâlâ sağlam
   olduğunu) find-references ile yeniden doğrula.
2. Bir healing aksiyonu tetiklendiğinde (test ortamında sentetik bir
   kural/metrikle simüle edilebilir mi, yoksa yalnızca gerçek runtime'da
   mı gözlemlenir?) mod farkının gerçekten davranışı değiştirdiğini
   (AutoPilot'ta anında uygula, CoPilot'ta pending göster, Manual'de
   hiçbir şey yapma) doğrula.
3. **Sonuç muhtemelen:** "Wiring sağlam, davranış farkı yalnızca
   healing aksiyonu tetiklendiğinde görünür — kullanıcının gözlemi
   doğru ama bu bir bug değil, henüz bir aksiyon tetiklenmediği için
   fark görülmedi." Bunu kanıtla, varsayma.

**Faz 0 çıktısı:** Wiring doğrulaması + davranış farkının nerede/nasıl
görüneceğinin netleşmesi. Onaya sun.

## Madde 5 — CoPilot Aksiyon Ayarları checkbox'ları

### Faz 0 (zorunlu, aynı disiplin)
1. Bu üç checkbox'ın (Bitrate/Çözünürlük/FPS) `rj_set_action_auto_approve`
   (I33'te tasarlanan API) ile gerçekten bağlı olduğunu find-references
   ile doğrula.
2. Startup senkronunun (I19 dersi: checkbox durumunun açılışta Rust'a
   itildiği) hâlâ çalıştığını doğrula.
3. Bu ayarın etkisinin yalnızca gelecekteki bir healing aksiyonunda
   (o kategori otomatik mi uygulanır yoksa onay mı ister) görüneceğini
   netleştir — anlık bir görsel geri bildirim beklenmemeli, ama bunun
   kullanıcıya hiç belirtilmediğini de not et (bu, madde 6'daki UX
   tartışmasına bir girdi olabilir — örn. "checkbox değiştiğinde küçük
   bir onay/bildirim gösterilebilir" gibi bir iyileştirme fikri, ayrı
   değerlendirilir).

**Faz 0 çıktısı:** Wiring doğrulaması. Onaya sun.

## Faz 1-3 (yalnızca gerçek bug bulunan maddeler için)

Madde 2 ve/veya 3'te gerçek bir kırıklık bulunursa, ilgili maddenin
Faz 1 tasarımını ayrı ayrı sun — biri UI-yerel bir düzeltme olabilir,
diğeri Faz 3'ü bekleyen büyük bir mimari boşluk olabilir, ikisi aynı
büyüklükte değil, karıştırma.

Madde 4 ve 5'te wiring sağlam çıkarsa, kod değişikliği gerekmez —
yalnızca bulgunun "beklenen davranış, doğrulandı" olarak
`SESSION_NOTES.md`'ye kaydedilmesi yeterli.

## Sabit Kurallar

- Her madde kendi Faz 0'ından ayrı geçer, birini bitirip onay almadan
  diğerine geçilmez (bu talimat 4 farklı büyüklükte soru içeriyor,
  hepsini aynı anda karıştırma).
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Madde 4 ve 5'te özellikle: "muhtemelen tasarım gereği" gibi bir
  sonuca varmadan önce gerçek kod kanıtı şart — bu talimatın
  varsayımını (muhtemelen beklenen davranış) doğrulamak Faz 0'ın işi,
  varsaymak değil.
- Madde 6 (Ayarlar penceresinin genel eksikliği) bu talimatın kapsamı
  dışında — ayrı bir ürün/tasarım konuşması olarak bırak.
