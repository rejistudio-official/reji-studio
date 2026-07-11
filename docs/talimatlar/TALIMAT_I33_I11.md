# TALİMAT: I33 + I11 — CoPilot Onay Akışı ve Action-Queue Mimarisi

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md` (I11, I33), CONTEXT.md bölüm 3-5
**Hedef dosya konumu:** Bu talimat tamamlandığında `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

İki madde tek pakette ele alınacak, çünkü ikisi de aynı soruya değiniyor:
**"Healing motorunun ürettiği aksiyonlar kullanıcıya nasıl ulaşıyor ve
kullanıcının kararı motora nasıl geri dönüyor?"**

- **I33:** `rj_action_approve()` şu an stub — CoPilot modunda kullanıcı "onay"
  verdiğini sanıyor ama motor bunu hiç görmüyor. Gerçek implementasyon gerekiyor.
- **I11:** Action-queue'nun iki tüketicisi var. Önceki araştırma "ikisi de
  gerekli" sonucuna vardı; yarış koşulu üretmeyen, açıkça tanımlanmış bir
  fan-out veya tek-tüketici+yönlendirme mimarisine kavuşturulmalı.

**Kritik bağlam:** I19/I20 ile `HealingMode` artık 4 varyant
(`AutoPilot`/`CoPilot`/`Assist`/`Manual`) ve UI'daki mod seçimi gerçekten
Rust'a iletiliyor. Bu, CoPilot'un onay mekanizmasının sahte olmasını daha
görünür bir kusur haline getirdi — bu görev I19/I20'nin doğal devamı.

**I19'dan alınan ders (bu görevde tekrar etme):** I19'un gerçek kök nedeni
Rust tarafında değil, C++ wiring boşluğundaydı (sinyal Rust'a hiç
ulaşmıyordu). Bu görevde de sadece Rust tarafını implemente edip bırakma —
**UI'dan FFI'ya kadar uçtan uca zinciri find-references ile doğrula.**

---

## Faz 0 — Güncel `master`'a Karşı Yeniden Doğrulama (kod yazmadan önce, zorunlu)

Proje disiplini gereği eski rapor/varsayımlara güvenilmiyor. Şunları doğrula
ve bulgularını kısaca raporla:

1. `FABLE5_BUG_PLAN_V8.md`'deki I11 ve I33 maddelerini oku; I11 araştırma
   notundaki "iki tüketici de gerekli" tespitinin hâlâ geçerli olup olmadığını
   güncel koddan teyit et. İki tüketici tam olarak hangileri, hangi thread'de
   çalışıyorlar, aynı queue'dan mı pop ediyorlar?
2. `rj_action_approve()` stub'ının imzasını, çağrıldığı yerleri
   (find-references) ve UI tarafında onay butonunun/akışının **var olup
   olmadığını** tespit et. UI hiç çağırmıyorsa bu, I19 desenidir ve kapsamı
   büyütür — raporla.
3. `src/orchestrator/src/healing.rs`'te aksiyonların üretildiği yeri
   (`RuleEngine` entegrasyonu, `evaluate_adaptive()`) ve I19 sonrası 4 modun
   **fiili** semantiğini çıkar. Özellikle: `Assist` ile `CoPilot` şu an kodda
   nasıl ayrışıyor? (CONTEXT'e göre `Manual` = hiç otomatik aksiyon yok;
   `Assist`'in tam davranışı koddan teyit edilmeli, varsayma.)
4. Aksiyonun UI'a bildirildiği mevcut kanalı haritala (event/callback/metrics
   üzerinden mi?). `rj_metrics_poll`'un implemente olmadığını (I14, açık)
   hesaba kat — bu göreve bağımlılığı varsa erken bildir.

**Faz 0 çıktısı:** Kısa bir "mevcut durum haritası" (üretici → kuyruk →
tüketiciler → UI → onay dönüş yolu) ve varsayımlardan sapmalar. Bunu
onaya sun, ondan sonra tasarıma geç.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

1. **Kuyruk mimarisi kararı (I11):** Tek tüketici + iç yönlendirme mi, yoksa
   her tüketiciye ayrı kuyruk ile açık fan-out mu? Kararın gerekçesini,
   thread-safety garantilerini ve hangi yarış senaryolarını kapattığını yaz.
2. **Mod-başına aksiyon yaşam döngüsü tanımı:**
   - `AutoPilot`: aksiyon anında uygulanır, UI'a bilgi amaçlı bildirilir.
   - `CoPilot`: aksiyon **pending** durumuna alınır, benzersiz ID ile UI'a
     sunulur; yalnızca `rj_action_approve(id)` gelirse uygulanır.
   - `Assist` / `Manual`: Faz 0'da teyit edilen fiili semantiğe göre tanımla;
     I19'daki davranış değişikliklerini bozma.
3. **Pending aksiyon deposu:** ID üretimi, zaman aşımı/geçersizleşme (metrik
   düzeldiyse bekleyen aksiyon bayatlar — bunun politikası ne?), reddetme
   yolu (muhtemelen `rj_action_reject(id)` gerekiyor — FFI yüzeyine ekleme
   önerisi yap), mod değişiminde pending'lerin kaderi (Manual'a geçilince
   temizlenir mi? AutoPilot'a geçilince otomatik uygulanır mı? — öner,
   onay al).
4. **FFI yüzeyi:** `rj_action_approve`'un gerçek imzası, gerekiyorsa
   `rj_action_reject` ve pending listesini sorgulama yolu.
   `ffi-safety-review` skill'ini uygula (ABI, ownership, "yokluk
   iddialarında find-references" kuralı dahil).

## Faz 2 — İmplementasyon (küçük, mantıksal commit'ler; push öncesi onay)

Önerilen commit sırası (Faz 0/1 bulgularına göre uyarlanabilir):

1. **I11 mimari düzeltmesi** — kuyruk yapısının yeniden düzenlenmesi, mevcut
   davranışı koruyarak. Tek başına test edilebilir olmalı.
2. **Pending-approval deposu (Rust)** — ID'li yaşam döngüsü + birim testleri.
3. **`rj_action_approve` (ve varsa `rj_action_reject`) gerçek implementasyonu**
   — FFI roundtrip testiyle birlikte.
4. **C++ wiring** — UI onay/red akışının FFI'ya gerçekten bağlanması.
   I19 dersini uygula: sinyalin Rust'a ulaştığını uçtan uca doğrula,
   startup senkronunu unutma.
5. Gerekirse: mod değişimi × pending etkileşimi ayrı commit.

## Faz 3 — Test ve Dürüstlük Sınırları

- Rust birim testleri: kuyruk davranışı, pending yaşam döngüsü, mod-başına
  yönlendirme, approve/reject/timeout yolları.
- FFI roundtrip testi (build+link dahil), I19/I20'de yapıldığı gibi.
- **Kullanıcıya bırakılacaklar (otonom zorlanmayacak):** GUI'de CoPilot
  modunda gerçek bir eşik aşımı → pending aksiyonun görünmesi → onayla →
  aksiyonun uygulanması → reddet → uygulanmaması gözlemi. Bunu raporunda
  "test edildi" değil "kod incelemesi + otomatik testle doğrulandı, GUI
  davranış onayı kullanıcıda" olarak nitele.
- Kullanıcıya görünen her davranış değişikliğini (örn. CoPilot'ta artık
  aksiyonların gerçekten beklemesi) açıkça listele.

## Faz 4 — Dokümantasyon

- `FABLE5_BUG_PLAN_V8.md`: I11 ve I33 durumlarını, bulunan ek sorunlar varsa
  yeni maddelerle birlikte güncelle.
- `docs/SESSION_NOTES.md`: oturum girdisi.
- İlgiliyse `.claude/skills/ffi-safety-review/SKILL.md`'ye bu görevden çıkan
  yeni kural/desen ekle.
- Bu talimatı `docs/talimatlar/` arşivine taşı.

## Sabit Kurallar (hatırlatma)

- Her commit küçük ve mantıksal; push öncesi onay bekle.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımını her raporda açıkça belirt.
- Faz 0 bulguları eski varsayımlarla çelişirse, implementasyona geçmeden
  önce durup raporla — I2/I3'te olduğu gibi kapsam tamamen değişebilir.
