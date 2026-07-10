# Claude Code Talimatı — V8 Bug Planı: I2/I3/I28-I31 Paketi, Alt-Adım A (Keşif)

**Önce küçük bir skill düzeltmesi, sonra keşif.** Kod değişikliği YOK bu
talimatta — sadece harita çıkarma + bir bayat dokümantasyon hatası düzeltmesi.

## Adım 0 — `vulkan-interop-debug` skill'indeki bayat iddiayı düzelt

Skill şu an diyor ki:
> *"Cross-adapter transfer (`GpuResourceManager` SharedHandle AMD→NVIDIA)
> tasarlandı ama **aktif değil**: `same_adapter_ = true` hardcode (preview-only).
> Cross-adapter varsayan bir analiz yapma."*

Bu **yanlış** — güncel kod (`gpu_resource_manager.cpp:222-236`) gerçek bir
LUID karşılaştırması yapıyor:
```cpp
same_adapter_ = (display_desc.AdapterLuid.LowPart == encode_desc.AdapterLuid.LowPart
               && display_desc.AdapterLuid.HighPart == encode_desc.AdapterLuid.HighPart);
```
Referans donanımda (AMD 780M display + NVIDIA RTX 4070 encode) bu **false**
çıkar — yani cross-adapter yolu (`create_cross_adapter_shared()`) **gerçekten
aktif**, "preview-only" değil, gerçek yayın yolunun kendisi.

**Düzelt:**
```diff
- Cross-adapter transfer (`GpuResourceManager` SharedHandle AMD→NVIDIA)
-   tasarlandı ama **aktif değil**: `same_adapter_ = true` hardcode (preview-only).
-   Cross-adapter varsayan bir analiz yapma.
+ Cross-adapter transfer (`GpuResourceManager` SharedHandle AMD→NVIDIA) GERÇEKTEN
+   AKTİF — `same_adapter_` gerçek LUID karşılaştırmasıyla belirleniyor
+   (`gpu_resource_manager.cpp:222`), hardcode değil. Referans donanımda
+   (AMD display + NVIDIA encode) `same_adapter_=false` çıkar, cross-adapter
+   yolu (`create_cross_adapter_shared`) devrede. Bu not 2026-07 öncesi
+   bayattı, I2/I3 keşfi sırasında düzeltildi.
```
Bunu ayrı, küçük bir commit yap: `docs(skill): vulkan-interop-debug — same_adapter_ hardcode iddiasını düzelt (bayattı)`.

## Adım 1-4 — Keşif (kod yazma, sadece harita çıkar + SESSION_NOTES'a yaz)

### 1. I30'dan başla: KEYEDMUTEX flag'i eklenirse ne açılır?

`create_cross_adapter_shared()`'a `D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX`
eklendiğini VARSAYARAK (henüz elle ekleme, sadece kod akışını izle):
- `keyed_mutex_display_`/`keyed_mutex_encode_` (`QueryInterface(IDXGIKeyedMutex)`
  ile doldurulacak alanlar) `transfer()` içinde şu an gerçekten kullanılıyor
  mu, yoksa hâlâ tamamen mi ölü? Tam çağrı zincirini bul.
- Flag eklenince `create_cross_adapter_shared()`'ın kendisi başka bir yerde
  bozulur mu (ör. AMD driver'ının bu flag kombinasyonunu reddetme ihtimali —
  kodda bunu test eden bir yer var mı, yoksa sadece "çalışır" mı varsayılıyor)?

### 2. I2'nin tam kapsamını doğrula

`capture_next()`'in AMD path'te (cross-adapter, `use_cpu_fallback_` false
İKEN, yani gerçek NT-handle paylaşımı başarılıyken) hangi senkronizasyon
mekanizmasını kullandığını tam olarak izle — I3'ün tarif ettiği
`AcquireSync(0)`/`ReleaseSync(1)` kalıbı gerçekten `use_cpu_fallback_==false`
yolunda mı çalışıyor, yoksa I3'ün tarif ettiği kod aslında farklı bir yola mı
ait? (V8 I3 yazıldığından beri kod değişmiş olabilir, doğrula.)

### 3. I28/I29/I31'i aynı çağrı zincirinde konumlandır

`execute_copy()`'nin (I28'in `oldLayout=UNDEFINED` bulduğu yer) hangi
`slot`/`memory` ile çalıştığını, `PreviewWidget::get_shared_texture_memory()`'nin
(I29) döndürdüğü memory ile **aynı** nesne olup olmadığını izle. Format
zincirini (I31) da uçtan uca yaz: DXGI native format → cross-adapter texture
format → Vulkan import format → GL/shader format, her geçişte hangi format
kullanılıyor, nerede swizzle var/yok.

### 4. Tek bir "gerçek durum haritası" belgesi üret

`docs/SESSION_NOTES.md`'ye (ayrı bir bölüm, `### V8 I2/I3/I28-I31 — Keşif`)
şunları yaz:
- Yukarıdaki 3 sorunun cevapları
- I2/I3/I28/I29/I30/I31'in her biri için: "hâlâ tarif edildiği gibi mi,
  yoksa güncel kodda farklı mı" (I4/I5'te yaptığımız "yeniden doğrulama"
  disiplini)
- Önerilen gerçek düzeltme sırası (belki I30 gerçekten önce gelmeli, belki
  başka bir sıra daha mantıklı çıkar — keşfin sonucuna göre)
- Hangi düzeltmelerin TEK bir commit'te, hangilerinin ayrı ayrı yapılabileceği
  (V8'in "hepsi tek oturumda" notu ön-varsayımdı, keşif bunu ya doğrular ya
  da ayrıştırılabileceğini gösterir)

## Doğrulama Checklist

- [ ] `vulkan-interop-debug` skill düzeltmesi ayrı commit'te
- [ ] 4 sorunun cevapları SESSION_NOTES'ta
- [ ] Her I-maddesi için "hâlâ geçerli mi" durumu netleştirildi
- [ ] Önerilen düzeltme sırası + gruplama önerisi yazıldı
- [ ] Kod değişikliği YOK (bu adımda) — sadece skill fix + doküman
- [ ] Push yapma (skill fix commit'i hariç, onu ayrı onaylatabilirsin) —
      keşif bulgularını raporla, ben Alt-Adım B (gerçek düzeltme) talimatını
      bu bulgulara göre yazacağım

## Sınır

Bu tamamen bir **okuma/haritalama** görevi. `execute_copy()`, `transfer()`,
`create_cross_adapter_shared()` gibi fonksiyonlara TEK SATIR bile kod
eklemeyin/değiştirmeyin — sadece izleyin, belgeleyin.
