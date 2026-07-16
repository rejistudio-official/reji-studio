# TALİMAT: Paylaşılabilir Kural Setleri — Dışa/İçe Aktar

**Kaynak:** `docs/ROADMAP.md`, "Farklılaşma Stratejisi" sütun 3 —
`rules.json`'ı git'te versiyonlanabilir/toplulukla paylaşılabilir kılmak.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

`rules.json` zaten insan-okunur bir format — bu özellik, kullanıcının
mevcut kural setini bir dosyaya kaydedip paylaşabilmesini, ve başkasının
paylaştığı bir kural setini güvenle içe aktarabilmesini sağlıyor. Bu,
Ayarlar Zenginleştirme #1'in (Kuralları Düzenle + hot-reload) doğal
devamı — `rj_reload_rules`, dosya yolu yardımcıları ve durum-geri
bildirimi deseni zaten hazır, yeniden kullanılacak.

**Kapsam sınırı (baştan net):** Bu bir "save as / open as" işlevi —
versiyon/yazar metadata'sı, diff/önizleme, çakışma çözümü YOK (MVP,
YAGNI). Ayrıca **kalibrasyon verisi paylaşılmıyor** — Özellik #5'te
zaten netleşmişti: kalibrasyon Rust-içi bellekte tutuluyor,
`rules.json` şemasına hiç yazılmıyor, dolayısıyla dışa aktarılan dosya
yalnız kural mantığını taşır, alıcının donanımına özgü hiçbir veri
sızmaz. Bu, Faz 0'da yeniden doğrulanmalı (bayat olabilir).

---

## Faz 0 — Doğrulama (kod yazmadan, zorunlu)

1. **`rules.json` şemasının hâlâ değişmediğini** teyit et (Özellik #5'in
   "şema DEĞİŞMEZ" kararı hâlâ geçerli mi — kod yorumunu yeniden bul).
2. **`rj_reload_rules`'ın gerçekte ne kadar doğruladığını** izle —
   yalnızca JSON syntax mı kontrol ediyor, yoksa gerçek kural şemasına
   (alan adları, tip uyumu) karşı da mı doğruluyor? İçe aktarımın
   güvenliği buna bağlı.
3. **Kuralları Düzenle işinden kalan yardımcıları** (`rulesFilePath()`,
   durum-geri bildirim widget'ı `lbl_rules_`, `QFileSystemWatcher`
   kurulumu) incele — bunlar yeniden kullanılacak, tekrar icat
   edilmeyecek.
4. **Kritik etkileşim sorusu:** İçe aktarım sırasında dosya
   `rules.json`'ın üzerine yazılırsa, eğer otomatik-yeniden-yükleme
   (watcher) aktifse, watcher bu değişikliği **kendiliğinden** algılayıp
   `rj_reload_rules`'ı zaten çağıracak. İçe aktarım kodunun **ayrıca**
   `rj_reload_rules` çağırması, watcher'ın debounce'lı tetiklemesiyle
   çakışıp çift-reload'a yol açabilir mi? Bu akışı net çözümle (örn.
   içe aktarım yalnızca dosyayı yazsın, reload'u watcher'a bıraksın —
   ya da watcher aktif değilse içe aktarım kendi reload'unu tetiklesin).
5. Projede zaten kullanılan `QFileDialog` deseni var mı (tutarlılık
   için) kontrol et.

**Faz 0 çıktısı:** Yukarıdaki beş sorunun cevabı. Onaya sun.

## Faz 1 — Tasarım (implementasyondan önce onaya sunulacak)

1. **Dışa aktar:** `QFileDialog::getSaveFileName` ile hedef konum sor
   (varsayılan dosya adı önerisi: `reji-rules-<tarih>.json` gibi),
   mevcut `rules.json`'ı oraya kopyala. Başarı/hata `lbl_rules_`
   widget'ında.
2. **İçe aktar — güvenlik akışı (kritik, dikkatle tasarlanmalı):**
   - `QFileDialog::getOpenFileName` ile kaynak dosya seç.
   - **Doğrulamadan önce üzerine yazma.** Seçilen dosyayı geçici bir
     konuma kopyala, `rj_reload_rules` (veya varsa ayrı bir doğrulama-
     only fonksiyon) ile önce **doğrula**, başarısızsa hiçbir şeyi
     değiştirmeden hata göster.
   - Doğrulama başarılıysa: **mevcut `rules.json`'ı yedekle** (örn.
     `rules.json.backup`, I10 felsefesi — geri dönüşsüz kayıp yok),
     sonra yeni dosyayı asıl konuma kopyala.
   - Faz 0 madde 4'ün cevabına göre reload'u tetikle (watcher'a bırak
     veya elle çağır, çift-tetiklemeden kaçın).
3. **UI konumu:** Settings dialog'daki "Kural Yönetimi" bölümüne,
   "Kuralları Düzenle..." butonunun yanına iki yeni buton
   ("Dışa Aktar...", "İçe Aktar...").
4. **Metadata YOK** (kapsam sınırı) — dosya yalnız ham `rules.json`
   içeriği, ekstra sarmalama/versiyon bilgisi eklenmez.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük commit'ler)

Örnek sıra:
1. Dışa aktar butonu + dosya kopyalama + geri bildirim.
2. İçe aktar — doğrula-önce-yaz güvenlik akışı + yedekleme + reload
   tetikleme.
3. Dokümantasyon: `ROADMAP.md` sütun 3'ü "implemente edildi" olarak
   işaretle, `SESSION_NOTES.md`, `FFI_CONTRACT.md`'ye gerek yoksa
   dokunma (yeni FFI beklenmiyor).

## Faz 3 — Test ve Dürüstlük Sınırları

- Birim/entegrasyon: geçerli bir dosyanın içe aktarılabildiğini,
  **bozuk bir dosyanın reddedilip mevcut kuralların dokunulmadan
  kaldığını** (en kritik senaryo) doğrula.
- Yedekleme dosyasının gerçekten oluştuğunu doğrula.
- Regresyon: mevcut `rules`/healing testleri + Kuralları Düzenle'nin
  hot-reload testleri PASS kalmalı (watcher ile çakışma yok).
- **GUI görsel doğrulaması kullanıcıda kalacak** — gerçek dışa/içe
  aktarım akışını, özellikle bozuk dosya senaryosunu gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: iki yeni buton, mevcut
  akışlar (Kuralları Düzenle, hot-reload) değişmiyor.

## Sabit Kurallar (hatırlatma)

- Küçük commit'ler; tamamlanınca push öncesi onay.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- İçe aktarımda **doğrulamadan önce asla üzerine yazma** — bu talimatın
  tek gerçek güvenlik gereksinimi, esnetilmemeli.
- Faz 0 bulguları (özellikle `rj_reload_rules`'ın doğrulama derinliği
  veya watcher-çakışma sorusu) varsayımlarla çelişirse dur, raporla.
