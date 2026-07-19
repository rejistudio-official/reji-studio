# TALİMAT: Donanım Profilleme Sistemi — Kapsam Belirleme (Yalnızca Faz 0)

**Kaynak:** Kullanıcı fikri — uygulamanın kurulduğu donanımı inceleyip
üç profilden (Stabilite / Performans / Verimlilik) birine göre kendini
otomatik ayarlaması.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Uyarı

**Bu talimat yalnızca Faz 0 — hiçbir kod yazılmayacak, implementasyon
kararı verilmeyecek.** Amaç, belirsiz bir fikri ("donanıma göre kendini
ayarlasın") somut, boyutlandırılmış bir kapsama dönüştürmek — Ses
Ayarları ve Kural Motoru Görünürlüğü'nde izlenen disiplinin aynısı.

**Bu, muhtemelen bu projenin şimdiye kadarki en geniş kapsamlı Faz 0
turu olacak** çünkü tek bir bileşene değil, **üç mevcut alt sistemin
kesişimine** dokunuyor: donanım tespiti (`CapabilityDetector`), kural
setleri (Sütun 3'ün dışa/içe aktar altyapısı), ve kalibrasyon
(Özellik #5). Acele etme — bu turun tek görevi doğru soruları sormak.

**Üç profil (kullanıcının tanımı):**
- **Stabilite** — marjinal/düşük donanımda güvenli, tutucu ayarlar
  (kare düşürmeme/çökmeme önceliği).
- **Performans** — güçlü donanımda maksimum kalite (yüksek bitrate/
  çözünürlük/FPS).
- **Verimlilik** — muhtemelen laptop/batarya senaryosu için düşük güç
  tüketimi önceliği (Stabilite'den farklı — Stabilite "donanım
  yetersiz" der, Verimlilik "donanım yeterli ama batarya/güç tasarrufu
  isteniyor" der). Bu ayrımı Faz 0'da netleştir.

---

## Faz 0 — Kapsamlı Araştırma (kod yazmadan, çok parçalı)

### Bölüm A — Mevcut donanım tespiti neyi zaten biliyor

1. `CapabilityDetector`'ı (I2/I3 alanı, GPU vendor tespiti — WGC/DXGI
   yol seçimi için kullanılıyordu) bul, tam olarak neyi tespit ettiğini
   çıkar.
2. `encode_nvenc.cpp`'nin bildiği donanım sınırlarını (I23'ten —
   `maxEncodeWidth/Height`, NVENC'in gerçek encode kapasitesi) listele.
3. Bunların dışında, üç profil için gerekebilecek ama şu an **hiç
   tespit edilmeyen** sinyalleri listele (tahminen, Faz 0'da teyit et):
   - CPU çekirdek/thread sayısı.
   - Toplam RAM miktarı.
   - GPU VRAM miktarı.
   - **AC/batarya durumu** (Windows `GetSystemPowerStatus` API'si) —
     Verimlilik profilinin muhtemelen en kritik sinyali.
   - Laptop mı masaüstü mü ayrımı (varsa güvenilir bir Windows API
     yöntemi var mı, yoksa yalnızca batarya varlığından mı çıkarılabilir).

### Bölüm B — Üç profilin somut anlamı (en kritik bölüm)

Her profil için, **projenin bugün gerçekten kontrol edebildiği**
ayarları envanterle — hayali bir kontrol yüzeyi varsayma:

1. **Bitrate/FPS** (Video Ayarları'ndan — gerçek, kontrol edilebilir).
2. **Çözünürlük** — bilinen açık bir boşluk var: encode-time
   downscale mekanizması henüz yok (Todoist'te kayıtlı, "büyük iş"
   olarak ertelenmiş). Profil sistemi çözünürlüğü kontrol etmeyi
   hedefliyorsa, bu bağımlılığı açıkça yüzeye çıkar — profil sisteminin
   bir kısmı bu boşluk kapanana kadar eksik kalabilir.
3. **Healing kural eşikleri** (`rules.json`) — üç profil, gerçekte üç
   farklı `rules.json` dosyası mı olmalı (Sütun 3'ün dışa/içe aktar
   altyapısını doğrudan kullanarak — "kutu zaten var, üç önceden
   hazırlanmış kural seti hazırla ve profil seçimine göre yükle")?
   Bu, muhtemelen en doğal ve en az yeni kod gerektiren yaklaşım —
   değerlendir.
4. **Kalibrasyon başlangıç noktası** (Özellik #5) — profil, kalibrasyonun
   ilk varsayımlarını (örn. daha agresif/tutucu eşik aralığı) etkileyebilir
   mi, yoksa bu iki sistem birbirinden bağımsız mı kalmalı?
5. **Ses ayarları** — muhtemelen ilgisiz (profil kavramına dahil etme,
   gerekçesini belirt).

**Faz 0'ın bu bölümdeki görevi:** Yukarıdaki envanteri çıkarıp, "üç
profil gerçekte kaç farklı ayarı değiştiriyor" sorusuna somut bir sayı/
liste ile cevap vermek.

### Bölüm C — Mimari teslimat şekli (kritik karar sorusu)

**"Kutu önce" ilkesi (Ses Ayarları'ndan) burada da geçerli** — UI
tasarlamadan önce, profilin **nasıl uygulanacağı** netleşmeli:

1. **Seçenek 1 — Üç hazır `rules.json` + config ön-ayarı:** Her profil,
   önceden hazırlanmış bir kural seti dosyası + bir dizi varsayılan
   config değeri (bitrate/FPS) olarak paketlenir. Profil seçilince
   ilgili dosya `rj_reload_rules` ile yüklenir (Sütun 3 altyapısı
   yeniden kullanılır). Muhtemelen en düşük riskli, en az yeni kod
   gerektiren yaklaşım.
2. **Seçenek 2 — Dinamik hesaplama:** Donanım sinyallerinden çalışma
   zamanında bitrate/eşik değerleri hesaplanır (formülle). Daha esnek
   ama daha karmaşık, test edilmesi daha zor, "neden bu değer seçildi"
   açıklanabilirliği daha düşük (Özellik #1'in şeffaflık ilkesiyle
   gerilebilir — kullanıcıya "sistem böyle karar verdi" demek "kural X
   tetiklendi" demekten daha az açıklanabilir).

Faz 0, hangi seçeneğin (veya bir hibritin) daha uygun olduğunu
gerekçeli olarak öner.

### Bölüm D — Karar zamanlaması (Ses Ayarları'ndaki gibi kritik bir sınır sorusu)

1. **Yalnızca ilk kurulumda bir kez mi** (basit, kullanıcı sonra elle
   değiştirebilir)?
2. **Her açılışta yeniden mi değerlendirilir** (örn. AC'den bataryaya
   geçiş fark edilirse)?
3. **Çalışırken canlı mı izlenir** (örn. batarya bağlantısı kesilirse
   Verimlilik'e otomatik geçiş) — bu, en karmaşık ve muhtemelen bu
   turun kapsamını aşan bir seçenek, açıkça değerlendirip muhtemelen
   elemeyi öner.

### Bölüm E — Kullanıcı kontrolü

Otomatik tespit, kullanıcının override edebileceği bir öneri mi
olmalı, yoksa sessizce mi uygulanmalı? (Muhtemelen öneri + manuel
seçim — otomatik ama şeffaf, Özellik #1'in felsefesiyle tutarlı.)

---

## Faz 0 Çıktısı (bu talimatın nihai teslimatı)

1. Bölüm A-E'nin tam cevapları.
2. Gerçek boyut tahmini — (küçük/orta/büyük, hatta Ses Ayarları
   gibi MVP'ye bölünmesi gerekip gerekmediği).
3. Önerilen bir MVP tanımı (örn. "yalnızca ilk-kurulumda, yalnızca üç
   hazır `rules.json` + bitrate/FPS ön-ayarı, çözünürlük hariç, kullanıcı
   override edebilir").
4. Açık bağımlılıklar listesi (özellikle çözünürlük boşluğu).

**Bunu onaya sun. Bu talimat burada biter — implementasyon kararı bu
rapordan sonra, ayrı ve odaklı bir talimat olarak yazılacak.**

## Sabit Kurallar

- Kod değişikliği yok, bu tamamen bir keşif/kapsam turu.
- "Kod incelemesiyle doğrulandı" dışında bir dürüstlük sınıfı
  gerekmiyor.
- Emin olunmayan bir konuda (örn. bir Windows API'sinin güvenilirliği)
  tahmin etmek yerine `web_search` ile doğrula veya "ek araştırma
  gerekir" diye işaretle.
- Bu talimatın ruhu: Büyük ve belirsiz bir fikri, küçük ve gerekçeli
  bir MVP'ye indirgemek — Ses Ayarları ve Kural Motoru Görünürlüğü'nde
  izlenen yol. Kapsamın büyük çıkması sorun değil, gizlenmesi sorun.

---
---

# FAZ 0 RAPORU — Sonuçlar (Tamamlandı ve onaylandı: 2026-07-19)

> Tüm bulgular kod incelemesiyle doğrulandı. "Test edildi" iddiası yok.
> Belirsiz noktalar `⚠️` ile işaretlendi.

## Bölüm A — Mevcut donanım tespiti

- **Gerçek `CapabilityDetector`** (`src/ui/render_capability.h:30`): yalnız
  **render yolu seçimi** için vendor tespiti (AMD `0x1002`, NVIDIA `0x10DE`,
  Intel `0x8086`) → `RenderProfile`. Donanım *sınıflandırması* değil. İsim
  çakışması var — profil sistemi kendi kavramını bununla karıştırmamalı.
- **`GpuScan`** (`src/pipeline/capture/capture_dxgi.h:19`, `.cpp:192`): asıl
  envanter. Adaptör başına `description`, `vendor_id`, `dedicated_vram_mb`
  (`DXGI_ADAPTER_DESC1.DedicatedVideoMemory`). `find_encode_adapter` NVENC için
  NVIDIA `0x10DE` arıyor. → **GPU vendor + VRAM init anında biliniyor.**
- **NVENC sınırları (I23):** `maxEncodeWidth/Height` init çözünürlüğüne kilitli
  (`encode_nvenc.cpp:225`, J9/HP1) — healing yalnız tavanın altına ölçekler.
  Runtime `set_bitrate`/`set_resolution`(DRC)/`set_fps_limit` kanıtlı.
- **Metrik kaynak durumu (teyitli, `metrics_collector.cpp`):** ✅ gerçek =
  `memory_usage_pct` (GlobalMemoryStatusEx), `cpu_load_pct`/`gpu_load_pct`
  (PDH), `frame_drop_pct`, `network_*` (SRT). ❌ **STUB (hep 0)** =
  `gpu_temp_c`, `cpu_temp_c` (WMI/ADL/NVAPI hepsi `return 0`, `:107-110`).
- **HİÇ tespit edilmeyen sinyaller (grep boş):** AC/batarya
  (`GetSystemPowerStatus` kodda yok), laptop/masaüstü ayrımı, CPU çekirdek
  sayısı. Toplam RAM ⚠️ kısmen (`ullTotalPhys` okunmuyor, ucuz eklenir).
  VRAM ✅ zaten var.
- **⚠️ Windows API (araştırıldı):** `GetSystemPowerStatus` AC/batarya için
  **güvenilir**; laptop/masaüstü kesin ayrımı için tek başına yetersiz
  (UPS'li masaüstü kenar durumu) → kesinlik gerekirse WMI
  `Win32_SystemEnclosure.ChassisTypes`.

## Bölüm B — Kontrol edilebilen ayarlar

| Ayar | Durum |
|---|---|
| Bitrate | ✅ Tam (`bitrate_spin` 500–50000, `set_bitrate`, healing reduce/recover) |
| FPS | ✅ Tam (`combo_fps` 30/60/120, `set_fps_limit` tavan 120, `cap_fps`) |
| Çözünürlük | ⚠️ **Boşluk** — healing DRC var ama **kullanıcı-görünür kalıcı çıkış-çözünürlüğü kontrolü YOK** (Video ayarında yalnız bitrate+FPS). Init tavanı = capture/monitör çözünürlüğü. Kalıcı profil-downscale için doğru ölçekleyici gerekip gerekmediği ⚠️ teyitsiz (Todoist "büyük iş"). |
| `rules.json` eşikleri | ✅ Tam + dışa/içe aktar + hot-reload altyapısı hazır (Sütun 3, 16 Tem 2026) |
| Kalibrasyon (Özellik#5) | Bağımsız kalır (yalnız `memory_usage_pct`, şema değişmez) |
| Ses | Profil dışı (güç/yük profiliyle ilgisiz) |

**⚠️ Kavram çakışması:** `rules.json`'daki `modes[]` = healing otonomi modu
(AutoPilot/CoPilot/Assist/Manual), profil DEĞİL. Profil, eşik *değerlerini*
değiştirir (ayrı dosya), yeni bir `modes` ekseni icat etmez.

## Bölüm C-D-E — Onaylanan kararlar

- **C — Mimari (ONAYLANDI: Hibrit):** Donanım sinyali → **basit eşik tablosu**
  → hangi hazır profilin **önerileceğini** belirler; profil içeriği
  **statik/hazır** kalır (üç `rules.json` + config preset). Saf dinamik
  hesaplama (Seçenek 2) reddedildi — Özellik#1 şeffaflık ilkesiyle geriliyor.
- **D — Zamanlama (ONAYLANDI):** Yalnız **ilk kurulumda**, öneri olarak.
  Canlı izleme kapsam dışı (healing'in işi).
- **E — Kullanıcı kontrolü (ONAYLANDI):** Otomatik tespit → **override
  edilebilir öneri** (sessiz uygulama değil).

## Onaylanan MVP

İlk-kurulum donanım profili **önerisi**: GPU vendor + VRAM (hazır) + toplam
RAM (ucuz ekleme) + AC/batarya (yeni) → eşik tablosu → üç hazır `rules.json`
+ bitrate/FPS preset'i. **Çözünürlük hariç.** Kullanıcı override eder.
Kalibrasyon/ses bağımsız.

---
---

# FAZ 1 — TASARIM TASLAĞI (onay bekliyor)

> Kod yok. Bu belge, MVP'nin somut sayılarını/kararlarını içerir ve
> implementasyondan önce onaya sunulur. Sayılar mevcut
> `docs/config/rules.json.template` (bugünkü varsayılan) temel alınarak
> türetilmiş **ilk taslak** — kullanıcı değerlendirip ayarlayacak, sıfırdan
> üretmeyecek.

## 1. Eşik Tablosu — Donanım → Profil Önerisi (kaba, üç kural)

Yukarıdan aşağıya, **ilk eşleşen** kazanır (puanlama YOK — YAGNI):

```
1) Güç kaynağı = batarya (GetSystemPowerStatus: ACLineStatus == 0)
        → VERİMLİLİK öner
2) VRAM < 4096 MB  VEYA  toplam RAM < 8192 MB
        → STABİLİTE öner
3) Aksi halde (AC güç + yeterli VRAM/RAM)
        → PERFORMANS öner
```

**Kullanılan sinyaller:** VRAM (`GpuScan.dedicated_vram_mb`, hazır), toplam RAM
(`GlobalMemoryStatusEx.ullTotalPhys`, ekle), AC/batarya
(`GetSystemPowerStatus`, ekle). Üç sinyal, üç kural — bilinçli olarak basit.

**Tasarım notları / kenar durumlar:**
- Kural 1 önce gelir: batarya varsa VRAM ne olursa olsun Verimlilik önerilir
  (güç önceliği). Kullanıcı fişteyken bunu Performans'a override edebilir.
- ⚠️ **UPS'li masaüstü** kural 1'i yanlış tetikleyebilir. Hafifletme:
  öneri zaten override edilebilir; istenirse "batarya var **ve** deşarj oluyor"
  (`ACLineStatus == 0`) koşulu bu riski küçültür (taslakta böyle önerildi).
- ⚠️ **NVENC-uyumlu NVIDIA GPU yoksa** (`find_encode_adapter` düşerse) profil
  önerisi yine üretilir ama encode yolu farklı olabilir — bu MVP'nin varsayımı
  "NVENC var". Kenar durum tasarımda kısa bir not olarak işaretlenmeli, MVP'yi
  bloke etmemeli.
- Eşik sabitleri (4096 MB / 8192 MB) `rj::constants`'a adlandırılmış sabit
  olarak konmalı (magic number değil, coding-style).

## 2. Üç `rules.json` — Somut Eşik Farkları

Temel çizgi = **mevcut `docs/config/rules.json.template`** → değişmeden
**PERFORMANS** olur. Diğer ikisi bundan türetilir.

**⚠️ Kritik dürüstlük notu (tasarımı yönlendiriyor):** `gpu_temp_c` ve
`cpu_temp_c` STUB (hep 0) ve `RuleEngine` termal kuralları guard'layıp atlıyor
(`rules.rs:481`). Bu yüzden **profiller arası termal eşik farkı gerçekte
tetiklenmez** — üç profilin *gerçek* davranış farkı yalnız **gerçek**
metriklere dayanmalı: `frame_drop_pct`, `cpu_load_pct`, `gpu_load_pct`,
`memory_usage_pct`. Termal kurallar üç dosyada da aynı bırakılır (nötr,
gelecekte termal okuma gelince kendiliğinden anlamlanır).

### 2a. PERFORMANS (= mevcut şablon, değişmez)
- `frame_drop_mild` (5–10% → −250), `frame_drop_high` (>10% → −500),
  `frame_drop_recovery` (<5% → +250), `cpu_load_high` (>90 → cap 30),
  `memory_pressure` (>85 → scale 0.25), termal kurallar (stub, nötr).
- `hysteresis_ms: 10000`.
- **Preset:** bitrate **12000 kbps**, FPS **60**.

### 2b. STABİLİTE (daha erken + agresif, gerçek metriklere dayalı)
Marjinal/düşük donanımda güvenli — kare düşürmeme/çökmeme önceliği.

| Kural | Performans | → Stabilite | Gerekçe |
|---|---|---|---|
| `frame_drop_mild` | `5–10%`, −250 | **`3–8%`, −300** | Daha erken, daha büyük adım |
| `frame_drop_high` | `>10%`, −500 | **`>8%`, −750** | Daha erken, daha sert |
| `frame_drop_recovery` | `<5%`, +250 | **`<3%`, +150** | Temkinli geri tırmanış |
| `cpu_load_high` | `>90`, cap 30 | **`>75`, cap 30** | CPU baskısına erken tepki |
| `memory_pressure` | `>85`, scale 0.25 | **`>70`, scale 0.5** | Erken, ama daha az sert ölçek |
| **YENİ** `gpu_load_high` | — | **`>80`, −500 bitrate** | GPU yükünü de kap (gerçek metrik, profilde kullanılmıyordu) |
| termal kurallar | (stub) | aynı | Nötr — stub |

- `hysteresis_ms: 6000` (daha hızlı tepki).
- **Preset:** bitrate **6000 kbps**, FPS **30** (güvenli tavan).

### 2c. VERİMLİLİK (güç tasarrufu ekseni — Stabilite'den farklı)
Donanım *yeterli* ama batarya/güç tasarrufu isteniyor. Panik değil, **güç
önceliği**: düşük tavan + yükü erken kısarak GPU/CPU'yu düşük tutmak.

| Kural | Performans | → Verimlilik | Gerekçe |
|---|---|---|---|
| `frame_drop_*` | (aynı) | aynı | Donanım yeterli, kare düşürme derdi yok |
| `cpu_load_high` | `>90`, cap 30 | **`>80`, cap 30** | Güç için FPS'i erken kıs |
| **YENİ** `gpu_load_high` | — | **`>80`, cap 30** | GPU'yu düşük tutmak = güç tasarrufu |
| `frame_drop_recovery` | `<5%`, +250 | **çıkarıldı** | Düşük tavanda yüksek bitrate'e tırmanma istenmiyor |
| `memory_pressure` | `>85`, 0.25 | aynı | Değişmez |
| termal kurallar | (stub) | aynı | Nötr |

- `hysteresis_ms: 10000`.
- **Preset:** bitrate **4500 kbps**, FPS **30** (düşük güç tavanı).

> **Ayrımın özeti:** Stabilite "donanım yetersiz → güvenli kal" (eşikler erken,
> tepki hızlı). Verimlilik "donanım yeterli → az güç harca" (tavan düşük, yük
> erken kısılır, geri tırmanış yok). İkisi farklı eksende — talimatın istediği
> ayrım korundu.

## 3. Teslimat Mekaniği (mevcut altyapıyı yeniden kullanır)

- Üç dosya `docs/config/profiles/` altında gömülü kaynak olarak paketlenir
  (mevcut `rules.json.template` gömme deseni — `rules_template.qrc`/AUTORCC).
- Profil seçimi → ilgili dosyayı `~/.reji/rules.json`'a yaz →
  **watcher/`rj_reload_rules`** zaten reload eder (Sütun 3'te kurulu
  doğrula-önce-yaz + `.backup` yolu aynen kullanılır, çift-reload'dan kaçın).
- Bitrate/FPS preset'i encoder init'e giden config'e uygulanır (mevcut
  `bitrate_spin`/`combo_fps` değerlerini set eder — kullanıcı sonra elle
  değiştirebilir).
- **Yeni FFI beklenmiyor** (yalnız mevcut reload + config yolu).

## 4. UI / Akış (ilk kurulum)

- İlk açılışta bir kez: sistem donanıma bakar → öneri kutusu:
  *"Donanımınız için **Performans** profili öneriliyor (NVIDIA, 12 GB VRAM,
  AC güç). [Uygula] [Profili değiştir ▾] [Manuel]"*.
- Kullanıcı seçer/override eder; seçim `QSettings`'e yazılır (bir daha
  otomatik sormaz — "ilk kurulum" sınırı).
- Şeffaflık: hangi sinyalin hangi profili tetiklediği kutuda **görünür**
  (Özellik#1 felsefesi — "sistem böyle karar verdi" değil, "şu sinyaller
  şu profili öneriyor").

## 5. Kapsam Sınırları (MVP dışı — bilinçli)

- ❌ Çözünürlük profili (B.2 boşluğu — ayrı, önce çözünürlük kontrolü işi).
- ❌ Her açılışta yeniden değerlendirme / canlı izleme (Faz 2+).
- ❌ Kalibrasyonla birleşme (bağımsız kalır).
- ❌ Puanlama/formül tabanlı dinamik hesaplama (reddedildi — Bölüm C).
- ❌ WMI chassis kesinliği (batarya sezgiseli MVP'ye yeter; gerekirse sonra).

## 6. Açık Sorular (onayda netleşecek)

1. **Preset sayıları** (12000/6000/4500 kbps, FPS 60/30/30) — kullanıcı ayarlar.
2. **Eşik sabitleri** (VRAM 4 GB, RAM 8 GB) — makul mü, yoksa farklı sınır mı?
3. **Verimlilik'te `frame_drop_recovery` gerçekten çıkarılsın mı**, yoksa düşük
   step'le (+150, düşük tavana kadar) mı kalsın?
4. **UPS kenar durumu** için "batarya var **ve** deşarj" koşulu benimsensin mi?

**Bu taslak onaya sunulur. Onaylanınca Faz 2 (implementasyon, küçük
commit'ler) ayrı yürütülür.**

### Faz 1 — Onaylanan Kararlar (2026-07-19)

1. **Preset sayıları ONAYLANDI:** Performans 12000 kbps / 60 FPS · Stabilite
   6000 / 30 · Verimlilik 4500 / 30.
2. **Eşik sabitleri ONAYLANDI:** VRAM 4096 MB, RAM 8192 MB (kaba olması
   bilinçli; override koruması var).
3. **Verimlilik `frame_drop_recovery` → tamamen ÇIKARILDI** (sabit-düşük güç
   felsefesi; geri tırmanış güç dalgalanması yaratır).
4. **UPS kenar durumu → MVP DIŞI.** Eşik kuralı 1 sade kalır:
   `ACLineStatus == 0` → Verimlilik. "Deşarj oluyor" ek koşulu eklenmez (YAGNI).

---
---

# FAZ 2 — İMPLEMENTASYON TALİMATI (küçük commit'ler, her aşama onaya sunulur)

> Kod bu talimatta yazılmaz — bu, Faz 2'yi yürütecek ayrı oturum(lar) için
> commit-bazlı yürütme planıdır. Her commit bağımsız test edilebilir ve push
> öncesi onaya sunulur. TDD: saf mantık için testler önce.

## Genel İlkeler (bu iş için)

- **Küçük commit'ler**, her biri tek sorumluluk; push öncesi onay.
- **Yeni FFI YOK** beklenir — yalnız mevcut `rj_reload_rules` + config yolu.
  Eğer implementasyon sırasında FFI gereği çıkarsa **DUR, raporla** (varsayım
  çürüdü).
- **Dürüstlük sınıfı raporda açık:** "test edildi" vs "kod incelemesiyle
  doğrulandı" ayrımı. GUI görsel doğrulaması kullanıcıda kalır.
- `tests/baseline_metrics.txt` asla commit edilmez.
- Sabitler (`4096`, `8192`, preset değerleri) `rj::constants`'a adlandırılmış
  sabit olarak — magic number yok.

## Commit 1 — Üç profil veri dosyası + gömme

- `docs/config/profiles/performance.json` = **mevcut `rules.json.template`'in
  birebir kopyası** (temel çizgi).
- `docs/config/profiles/stability.json` ve `efficiency.json` — Faz 1 tablo 2b/2c
  eşikleriyle. **`modes[]` dizileri şablonla AYNI** (healing otonomi modlarıyla
  uyum bozulmasın), yalnız `condition`/`params`/`hysteresis_ms` değişir.
  - Stabilite: mild `3–8%`/−300, high `>8%`/−750, recovery `<3%`/+150,
    cpu_load `>75`/cap30, **yeni** gpu_load `>80`/−500 (`bitrate_reduce`),
    memory `>70`/scale0.5, termal aynı, `hysteresis_ms: 6000`.
  - Verimlilik: frame_drop kuralları Performans ile aynı, **recovery kuralı
    YOK**, cpu_load `>80`/cap30, **yeni** gpu_load `>80`/cap30, memory aynı,
    termal aynı, `hysteresis_ms: 10000`.
- `rules_template.qrc`'ye üç dosyayı ekle (AUTORCC gömer;
  `:/config/profiles/*.json`).
- **Doğrulama:** Üçünün de `RuleEngine`/`hot_reload` şema doğrulamasından
  geçtiğini gösteren bir test (Rust `rules.rs` testi veya mevcut import-validate
  yolu). Bozuk alan → parse hatası kanıtı.

## Commit 2 — Donanım sinyali toplama (saf veri + Windows API)

- Yeni küçük modül (öneri: `src/ui/profile_advisor.{h,cpp}` — profil önerisi bir
  ilk-kurulum/UI concern'ü, C++ tarafı doğal ev).
- Saf, POD sinyal yapısı: `struct HwSignals { uint32_t vendor_id; uint64_t
  vram_mb; uint64_t total_ram_mb; bool on_battery; }`.
- Toplama fonksiyonları:
  - VRAM + vendor: mevcut `GpuScan` sonucundan (`dedicated_vram_mb`) — yeniden
    icat etme, encode adaptörünün değerini kullan.
  - Toplam RAM: `GlobalMemoryStatusEx(...).ullTotalPhys` (yeni okuma;
    `metrics_collector` yalnız yüzde kullanıyordu).
  - `on_battery`: `GetSystemPowerStatus(...).ACLineStatus == 0`.
- **Doğrulama:** Toplama fonksiyonları I/O'ya bağlı olduğundan burada yalnız
  kod incelemesi + elde çalıştırıp değerleri loglama (kullanıcı gözlemi);
  saf karar mantığı Commit 3'te izole test edilir.

## Commit 3 — Eşik tablosu (saf fonksiyon) + birim testleri [TDD]

- Saf fonksiyon: `ProfileId suggest_profile(const HwSignals&)` — üç kural,
  ilk eşleşen:
  1. `on_battery` → `Efficiency`
  2. `vram_mb < kVramLowMb (4096)` **||** `total_ram_mb < kRamLowMb (8192)` →
     `Stability`
  3. aksi halde → `Performance`
- **TDD — testler ÖNCE** (C++ Google Test, `tests/`):
  - batarya → Efficiency (VRAM yüksek olsa bile; kural 1 önce).
  - düşük VRAM (3072) + AC → Stability.
  - düşük RAM (4096) + AC + yüksek VRAM → Stability.
  - yüksek VRAM + yüksek RAM + AC → Performance.
  - sınır: VRAM tam 4096 / RAM tam 8192 → Performance (`<` kesin sınır).
- Saf fonksiyon → tam test kapsamı hedeflenebilir.

## Commit 4 — Profil uygulama yolu (dosya yaz + reload + preset)

- `ProfileId` → ilgili gömülü `:/config/profiles/<id>.json`'u **Sütun 3'ün
  içe-aktarım güvenlik akışıyla** uygula: geçici konumda `rj_reload_rules` ile
  doğrula → `rules.json.backup` al → `~/.reji/rules.json`'a yaz →
  watcher/elle reload (çift-reload'dan kaçın — Sütun 3 kararı aynen).
  **Yeni yol icat etme**, mevcut import fonksiyonunu genelleştir/yeniden kullan.
- Preset uygula: profilin bitrate/FPS'ini `bitrate_spin`/`combo_fps`'e ve
  encoder init config'ine yaz. **İlk-kurulum → yayın başlamadan** uygulanır,
  canlı reconfigure gerekmez (basit).
- **Doğrulama:** Bir profil uygulanınca `~/.reji/rules.json`'un ilgili dosyayla
  eşleştiği + `.backup`'ın oluştuğu; bozuk profil senaryosunun mevcut kuralları
  dokunmadan bıraktığı (en kritik senaryo, Sütun 3'ten miras).

## Commit 5 — İlk kurulum UI + kalıcılık

- İlk açılışta bir kez: `QSettings` "profil_soruldu" bayrağı yoksa → öneri
  kutusu. `suggest_profile` sonucunu + **tetikleyen sinyalleri görünür** göster
  (vendor, VRAM, güç kaynağı). [Uygula] [Değiştir ▾] [Manuel].
- Seçim sonrası bayrağı yaz — bir daha otomatik sormaz.
- Mevcut `QFileDialog`/dialog + `lbl_rules_` geri-bildirim desenleriyle tutarlı.
- **Doğrulama:** Mantık (bayrak okuma/yazma, öneri metni) kod incelemesi +
  birim test edilebildiği kadar; **görsel akış kullanıcıda** (öneri kutusu,
  override, tekrar-sormama).

## Commit 6 — Dokümantasyon

- `ROADMAP.md`: donanım profili özelliğini uygun bölüme "implemente edildi"
  notuyla ekle (Farklılaşma Stratejisi ile ilişkilendir — Sütun 1 şeffaflık +
  Sütun 2 hibrit-GPU laptop nişiyle örtüşür).
- `SESSION_NOTES.md`: özet.
- Bu talimatı "Faz 2 tamamlandı" olarak işaretle.
- `FFI_CONTRACT.md`'ye dokunma (yeni FFI yok — beklenti bu; aksi çıkarsa dur).

## Faz 3 — Test ve Dürüstlük Sınırları

- **Birim:** `suggest_profile` tam kapsam (Commit 3). Üç profil json'un parse
  edildiği + bozuğun reddedildiği.
- **Regresyon:** Mevcut `rules`/healing testleri + Sütun 3 import/watcher
  testleri PASS kalmalı (aynı yolu yeniden kullandığımız için kritik).
- **Kullanıcıda kalan görsel doğrulama:** ilk-kurulum öneri kutusu, override,
  profil değişince gerçek bitrate/FPS + kural setinin uygulanması.
- **Bilinen dürüstlük sınırı (rapora yazılacak):** `gpu_temp_c`/`cpu_temp_c`
  STUB olduğundan profiller arası **termal** fark gerçekte tetiklenmez; profil
  davranış farkı yalnız gerçek metriklerde (frame_drop/cpu_load/gpu_load/memory)
  gözlemlenebilir. Bu, bir kusur değil bilinçli kapsam — açıkça belirtilmeli.

## Sabit Kurallar (hatırlatma)

- Küçük commit'ler; tamamlanınca push öncesi onay.
- Varsayımla çelişen bulguda (özellikle "yeni FFI yok" veya import yolunun
  yeniden kullanılabilirliği) **dur, raporla**.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı her raporda açık.

---
---

# FAZ 2 — TAMAMLANDI (20 Temmuz 2026)

Dal `feat/hardware-profiling`, 6 commit, her biri push öncesi onaylandı. Plandaki
varsayımlar tuttu: **yeni FFI yok** (`FFI_CONTRACT.md`'ye dokunulmadı), import
yolu (Sütun 3) yeniden kullanıldı.

| Commit | İçerik | Doğrulama |
|---|---|---|
| 1 | Üç profil json + qrc gömme | 4 Rust testi (şema + değişmezler) PASS |
| 2 | `profile_advisor` sinyal toplama (RAM/batarya) | reji_ui build OK |
| 3 | `suggest_profile` saf fonksiyon | TDD RED→GREEN, 7 test |
| 4 | `applyProfile` + `writeValidatedRules` (DRY) + preset | 11 test, reji_app link OK |
| 5 | İlk-kurulum öneri UI + `max_gpu_vram_mb` | reji_app link OK |
| 6 | Dokümantasyon | — |

**Onaylanan MVP birebir uygulandı:** ilk-kurulum önerisi, üç hazır `rules.json` +
bitrate/FPS preset, çözünürlük hariç, override edilebilir, kalibrasyon/ses
bağımsız. Eşik tablosu ve preset sayıları Faz 1'de onaylanan değerlerle.

**Dürüstlük sınırı:** Saf mantık (suggest/preset) + profil json şeması otomatik
test edildi; GUI ilk-kurulum akışının görsel doğrulaması kullanıcıda. Termal
metrikler STUB olduğundan profiller arası termal fark gerçekte tetiklenmez
(bilinçli kapsam). Bkz. `SESSION_NOTES.md` "Donanım Profilleme" + `ROADMAP.md`
Farklılaşma Stratejisi Sütun 4.

**Kapsam dışı kalan (gelecek iş):** çözünürlük profili (çıkış-çözünürlüğü kontrolü
boşluğu — B.2), her açılışta yeniden değerlendirme, kalibrasyonla birleşme,
WMI chassis kesinliği.
