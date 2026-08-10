# Reji Studio — Geliştirme Ritmi ve Belge Yapısı

**Kaynak:** Volt HDL projesi için hazırlanmış bir "hızlandırıcılar +
tarama ritmi" belgesi. Bu doküman onu Reji'ye uyarlıyor — ama körü
körüne değil: Volt **greenfield** (hiç kod yok, 40 belge hazır), Reji
ise **olgun** (aylarca çalışılmış, V8/V9/V10 kapanmış, çalışan bir
ürün). Bazı öneriler doğrudan geçerli, bazıları Reji'de zaten var,
bazıları hiç uymuyor.

**Bu belge bir talimat değil, bir öneri paketi.** Hangi parçaların
uygulanacağı ayrı ayrı karara bağlanmalı.

> **Faz 0 doğrulaması (2026-08-10):** Belgedeki ölçülebilir iddialar
> depo üzerinde doğrulandı. Düzeltilen noktalar: CLAUDE.md gerçek
> boyutu (§B5), justfile'ın gerçek hedef listesi ve `just lint`
> iddiasının güncel olmadığı (§B3), B1 tablosundaki bug yaşam
> sürelerinin `git log -S` kanıtları (§B1), docs/ ağacının gerçek
> içeriği (§B4). Doğrulanamayan iddialar "kesinleşmedi" olarak
> işaretlendi.

---

## Bölüm A — Transfer OLMAYAN öneriler (ve nedenleri)

Bunları uygulamayalım; Volt'a özgü ya da Reji'de zaten çözülmüş:

| Volt önerisi | Reji'de durum |
|---|---|
| "Karar verme bitti, %90 kod yazmaya" | Reji'de kararlar **iş sırasında** veriliyor (Faz 0 → onay → Faz 1). Bu, Reji'nin en değerli disiplini — "kararlar bitti" varsayımı buraya uymaz. |
| Dikey dilim stratejisi | Zaten uygulanıyor — her tur (Ses Ayarları, Donanım Profilleme, ISource) çalışan bir dilim üretti. |
| "Kapsamı acımasızca kıs" | Zaten kurulu refleks: MVP'ye daraltma (SRT ses ertelendi, kural düzenleme ertelendi, çözünürlük ertelendi). |
| Test-önce (40 UI test dosyası) | Reji'de TDD zaten uygulanıyor (RED→GREEN, `send_diag.h`, `reinit_trigger_policy.h`, `profile_advisor`). |
| Referans uygulamalardan öğrenme | Zaten yapılıyor (obs-websocket protokolü, OBS davranış emsalleri, `veovera/enhanced-rtmp` kaydı). |
| Türkçe/İngilizce kararı | Reji tek kişilik ve Türkçe yürüyor. Açık kaynak katkı hedefi netleşmeden değiştirmek erken. **Açık soru olarak kalsın.** |

---

## Bölüm B — Reji'ye GERÇEKTEN uyan öneriler

### B1. "Bu bug ne kadar süre fark edilmeden yaşadı?" ölçütü ⭐

**Volt'un en değerli fikri ve Reji için en acil olanı.** 2026-08-03
civarındaki turlar bunu acı şekilde gösterdi. Aşağıdaki tablo Faz 0
doğrulamasında (2026-08-10) `git log -S` ile teyit edildi; her satırda
giriş ve düzeltme commit'leri verildi:

| Bug | Giriş | Düzeltme | Yaşam süresi | Doğrulama durumu |
|---|---|---|---|---|
| WS Ping/Pong (her istemci ~20 sn'de düşüyordu) | `1c29e43` 2026-06-29 (ws_server.rs oluşturuldu) | `2a8f83d` 2026-07-31 | **~1 ay** | ✅ Doğrulandı. Özgün iddia "aylarca — Faz 1'den beri" abartılıydı; WS sunucusu 2026-06-29'da girdi. |
| `applyProfile` qrc kaydı çalışmıyordu (ölü kaynak) | `4a5807c` 2026-07-20 (Donanım Profilleme Commit 4) | `72b4b09` 2026-07-21 (V10 L1-ek) | **~1 gün** (git'te) | ✅ Kısmen doğrulandı: "Donanım Profilleme'den beri" doğru, ama git'te görünen ömür 1 gün. Ayrıca bkz. `talimatlar/ACIL_L1_QRC_REGRESYON.md`. |
| `gpu_load_pct` hiç kopyalanmıyordu | `c9f0a8d` 2026-06-24 (alan RjMetricSample'a eklendi, H10) | `d89f2b7` 2026-08-03 | **~6 hafta** | ✅ Doğrulandı. Özgün iddia "v0.5'ten beri" gevşekti: v0.5.0 tag'i 2026-06-04, alan ise 2026-06-24'te (v0.5.1 sonrası dönem) girdi. |
| FrameProfiler hiç örnek toplamıyordu | `6f6820b` 2026-06-02 (v0.4, modülün ilk commit'i) | `598520a` 2026-08-03 | **≤2 ay** | ⚠ Kesinleşmedi: modülün yaşı kesin (2 ay), ama "başından beri hiç örnek toplamadığı" git ile tek başına kanıtlanamadı. |
| İçe aktarım `QFile::copy` hatası | `5a16f81` 2026-07-16 (kural seti dışa/içe aktarma) | `c99f1b6` 2026-07-20 | **4 gün** | ✅ Doğrulandı: "özelliğin ilk commit'inden beri" doğru — ama süre 4 gün, "aylarca" sınıfında değil. |
| Preview `Map(READ)` darboğazı | `aa02d1c` 2026-06-28 (WGC + CPU preview yolu) | `5a816e5` 2026-08-03 (SendDiag Faz 3) | **~5 hafta** | ✅ Doğrulandı: "WGC yolundan beri" tutarlı. |

**Uygulama:** Her bulunan bug'da `git log -S` ile "ne zaman girdi"
sorusunu sor ve **SESSION_NOTES'a kaydet.** Aylık gözden geçirmede bu
listeye bak: en uzun yaşayan bug'ların ortak özelliği ne?

**İlk cevap zaten belli:** Ortak desen **"mekanizma var, son
bağlantı eksik."** FrameProfiler'ın kaydı, `gpu_load_pct`'nin
kopyalanması, `REJI_VULKAN_MOCK`'un workflow'a geçirilmesi,
`markCopyStart/End`'in çağrılması — hepsi doğru tasarlanmış ama hiç
bağlanmamış. (Doğrulamanın ikinci dersi: en uzun yaşayanlar Rust/C++
sınırındaki "kopyalanmayan alan / çağrılmayan kayıt" türü; UI-yakını
bug'lar günler içinde yakalanıyor.)

### B2. Yeni mekanizma eklerken zorunlu soru ⭐

B1'in doğrudan sonucu. `CLAUDE.md`'ye eklenecek bir kural:

> **Yeni bir mekanizma (metrik, event, bayrak, callback, config alanı)
> eklerken üç soruyu cevapla ve raporda göster:**
> 1. Bunu kim **çağırıyor**? (çağıran yoksa ölü kod)
> 2. Bunu kim **tüketiyor**? (tüketici yoksa sinyal boşa gidiyor)
> 3. Çalıştığını hangi **test** kanıtlıyor? (uçtan uca, yalnız birim
>    değil)
>
> Bu tek kural, B1 tablosundaki altı bug'ın en az dördünü baştan
> önlerdi.

### B3. Katmanlı tarama ritmi (Reji'ye uyarlanmış)

Volt'un tek-dilli (Rust) ritmi Reji'nin üç dilli yapısına doğrudan
uymuyor. Reji versiyonu:

**Faz 0 tespiti (2026-08-10):** justfile'daki mevcut hedefler:
`build`, `rebuild`, `run`, `test`, `review`, `review-check`, `shield`,
`abi-check`, `zig-abi`, `log`. **`lint`, `daily` ve `weekly` hedefleri
YOK.** Özgün belgedeki "`just lint` + PostToolUse hook — bugün
kuruldu" iddiası bu depoda doğrulanamadı: justfile'da `lint` hedefi
yok, `.claude/settings.json`'da PostToolUse hook'u yok. Bu adım
**henüz kurulmamış** sayılmalı.

**Her Rust düzenlemesinde (KURULDU, 2026-08-10 — `79ef519`):**
PostToolUse hook → `scripts/lint.ps1` (`cargo clippy --all-targets
-D warnings`; kırmızıda exit 2 ile stderr Claude'a döner). Ölçülen
artımlı süre ~2,6 sn — 30 sn akış-bozma eşiğinin altında. `.rs` dışı
düzenlemeler 0,2 sn'de sessiz geçer.

**Her commit'te (henüz yok — öneri):**
Pre-commit hook: `cargo fmt --check` + `cargo clippy -- -D warnings`.
**Dikkat:** C++ tarafı (`ctest`) commit hook'una **konmamalı** —
tam build 60+ saniye, hook'u kimse tolere etmez.

**Her push'ta (kısmen var):**
`quality.yml` ✅ mevcut (clippy adımı `21cc80e` ile eklendi, C++
derleme adımları `51583de` ile çıkarıldı).
`build.yml` ❌ (üç parçalı onarım planı Todoist'te P3 — bu belgede
doğrulanmadı, kaynak: özgün belge).

> **Çağrı yolu güncellemesi (2026-08-10):** Birincil yol doğrudan
> script çağrısıdır — `just`'a bağımlı değildir, PATH'i yenilenmemiş
> kabuklarda ve CI'da da çalışır. `just daily`/`just weekly` hedefleri
> sarmalayıcı olarak duruyor (just'ın göründüğü kabuklarda kısayol).
> Clippy adımı da artık `weekly.ps1`'in **içinde** — eskiden yalnız
> `just weekly: lint` zincirinde koşuyordu, script tek başına
> çağrıldığında atlanıyordu.

**Günlük (öneri, ~5 dk):**
```
powershell -ExecutionPolicy Bypass -File scripts\daily.ps1
  → ctest (tam paket, 26/26 beklenir)
  → cargo test
  → git diff --stat HEAD~1
  → TODO/FIXME sayımı
```

**Haftalık (öneri, ~30 dk) — Reji'ye özgü kontroller:**
```
powershell -ExecutionPolicy Bypass -File scripts\weekly.ps1
  → cargo clippy --all-targets -D warnings
  → cargo audit + cargo outdated
  → ABI ritüeli: ffi_auto.h'ı build.rs'e dokunarak yeniden ürettir,
    SHA256 aynı mı? (git diff İŞE YARAMAZ — dosya gitignore'da;
    2026-08-10 B6 denemesinde öğrenildi)
  → DOKUNMA dosyaları değişmiş mi? (metrics.rs, ffi_bridge.h)
  → Test sayısı düştü mü? (ctest 26, cargo 145+5+37 — sayılar
    özgün belgeden, kesinleşmedi)
  → tests/baseline_metrics.txt yanlışlıkla commit'lenmiş mi?
  → [SendDiag] bütçe kontrolü: tot < 16.7ms mi?
    (5a816e5 preview düzeltmesinin regresyona uğramadığı)
  → kullanicida etiketli görev sayısı artıyor mu?
    (kod bitiyor ama doğrulanmıyorsa borç birikiyor)
```

**Aylık (öneri):**
- ROADMAP'teki "gelecek fikir" kayıtları hâlâ geçerli mi?
- Faz sıralaması değişti mi? (Faz 3 ↔ Faz 5 bağımlılığı gibi)
- B1 listesi: en uzun yaşayan bug'ların ortak deseni ne?

### B4. `docs/README.md` indeksi ⭐

**Faz 0 tespiti (2026-08-10):** `docs/README.md` gerçekten yok
(yalnız `talimatlar/README.md` var). Ağacın gerçek içeriği özgün
belgedekinden daha geniş: kökte ~35 dosya + 5 klasör; `talimatlar/`
altında **60 dosya** (özgün belge "~20" diyordu); `FABLE5_BUG_PLAN`
V1'den V10'a **10 dosya**; ayrıca `reviews/` (çok-model tarama
çıktıları), `superpowers/` (fases/plans/specs), `config/`
(şablonlar) ve `archive/` (yalnız `memory.md` + `progress.md`,
ikisi de "⚠ ARŞİV — GÜNCEL DEĞİL" uyarılı).

**Sorun:** Hangi soruya hangi belgenin cevap verdiği yalnız
hafızada. Yeni bir oturumda (veya `/clear` sonrası) yanlış dosyaya
bakma riski var.

**Öneri — gerçek dosyalara göre düzeltilmiş indeks tablosu:**

| Soru | Dosya |
|---|---|
| Şu an ne durumdayız? | `CONTEXT.md` |
| Hangi işler planlı? | `ROADMAP.md` |
| Falanca tur ne yaptı? | `SESSION_NOTES.md` |
| FFI sözleşmesi ne? | `FFI_CONTRACT.md` |
| Benchmark sonuçları? | `BENCHMARK_RESULTS.md` |
| V10'da hangi bug'lar vardı? | `FABLE5_BUG_PLAN_V10.md` (V1–V9 aynı desende) + `V10_SENTEZ_TRIYAJ.md` |
| Falanca özellik nasıl yapıldı? | `talimatlar/` (60 dosya; kendi `README.md`'si var) |
| Wiring kararları neydi? | `FAZ0_RAPOR_WIRING.md`, `ON_KONTROL_WIRING_BAYATLIK.md` |
| Vulkan tarafı nasıl çalışır? | `VULKAN_DEV_GUIDE.md`, `vulkan-sync-diagram.md` |
| Ajan/model tarama çıktıları? | `reviews/` (+ `reviews/REVIEW_GUIDE.md`) |
| Healing log şeması? | `HEALING_LOG_SCHEMA.md` |
| Eski/geçersiz kayıtlar | `archive/` (memory.md, progress.md — ⚠ ARŞİV uyarılı) |

Not: Depo kökünde ayrıca bir `CONTEXT.md` (sohbet asistanının GitHub
doğrulamalı özeti) var; proje bağlamının kaynağı `docs/CONTEXT.md`'dir.
İndeks yazılırken bu ayrım belirtilmeli.

### B5. `CLAUDE.md` boyut denetimi

**Faz 0 ölçümü (2026-08-10):** `CLAUDE.md` = **8.213 bayt (8,0 KB),
164 satır.** Volt'un 5 KB eşiği aşılmış durumda — yani "önce ölç"
adımı tamamlandı ve sonuç: taşıma tartışması gerçekten gündemde.

**Ama dikkat:** Reji'nin CLAUDE.md'sindeki kurallar (8b dal disiplini,
DOKUNMA listesi, baseline_metrics kuralı) **gerçekten her oturumda
gerekli** — bunlar "işaret" değil "kural", taşınırsa unutulur.
Taşınacak şey varsa, bunlar değil. Karar Aşama 2'de (B5 maddesi)
ölçüme dayanarak verilecek.

### B6. Haftalık sağlık taraması `/goal` ile

`/goal`'un ardışık başarıları bu fikri destekliyor. B3'ün
`weekly.ps1`/`daily.ps1` altyapısı kurulduktan sonra prompt sadeleşti:
`/goal`'un işi kanıt toplamak değil, raporlamak ve sapmaları
yorumlamak. Nihai prompt (ilk deneme 2026-08-10,
`docs/health/health-2026-08-10.md`; script çağrıları doğrudan —
just'a bağımlılık yok):

```
/goal Haftalık sağlık taraması:
`powershell -ExecutionPolicy Bypass -File scripts\weekly.ps1` ve
`powershell -ExecutionPolicy Bypass -File scripts\daily.ps1` çalıştır,
çıktılarını docs/health/health-YYYY-MM-DD.md raporuna işle.

Rapor şunları içermeli: her kontrolün sonucu (geçti/uyarı/hata),
beklenen değerlerden SAPMALAR açıkça işaretli (ctest 26, cargo
145+5+37, ABI hash aynı, DOKUNMA dosyaları temiz), ve TODO/FIXME
sayısı.

Kapsam: SADECE rapor yaz, kod DEĞİŞTİRME, bulunan sorunları
DÜZELTME. Sapma varsa raporla ve dur — triyaj insana ait.
15 turdan sonra dur.
```

**Kritik kısıt:** "sapma varsa raporla ve dur" — tarama ile düzeltmeyi
ayır. Tarama bulguları insan triyajından geçmeli (V10'da olduğu gibi;
siyah kutu turundaki üç yanlış düzeltme bu ayrımın gerekçesi).

**İlk denemenin dersi (2026-08-10):** İlk koşum, taranan koddan önce
tarayıcının kendisinde iki kusur buldu — ABI ritüelinin git-diff
kontrolü boş doğrulamaydı (ffi_auto.h gitignore'da), TODO sayacı
kendini sayıyordu. B2'nin üç sorusu araçlara da uygulanmalı.

---

## Bölüm C — Uygulama Sırası (önerilen)

**Tamamlandı (Faz 0, 2026-08-10):**
1. ~~`CLAUDE.md` boyutunu ölç~~ → 8,0 KB / 164 satır (B5)
2. ~~justfile envanteri~~ → `lint`/`daily`/`weekly` yok (B3)
3. ~~B1 yaşam sürelerini `git log -S` ile teyit et~~ → tablo güncellendi

**Kısa vade — TAMAMLANDI (2026-08-10, her madde ayrı onayla):**
4. ~~`CLAUDE.md`'ye B2 kuralı~~ ✅ (`17ca5a5`)
5. ~~`docs/README.md` indeksi (B4)~~ ✅ (`608eda2` — + belge statüleri)
6. ~~`justfile`'a `daily`/`weekly` (B3)~~ ✅ (`6dbfad2`, `6e93eed` — + `lint`)
7. ~~`CLAUDE.md` boyut kararı (B5)~~ ✅ (`83a51a7` — §10 silindi,
   8,6 → 7,2 KB; kalan bağlayıcı, zorla küçültme yok)

**Orta vade:**
8. ~~PostToolUse hook kurulumu~~ ✅ 2026-08-10 (`79ef519` —
   `scripts/lint.ps1` + settings.json `hooks` kaydı; süre ölçümü
   ~2,6 sn < 30 sn eşiği. Bulgu: 06-10'dan beri duran
   `.claude/hooks/hooks.json` Claude Code'un okumadığı şema/konumdaydı,
   hook 2 ay boyunca hiç çalışmamıştı — B1 defterine işlendi)
9. Pre-commit hook (yalnız Rust, hızlı olmalı)
10. ~~Haftalık `/goal` sağlık taraması denemesi (B6)~~ ✅ ilk deneme
    2026-08-10 (`docs/health/health-2026-08-10.md`)
11. ~~B1 kaydı: SESSION_NOTES'a "bug yaşam süresi" bölümü aç~~ ✅
    2026-08-10 (alışkanlık olarak sürdürülecek)

**Bu belge kapsamı dışı:**
- `build.yml` onarımı (ayrı talimat, Todoist P3)
- Türkçe/İngilizce kararı (açık soru)

---

## Kapanış Notu

Volt belgesinin en değerli fikri, hızlandırıcılar değil **B1'deki
ölçüt**: *"Bir hata ne kadar süre fark edilmeden kalabildi?"*

B1 tablosundaki altı bug'ın en uzun yaşayanları (WS Ping/Pong ~1 ay,
`gpu_load_pct` ~6 hafta, FrameProfiler ≤2 ay, Map(READ) ~5 hafta)
hiçbiri kod incelemesiyle bulunmadı; ya canlı GUI testi ya bağımsız
model taraması ya da `/goal`'un kesin doğrulama koşulu ortaya çıkardı.

Bu, ritmin nerede zayıf olduğunu söylüyor: **statik analiz iyi
(clippy artık CI kapısında), ama "bu mekanizma gerçekten çalışıyor
mu" sorusu hâlâ elle soruluyor.** B2'deki üç soru, bunu sistematik
hale getirmenin en ucuz yolu.
