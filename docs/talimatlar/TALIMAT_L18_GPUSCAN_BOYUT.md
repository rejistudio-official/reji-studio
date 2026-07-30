# TALİMAT: L18 Ek Bulgu — GpuScan'in WGC'de Boş Kalması (Yalnız Boyut Tespiti, Faz 0)

**Kaynak:** L18'in canlı doğrulaması, max_vram_vendor_id()/max_gpu_vram_mb()'nin
source_->dxgi()->gpu_scan()'e bağımlı olduğunu, WGC backend aktifken
(Win11'de her zaman kazanan yol) dxgi()'nin null döndüğünü, bu yüzden
Donanım Profilleme diyaloğunun hedef donanımın neredeyse tamamında
vendor=0/VRAM=0 gördüğünü ve her zaman Stabilite önerdiğini buldu —
kart ne kadar güçlü olursa olsun.

Bu talimat kod yazmaz — yalnızca düzeltmenin boyutunu tespit eder.

---

## Görevin Özü

1. GpuScan'in şu an nerede/nasıl doldurulduğunu bul
   (capture_dxgi.h/.cpp — muhtemelen DxgiCapturePipeline'ın
   kurulumu sırasında bir DXGI factory/adaptör taraması yapılıyor).
   Bu tarama, aktif bir DXGI capture session'ı gerektiriyor mu, yoksa
   yalnızca IDXGIFactory1::EnumAdapters1 gibi bağımsız bir çağrı mı?
2. Önemli emsal kontrolü: CapabilityDetector/render_capability.h
   de kendi başına GPU vendor tespiti yapıyordu (WGC/DXGI render yolu
   seçimi için) — bu, DxgiCapturePipeline'dan bağımsız mı çalışıyor?
   Eğer öyleyse, aynı deseni (bağımsız adaptör taraması) GpuScan için
   de kullanmak mümkün olabilir — sıfırdan icat değil, var olan bir
   deseni tekrar kullanmak.
3. GpuScan verisinin (vendor_id + dedicated_vram_mb, adaptör listesi)
   DxgiCapturePipeline nesnesinin durumundan bağımsız, yalnızca
   bir DXGI factory ile tek seferlik enumerate edilebilir bağımsız bir
   fonksiyona çıkarılmasının maliyetini tahmin et.
4. main_window/pipeline.cpp'deki çağıranların (max_vram_vendor_id,
   max_gpu_vram_mb, profil öneri akışı) bu bağımsız fonksiyona nasıl
   geçeceğini taslakla (kod yazma, yalnızca plan).

## Faz 0 Çıktısı

- Küçük/orta ise: Somut bir düzeltme önerisi (dosya/fonksiyon
  düzeyinde) + tahmini commit sayısı. Onaya sun, küçükse aynı turda
  ilerlenebilir.
- Büyükse (örn. DXGI factory kurulumunun kendisi ağır/yan etkili
  bir şeye bağlıysa): Gerekçeyle birlikte "V11 adayı" olarak
  FABLE5_BUG_PLAN_V10.md'ye not düşülmesini öner.

## Sabit Kurallar

- Bu turda kod yazma — yalnızca boyut tespiti ve öneri.
- Emin olunmayan noktada tahmin etme, "ek araştırma gerekir" diye
  işaretle.
