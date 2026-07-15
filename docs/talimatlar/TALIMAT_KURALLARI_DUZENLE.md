# TALİMAT: "Kuralları Düzenle..." Stub'ını ve Otomatik Reload'u Bağlama

**Kaynak:** Ayarlar Penceresi Araştırma Turu'nun bulgusu — `Kuralları
Düzenle...` butonu ve `Otomatik yeniden yükle (dosya değiştiğinde)`
checkbox'ı UI'da var ama gerçek işlevleri şüpheli/doğrulanmamış ("yalancı
buton" deseni — CUT/FADE'in üçüncü tekrarı).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

İki UI elemanının gerçek durumunu netleştirip (varsa) eksik wiring'i
tamamlamak:
1. `Kuralları Düzenle...` butonu.
2. `Otomatik yeniden yükle (dosya değiştiğinde)` checkbox'ı.

**Bu, düşük risk/düşük maliyet olarak değerlendirildi** çünkü `rj_reload_rules`
(I24'te `cstr_bounded` ile sertleştirilmiş) zaten hazır ve güvenli — yeni
bir FFI yüzeyi muhtemelen gerekmiyor, yalnızca UI tarafının bu mevcut
fonksiyona doğru bağlanması gerekiyor olabilir.

---

## Faz 0 — Doğrulama (kod yazmadan, zorunlu)

1. **`Kuralları Düzenle...` butonunun tam mevcut davranışını** izle
   (`settings_dialog.cpp`) — buton tıklanınca gerçekte ne oluyor?
   Üç olası durum:
   - (a) Hiçbir şey (tamamen boş slot/no-op).
   - (b) Bir şey oluyor ama yanlış/eksik (örn. yanlış dosya yolu açılıyor,
     ya da bir dialog açılıp hiçbir işlevi yok).
   - (c) Aslında çalışıyor ama görünmez bir şekilde (örn. arka planda bir
     komut çalıştırıyor ama kullanıcıya geri bildirim yok).
2. **`Otomatik yeniden yükle` checkbox'ının** gerçek durumunu izle —
   işaretlenince bir dosya-izleme mekanizması (örn. Qt'nin
   `QFileSystemWatcher`'ı) kuruluyor mu, yoksa yalnızca bir bool
   değişkeni set edip hiçbir şey yapmıyor mu?
3. `rj_reload_rules`'ın (I24) güncel imzasını ve `rules.json`'un gerçek
   dosya yolunu (`paths.rs` deseni, I21) teyit et.
4. Projede zaten bir dosya-izleme deseni var mı (başka bir yerde
   `QFileSystemWatcher` veya benzeri kullanılıyor mu) — varsa onu tekrar
   kullan, yoksa yeni bir bağımlılık/desen eklemenin gerekçesini değerlendir.

**Faz 0 çıktısı:** İki elemanın da gerçek durumu (a/b/c sınıflandırması)
+ `rj_reload_rules` ve dosya yolu teyidi + mevcut dosya-izleme emsali
olup olmadığı. Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

**`Kuralları Düzenle...` için karar noktası:** İki makul yaklaşım var —

- **(a) Harici editör aç (önerilen, düşük maliyet):** `rules.json`
  dosyasını sistemin varsayılan metin editöründe aç (Qt'de
  `QDesktopServices::openUrl(QUrl::fromLocalFile(path))`). `rules.json`
  zaten insan-okunur bir format olarak tasarlandı (Farklılaşma
  Stratejisi sütun 3'te de bu netleşmişti) — uygulama içi bir JSON
  editörü inşa etmek gereksiz bir yatırım olur, harici editör zaten
  syntax highlighting/hata kontrolü gibi şeyleri bedava sağlar.
- **(b) Uygulama içi editör:** Çok daha büyük bir iş (metin editörü
  widget'ı, syntax validation, kaydetme akışı) — bu, "kural motoru
  görünürlüğü" (Faz 0 madde 4, büyük maliyetli olarak işaretlenmişti)
  ile örtüşür, bu talimatın kapsamına girmemeli.

**Öneri: (a).** Faz 0 bulgusuna göre kesinleştir, onaya sun.

**`Otomatik yeniden yükle` için:**
- Checkbox işaretliyken `QFileSystemWatcher` (veya projenin mevcut
  deseni) `rules.json`'u izlemeli, değişiklik algılanınca `rj_reload_rules`
  çağrılmalı.
- Kullanıcıya geri bildirim: reload başarılı/başarısız olduğunda kısa
  bir durum mesajı (I10 "sessizce yutma" dersini burada da uygula —
  reload başarısız olursa kullanıcı bunu görmeli, sessizce eski
  kurallarla devam edilmemeli).
- Checkbox işaretsizken izleme tamamen kapalı olmalı (kaynak israfı
  yok).

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler)

Örnek sıra (Faz 1 onayına göre uyarlanacak):
1. `Kuralları Düzenle...` → harici editör açma.
2. `QFileSystemWatcher` kurulumu + checkbox'a bağlama + `rj_reload_rules`
   çağrısı + başarı/hata geri bildirimi.
3. Dokümantasyon: Ayarlar araştırma raporuna çapraz referans,
   `SESSION_NOTES.md`, `ROADMAP.md`'deki ilgili nota güncelleme.

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim/entegrasyon: `rj_reload_rules`'ın dosya değişince gerçekten
  çağrıldığını test edebiliyorsan test et (dosya sistemi olayları test
  ortamında simüle edilebilir mi, değilse kod incelemesiyle doğrula).
- Regresyon: mevcut `rules`/healing testleri PASS kalmalı.
- **GUI görsel doğrulaması kullanıcıda kalacak** — butona basınca gerçekten
  editörün açıldığını, dosyayı değiştirip kaydedince (otomatik reload
  açıkken) kuralların gerçekten yeniden yüklendiğini gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: iki UI elemanı artık gerçekten
  çalışıyor (önceden ya hiçbir şey yapmıyordu ya da belirsizdi) — bunu
  açıkça listele.

## Sabit Kurallar (hatırlatma)

- Küçük commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" / "muhakemeyle kabul
  edilebilir" ayrımı her raporda açık.
- Faz 0 bulguları bu talimatın varsayımlarıyla (özellikle butonun
  "tamamen boş" olduğu varsayımı) çelişirse — örneğin buton kısmen
  çalışıyor ama farklı bir şekilde kırıksa — dur, gerçek duruma göre
  tasarımı yeniden çerçevele, raporla.

---

## Kapanış — Uygulama Tamamlandı (15 Tem 2026)

**Faz 0 (kod incelemesiyle doğrulandı):** Her iki eleman da **(a) tamamen
no-op** — `editRulesRequested`/`autoReloadToggled` hiçbir slot'a bağlı
değildi, `isAutoReloadEnabled()` hiç okunmuyordu, projede
`QFileSystemWatcher` emsali yoktu. Talimatın "yalancı buton" varsayımı
doğru çıktı. Yeni FFI gerekmedi (`rj_reload_rules` I24'te hazır); kanonik
yol `%USERPROFILE%\.reji\rules.json`, `paths.rs`'i kullanmıyor.

**Uygulama — dal `feat/rules-edit-hotreload`, 3 commit:**
1. **Kuralları Düzenle → harici editör** (`QDesktopServices::openUrl`);
   dosya yoksa gömülü şablondan (`.qrc`/AUTORCC, tek kaynak
   `docs/config/rules.json.template`) tohumlanır; hata `QMessageBox` (I10).
2. **Otomatik hot-reload:** `QFileSystemWatcher` (dosya + üst dizin) +
   300ms debounce + atomic-save yeniden-ekleme → `rj_reload_rules(path)`.
   Geri bildirim ayrı kalıcı widget (`lbl_rules_`); hata **yapışkan** —
   yalnız sonraki başarılı reload temizler (zaman aşımı yok). Durum
   `QSettings`'te kalıcı.
3. **docs + etiket:** SESSION_NOTES/ROADMAP/bu arşiv + etiket
   "(JSON/TOML)" → "(JSON)".

**Doğrulama sınıfı:** kod incelemesi + **derleme/link doğrulandı**
(`reji_app`, `rj_reload_rules` sembolü Rust lib'inden çözüldü). **GUI
görsel doğrulaması kullanıcıda** (Faz 3). **Tasarım sapması (onaylı):**
geri bildirim Faz 1'de `lbl_status_` denmişti; yapışkan-hata gereksinimi
için ayrı kalıcı widget'a geçildi.

Detay: `SESSION_NOTES.md` "Ayarlar zenginleştirme #1".
