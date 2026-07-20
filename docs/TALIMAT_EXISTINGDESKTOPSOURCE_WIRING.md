# TALİMAT: ExistingDesktopSource Wiring — run_frame()'in ISource'a Geçişi

**Kaynak:** `docs/talimatlar/TALIMAT_EXISTINGDESKTOPSOURCE.md`'nin Faz 0
kapsam kararı — o tur bilinçli olarak yalnızca izole adapter'ı yazdı
(MVP daraltma disiplini); pipeline'ın gerçek çalışma zamanı akışını
değiştirme işi BU talimata ayrıldı.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Risk Profili

`Pipeline::Impl`/`run_frame()`'in capture erişimini `CaptureSubsystem`'den
`ExistingDesktopSource`'a (ISource) taşımak. Bu, capture'ın TEMEL çalışma
yolunu değiştirir — önceki turun aksine gerçek davranış değişikliği yüzeyi
vardır. Kullanıcının gerçek donanımda en dikkatli test etmesi gereken tur
budur (görüntü doğru mu, gecikme aynı mı, recovery hâlâ çalışıyor mu).

## Önceki Turun Faz 0'ından Devredilen Envanter (2026-07-20 itibarıyla)

- `CaptureSubsystem`'i yalnızca iki dosya kullanır: `pipeline.cpp`
  (~30 çağrı noktası) ve `recovery_coordinator.cpp` (`CaptureSubsystem&`
  parametresi + `Config` ile yeniden init). `main_window.cpp` doğrudan
  dokunmaz (önceki talimattaki varsayımın aksine — teyitli bulgu).
- `pipeline.cpp`'nin `dxgi()` kaçış kapısı kullanımları: setProfiler,
  set_use_keyed_mutex, encode_gpu()->d3d_device(), init_preview_staging,
  set_preview_requested, shared_texture, map/unmap_preview_frame,
  gpu_scan (rj_get_gpu_*). Adapter'daki `dxgi()` accessor'ı bunlar için
  bilinçli olarak korunmuştu — bu tur da kapatmak zorunda değil
  (kapatmak gerçek kompozisyon turunun işi).
- `emit_wgc_preview()` (WGC CPU staging + ComPtr state) CaptureSubsystem'de
  yaşıyor; ISource kontratında karşılığı YOK (bilinçli — preview
  orkestrasyon meselesi). Wiring'de karar gerekir: adapter'a taşınır mı,
  orkestratörde ayrı yardımcıya mı çıkar?

## Kritik Semantik Fark (önceden bilinen, sessizce geçme)

`CaptureSubsystem::handle_null_frame()` **edge** semantiği taşır: eşikte
sayacı SIFIRLAYIP bir kez `true` döner; orkestratör o tikte
`RecoveryCoordinator::handle_device_lost()` çağırır. `ISource::state()`
ise **level** semantiği taşır: eşik aşıldıkça `NeedsReinit` döndürmeye
devam eder (geçerli kare gelirse veya reinit olursa temizlenir).
Orkestratör level→edge dönüşümünü kendi yapmalı (örn. yalnız
Running→NeedsReinit geçişinde bir kez tetikle, reinit sonrası
`shutdown()+init()` sayacı zaten sıfırlar). Bu dönüşüm yanlış kurulursa
recovery fırtınası (her tikte reinit) doğar — birim testiyle kilitle.

## Faz 0 — Açık Kararlar (kod yazmadan, onaya sun)

1. **CaptureSubsystem'in kaderi:** Adapter'ın yanında yaşamaya devam mı
   (ölü kod riski), ExistingDesktopSource içine emilip silinir mi, yoksa
   orkestratör doğrudan ISource tutup CaptureSubsystem tamamen kalkar mı?
   Öneri (önceki tur Faz 0 analizi): orkestratör `ExistingDesktopSource`
   tutar, `CaptureSubsystem` silinir; `emit_wgc_preview` ya adapter'a
   adapter-özel (kontrat dışı) metod olarak taşınır ya da orkestratör
   yardımcısına çıkar. Karar gerekçelendirilmeli.
2. **RecoveryCoordinator imzası:** `CaptureSubsystem&` →
   `ExistingDesktopSource&` (somut tip; ISource'a genellemek reinit
   Config'i yüzünden bu turda zorlama olur — YAGNI).
3. **Null-streak tetikleyicisi:** yukarıdaki level→edge dönüşümünün tam
   yeri ve testi.

## Faz 1-2 — Tasarım Teyidi + Küçük Commit'ler

CLAUDE.md Bölüm 8b: davranış değiştiren çok-commit'li tur → feature dalı
zorunlu. Önerilen sıra: (1) orkestratör üye değişimi + init/shutdown,
(2) run_frame() capture bloğu, (3) recovery yolu, (4) preview yolları,
(5) CaptureSubsystem silme + doküman.

## Faz 3 — Test ve Dürüstlük Sınırları (bu turun en kritik bölümü)

- PipelineCharacterization: baseline sayısal karşılaştırma (fps ~60,
  drop deseni, bitrate healing adımı aynı karede) — önceki turda kurulan
  `baseline_metrics.pre_*.txt` kopyalama kalıbını izle.
- Mevcut testler: ExistingDesktopSourceTest, DesktopSourceLogicTest,
  PipelineIntegration, OutputSubsystemTest yeşil kalmalı; bilinen 2 kırık
  (FrameProfilerTest/ShaderCacheTest) dışında yeni kırık YOK.
- Null-streak recovery yolu için yeni birim/entegrasyon testi (level→edge).
- reji_app tam link + kullanıcı GUI gözlemi: **bu turda zorunlu ve
  raporda büyük harfle vurgulanmalı** — WGC ve DXGI fallback yollarının
  İKİSİ de gerçek donanımda doğrulanmalı (görüntü, preview, recovery).

## Sabit Kurallar

- tests/baseline_metrics.txt asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Davranış farkı bulunursa (özellikle DXGI yolunda `capture_next()` →
  `next_frame()` sarmalama farkları, timestamp/dims doldurma) dur,
  raporla — sessizce "aynı gibi" diye geçme.
