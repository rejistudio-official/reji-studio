# TALİMAT: ExistingDesktopSource — ISource Arayüzünün İlk İmplementasyonu

**Kaynak:** `docs/talimatlar/TALIMAT_ISOURCE_ARAYUZ_TASARIMI.md`'nin
bıraktığı takip işi — `i_source.h` kontratının mevcut WGC/DXGI capture
koduna uyarlanması.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Görevin Özü ve Kritik Güvence

Bu turun tek gerçek başarı kriteri: `ExistingDesktopSource`
eklendikten sonra hiçbir fonksiyonel davranış değişmemeli. Bu bir
refactor/adaptasyon turu — yeni bir özellik değil. Faz 1 tasarımının
delegasyon tablosu zaten hazır (aşağıda tekrarlanıyor); bu talimatın işi
onu dikkatle, doğrulanabilir şekilde koda dökmek.

**Önceki turdan miras kalan delegasyon planı:**

| ISource üyesi | Delegasyon |
|---|---|
| kurucu | IScreenCapture::Config alır (output_index, timeout_ms, allow_cross_adapter), saklar |
| init() | IScreenCapture::create() + capture_->init(cfg_) — bugünkü CaptureSubsystem::init ile birebir |
| next_frame() | capture_->next_frame() → CapturedFrame'i SourceFrame'e alan-alan eşler; format DXGI path'te surface_format()'tan, WGC path'te frame pool formatından; timestamp_us acquire-anı QPC'den doldurulur |
| metadata() | capture_->width()/height(), format, capture_->d3d_device() |
| state() | null-streak sayacını (bugünkü CaptureSubsystem::handle_null_frame'deki 60-kare eşiği) içeride tutar → eşikte NeedsReinit bildirir; kurtarma kararı orkestratörde kalır |
| shutdown() | capture_ reset — bugünkü RAII teardown |

**Geçiş dönemi kaçış kapısı (bilinçli, kontrata girmez):** dxgi()
benzeri bir accessor, adapter'a özel olarak kalır (shared_texture(),
map_preview_frame(), gpu_scan() gibi tek-kaynak DXGI'ye özgü
erişimler için). Bunu kapatmak gerçek kompozisyon turunun işi, bu
talimatın değil.

---

## Faz 0 — Güncel Durum Teyidi + Açık Kararın Çözümü (kod yazmadan)

1. **Bayatlık kontrolü:** i_source.h, CaptureSubsystem,
   IScreenCapture, DxgiScreenCapture, WgcScreenCapture'ın güncel
   master'a karşı hâlâ önceki turda tarif edildiği gibi olduğunu
   doğrula.
2. **Açık karar — CaptureSubsystem'in kaderi:** Önceki tasarım turu
   bunu implementasyona bırakmıştı: CaptureSubsystem adapter'ın iç
   parçası mı olacak, yoksa orkestratör doğrudan ISource mı tutacak?
   İkisinin de mevcut çağıranlara (pipeline.cpp, main_window.cpp)
   etkisini incele, daha düşük riskli/daha az dosya değiştiren
   seçeneği belirle ve gerekçele.
3. **Wiring kapsamı sorusu (kritik, boyutlandırma):** Bu talimat yalnızca
   ExistingDesktopSource sınıfını yazıp izole doğru çalıştığını mı
   kanıtlayacak (katkı: yeni, kullanılmayan bir sınıf — en düşük risk),
   yoksa pipeline.cpp'nin gerçek çalışma zamanı akışını (run_frame())
   bu yeni sınıfı kullanacak şekilde değiştirmeyi de mi kapsayacak
   (daha yüksek risk, gerçek davranış değişikliği yüzeyi)? İkinci
   seçenek doğruysa, bunun "davranış değişmemeli" sözünü nasıl
   koruyacağını (kapsamlı regresyon testi planı) netleştir. Bu net
   değilse iki ayrı talimata bölünmesini öner (Ses Ayarları'nda
   izlenen MVP daraltma disiplini).

Faz 0 çıktısı: Bayatlık teyidi + CaptureSubsystem kararı +
wiring kapsamı önerisi. Onaya sun.

## Faz 1 — Kısa Tasarım Teyidi (önceki tur zaten çoğunu belirledi)

Faz 0'ın açık kararlarına göre kısa bir netleştirme — büyük bir yeniden
tasarım beklenmiyor, önceki turun delegasyon tablosu geçerliliğini
koruyor. Yalnızca Faz 0'da bulunan açık noktaları (CaptureSubsystem,
wiring kapsamı) kesinleştir.

## Faz 2 — İmplementasyon (küçük commit'ler, CLAUDE.md Bölüm 8b'ye uy)

Örnek sıra (Faz 0/1 kararına göre uyarlanacak):
1. ExistingDesktopSource sınıfı (.h/.cpp) — kurucu, init(),
   shutdown().
2. next_frame() — alan eşleme, format/timestamp doldurma.
3. metadata() + state() (null-streak → SourceState saf mantığı,
   mümkünse TDD ile ayrı test edilebilir bir fonksiyona çıkarılarak).
4. (Faz 0'ın kapsam kararına göre) pipeline.cpp wiring — varsa.
5. Dokümantasyon.

## Faz 3 — Test ve Dürüstlük Sınırları (bu turun en kritik bölümü)

- "Davranış değişmedi" iddiası en sıkı şekilde kanıtlanmalı:
  - Birim testleri: next_frame()'in alan eşlemesini (mock/saf veri ile),
    state()'in null-streak eşiğini doğru şekilde NeedsReinit'e
    çevirdiğini doğrula.
  - Regresyon: PipelineCharacterization + mevcut capture testleri
    birebir aynı sonuçları vermeli (yalnızca PASS değil, mümkünse
    sayısal çıktıların — fps, drop sayısı vb. — önceki turlarla
    karşılaştırılabilir olduğu belirtilmeli).
  - Derleme + link: reji_app'in tam link olduğunu, yeni sınıfın
    mevcut hiçbir sembolü bozmadığını doğrula.
- GUI/gerçek donanım gözlemi kullanıcıda kalacak — gerçek capture'ın
  hâlâ aynı şekilde çalıştığını (görüntü doğru, gecikme aynı) gözlemlemek.
- Eğer wiring bu turun kapsamındaysa: bu, kullanıcının en dikkatli
  test etmesi gereken kısım olacak — capture'ın temel çalışma yolu
  değişiyor, bunu raporda özellikle vurgula.

## Sabit Kurallar

- CLAUDE.md Bölüm 8b'ye göre dal kararı ver — muhtemelen çok-commit'li,
  dal gerekebilir.
- tests/baseline_metrics.txt asla commit edilmez.
- "Test edildi" / "kod incelemesiyle doğrulandı" ayrımı raporda açık.
- Bu talimatın tek ve en önemli kuralı: davranış değişikliği yok.
  Faz 0/2/3'te bu iddiayı zayıflatan bir bulgu çıkarsa (örn. format
  eşlemesinde bir edge-case farkı), dur, raporla — sessizce "aynı
  gibi" diye geçme.
