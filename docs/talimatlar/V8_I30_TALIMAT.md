# Claude Code Talimatı — V8/I30: GpuResourceManager Ölü Kod Temizliği

`.claude/skills/vulkan-interop-debug/SKILL.md` yüklü.

**Kaynak:** `docs/FABLE5_BUG_PLAN_V8.md`, madde I30 (09.07.2026 keşfi ile
yeniden konumlandırıldı: "ölü kodu hedefliyor").

## Genişletilmiş kapsam — talimat yazarken bulundu, açıkça belgeleniyor

V8'in I30'u sadece `keyed_mutex_display_`/`keyed_mutex_encode_`/`copy_fence_`
üyelerini hedefliyordu. Bu talimatı hazırlarken `gpu_resource_manager.cpp:92-98`'deki
yorumu inceledim — orada **birden fazla flag kombinasyonunun test edilip
hepsinin `E_INVALIDARG` ile başarısız olduğu**, kök nedenin "AMD iGPU + NVIDIA
dGPU cross-vendor D3D11 NT handle sharing bu Optimus/hybrid topolojide
desteklenmiyor" olduğu **açıkça belgelenmiş**. Bu, sadece keyed mutex
üyelerinin değil, `transfer()`'in **tüm "Cross-adapter path" dalının**
(satır ~299-306, `CopyResource`+`Flush`+`wait_display_gpu_idle()`) da mantıken
ulaşılamaz olduğu anlamına geliyor — çünkü `create_cross_adapter_shared()`
her zaman başarısız olacağı için `use_cpu_fallback_` her zaman `true` olur,
kod `if (use_cpu_fallback_)` dalından asla çıkamaz.

**Bu talimat ikisini birlikte ele alıyor** (aynı kök nedenin iki belirtisi):
ölü üyeler VE ölü kod dalı.

## Yapılacaklar

### 1. `gpu_resource_manager.h`'den kaldır

```cpp
Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_display_;  // KALDIR
Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_encode_;   // KALDIR
Microsoft::WRL::ComPtr<ID3D11Query>     copy_fence_;           // KALDIR
void wait_display_gpu_idle();                                   // KALDIR (tanımı da)
```

### 2. `gpu_resource_manager.cpp`'de

- `wait_display_gpu_idle()` fonksiyon tanımını (satır ~176-183) kaldır.
- `shutdown()`'daki üç `Reset()` çağrısını kaldır.
- `transfer()`'in son bloğunu (satır ~298-306, `same_adapter_`/`use_cpu_fallback_`
  ikisi de false olan durum) **koru ama netleştir** — tamamen silme, çünkü:
  - Teorik olarak `use_cpu_fallback_` her zaman true olacağı doğrulandı AMA
    bu, `init()`'teki mevcut mantığa dayanıyor (satır 253-257: cross-adapter
    başarısızsa CPU fallback'e düş). Bu mantık gelecekte değişebilir (örn.
    biri `create_cross_adapter_shared()`'ı gerçekten çalışır hale getirirse).
  - Kod tamamen silinirse, o gelecekteki değişiklik `transfer()`'in bu dalını
    da YENİDEN yazmak zorunda kalır — hafızayı kaybetmiş oluruz.
  - **Öneri:** Kodu SİLME, ama başına açıklayıcı bir yorum + defansif bir log/assert ekle:
    ```cpp
    // Cross-adapter path: teorik olarak buraya asla ulasilmamali (V8/I30 kesfi —
    // AMD/NVIDIA cross-vendor D3D11 NT-handle paylasimi bu topolojide desteklenmiyor,
    // create_cross_adapter_shared() her zaman basarisiz olur, use_cpu_fallback_
    // her zaman true olur). Buraya ulasiliyorsa (orn. farkli donanim/surucu),
    // ASAGIDAKI KOD SENKRONIZASYON ICERMIYOR — encode_gpu_ okumaya baslamadan
    // once display_gpu_ yazmasinin bittigi garanti edilmiyor (eski keyed-mutex
    // denemesi kaldirildi, hicbir yerine bir sey konmadi). Bu dala fiilen
    // giriliyorsa, once gercek bir senkronizasyon mekanizmasi (fence/keyed-mutex)
    // eklenmeden GUVENLI DEGILDIR.
    fprintf(stderr, "[GpuRM] WARNING: cross-adapter path reached — see V8/I30 comment, sync missing\n");
    auto* ctx = display_gpu_->d3d_context();
    ctx->CopyResource(shared_tex_display_.Get(), src);
    ctx->Flush();
    // wait_display_gpu_idle() kaldirildi (dead code) — bu satir artik
    // SENKRONIZASYONSUZ. Yukaridaki uyari log'u budur.
    return encode_tex_.Get();
    ```
  Bu yaklaşım, "sessizce yanlış çalışan kod" yerine "yüksek sesle uyaran ama
  yine de mevcut kod iskeletini koruyan" bir denge kuruyor.

### 3. `create_cross_adapter_shared()`'a dokunma

Bu fonksiyon hâlâ çağrılıyor (init sırasında, cross-adapter denemesi) ve
başarısızlık durumunda `use_cpu_fallback_`'e düşüren mantığın bir parçası —
kaldırılamaz, sadece keyed mutex ile ilgisi olmayan kısmı zaten aynı kalıyor.

## Test

- `cmake --build --preset release` temiz.
- `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok.
- Runtime log'da (`run.log`) hâlâ `same_adapter=false` + `E_INVALIDARG` +
  CPU-fallback mesajlarının aynı şekilde çıktığını doğrula (davranış
  DEĞİŞMEMELİ, sadece ölü kod temizlendi + uyarı log'u eklendi).

## Doğrulama Checklist

- [ ] Üç üye + `wait_display_gpu_idle()` kaldırıldı
- [ ] `shutdown()`'daki üç `Reset()` kaldırıldı
- [ ] `transfer()`'in son dalı KORUNDU ama açıklayıcı yorum + uyarı log'u eklendi
- [ ] `cmake --build --preset release` temiz
- [ ] `ctest --test-dir build` — bilinen 2 hariç yeni kırılma yok
- [ ] Runtime davranışı (log çıktısı) değişmedi, sadece ölü kod/yorum eklendi
- [ ] `docs/FABLE5_BUG_PLAN_V8.md`'de I30 satırına `[DÜZELTILDI]` + genişletilmiş
      kapsam notu (sadece üyeler değil, transfer()'in son dalı da ele alındı)
- [ ] `docs/SESSION_NOTES.md`'ye özet
- [ ] Commit: `refactor(gpu): V8/I30 — GpuResourceManager ölü keyed-mutex/fence kodu temizliği + cross-adapter dalına senkronizasyon uyarısı`
- [ ] Push yapma — özet raporla, onay bekle

## Sınır

I28 (validation-layer doğrulaması), I2/I3 (preview yolu incelemesi) bu
talimatın kapsamı DIŞINDA.
