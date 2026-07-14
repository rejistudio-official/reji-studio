# TALİMAT: Özellik #1 — CoPilot Aksiyon Açıklaması

**Kaynak:** `docs/ROADMAP.md`, "Gelecek Özellikler — Sinerjik Değerlendirme",
madde 1 (en yüksek öncelik, en düşük maliyet).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine taşınmalı.

---

## Görevin Özü

Bu, V8/V9'daki gibi bir **bug düzeltmesi değil, yeni bir özellik**. Yine
de aynı disiplin geçerli: önce mevcut mimariyi (I33'ün action-queue/event
sistemi) gerçekten anlamadan tasarıma geçilmeyecek.

**Hedef:** CoPilot'ta bir pending aksiyon göründüğünde (veya AutoPilot/
Assist'te bir aksiyon otomatik uygulandığında), kullanıcının **hangi
kuralın hangi metrik eşiğini aştığı için bu aksiyonun önerildiğini/
uygulandığını** görebilmesi (örn. "GPU sıcaklığı 87°C, eşik 85°C").

**Neden önemli (motivasyon, unutulmasın):** Healing Plumbing turunda
`gpu_thermal_restore` kuralının metrik-stub yüzünden sessizce hep-true
kalıp anlamsız pending'ler ürettiğini bulmuştuk. O sorunu kod okuyarak
teşhis ettik — kullanıcı arayüzünde hiçbir açıklama olmadığı için
kullanıcının kendisi bunu asla fark edemezdi. Bu özellik, gelecekte
benzer bir anormalliğin kullanıcı tarafından **anında** fark edilmesini
sağlıyor — CoPilot'un güvenilirliğini korumanın bir parçası.

---

## Faz 0 — Mevcut Mimariyi Çıkar (kod yazmadan, zorunlu)

1. **`RjAction`/`RjActionEvent` struct'larının güncel tam tanımını** bul
   (`ffi.rs` veya `ffi_auto.h`) — I33'ten beri hangi alanlar var (`id`,
   `action_type`, `param1`, `param2`, `require_approval`, `kind` vb.).
   Açıklama için yer var mı, yoksa yeni alan mı gerekiyor?
2. **`rule_id` bilgisinin nereye kadar taşındığını izle** — I8/I33
   sırasında reject-cooldown mekanizması için `Action.rule_id: String`
   eklenmişti (`RuleEngine::apply_cooldown` ile ilişkili). Bu bilgi
   FFI sınırını geçip C++ tarafına/UI'a ulaşıyor mu, yoksa yalnızca
   Rust-içi mi kalıyor?
3. **Kuralın koşul metnini/bileşenlerini** (`rules.json`'daki
   `gpu_temp_c > 85` gibi) `RuleEngine::evaluate()` sonrası hâlâ elde var
   mı, yoksa değerlendirme anında mı atılıyor? Metrik adı + o anki değer +
   eşik değeri üçlüsünün aksiyon üretildiği anda erişilebilir olup
   olmadığını netleştir.
4. **UI tarafında pending/uygulanan aksiyonun gösterildiği yeri** bul
   (`HealingOverlay`, `main_window.cpp` — I33'te CoPilot onay
   akışının bağlandığı yerler) — şu an yalnızca aksiyon tipini mi
   gösteriyor, yoksa herhangi bir bağlam metni var mı?
5. **String/açıklama verisinin FFI sınırını nasıl geçeceği** — bu
   projede sabit-boyutlu alanlar (`RjActionEvent` gibi) veya
   `cstr_bounded` deseni (J1/I24) kullanılıyor. Serbest-metin bir
   açıklama string'i mi, yoksa yapılandırılmış alanlar (metrik-adı enum'u
   + iki sayısal değer, UI'da formatlanır) mı daha uygun — ikisinin de
   FFI güvenlik implikasyonlarını değerlendir.

**Faz 0 çıktısı:** Yukarıdaki beş sorunun cevabı + önerilen veri modeli
taslağı (yapılandırılmış alanlar mı, string mi). Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

Faz 0 bulgusuna göre şekillenecek, ama olası eksenler:

1. **Veri modeli kararı:** Önerilen yaklaşım — **yapılandırılmış alanlar**
   (metrik adı için küçük bir enum/sabit string tablosu + `current_value`
   + `threshold_value`, ikisi de `f32`/`i32`), serbest-metin string'den
   kaçınmak (FFI güvenlik yüzeyini büyütmemek, `ffi-safety-review`
   disipliniyle tutarlı). UI, bu üç parçadan insan-okunur cümleyi kendi
   oluşturur (yerelleştirme de kolaylaşır). Faz 0'da bunun mümkün olup
   olmadığını (RuleEngine'in bu üç değeri aksiyon üretim anında elde
   tutup tutamadığını) doğrula, tasarımı buna göre kesinleştir.
2. **FFI struct değişikliği:** `RjActionEvent`'e alan eklemek mi (ABI
   kırılması — `ffi-safety-review` prosedürü, `static_assert`, cbindgen
   yeniden üretimi) yoksa ayrı bir sorgu fonksiyonu mu (`rj_get_action_explanation(id)`,
   pending deposundan ek bilgi çekme) — hangisi I33'ün mevcut mimarisiyle
   daha uyumlu, öner.
3. **UI gösterimi:** Pending onay prompt'unda ve/veya uygulanan aksiyon
   bildiriminde açıklama nerede/nasıl görünecek (tooltip, ikinci satır,
   genişletilebilir detay) — mevcut `HealingOverlay` tasarımına en az
   müdahaleyle uyan yaklaşımı öner.
4. **Metrik adı okunabilirliği:** `gpu_temp_c` gibi ham metrik adının
   kullanıcıya "GPU Sıcaklığı" gibi okunabilir bir karşılığı olmalı —
   bu eşleme nerede tutulacak (Rust sabit tablo mu, C++ tarafı mı)?

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler)

Faz 1 onayına göre uyarlanacak, örnek iskelet:
1. Rust: `RuleEngine`'de aksiyon üretim anında açıklama verisinin
   yakalanması (metrik adı, değer, eşik).
2. FFI: struct değişikliği veya yeni sorgu fonksiyonu + `ffi-safety-review`
   prosedürü tam uygulanır.
3. C++: veriyi alıp UI'a ileten wiring (I19 dersi: uçtan uca doğrula,
   find-references ile çağrıldığını kanıtla).
4. UI: `HealingOverlay`'de/pending prompt'ta gösterim.
5. Dokümantasyon: `FFI_CONTRACT.md` güncelle, `ROADMAP.md`'de madde 1'i
   "implemente edildi" olarak işaretle, `SESSION_NOTES.md`.

## Faz 3 — Test ve Dürüstlük Sınırları

- Rust birim testi: açıklama verisinin doğru metrik/değer/eşikle
  üretildiğini doğrula (sentetik kural + metrik ile).
- FFI roundtrip testi (yeni alan/fonksiyon varsa).
- Regresyon: `PipelineCharacterization` + mevcut healing testleri.
- **GUI görsel doğrulaması muhtemelen kullanıcıda kalacak** — gerçek bir
  pending aksiyonun açıklamayla birlikte doğru göründüğünü gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: yalnızca ek bilgi gösterimi,
  var olan onay/red/timeout akışı değişmiyor — bunu açıkça belirt.

## Sabit Kurallar (hatırlatma)

- Küçük, mantıksal commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları bu talimatın varsayımlarıyla (özellikle "RuleEngine bu
  veriyi elde tutuyor" varsayımı) çelişirse — örneğin veri zaten
  değerlendirme anında atılıyorsa ve saklamak RuleEngine'e önemli bir
  değişiklik gerektiriyorsa — dur, kapsamı ve maliyeti yeniden
  değerlendirip raporla. Bu özelliğin "en düşük maliyet" varsayımı Faz
  0'da doğrulanmalı, körü körüne kabul edilmemeli.
