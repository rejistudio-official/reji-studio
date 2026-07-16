# TALİMAT: Ayarlar Penceresi Kapsamlı Araştırması (Yalnızca Keşif)

**Kaynak:** GUI Gözlem Turu'nda ortaya çıkan Madde 6 (UX) — "Ayarlar
penceresi temel seviyede kalmış" gözlemini somutlaştırma ihtiyacı.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

**Bu talimat yalnızca araştırma — hiçbir kod değişikliği yapılmayacak.**
Amaç, "ayarlar zenginleştirilmeli" gibi belirsiz bir gözlemi, somut ve
önceliklendirilebilir bulgulara dönüştürmek. Çıktı bir Faz 0 raporu
olacak; implementasyon kararı bu rapordan sonra, ayrı talimat(lar)la
verilecek.

İki ayrı soru var, ikisi de araştırılacak:

1. **`Kuralları Düzenle...` butonu ne yapıyor?** (Settings dialog'da
   zaten var — GUI ekran görüntüsünde görüldü.) Bu, kendi başına
   ilginç bir keşif olabilir — belki zaten var olan ama az bilinen bir
   özellik, belki de yarım kalmış bir stub.
2. **Genel eksiklik envanteri:** Pipeline/RuleEngine/Encoder/Output'un
   desteklediği ama Settings UI'da hiç yüzeye çıkmayan yetenekler neler?

---

## Faz 0 — Araştırma (kod yazmadan)

### Bölüm A — `Kuralları Düzenle...` butonu

1. Butonun bağlı olduğu slot'u bul (`settings_dialog.cpp`).
2. Ne açtığını/yaptığını izle — harici bir editör mü açıyor, uygulama
   içi bir editör mü, yoksa yalnızca dosya yolunu mu gösteriyor?
3. `Otomatik yeniden yükle (dosya değiştiğinde)` checkbox'ının da
   gerçekten `rj_reload_rules`'a (I24'te sertleştirilmiş) bağlı olup
   olmadığını doğrula.
4. Eğer bu buton yarım/stub ise, bunu da bulgu olarak işaretle — GUI
   turundaki CUT/FADE'e benzer bir "bağlı ama etkisiz" deseni olabilir.

### Bölüm B — Genel eksiklik envanteri

`SettingsDialog`'daki mevcut tüm alanları listele, sonra şu soruyu her
biri için sor: **"Pipeline/RuleEngine/Encoder/Output tarafında bu
alanın karşılığı/genişletilmiş hâli var mı, UI'da hiç görünmüyor mu?"**

Özellikle şu alanları kontrol et (kod tabanında zaten var olma ihtimali
yüksek, UI'a hiç yansımamış olabilir):

1. **Ses ayarları** — WASAPI capture ile ilgili herhangi bir kullanıcı
   ayarı var mı (cihaz seçimi, seviye), yoksa tamamen sabit mi?
2. **Bitrate/çözünürlük manuel sınırları** — Özellik #5'in kalibrasyonu
   (`MEM_MIN=50, MEM_MAX=95` gibi clamp'ler) kullanıcıya görünür/
   ayarlanabilir mi, yoksa yalnızca kod-içi sabitler mi?
3. **NVENC/encoder parametreleri** — `encode_nvenc.cpp`'de kullanıcının
   göremediği ama var olan parametreler (preset, profile, GOP vb.) var
   mı?
4. **Kural motoru görünürlüğü** — `rules.json`'daki kuralların (isim,
   koşul, aksiyon) Settings UI'dan **görüntülenebildiği** ama
   **düzenlenemediği** bir ara durum mu var, yoksa tamamen kör kutu mu?
5. **WS/kimlik doğrulama detayları** (I8) — parola dışında (örn. hangi
   portun dinlendiği, aktif bağlantı sayısı) UI'da görünen bir şey var
   mı?
6. **Healing log** (Özellik #3) — `healing_log` tablosunun herhangi bir
   UI'dan görüntülenebilir olup olmadığını kontrol et (muhtemelen hayır
   — SQLite dosyası harici araçla açılıyor — ama teyit et, bu da bir
   gelecek özellik adayı olabilir).

### Bölüm C — Rakip karşılaştırması (hafif, opsiyonel)

Eğer kolayca yapılabilirse: OBS'in Settings panelinin genel
kategorilerini (Output, Audio, Video, Hotkeys, Advanced) zihinde
tut ve Reji'nin hangi kategorilerde tamamen boş olduğunu not et — bu
bir kod incelemesi değil, yalnızca kıyaslama amaçlı bir gözlem, derin
araştırma gerekmez.

---

## Faz 0 Çıktısı (bu talimatın nihai teslimatı)

1. `Kuralları Düzenle...` bulgusu (çalışıyor / stub / yarım).
2. Envanterlenmiş eksiklik listesi — her biri için: (a) kod tarafında
   zaten var mı yok mu, (b) UI'a eklemenin tahmini maliyeti (küçük/
   orta/büyük), (c) kullanıcı-değeri tahmini (düşük/orta/yüksek).
3. Önceliklendirilmiş bir öneri sırası — hangi 2-3 eksikliğin ayrı
   talimatlarla ele alınmaya en çok değeceği.

**Bunu onaya sun. Bu talimat burada biter — implementasyon kararı bu
rapordan sonra, ayrı ve odaklı talimat(lar) olarak yazılacak.**

## Sabit Kurallar

- Kod değişikliği yok, bu tamamen bir keşif turu.
- "Kod incelemesiyle doğrulandı" dışında bir dürüstlük sınıfı
  gerekmiyor (implementasyon yok, test yok).
- Bulgu belirsizse ("bu alan var mı yok mu emin değilim") tahmin
  etmek yerine "bulunamadı, ek araştırma gerekir" diye işaretle.

---

# Faz 0 Bulguları (tamamlandı — kod incelemesiyle doğrulandı)

**Durum:** DONE (keşif). Kod değişikliği yapılmadı. Tüm bulgular
find-references / kod incelemesiyle doğrulandı.

## Bölüm A — `Kuralları Düzenle...` butonu → STUB (ölü buton)

- Buton `onEditRulesClicked()` slot'una bağlı; slot yalnızca
  `emit editRulesRequested()` yapıyor (`settings_dialog.cpp:284-286`).
- `editRulesRequested()` sinyali **tüm `src/` içinde hiçbir slot'a
  `connect` edilmemiş** — yalnız `settings_dialog.h` (bildirim) ve
  `.cpp` (emit) içinde geçiyor. Butona tıklamak hiçbir şey yapmıyor.
- **`Otomatik yeniden yükle` checkbox'ı da aynı:** `autoReloadToggled(bool)`
  emit ediyor ama o sinyal de hiçbir yere bağlı değil → `rj_reload_rules`'a
  bağlı DEĞİL, işlevsiz.
- `rj_reload_rules` FFI'ı mevcut ve I24'te sertleştirilmiş
  (`ffi.rs:1324`), ama C++ tarafından hiç çağrılmıyor — kodun kendi
  yorumu bunu doğruluyor (`ffi.rs:1341`: "rj_reload_rules'ın üretimde
  C++ çağrısı yok").
- Desen: GUI turundaki CUT/FADE ile aynı sınıf — "emit ediliyor ama
  tüketilmiyor", uçtan uca yarım kalmış.

## Bölüm B — Eksiklik envanteri

Mevcut **çalışan** alanlar (gerçekten bağlı): Healing modu
(`rj_set_healing_mode`), Co-Pilot bitrate/çözünürlük/fps auto-onay
(`rj_set_action_auto_approve`), Yayın çıkışı (protokol/SRT/RTMP), WS
parolası.

| # | Eksiklik | Kodda var mı? | UI maliyeti | Kullanıcı değeri |
|---|----------|---------------|-------------|------------------|
| 1 | Bitrate/çözünürlük/FPS manuel | Evet — `Pipeline::Config`'de var ama SABİT (1920×1080/60/6000). Calibration clamp'leri `MEM_THRESHOLD_MIN=50/MAX=95` (`calibration.rs:36`) kod-içi sabit. | Küçük–Orta | Yüksek |
| 2 | Ses ayarları | Kısmen — WASAPI capture altyapısı var (channels/sample_rate/loopback), ama `audio_enabled=false` hardcoded (`main.cpp:32`) ve hep `GetDefaultAudioEndpoint` → cihaz seçimi yok. UI'da ses hiç yok. | Orta–Büyük | Yüksek |
| 3 | NVENC parametreleri | Evet — codec (H264/HEVC var, sabit H264), `gop_size=120` sabit, preset `P4 + ULTRA_LOW_LATENCY` sabit (`encode_nvenc.cpp:171,216,237`). Hiçbiri UI'da yok. | Orta | Orta |
| 4 | Kural motoru görünürlüğü | Hayır — kuralları listeleyen FFI YOK (`rj_get_rules` yok). Tam kör kutu; UI'dan ne görüntülenir ne düzenlenir. | Büyük | Orta–Yüksek |
| 5 | WS/auth detayları | Kısmen — `rj_get_ws_port()` var ama yalnız log'a yazılıyor (`command_router.cpp:84`), UI'da yok. Aktif bağlantı sayısı FFI'ı yok. | Küçük (port) / Orta (conn) | Orta |
| 6 | Healing log | Kısmen — `healing_log.sqlite` yazılıyor, ama okuma/sorgu FFI YOK. UI'dan görüntülenemiyor, harici SQLite gerekli. *Teyit edildi.* | Büyük | Orta |

## Bölüm C — OBS karşılaştırması (gözlem)

OBS: Output / Audio / Video / Hotkeys / Advanced. Reji'de:
**Output** kısmen (protokol/host/key), **Audio** tamamen boş,
**Video** tamamen boş, **Hotkeys** yok, **Advanced** yok.

---

# Nihai Önceliklendirme (kullanıcı onayı — sıra yeniden düzenlendi)

Faz 0'ın önerdiği sıra ile kullanıcının kabul ettiği nihai sıra farklı;
her ikisi de kayıt için burada. Kullanıcı üç önceliğe katıldı, sırayı
gerekçeleriyle değiştirdi.

1. **`Kuralları Düzenle` stub'ını tamamla** *(en ucuz, önce bu)* —
   FFI (`rj_reload_rules`) zaten sertleştirilmiş/hazır, maliyet en
   düşük. Şu an aktif olarak yanıltıcı bir durum var: kullanıcı butona
   basıyor, hiçbir şey olmuyor — güven kırıcı (Özellik #1'in çözmeye
   çalıştığı sorunun tersi). Düşük riskli, bariz tutarsızlığı kapatır.
   → **✅ UYGULANDI (15 Tem 2026):** `TALIMAT_KURALLARI_DUZENLE.md` +
   `SESSION_NOTES.md` "Ayarlar zenginleştirme #1". Buton + otomatik
   reload artık gerçekten çalışıyor.
2. **Video ayarları (bitrate/çözünürlük/FPS manuel)** — en iyi
   maliyet/değer oranı; OBS paritesindeki en göze çarpan boşluğu
   ("Video: tamamen boş") kapatır. `Config` alanları zaten mevcut.
3. **Ses ayarları** *(ayrı, büyük iş — kendi talimatını hak eder)* —
   Kapsamı en geniş kalem. **Faz 0 ön-koşulu:** `audio_enabled=false`
   olmasının bilinçli bir karar mı (I8/J14'teki gibi) yoksa gerçekten
   hiç bitirilmemiş bir özellik mi olduğunun netleştirilmesi — "eksik"
   demeden önce bu ayrım yapılmalı.

Bu üç kalem ayrı ve odaklı talimatlar olarak yazılacak; bu talimat
burada kapanır.
