# TALİMAT: Ayarlar UX — Kalan Üç Fikir (Madde 6)

**Kaynak:** GUI Gözlem Turu'ndan doğan "Ayarlar penceresi UX geliştirmesi
(Madde 6)" — Video ve Ses ayarlarından **farklı**, üç ayrı küçük/orta
fikir.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü

Üç bağımsız alt-parça, farklı büyüklüklerde. Her biri kendi Faz 0'ından
geçecek, birbirine bağımlı değiller — istersen sırayla, istersen
paralel raporlanabilir.

---

## Bölüm A — Mod/CoPilot checkbox değişikliklerine anlık bildirim

**Sorun:** Healing modu veya per-kategori auto-onay checkbox'ları
değiştirildiğinde, etki yalnızca **bir sonraki healing aksiyonu
tetiklendiğinde** görünür oluyor (GUI Gözlem Turu'nda "beklenen davranış,
bug değil" diye kapatılmıştı) — ama kullanıcıya bu bekleyişin kendisi
hiç belirtilmiyor, "ayar kaydedildi mi" belirsizliği kalıyor.

### Faz 0 (kısa)
1. Mevcut `lbl_status_`/`lbl_rules_` gibi durum-bildirim widget'larının
   desenini incele (Kuralları Düzenle'de kurulan ayrı-kalıcı-widget
   deseni buraya da uyarlanabilir mi?).
2. Settings dialog'un OK/Apply akışını (`onOkClicked`) izle — ayar
   gerçekten Rust'a ulaştığında (I19 deseni) bir geri bildirim
   üretilebilecek nokta var mı?

### Faz 1 (kısa onay)
Basit bir toast/durum mesajı: "Mod: CoPilot — bir sonraki healing
aksiyonunda etkili olur" gibi, OK'e basınca kısa süreliğine görünen bir
bildirim. Yeni bir mekanizma icat etme — mevcut durum-widget desenini
kullan.

---

## Bölüm B — CUT/FADE'in Faz 3'e bağımlılığını belirtmek

**Gerginlik notu (önemli):** GUI Gözlem Turu'nda bu konuda zaten bir
tartışma yapılmıştı — butonları **devre dışı bırakmak** o zaman
reddedilmişti çünkü "butonlar teknik olarak çalışıyor (gerçek shader
geçişi), yalnızca görünür bir fark yaratmıyor çünkü tek kaynak var" —
devre dışı bırakmak, çalışan bir mekanizmayı yanlışlıkla "kırık" gibi
göstermek olurdu. Şimdi bu fikir tekrar gündemde — **çelişkiyi çöz,
körü körüne ikisinden birini seçme.**

### Faz 0 (zorunlu, bu gerginliği netleştirsin)
1. Butonları tamamen devre dışı bırakmadan, yalnızca **bilgilendirici**
   bir işaret eklemenin mümkün olup olmadığını değerlendir — örn. bir
   tooltip ("Şu an tek kaynak var, sahne kompozisyonu Faz 3'te
   gelecek") veya küçük bir ikon/rozet. Buton **tıklanabilir ve
   işlevsel kalmalı** (mevcut shader geçişi çalışmaya devam etmeli),
   yalnızca kullanıcı beklentisini doğru ayarlayan bir ipucu eklensin.
2. Bunun GUI Gözlem Turu'ndaki "devre dışı bırakma" kararıyla
   çelişmediğini teyit et — çelişiyorsa (örn. tooltip eklemek teknik
   olarak devre dışı bırakmayı gerektiriyorsa), dur ve hangi kararın
   geçerli olacağını sor.

### Faz 1 (kısa onay)
Faz 0 bulgusuna göre — muhtemelen yalnızca bir `QToolTip` veya benzeri,
düşük riskli bir ekleme.

---

## Bölüm C — Genel ayarlar zenginleştirmesi (yalnızca araştırma)

**Bu bölüm implementasyon değil, yalnızca envanter** — "zenginleştir"
çok belirsiz bir talep, Ayarlar Araştırması'nın orijinal Bölüm B'sinde
zaten iki aday bulunmuştu ama henüz talimata dönüşmedi:
- **Kural motoru görünürlüğü** (madde 4, Ayarlar Araştırması'nda "büyük"
  olarak işaretlenmişti — kuralları GUI'den görüntüleme/düzenleme).
- **WS/auth detayları** (madde 5, "küçük-orta" — port/aktif bağlantı
  sayısı gösterimi).

### Faz 0 (yalnızca güncelleme, yeniden keşif değil)
1. Bu iki adayın hâlâ geçerli olup olmadığını kısaca teyit et (Video/Ses
   ayarları eklendiğinden beri kod değişmiş olabilir).
2. Video ve Ses ayarlarının eklenmesiyle Settings dialog'un genel
   görünümünün nasıl değiştiğini gözden geçir — belki başka boşluklar
   artık daha belirgin hale gelmiştir (örn. tutarlı bir kategori
   düzeni eksikliği).
3. **Yeni bir öncelik listesi üret, ama implementasyona geçme.**

**Faz 0 çıktısı:** Güncellenmiş, önceliklendirilmiş bir aday listesi.
Kullanıcıya sun — hangisinin (varsa) ayrı bir talimata dönüşeceğine
kullanıcı karar verecek.

---

## Sabit Kurallar

- Bölüm A ve B küçük, CLAUDE.md Bölüm 8b'ye göre dal kararı ver
  (muhtemelen ikisi birlikte tek küçük dal veya `master`'a doğrudan,
  büyüklüğe göre).
- Bölüm C'de kod yazma — yalnızca rapor.
- `tests/baseline_metrics.txt` asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bölüm B'deki gerginlik netleşmeden implementasyona geçme — bu
  talimatın en hassas noktası.
