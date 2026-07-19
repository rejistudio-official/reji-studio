# TALİMAT: Ayarlar Kategori Düzeni — QTabWidget

**Kaynak:** Ayarlar UX Madde 6, Bölüm C — P2. Video+Ses eklendikten
sonra Settings dialog'un 7 `QGroupBox`'ı tek sütunda istiflenmesi
(saf UI, FFI yok, düşük risk).
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

Settings dialog şu an tüm ayar gruplarını (Healing Modu, Co-Pilot
Aksiyon Ayarları, Kural Yönetimi, Video, Yayın Çıkış, Ses, WS) tek bir
uzun dikey sütunda gösteriyor. Bunu `QTabWidget` ile kategorilere
ayırmak — OBS'in Output/Audio/Video/Advanced sekme deseniyle benzer bir
düzen.

**Bu bir görsel yeniden düzenleme** — hiçbir widget'ın kendisi, sinyal/
slot bağlantısı, ya da FFI çağrısı değişmiyor, yalnızca hangi konteynerde
göründükleri değişiyor. Risk düşük, ama Faz 0'da mevcut yapının tam
haritası çıkarılmadan implementasyona geçilmeyecek — yanlışlıkla bir
widget'ı bağlantısından koparma riski var.

---

## Faz 0 — Doğrulama (kod yazmadan)

1. `settings_dialog.cpp`'deki tüm `QGroupBox`'ları ve mevcut layout
   yapısını (hangi sırayla, hangi parent'a ekleniyor) tam olarak çıkar.
2. Her grubun içindeki widget'ların sinyal/slot bağlantılarının grup
   taşınırken bozulmayacağını teyit et (Qt'de parent değişimi genelde
   sinyal/slot'u etkilemez, ama dialog'un `exec()` ile tekrar tekrar
   açılma deseniyle — main_window.cpp:668 — bir etkileşim olup
   olmadığını kontrol et).
3. Mantıklı sekme gruplandırmasını öner (örnek, Faz 0'da teyit/düzelt):
   - **Video:** Video Ayarları grubu.
   - **Ses:** Ses Ayarları grubu.
   - **Yayın Çıkışı:** Yayın Çıkış Ayarları (SRT/RTMP) + WS grubu (WS
     bir kontrol kanalı, çıkışla ilgili değil — ayrı bir "Ağ"/"Uzaktan
     Kontrol" sekmesi de düşünülebilir, Faz 0'da karar ver).
   - **Self-Healing:** Healing Modu + Co-Pilot Aksiyon Ayarları + Kural
     Yönetimi (üçü de aynı konuya ait, doğal bir grup).
4. Dialog'un mevcut boyut/geometri varsayımlarını (sabit genişlik/
   yükseklik var mı) kontrol et — sekmelere bölünce boyut ihtiyacı
   değişebilir.

**Faz 0 çıktısı:** Tam widget haritası + önerilen sekme gruplandırması
(4 sekme öneriliyor, Faz 0 bulgusuna göre ayarlanabilir). Onaya sun.

## Faz 1 — Tasarım (kısa onay yeterli olabilir)

1. `QTabWidget` kurulumu — mevcut `QVBoxLayout`'un yerini alacak
   şekilde, her sekme bir `QWidget` + kendi `QVBoxLayout`'u içinde
   ilgili `QGroupBox`'ları barındırır.
2. Sekme isimleri: Faz 0'ın önerdiği gruplandırmaya göre (örn. "Video",
   "Ses", "Yayın Çıkışı", "Self-Healing").
3. Varsayılan açılış sekmesi — muhtemelen en sık kullanılan (Video veya
   Yayın Çıkışı), Faz 0 bulgusuna göre karar ver.
4. Mevcut buton/OK-Cancel akışı değişmiyor — yalnızca üst içerik
   sekmelere bölünüyor.

Tasarımı onaya sun, implementasyondan önce.

## Faz 2 — İmplementasyon (küçük, muhtemelen tek commit)

CLAUDE.md Bölüm 8b'ye göre değerlendir — FFI yok, güvenlik-hassas değil,
muhtemelen `master`'a doğrudan gidebilir. (Önceki talimatta bu tahmin
yanlış çıkmıştı — bu sefer FFI olmadığından gerçekten uygulanabilir,
ama yine de kendi değerlendirmeni yap, körü körüne bu notu kabul etme.)

## Faz 3 — Test ve Dürüstlük Sınırları

- Derleme + link doğrulaması (UI değişikliği, birim testi genelde
  uygulanamaz — Qt widget testleri bu projede yok, GUI Gözlem Turu'nda
  da bu sınır kabul edilmişti).
- **GUI görsel doğrulaması kullanıcıda kalacak** — sekmelerin doğru
  göründüğünü, tüm ayarların hâlâ erişilebilir ve çalışır olduğunu
  (özellikle her widget'ın sinyal/slot bağlantısının bozulmadığını)
  gözlemlemek.
- Kullanıcıya görünen davranış değişikliği: Ayarlar penceresinin görsel
  düzeni değişiyor (sekmeli), işlevsellik aynı kalıyor — bunu açıkça
  belirt.

## Sabit Kurallar

- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık —
  bu işte çoğu doğrulama kod incelemesi + derleme olacak, GUI'nin
  gerçek görsel doğruluğu kullanıcıda.
- Faz 0'da bir widget'ın taşınmasının sinyal/slot'unu bozma riski
  bulunursa, o widget'ı olduğu yerde bırak, gerekçele — tüm grupları
  zorla taşımak zorunlu değil.
