# Not: docs/ROADMAP.md'ye eklenecek metin (kurtarılan, eksik kalmış bölüm)

Video/Ses Ayarları MVP'sinin tamamlandığı bölümün (2026-07-17 tarihli)
hemen ardına ekle — bu fikir o dönemde, ses kodek kararı sırasında
doğmuştu.

---

## Gelecek Fikir — H.265/AV1 Kodek Desteği (henüz taahhüt edilmedi)

**İçerik:** Şu an video kodek olarak yalnızca H.264 (NVENC üzerinden)
kullanılıyor. H.265 (HEVC) ve AV1'e genişletme fikri kayda geçirildi —
hiçbir araştırma/tasarım yapılmadı, yalnızca fikrin kaybolmaması için
not düşüldü.

**Bilinen teknik zemin (doğrulanmış, ama derinlemesine araştırılmadı):**
- Hedef donanım (RTX 4070 Laptop) NVENC üzerinden hem H.265 hem AV1
  donanım encode'unu destekliyor — teknik olarak mevcut donanımda mümkün.
- **Lisans riski (H.265):** HEVC üç ayrı patent havuzu (MPEG LA, HEVC
  Advance, Velos Media) altında lisanslanıyor — açık kaynak bir proje
  için AAC/FDK-AAC kararında (Ses Ayarları MVP'si) uygulanan aynı
  dikkatle değerlendirilmesi gerekiyor. AV1 lisans-ücretsiz, bu açıdan
  daha güvenli.
- **Konteyner kısıtı:** Klasik FLV (mevcut RTMP mux yolu) yalnızca
  H.264'ü standart taşır — Ses Ayarları MVP'sinde libopus'un aynı
  sebeple (klasik FLV'de standart taşınmaması) elenmesiyle birebir aynı
  kısıt. H.265/AV1 için "Enhanced RTMP" spesifikasyonuna (Twitch'in de
  parçası olduğu bir konsorsiyum tanımı) geçmek gerekir — yalnızca
  encoder değişikliği değil, mux katmanının da güncellenmesi anlamına
  gelir.
- **Platform desteği karışık:** YouTube AV1 ingest'i destekliyor,
  Twitch'in desteği sınırlı/gelişmekte. H.265 desteği her iki platformda
  da tutarsız.

**Durum:** Yalnızca fikir kaydı — aktif değerlendirme yok, önkoşul yok,
taahhüt yok. İleride ele alınırsa, Ses Ayarları'nda izlenen sırayla
(önce encoder/lisans araştırması, sonra mux/konteyner araştırması, sonra
tasarım) ilerlemesi önerilir — aynı "kutu önce" dersi burada da geçerli.
