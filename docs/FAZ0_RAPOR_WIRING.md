# Faz 0 Raporu: ExistingDesktopSource Wiring — Açık Kararlar

**Tarih:** 2026-07-29 · **Yöntem:** yalnız kod incelemesi (kod yazılmadı,
test koşulmadı) · **Durum:** Üç karar da kullanıcı tarafından ONAYLANDI.
**Ön koşul:** Ön-kontrol (ON_KONTROL_WIRING_BAYATLIK.md) tamamlandı;
TALIMAT_EXISTINGDESKTOPSOURCE_WIRING.md'ye iki ek işlendi (ön-kontrol
eki + S1-ek4 eşleme eki).

---

## 0. Ön-kontrolden devralınan durum

- Talimatın "kritik semantik fark" uyarısı hâlâ doğru:
  `handle_null_frame()` edge (eşikte sıfırla + bir kez `true`,
  capture_subsystem.cpp:44-50), `NullStreakTracker` level
  (`needs_reinit()` eşik aşıldıkça `true`, desktop_source_logic.h:63-65).
  Eşik iki tarafta da 60, test kilidi mevcut.
- Doğrulanmış kritik bulgu: `RecoveryCoordinator::handle_device_lost()`
  cihaz sağlıklıyken (`GetDeviceRemovedReason() == S_OK`) reinit yapmadan
  `false` döner (recovery_coordinator.cpp:63, :89). V10 SRT gözlemindeki
  "Capture loss detected (60 frames) — reinit" satırı statik ekranda
  ~saniyede bir basılıyor ama gerçek reinit olmuyor (no-op çağrı + log).
  Bu, Karar 3'ün temelini oluşturur.
- L5 init birleştirmesi wiring varsayımlarıyla uyumlu: `initPipeline()` →
  `pipeline_->init()` → `wireUpPipeline()` → `startFrameThread()`;
  `run_frame()` tek adanmış thread'de, wiring bittikten sonra başlar
  (main_window.cpp:314-333).

## 1. Karar 1 — CaptureSubsystem'in kaderi ✅ ONAYLANDI: sil; preview adapter'a

### Kullanım yüzeyi envanteri (pipeline.cpp, 2026-07-29 grep)

`capture_sub_` üzerinde ~35 çağrı noktası; kategorilere ayrılmış hâli:

| Kategori | Çağrılar | Adapter karşılığı |
|---|---|---|
| Yaşam döngüsü | `init(cap_cfg)` (426), `shutdown()` (810) | `init()` / `shutdown()` — Config ctor'da saklı |
| Kare alımı | `next_frame()` (673) | `next_frame()` → `SourceFrame` |
| Boyut/cihaz | `width()/height()` (434-435, 708-709, 720-722), `d3d_device()` (462), `has_capture()` (461, 653, 670) | `metadata()`; `has_capture()` → `state() != Uninitialized` |
| `dxgi()` kaçış kapısı | setProfiler (432), set_use_keyed_mutex (443, 644), encode_gpu()->d3d_device() (459-460), init_preview_staging (532, 602), set_preview_requested (592), shared_texture (601), preview map/unmap (693, 713-714), gpu_scan (846-865) | `dxgi()` — adapter'da bilinçli korunmuş, bu turda kapanmıyor |
| Null-streak | `handle_null_frame()` (743) | `state()` + level→edge dönüştürücü (Karar 3) |
| WGC preview | `emit_wgc_preview()` (731), `is_wgc()` (730) | ISource kontratında YOK — Karar 1'in konusu |

recovery_coordinator.cpp tarafı: `dxgi()`, `d3d_device()`, `shutdown()`,
`init(cap_cfg)`, `width()/height()`, `has_capture()` — hepsi adapter
yüzeyiyle karşılanıyor. Başka kullanıcı yok: `CaptureSubsystem`'e yalnız
pipeline.cpp ve recovery_coordinator.cpp dokunur; main_window.cpp L5
sonrası da dokunmaz.

### Karar ve gerekçe

**Orkestratör `ExistingDesktopSource` tutar, `CaptureSubsystem` silinir;
`emit_wgc_preview` ve `is_wgc` adapter'a kontrat-dışı metod olarak
taşınır** (dxgi() kaçış kapısı deseni). Gerekçe: `emit_wgc_preview`'ın
state'i (`wgc_staging_tex_` ComPtr, lazy oluşturma, çözünürlük
değişiminde reset) capture cihazına ve kare texture'larına bağlı — yaşam
döngüsü kaynağın reinit'iyle birlikte döner; adapter'da yaşaması cohesion
açısından doğru. Tetikleme kararı (`is_wgc() && preview_cb`)
orkestratörde kalır.

Elenen alternatifler: (b) preview'ı orkestratör yardımcısına çıkarmak —
D3D11 staging state orkestratöre taşınır, reinit'te staging reset
koordinasyonu ek sorumluluk olur; kontrat-dışı metod deseni varken
fazlalık. (c) CaptureSubsystem'i yaşatmak — ölü kod riski, ikili
null-streak yapısı süresiz kalır.

## 2. Karar 2 — RecoveryCoordinator imzası ✅ ONAYLANDI: ExistingDesktopSource& (somut tip)

`handle_device_lost()`'un ihtiyaç duyduğu her şey adapter'da var. Ek
sadeleşme: bugün recovery içinde `CaptureSubsystem::Config` yeniden
kurulur (recovery_coordinator.cpp:70-72, 98-100); adapter Config'i
ctor'da sakladığından reinit `source.shutdown(); source.init()`'e iner —
Config kurulum tekrarı kalkar. `ISource&`'a genelleme reinit Config'i
için ek mekanizma gerektirir; bu turda YAGNI.

İmplementasyon notu: recovery yolunda WGC reinit'in "recast'te null'a
düşme" davranışı (`capture_dxgi_` null kalır) adapter'ın `dxgi_`
cache'inde birebir korunmalı.

## 3. Karar 3 — level→edge dönüşümü ve re-arm ✅ ONAYLANDI: A (periyodik re-arm, 60 kare)

### Problem

Adapter `state()`'i level: `NeedsReinit` null'lar sürdükçe kalır, geçerli
karede temizlenir. Orkestratör edge'e çevirmeli. Salt geçiş-bazlı tetiğin
deliği: tetiklenen `handle_device_lost()` cihaz sağlıklıyken no-op
`false` döner → `shutdown()+init()` olmaz → tracker sıfırlanmaz → state
`NeedsReinit`'te takılı → yeni geçiş yok → BİR DAHA HİÇ tetiklenmez.
Bugünkü kod her 60 null'da yeniden dener; cihaz-sağlıklı gerçek kayıpta
(S_OK ama akış ölü) bu periyodik tekrar deneme yük taşıyor olabilir.

### Seçilen tasarım: A — davranış-koruyucu periyodik re-arm

`frame_drop_policy.h` deseninde saf, header-only `reinit_trigger_policy.h`;
küçük bir sınıf her karede `SourceState` beslenir: Running→NeedsReinit
geçişinde tetikler, state `NeedsReinit`'te kaldıkça her
`kNullStreakReinitThreshold` (60) karede yeniden tetikler.

- Bugünkü cadence BİREBİR korunur: bugün streak 60'ta tetik + sıfırla →
  120'de tekrar; tasarımda geçişte (60) tetik + re-arm sayacı → 120'de
  tekrar. Wiring turu davranış-koruyucu kalır — bu turun risk profili
  için en önemli özellik.
- Gerçek reinit gerçekleşirse `shutdown()+init()` tracker'ı sıfırlar,
  döngü temiz başlar (talimatın orijinal varsayımı bu yolda zaten doğru).
- Birim testleri (test_desktop_source_logic.cpp yanına): (1) no-op
  deneme senaryosu — NeedsReinit sürerken tetik yalnız her 60 karede,
  her-tik fırtınası yok; (2) geçerli kare gelince Running'e dönüş ve
  tetikleyicinin temizlenmesi; (3) reinit sonrası temiz başlangıç;
  (4) eşik sabiti `kNullStreakReinitThreshold` ile kilitli.

### Neden B/C değil

- **B (salt geçiş-bazlı tek tetik):** Statik ekrandaki log fırtınasını
  keser ama yukarıdaki delik nedeniyle cihaz-sağlıklı gerçek kayıpta
  recovery şansı kaybolur — davranış değişikliği, bu tura ait değil.
- **C (backoff'lu re-arm, 60→120→240…):** Spam'i azaltır ama yeni
  davranış yüzeyi ve yeni ayar sabitleri açar — YAGNI; log-spam azaltma
  istenirse ayrı küçük bir sonraki tur işi.

## 4. Kapsam dışı / değişmeyen noktalar

- `dxgi()` kaçış kapısı bu turda kapanmıyor (gerçek kompozisyon turunun işi).
- Yeni `run_frame()` capture bloğu S1-ek4 eşlemesini aynen korur: null
  `SourceFrame` → `FrameOutcome::NoNewFrame` (drop değil), encode hatası
  → `EncodeFailed`.
- Faz 1-2 commit sırası ve Faz 3 test/dürüstlük kuralları (baseline
  karşılaştırma, gerçek donanımda WGC+DXGI GUI doğrulaması ZORUNLU)
  talimattaki gibi geçerli. Merge öncesi canlı GUI doğrulaması kullanıcı
  tarafından ayrıca şart koşuldu.
