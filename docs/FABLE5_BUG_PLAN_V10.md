# FABLE5_BUG_PLAN_V10.md — Reji Studio Dördüncü Nesil Bug Planı (V9-Sonrası Yeni Kod)

**Durum:** 🟡 İSKELET — tarama henüz yapılmadı. Kapsam paketi ve ortak
prompt hazır (`docs/V10_TARAMA_PROMPT.md`), üç modele bağımsız tarama
kullanıcı tarafından yaptırılacak.

**Hazırlayan:** _(sentez sonrası doldurulacak — üç bağımsız model
incelemesinin sentezi)_
**Kaynak incelemeler:** _(yer tutucu — model başına: ad, dosya sayısı,
token, tarih/saat)_
- Claude Fable 5 — _(tarih/saat, kapsam)_
- Claude Opus 4.8 — _(tarih/saat, kapsam)_
- GLM 5.2 — _(tarih/saat, kapsam)_

**İlişki:** V9 planı (J1-J16 + HP1-HP4) 14.07.2026'da, K-serisi
(K1-K7 Vulkan/GL interop) hemen ardından kapandı. Bu tur, **V9
kapanışından (`efe0fec`) sonra eklenen ve hiç bağımsız taramadan
geçmemiş** kod kütlesini hedefliyor: ses pipeline'ı, donanım
profilleme, kural yönetimi zinciri, WS/Ayarlar genişletmeleri, ISource
katmanı ve orchestrator healing/telemetri zinciri (Özellik#1-5 +
GetStats). Tetikleyici: son canlı GUI testlerinin bu bölgede üç gerçek
bug bulması (hot-reload kuruluş-sırası `449c084`, içe aktarım
kopyalama `c99f1b6`, dışa aktarım kör-kopyalama `e36176e`). Amaç:
Faz 3 wiring'i bu bölgeye dokunmadan önce temiz baseline.

---

## Metodoloji ve Güvenilirlik Notu

Bu belge (dolduğunda), **henüz Claude Code tarafından doğrulanmamış**
ham bulguların sentezi olacak. V8/V9 boyunca defalarca kanıtlandığı
gibi (I2/I3, I29/I31, J10/J11 çürütmeleri), bir bulgunun planda yer
alması doğru olduğu anlamına gelmez — yalnızca **araştırılmaya değer**
olduğu anlamına gelir. Her madde, ele alınmadan önce güncel `master`'a
karşı Faz 0 doğrulamasından geçmelidir (proje disiplini, istisnasız).

**Çapraz doğrulama derecesi** (her maddede belirtilecek):
- 🟢 **3/3** — üç inceleyicinin de bağımsız bulduğu madde (en yüksek güven)
- 🟡 **2/3** — iki inceleyicinin bulduğu madde
- 🔵 **1/3** — tek inceleyicinin bulduğu, kod parçasıyla desteklenen madde (doğrulama şart)

**Numaralandırma:** Bu turun maddeleri **L** öneki alır (V8=I, V9=J,
Vulkan/GL turu=K).

---

## Kapsam Özeti

Tam liste ve kapsam-dışı kalemler: `docs/V10_TARAMA_PROMPT.md`
(kanonik). Özet — `git diff efe0fec..master`, docs hariç 88 dosya,
~7.250 satır ekleme:

| Grup | Bölge | Öne çıkan riskler |
|---|---|---|
| 1 | Ses pipeline'ı (audio/*, output_subsystem, rtmp_transport.zig) | SPSC thread sınırları, MF/COM yaşam döngüsü, yeni FFI/ABI yüzeyi |
| 2 | Donanım profilleme (profile_advisor, profiles/*.json, applyProfile) | Karar mantığı, profil-içerik tutarlılığı |
| 3 | Kural yönetimi zinciri (import/export/hot-reload, snapshot FFI) | Son üç bug'ın bölgesi — Qt dosya-sistemi hata yolları |
| 4 | WS/Ayarlar (ConnectionGuard, QTabWidget, bitrate_policy) | RAII çıkış yolları, persistence |
| 5 | ISource katmanı (i_source.h, ExistingDesktopSource) | Kontrat/adapter doğruluğu (izole, wire edilmemiş) |
| 6 | Orchestrator healing/telemetri (calibration, healing_log, sys_stats, VendorEvent) | SQLite yazma yolları, fan-out, FFI deltası |

Kapsam dışı: V8/V9/K-serisinin sertleştirdiği eski bölgeler,
`run_frame()` capture-wiring (Faz 3 değiştirecek), 12 kalemlik
bilinen/bilinçli açık listesi (prompt §3c — yanlış-pozitif önleme).

---

## Özet Tablo

_(tarama + sentez sonrası doldurulacak)_

| # | Madde | Doğrulama | Şiddet | Durum |
|---|---|---|---|---|
| L1 | _(yer tutucu)_ | | | |

---

## Bulgular

_(tarama + sentez sonrası buraya eklenecek — her madde: konum, iddia,
çapraz doğrulama derecesi, kanıt, Faz 0 doğrulama sonucu, düzeltme
commit'i)_

---

## Süreç Notları

- [ ] Üç model taraması çalıştırıldı (kullanıcı) — rapor dosyaları: _(yol)_
- [ ] Sentez yapıldı, L-numaraları atandı
- [ ] Linear'da V10 issue açıldı _(bkz. talimat Bölüm D — tarama
      sonuçları bağlandığında güncellenecek)_
- [ ] Sprint'lere bölündü, Faz 0 doğrulamaları başladı
- Tamamlanınca `TALIMAT_V10_TARAMA_HAZIRLIK.md` → `docs/talimatlar/`
  arşivine taşınacak.
