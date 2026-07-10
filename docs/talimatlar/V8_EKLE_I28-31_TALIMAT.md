# Claude Code Talimatı — V8 Bug Planına 4 Yeni Madde Ekle (I28-I31)

**Kaynak:** 06.07.2026 taze taramaları (Fable5, Opus 4.8, GLM-5.2, MiniMax-M3).
Bu SADECE `docs/FABLE5_BUG_PLAN_V8.md`'yi güncelleyen bir dokümantasyon
görevi — kod değişikliği YOK. I2/I3 çalışmasına başlamadan önce planı
zenginleştiriyoruz.

## 1. Öncelik Matrisi tablosuna, I3 satırından hemen sonra ekle

```
| I28 | Opus        | execute_copy() acquire barrier oldLayout=UNDEFINED — D3D11'in yazdigi pikselleri siliyor (spec: UNDEFINED = "icerik onemsiz") | copy_optimizer.cpp | Kritik | Sprint 1 |
| I29 | Opus+Fable  | Keyed mutex yanlis/eslesmeyen memory nesnesini koruyor olabilir — slot-0 "kanonik" varsayimi blit kaynagiyla eslesmeyebilir | preview_widget.cpp, external_memory_bridge.cpp, copy_optimizer.cpp | Kritik | Sprint 1 |
| I30 | MiniMax     | Cross-adapter shared texture'da D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX flag'i YOK (capture_dxgi.cpp'de var, gpu_resource_manager.cpp'de yok) | gpu_resource_manager.cpp | Kritik | Sprint 1 |
| I31 | Opus+GLM    | BGRA/RGBA format tutarsizligi — cross-adapter RGBA zorluyor, Vulkan/GL BGRA bekliyor, kanal takasi riski | gpu_resource_manager.cpp, preview_widget.cpp, gpu_interop_subsystem.cpp | Yuksek | Sprint 1 |
```

## 2. Detaylı bölüm — I3'ün detay bölümünden SONRA, I4'ün detay bölümünden ÖNCE ekle

`### I4 — CPU Fallback...` başlığından hemen önce bu dört bölümü ekle:

```markdown
### I28 — execute_copy() Acquire Barrier oldLayout=UNDEFINED Pikselleri Siliyor

**Kaynak:** Opus 4.8 (1.3) — 06.07.2026 taze tarama.

**Sorun:**
```cpp
// copy_optimizer.cpp, execute_copy() — D3D11'den import edilen staging image icin:
barrier_staging.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
// Vulkan spec: UNDEFINED = "mevcut icerik onemsiz, driver silebilir"
// Ama keyed mutex D3D11'in AZ ONCE yazdigi pikselleri Vulkan'a devrediyor —
// icerik onemli, "don't care" degil. Bazi driverlarda (ozellikle AMD 780M)
// bu, garbage/siyah kareye yol acabilir.
```

**Cozum:** Opus'un onerisi: `oldLayout = VK_IMAGE_LAYOUT_GENERAL` kullan (D3D11
keyed-mutex uzerinden devredilen bellek icin uyumlu tek layout), UNDEFINED'i
sadece gercekten "ilk kullanim, icerik onemsiz" durumunda (orn. texture'in
ilk allocation'i) kullan.

**Not:** I5 (submit basarisiz sonrasi layout state) ile KARISTIRILMAMALI —
I5 zamanlama/state-kayit sorunuydu (ne zaman yazildigi), I28 deger sorunu
(hangi layout degerinin dogru oldugu). Ikisi de cozulmus olsa bile birbirinden
bagimsiz, ayri commit'ler olmali.

---

### I29 — Keyed Mutex Yanlis/Eslesmeyen Memory Nesnesini Koruyor Olabilir

**Kaynak:** Opus 4.8 (3.2) + Fable 5 (3.1/3.2) — 06.07.2026 taze tarama,
bagimsiz olarak ayni koddaki ayni riske isaret ediyor.

**Sorun:** `PreviewWidget::bridge_->get_shared_texture_memory()` slot-0'i
"kanonik" varsayarak donuyor (yorum: "tum pool slotlari ayni D3D11 texture
memory'sini paylasiyor"). Eger Zig bridge gercekte HER slot icin ayri
`VkDeviceMemory` import ediyorsa (3 ayri `vkAllocateMemory` cagrisi, ayni
NT handle'dan), keyed mutex'i slot-0'in memory'si uzerinden acquire edip
farkli bir slot'un image'inden blit yapmak, VK_KHR_win32_keyed_mutex
spec'ine gore belirsiz/yanlis olabilir — driver bunu dedup edip
etmeyecegine bagli.

**Cozum:** Bridge'in gercekte kac ayri memory import ettigini dogrula
(Zig tarafi, `external_memory_bridge.zig` veya karsiligi). Eger per-slot
ayriysa: `get_shared_texture_memory()` yerine `get_staging_memory_for_image(img)`
gibi, gercek blit kaynagina bagli bir memory dondur.

**Bagimlilik:** I3 (keyed mutex key protokolu) ile ayni bolge — I3
duzeltmesi yazilirken bu da birlikte incelenmeli.

---

### I30 — Cross-Adapter Shared Texture'da KEYEDMUTEX Flag'i Yok

**Kaynak:** MiniMax-M3 — 06.07.2026 taze tarama, somut kod karsilastirmasi
(dusuk kaliteli rapor ama bu bulgu spesifik ve dogrulanabilir).

**Sorun:** Iki farkli shared texture olusturma yeri, iki farkli flag seti
kullaniyor:
```cpp
// capture_dxgi.cpp (Vulkan interop icin) — DOGRU:
desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED_NTHANDLE
               | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

// gpu_resource_manager.cpp (cross-adapter encode icin) — KEYEDMUTEX EKSIK:
desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED
               | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
```
`GpuResourceManager`'in `keyed_mutex_display_`/`keyed_mutex_encode_` uyeleri
zaten deklare edilmis ama (V8 bug planinin onceki bir bulgusuna gore, I2/I3
notlarinda) hic kullanilmiyor — bu, NEDEN kullanilmadiginin somut bir
aciklamasi olabilir: texture'in kendisi keyed mutex desteklemiyor.

**Cozum:** `create_cross_adapter_shared()`'daki texture olusturmaya
`D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX` ekle, `QueryInterface(IDXGIKeyedMutex)`
ile gercekten alinabildigini dogrula, sonra `keyed_mutex_display_`/
`keyed_mutex_encode_`'u `transfer()` icinde gercekten kullan (su an dead code).

**Bagimlilik:** I2'nin (cross-API sync yok) dogrudan bir alt-nedeni olabilir —
I2 talimati yazilirken bu flag eksikligi kok neden adaylarindan biri olarak
degerlendirilmeli.

---

### I31 — BGRA/RGBA Format Tutarsizligi Uc Yol Arasinda

**Kaynak:** Opus 4.8 (3.3) + GLM-5.2 (#2) — 06.07.2026 taze tarama,
bagimsiz bulundu.

**Sorun:** Cross-adapter shared texture RGBA zorlaniyor
(`create_cross_adapter_shared`, yorum: "AMD cross-adapter shared texture
RGBA format gerektirir"), ama:
- DXGI duplication kaynagi native BGRA (`DXGI_FORMAT_B8G8R8A8_UNORM`)
- Vulkan target pool BGRA (`gpu_interop_subsystem.cpp`)
- GL interop `GL_RGBA8` import ediyor

D3D11 `CopyResource` BGRA→RGBA gibi format-uyumsuz kopyalari desteklemez
(ayni format veya typeless+ayni-bit-genisligi gerekir) — sessizce
`E_INVALIDARG` ile basarisiz olabilir veya kare dusurebilir. Ayrica GL
tarafinda RGBA8 import + shader'daki `.bgra` swizzle, CPU-fallback yolu
(BGRA yuklenip swizzle) ile interop yolu (BGRA bellek RGBA8 olarak import
+ swizzle) arasinda TUTARSIZ olabilir — biri kirmizi/mavi kanallari ters
gosterebilir.

**Cozum:** Ya `CopyResource` yerine bir swizzle shader'i (pixel/compute)
kullan, ya da AMD driver'in `DXGI_FORMAT_B8G8R8A8_TYPELESS` ile cross-adapter
paylasima izin verip vermedigini dogrula. Vulkan target format, GL sized
internal format, ve shader swizzle'i UC yolda da (same-adapter, cross-adapter,
CPU-fallback) tutarli hale getir. Headless modda bilinen bir checker-pattern
kareyle renk-dogrulugu testi eklenmesi onerilir.

---

```

## 3. "Uygulama Notlari" bolumundeki I2+I3 notunu genislet

Şu paragrafı:
```
- **I2 + I3** (dual-GPU sync) en yuksek efor gerektiren madde; ...
```

şununla DEĞİŞTİR:
```
- **I2 + I3 + I28 + I29 + I30 + I31** hepsi ayni "AMD dual-GPU senkronizasyon"
  bolgesini kapsiyor — 06.07.2026 taze taramasi bu alani derinlemesine
  inceleyip 4 yeni, spesifik bulgu cikardi (layout degeri, keyed-mutex memory
  eslesmesi, eksik KEYEDMUTEX flag'i, format tutarsizligi). Bunlarin hepsi
  TEK bir odakli arastirma+duzeltme oturumunda ele alinmali — ayri ayri
  yapilirsa birbirini etkileme riski yuksek (ornegin I30'un flag duzeltmesi
  I3'un key protokolunu de degistirebilir). Pipeline::Impl refactoring'de
  kullanilan asamali yaklasim (karakterizasyon testi + baseline_metrics.txt)
  burada da sart — AMD donaniminda gorsel/otomatik regresyon testi olmadan
  bu alanda degisiklik yapmak riskli. Onerilen ilk adim: I30'un somut kod
  karsilastirmasindan baslayip (en dusuk belirsizlik), oradan I2/I3/I28/I29'a
  genislemek.
```

## Doğrulama Checklist

- [ ] Öncelik matrisine 4 satır eklendi (I28-I31)
- [ ] 4 detaylı bölüm I3 ile I4 arasına eklendi
- [ ] Uygulama Notları'ndaki I2+I3 paragrafı genişletildi
- [ ] Commit: `docs(plan): V8'e 4 yeni bulgu ekle (I28-I31, 06.07 taze tarama — AMD dual-GPU sync bölgesi)`
- [ ] Push yapma — onay bekle

Bu görevde kod değişikliği YOK, sadece plan dosyası güncelleniyor.
