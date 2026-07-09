# Reji Studio — Sekizinci Tarama Duzeltme Plani (V8)

**Tarih:** 02.07.2026
**Kaynaklar:**
  Fable 5     ($3.20) — En kapsamli, 88 dosya, rule-engine/action-queue kopuklugunu
                        tek basina bulan tek kaynak (I1)
  Opus 4.8    ($1.49) — Ayni 88 dosya, farkli acidan 2 ozgun bulgu (I11, I17)

**Onceki:** FABLE5_BUG_PLAN_V7.md (H1-H20 tamamlandi)

**Not (encoding):** Her iki kaynak raporda da buyuk harf "I" sistematik olarak
export sirasinda dusmus (`CRITICAL`->`CRTCAL`, `Impl`->`mpl`, `ABI`->`AB`,
`NVIDIA`->`NVDA`, `UI`->`U`). Bu plan yazilirken orijinal terimler baglamdan
geri kuruldu; ham raporlar `docs/` disinda referans olarak saklanmali.

---

## Model Karsilastirmasi

| Model    | Maliyet | Dosya | Ozgun/Cift-dogrulanmis Bulgu           | Kalite |
|----------|---------|-------|------------------------------------------|--------|
| Fable 5  | $3.20   | 88    | 1 kritik ozgun (I1) + 9 cift-dogrulanmis | 5/5    |
| Opus 4.8 | $1.49   | 88    | 2 onemli ozgun (I11, I17) + 9 cift-dogrulanmis | 5/5 |

**Cift-dogrulanmis bulgular** (iki model birbirinden bagimsiz ayni sorunu
bulmus) oncelik siralamasinda otomatik olarak bir kademe yukari alindi —
bagimsiz konsensus, guven duzeyini artirir.

---

## Oncelik Matrisi

| #   | Kaynak      | Sorun                                                        | Dosya                        | Oncelik | Sprint   |
|-----|-------------|---------------------------------------------------------------|-------------------------------|---------|----------|
| I1  | Fable       | Rule engine hicbir yerden evaluate() ile cagrilmiyor — self-healing adaptive pipeline tamamen dead code, CoPilot onayi sahte | healing.rs, ffi.rs        | Kritik  | Sprint 1 |
| I2  | Fable+Opus  | AMD path capture_next() cross-API sync yok — hedef donanimin ana yolu **[KEŞİF 09.07: YANLIŞ KONUMLANMIŞ — encode yolu referans HW'de her zaman CPU-fallback, senaryo oluşmuyor, bkz. SESSION_NOTES]** | capture_dxgi.cpp, copy_optimizer.cpp | Kritik | Sprint 1 |
| I3  | Fable+Opus  | Keyed-mutex/QFOT protokolu tutarsiz — AMD'de deadlock/timeout, veri bozulmasi **[KEŞİF 09.07: KISMEN GEÇERLİ, konum capture_dxgi.cpp'ye taşındı (preview yolu), GpuResourceManager'da değil]** | copy_optimizer.cpp, capture_dxgi.cpp | Kritik | Sprint 1 |
| I28 | Opus        | execute_copy() acquire barrier oldLayout=UNDEFINED — D3D11'in yazdigi pikselleri siliyor (spec: UNDEFINED = "icerik onemsiz") **[KEŞİF 09.07: KASITLI/DOKÜMANTE TASARIM (D2/E4 yorumu) — defekt olduğu şüpheli, validation-layer ile doğrulanmalı]** | copy_optimizer.cpp | Kritik | Sprint 1 |
| I29 | Opus+Fable  | Keyed mutex yanlis/eslesmeyen memory nesnesini koruyor olabilir — slot-0 "kanonik" varsayimi blit kaynagiyla eslesmeyebilir **[KEŞİF 09.07: ÇÜRÜTÜLDÜ — tek import 3 slota alias, uyuşmazlık yok. Komşuda gerçek bug bulundu → I32]** | preview_widget.cpp, external_memory_bridge.cpp, copy_optimizer.cpp | Kritik | Sprint 1 |
| I30 | MiniMax     | Cross-adapter shared texture'da D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX flag'i YOK (capture_dxgi.cpp'de var, gpu_resource_manager.cpp'de yok) **[KEŞİF 09.07: ÖLÜ KODU HEDEFLİYOR — flag eklemek encode yolunun E_INVALIDARG kök nedenini çözmez, keyed_mutex_* üyeleri zaten %100 ölü]** | gpu_resource_manager.cpp | Kritik | Sprint 1 |
| I31 | Opus+GLM    | BGRA/RGBA format tutarsizligi — cross-adapter RGBA zorluyor, Vulkan/GL BGRA bekliyor, kanal takasi riski **[KEŞİF 09.07: HARİTALANDI, DEFEKT YOK — preview yolunda tek swizzle noktası (shader .bgra), zincir tutarlı]** | gpu_resource_manager.cpp, preview_widget.cpp, gpu_interop_subsystem.cpp | Yuksek | Sprint 1 |
| I32 | Keşif (09.07) | invalidate_pool() aynı VkImage/VkDeviceMemory'yi 3 slotta ayrı ayrı free ediyor (tek import, 3-slot alias) — çözünürlük/reinit'te üçlü-free/UB | external_memory_bridge.zig:276-285 | Kritik | Sprint 1 |
| I4  | Fable+Opus  | CPU fallback transfer() row-pitch farkini yok sayiyor — buffer overrun/bozulma **[DÜZELTILDI]** | gpu_resource_manager.cpp | Kritik | Sprint 1 |
| I5  | Fable       | execute_copy() basarisiz submit sonrasi layout state'i yanlis guncelliyor — Vulkan spec ihlali **[DÜZELTILDI]** | copy_optimizer.cpp | Kritik | Sprint 1 |
| I6  | Opus        | is_copy_ready() shutdown ile ayni anda cagrilirsa olu device uzerinde vkWaitSemaphores | copy_optimizer.cpp | Kritik | Sprint 1 |
| I7  | Fable       | WasapiCapture shutdown — callback UAF penceresi (Unregister sirasi yanlis) | wasapi_capture.cpp | Kritik | Sprint 1 |
| I8  | Fable+Opus  | WS sunucusu auth'suz — drive-by stream kill saldiri vektoru | ws_server.rs | Yuksek | Sprint 2 |
| I9  | Fable+Opus  | CoUninitialize() RPC_E_CHANGED_MODE'da kosulsuz cagriliyor (2 yerde) | command_router.cpp | Yuksek | Sprint 2 |
| I10 | Fable+Opus  | SEH filtreleri EXCEPTION_ACCESS_VIOLATION/stack overflow yutuyor | command_router.cpp, wasapi_capture.cpp, +4 dosya | Yuksek | Sprint 2 |
| I11 | Opus        | Cift consumer race — C++ action thread (100ms poll) + UI'nin kendi 200ms poll'u ayni kuyruğu yariyor | command_router.cpp, main_window.cpp | Yuksek | Sprint 2 |
| I12 | Fable       | MainWindow yikim sirasi — GL widget paintGL yaparken copy_optimizer_.shutdown() cagrilabiliyor | main_window.cpp | Yuksek | Sprint 2 |
| I13 | Opus        | GL render tamamlanmamis Vulkan blit sonucunu orneklyebiliyor — ilk kare sira hatasi | preview_widget.cpp | Yuksek | Sprint 2 |
| I14 | Fable       | rj_metrics_poll deklare edilmis ama Rust implementasyonu yok — UI metrik barı muhtemelen hic guncellenmiyor | ffi_bridge.h, main_window.cpp | Yuksek | Sprint 2 |
| I15 | Fable+Opus  | rj_metrics_push hot-path'te mutex+heap alloc+String clone (RT ses/SRT thread) | ffi.rs | Orta | Sprint 3 |
| I16 | Fable+Opus  | query_gpu_load_pct her 1Hz pollde vector alloc | metrics_collector.cpp | Orta | Sprint 3 |
| I17 | Opus        | Iki rakip frame-pacing implementasyonu (FramePacer + DxgiFramePacing) cift pacing yapiyor | frame_pacer.cpp, frame_pacing.cpp | Orta | Sprint 3 |
| I18 | Opus        | wasapi_capture.cpp FFI'yi dogrudan cagiriyor — subsystem/orchestrator katmanini atliyor | wasapi_capture.cpp | Orta | Sprint 3 |
| I19 | Fable       | HEALING_MODE semantigi 4 katmanda (ffi.rs/healing.rs/ffi_bridge.h/UI) birbirinden farkli | healing.rs, ffi_bridge.h | Orta | Sprint 3 |
| I20 | Fable       | evaluate_adaptive() constructor-frozen self.mode okuyor, atomic'i degil — Assist modu hep AutoPilot gibi davraniyor | healing.rs | Orta | Sprint 3 |
| I21 | Fable+Opus  | Hardcoded C:\reji-studio\ yollari (3 dosyada) + freopen(stderr) kontrolsuz | ffi.rs, ws_server.rs, main.cpp | Dusuk | Sprint 4 |
| I22 | Fable+Opus  | ABI yorum satirlari bayat (56B/+51 vs gercek 64B/+55) | ffi_auto.h, ffi_bridge.h | Dusuk | Sprint 4 |
| I23 | Fable+Opus  | Bridge/optimizer slot sayaclari birbirinden bagimsiz — drift riski | external_memory_bridge.cpp, copy_optimizer.cpp | Dusuk | Sprint 4 |
| I24 | Opus        | rj_reload_rules/rj_connection_lost CStr::from_ptr sinirsiz — OOB read riski | ffi.rs | Dusuk | Sprint 4 |
| I25 | Fable       | zig_win32_compat.c stack guard fallback sabit deger (tahmin edilebilir canary) | zig_win32_compat.c | Dusuk | Sprint 4 |
| I26 | Opus        | add(u64,u64) demo fonksiyonu + it_works testi production crate'te birakilmis | lib.rs | Dusuk | Sprint 4 |
| I27 | Fable+Opus  | ITransport::send/shutdown SEH virtual-call riski — noexcept ile saglamlastirilmali (yapisal garanti, elle test degil) **[DÜZELTILDI]** | i_transport.h, srt_transport.cpp, rtmp_transport.cpp | Dusuk | Sprint 4 |

---

## Sprint 1 — Kritik: Dual-GPU Senkronizasyon ve Dead-Code Guvenlik

---

### I1 — Rule Engine Hicbir Yerden Cagrilmiyor (Self-Healing Adaptive Pipeline Dead Code)

**Kaynak:** Fable 5 (1.4, 6.8) — Opus bu zinciri kacirmis (sadece 4.3'te
`enqueue_action kullanilmiyor` diye satir arasi gecmis, kok nedeni teshis
etmemis).

**Onem:** SESSION_NOTES.md'de "HealingOverlay Co-Pilot onay akisi calisiyor"
diye kapatilan is aslinda yarim. `rj_action_approve` zaten stub oldugu
biliniyordu (her zaman `1` doner), ama asil sorun: approve gercek olsa bile
**hicbir aksiyon zaten kuyruga girmiyor**.

**Sorun:**
```rust
// healing.rs — HealingMonitor::current_metrics event'lerden doluyor
// AMA hicbir yerde:
//   rule_engine.evaluate(&self.current_metrics) -> Vec<Action>
//   for action in actions { enqueue_action(action) }
// cagrisi yok. rule_engine, FfiState icinde Arc<Mutex<Option<RuleEngine>>>
// olarak sadece rj_reload_rules tarafindan dokunuluyor.
// Sonuc: C++ tarafinda rj_action_dequeue hep bos kuyruk goruyor.
```

**Cozum:**
1. `rj_start_monitor_impl` icinde periyodik bir gorev baslat (ornegin 1s
   tick, mevcut metrics drainer tick'ine binebilir):
   ```rust
   // healing.rs icinde, monitor loop'unda:
   if let Some(engine) = rule_engine.lock().unwrap().as_ref() {
       let actions = engine.evaluate(&current_metrics, healing_mode.load());
       for action in actions {
           enqueue_action(action); // mevcut action_queue'ya push
       }
   }
   ```
2. Eger bu bilincli olarak ertelendiyse (henuz test edilmedi vb.), en azindan
   acikca isaretle ve C++ tarafindaki polling thread'i (HealingOverlay,
   action_thread_main) da devre disi birak — yarim calisan bir ozellik
   kullaniciya "calisiyor" gibi gosterilmemeli.
3. `rj_action_approve` stub'ini gercek implementasyona baglamadan once bu
   zincirin tamamlanmis olmasi sart — sirali bagimlilik: rule_engine wiring
   -> action queue -> approve.

**Test:** `rules.rs` icin zaten var olan entegrasyon testlerine ek olarak,
`healing.rs` icin "metric threshold asilinca action_queue'da eleman belirir
mi" testi ekle (mock RuleEngine + mock metrics ile).

---

### I2 — AMD Path Capture'da Cross-API Senkronizasyon Yok

**Kaynak:** Fable 5 (3.1) + Opus 4.8 (1.4, 3.4) — bagimsiz cift dogrulama,
hedef donanimin (AMD 780M) **ana** yolu oldugu icin en yuksek etki.

**Sorun:**
```cpp
// capture_dxgi.cpp — AMD fallback (use_keyed_mutex_ == false):
// CopyResource + Flush + D3D11 event query spin-wait
// Bu sadece D3D11 YAZMA'nin bittigini kanitliyor.
// Vulkan OKUMA'yi hicbir sey engellemiyor — sonraki frame'in
// CopyResource'i, Vulkan hala okurken shared texture'i ezebilir.
// QFOT acquire barrier'i (copy_optimizer.cpp) EXTERNAL'dan geliyor
// ama hicbir zaman eslesen bir release yok (D3D11 Vulkan release
// barrier'i bilmiyor) — validation layer bunu flag'liyor.
```

**Cozum (Opus 1.4 + Fable 3.1/3.3 birlestirilmis):**
1. Keyed mutex yoksa (AMD path), QFOT `EXTERNAL` indekslerini
   `VK_QUEUE_FAMILY_IGNORED` yap — zaten tanimsiz bir ownership transfer,
   validation hatasi uretiyor.
2. Ters fence uygula: D3D11 yazma bitince Vulkan'in bekleyecegi bir
   `ID3D11Fence`/paylasimli timeline semaphore olustur, VEYA shared texture'i
   double/triple-buffer yap ki D3D11 hic bir zaman Vulkan'in okudugu slotu
   yazmasin (pool altyapisi zaten var — D3D11 tarafina genislet).
3. `wait_display_gpu_idle()` + `YieldProcessor` spin-wait yerine 50 spin
   sonrasi `Sleep(0)`/`SwitchToThread` fallback (Opus 4.4 ile ayni kok neden).

**Oncelik notu:** Bu is Sprint 1'in en yuksek efor gerektiren maddesi —
tasarim degisikligi + hem D3D11 hem Vulkan tarafinda test gerektiriyor. Ayri
bir alt-oturum olarak planlanmali (onceki Pipeline::Impl refactoring gibi
asamali ilerlenebilir: once QFOT duzeltmesi [dusuk risk], sonra fence/pool
genisletme [yuksek risk]).

---

### I3 — Keyed-Mutex Key Protokolu Tutarsiz

**Kaynak:** Fable 5 (3.2) + Opus 4.8 (3.3) — ayni dosyalar, farkli acidan
ayni kok nedene isaret ediyor.

**Sorun:**
```
D3D11: AcquireSync(0) -> yaz -> ReleaseSync(1)   [her frame, capture_next()]
Vulkan: acquire key=1, release key=0             [sadece paintGL'de, frame dirty ise]

capture_next() HER FRAME calisiyor (frame thread),
execute_copy() SADECE paintGL'den cagriliyor (GL thread, frame dirty ise).

Iki capture arasinda tek paint olmazsa:
  D3D11 AcquireSync(0) dener, ama mutex son key=1'de birakilmis
  ve hicbir Vulkan acquire olmamis -> 16ms TIMEOUT her frame.
  Log "KeyedMutex timeout/fail" bunu sessizce dropped frame gibi gosteriyor.
```

**Cozum:** Fable'in onerdigi (b) secenegi tercih edilir (daha dusuk risk):
```cpp
// capture_next() icinde, AcquireSync(0, 0) non-blocking dene:
HRESULT hr = keyed_mutex_->AcquireSync(0, 0); // timeout=0
if (hr == DXGI_ERROR_WAS_STILL_DRAWING) {
    // Vulkan henuz tuketmedi — bu frame'in copy'sini atla, hata degil
    return kSkipWrite;
}
```
Boylece "Vulkan tuketmedi" durumu hata olarak degil, beklenen bir atlama
olarak ele alinir — mevcut 16ms blocking timeout kaldirilir.

**Test:** Sentetik dusuk-FPS paint senaryosu (paintGL'i kasitli throttle
ederek) ile capture_next()'in timeout'a girmeden devam ettigini dogrula.

---

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

### I32 — invalidate_pool() Üçlü-Free (I29 Keşfinin Yan Bulgusu)

**Kaynak:** V8 I2/I3/I28-I31 keşfi (Alt-Adım A, 09.07.2026) — I29'u araştırırken
bulundu, V8'in orijinal 26 maddesinde yoktu.

**Sorun:** `external_memory_bridge.zig`'deki pooled image sistemi, 3 slotun
hepsini **aynı** fiziksel `VkImage`/`VkDeviceMemory`'ye alias ediyor (tek NT-handle
import, 3 slota kopyalanıyor — bu kısım kasıtlı ve I29'un çürütülmesinin
sebebi). Ama `invalidate_pool()` (satır 276-285), her slotu BAĞIMSIZ sanıp
üçünde de ayrı ayrı `vkDestroyImage`+`vkFreeMemory` çağırıyor:
```zig
for (&state.image_pool) |*slot| {
    if (slot.image != null) {
        vk.vkDestroyImage(state.device, slot.image, null);  // 3 kez, AYNI handle
        slot.image = null;
    }
    if (slot.memory != null) {
        vk.vkFreeMemory(state.device, slot.memory, null);   // 3 kez, AYNI handle
        slot.memory = null;
    }
}
```
Slotlar aynı handle'ı paylaştığı için, texture pointer değiştiğinde (çözünürlük
değişimi/reinit) **aynı VkImage/VkDeviceMemory 3 kez free ediliyor** — undefined
behavior, potansiyel heap corruption/crash. İlk çağrı güvenli (hepsi dolu,
null değil), ama sonraki free'ler zaten geçersiz handle üzerinde çalışıyor.
Dedup guard yok.

**Cozum:** Free işlemini slot bazında değil, **fiziksel kaynak bazında** yap —
3 slotun aynı handle'ı paylaştığını bilen kodda, `vkDestroyImage`/`vkFreeMemory`
sadece BİR KEZ çağrılmalı:
```zig
fn invalidate_pool() void {
    // ... (GL memory objects, GPU idle wait aynı kalır) ...

    // Tek fiziksel kaynak, 3 slot alias — BİR KEZ free et
    if (state.image_pool[0].image != null) {
        vk.vkDestroyImage(state.device, state.image_pool[0].image, null);
        vk.vkFreeMemory(state.device, state.image_pool[0].memory, null);
    }
    for (&state.image_pool) |*slot| {
        slot.image = null;
        slot.memory = null;
    }
    // ... (D3D11 NT handle temizliği aynı kalır) ...
}
```

**Test:** Çözünürlük değişimi (veya `invalidate_pool()`'u manuel iki kez
tetikleyen bir birim testi) sonrası crash/AV olmadığını doğrula. Vulkan
validation layer açıkken çalıştırmak, double-free'yi net bir VUID ile
yakalayabilir (`VUID-vkDestroyImage-image-parameter` benzeri).

---

### I4 — CPU Fallback transfer() Row-Pitch Farkini Yok Sayiyor

**[DÜZELTILDI — 2026-07-06]** `transfer()`'deki tek-blok
`memcpy(mapped.RowPitch * height_)` kaldırıldı; satır-pitch güvenli
`reji::copy_mapped_rows` yardımcısına geçildi
(`src/pipeline/capture/pitch_copy.h`, D3D11'den bağımsız → test edilebilir).
Satır başına `std::min(src_pitch, dst_pitch)` byte kopyalanır — talimattaki
tercih; `width*bpp` yaklaşımı yerine seçildi çünkü yeni bir format→bpp hesabı
riski almadan hem overrun'ı hem satır kaymasını önler (her iki pitch de
`>= width*bpp` olduğundan tüm piksel verisi korunur). Sentetik birim testi:
`tests/test_gpu_resource_pitch.cpp` (GpuResourcePitchTest, 3/3 PASS). Kullanım
durumu: cross-adapter + NT-handle share başarısız yolunda tetiklenir (düşük
olasılıklı degradation escape, ölü kod değil — ayrıntı SESSION_NOTES 6 Tem).
Düzeltme commit: `d5852c6` (fix(gpu): V8/I4 — CPU fallback transfer()
row-pitch farkını hesaba kat).

**Kaynak:** Fable 5 (4.1) + Opus 4.8 (2.2) — iki model tamamen bagimsiz
olarak ayni satiri, ayni kok nedenle bulmus. En yuksek guven seviyeli bulgu.

**Sorun:**
```cpp
// gpu_resource_manager.cpp, transfer() CPU fallback:
memcpy(dst_mapped.pData, mapped.pData, mapped.RowPitch * height_);
// SOURCE'un RowPitch'i kullaniliyor, ama dst_mapped'in KENDI RowPitch'i
// farkli olabilir (DYNAMIC texture, farkli tiling/alignment).
// Pitch'ler farkliysa: satir satir kayma -> goruntu bozulmasi VEYA
// buffer overrun (dst pitch < src pitch ise).
```

**Cozum (Opus'un onerdigi kod, dogrudan uygulanabilir):**
```cpp
for (uint32_t y = 0; y < height_; ++y) {
    memcpy(
        (uint8_t*)dst_mapped.pData + y * dst_mapped.RowPitch,
        (const uint8_t*)mapped.pData + y * mapped.RowPitch,
        std::min(src_bytes_per_row, dst_bytes_per_row)
    );
}
```

**Test:** Farkli RowPitch degerleri (ornegin 1920x1080 vs alignment-padded
genislik) simule eden birim testi; mevcut karakterizasyon test harness'ine
(varsa) regresyon kontrolu olarak eklenebilir.

**Efor:** Dusuk — bu Sprint 1'de en hizli kapatilabilecek kritik madde.

---

### I5 — execute_copy() Basarisiz Submit Sonrasi Layout State'i Yanlis Guncelliyor

**[DÜZELTILDI — 2026-07-06]** `target_layouts_[slot]` (~283) ve
`staging_layouts_[slot]` (~303) atamaları submit ÖNCESİnden submit BAŞARISINDAN
SONRAsına (~389-390) taşındı — `will_signal_gl`/`last_used_slot_`/`frame_counter_`
ile aynı disiplin. `vkCmdPipelineBarrier` komut kaydı yerinde kaldı; yalnız state
ATAMASI taşındı. Submit başarısız (`return false`) yolunda artık HİÇBİR layout
state'i değişmiyor → sonraki frame'in barrier'ı yanlış `oldLayout` kurmaz. Doğrulama:
**statik kod incelemesi** (submit-fail yolu yalnız `timeline_counter_` H17 rollback'ini
yapıyor; taşınan diziler 283/303 ile submit arasında hiç OKUNMUYOR — tek okuma ~226,
önceki frame değeriyle) + ctest regresyon temiz (bilinen 2 hariç yeni kırılma yok).
Sentetik test YOK — `vkQueueSubmit`'i device-lost olmadan başarısız yaptırmak pratik
değil (dürüstlük: "test edildi" değil, "statik olarak doğrulandı").
Düzeltme commit: `407792a` (fix(vulkan): V8/I5 — execute_copy() layout state'i
yalnızca submit başarılıysa güncelle).

**Kaynak:** Fable 5 (1.3) — tek kaynak ama Vulkan spec ihlali acik.

**Sorun:**
```cpp
// copy_optimizer.cpp (~line 330):
target_layouts_[slot] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // SUBMIT ONCESI yaziliyor
VkResult r = vkQueueSubmit(...);
// submit basarisiz olursa (r != VK_SUCCESS), timeline_counter_ dogru
// rollback ediliyor (H17, V7'de duzeltildi) AMA target_layouts_ hala
// SHADER_READ_ONLY diyor — gercek image layout hic degismedi.
// Sonraki frame'in barrier'i oldLayout=SHADER_READ_ONLY_OPTIMAL kullanir,
// gercekte image UNDEFINED/TRANSFER_DST -> VUID-VkImageMemoryBarrier-oldLayout ihlali.
```

**Cozum:**
```cpp
VkResult r = vkQueueSubmit(...);
if (r == VK_SUCCESS) {
    target_layouts_[slot] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    staging_layouts_[slot] = /* ... */;
    // will_signal_gl_, last_used_slot_ guncellemeleri de burada kalsin
} else {
    // layout degismedi, state'i eski haliyle birak
}
```

**Efor:** Dusuk — tek fonksiyon icinde sira degisikligi.

---

### I6 — is_copy_ready() Shutdown ile Yarisiyor (Olu Device Erisimi)

**Kaynak:** Opus 4.8 (1.5) — tek kaynak, gercek crash riski.

**Sorun:**
```cpp
// copy_optimizer.cpp, is_copy_ready():
// paintGL (GL thread) her framede cagiriyor.
// device_ == VK_NULL_HANDLE kontrolu var AMA senkronize degil —
// ~MainWindow (main thread) shutdown() cagirip device_'i null yaparken
// GL thread ayni anda vkWaitSemaphores(device_, ...) cagirabiliyor
// -> torn read, potansiyel olu handle uzerinde Vulkan cagrisi.
```

**Cozum:** I12 (MainWindow yikim sirasi) ile birlikte cozulmeli — kok neden
ayni: GL widget/thread'in tamamen durdugundan emin olmadan optimizer/pipeline
shutdown() cagirilmamali. Ek olarak burada atomic bir `alive_` flag'i
`is_copy_ready()` basinda kontrol edilebilir (ucuz, ek guvenlik katmani):
```cpp
if (!alive_.load(std::memory_order_acquire)) return false;
```

**Bagimlilik:** I12 ile ayni PR'da ele alinmali.

---

### I7 — WasapiCapture Shutdown Callback UAF Penceresi

**Kaynak:** Fable 5 (1.6) — tek kaynak, net UAF senaryosu.

**Sorun:**
```cpp
// wasapi_capture.cpp, shutdown() (~line 150):
// clear_owner() (seq_cst store) -> sonra seh_shutdown_leaf() icinde
// UnregisterEndpointNotificationCallback cagriliyor.
// Bir callback owner_'i clear_owner()'dan ONCE yuklemisse, hala
// on_device_change(...) calisiyor olabilir; bu da wake_event_.reset()
// SONRASI SetEvent(wake_event_.get()) cagirabilir -> freed HANDLE uzerinde UAF.
```

**Cozum:**
```cpp
// Sira degistir:
UnregisterEndpointNotificationCallback(...); // MMDevice API kontratina gore
                                               // in-flight callback'lerin
                                               // donmesini BEKLER
clear_owner();
// ancak simdi handle'lari release et
```

**Efor:** Dusuk — iki satirin yer degistirmesi, ama davranissal test
(rapid device hot-plug sirasinda shutdown) onerilir.

---

## Sprint 2 — Yuksek: Guvenlik, Yasam Dongusu, UI Senkronizasyonu

---

### I8 — WS Sunucusu Auth'suz (Drive-by Stream Kill)

**Kaynak:** Fable 5 (6.3) + Opus 4.8 (6.4) — cift dogrulanmis, gercek
saldiri senaryosu (canli yayinci hedefi dusunulunce onemli).

**Sorun:** `127.0.0.1:7070-7073`'e herhangi bir lokal islem (veya kotu
niyetli bir tarayici sekmesi — CORS WebSocket'i engellemez) baglanip
`{"cmd":"stream_stop"}` gonderebilir.

**Cozum:**
1. Baslangicta rastgele bir oturum token'i uret, UI'ya ilet (ornegin
   control.html'e query param olarak veya ilk WS mesaji olarak beklenir).
2. `Origin` header'ini dogrula — tarayici origin'lerini reddet.
3. obs-websocket uyumluluk katmani (Faz 1) zaten `Identify` adiminda auth
   alani tasiyor — bu ikisini AYNI mekanizmada birlestirmek mantikli:
   Faz 1 tasariminda auth "yok" olarak planlandi, bu bulgu isiginda
   en azindan `Origin` kontrolu Faz 1'e eklenmeli.

**Not:** Bu maddeyi Faz 1 (OBS-WebSocket) calismasiyla koordineli yap —
ayni dosyada (`ws_server.rs`) cakisan degisiklikler olabilir.

---

### I9 — CoUninitialize() RPC_E_CHANGED_MODE'da Kosulsuz Cagriliyor

**Kaynak:** Fable 5 (6.2) + Opus 4.8 (6.2) — ayni fonksiyon, ayni satir,
bagimsiz bulundu.

**Sorun:**
```cpp
// command_router.cpp, action_thread_main():
HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
// ... is yapilir ...
CoUninitialize(); // hr == RPC_E_CHANGED_MODE olsa bile HER ZAMAN cagriliyor
// Bu durumda COM bu thread'de BU cagriyla initialize edilmedi,
// ama uninitialize cagrisi apartment ref count'unu dengesiz birakiyor.
```

**Cozum:** `wasapi_capture.cpp::supervisor_main()` ve
`srt_output.cpp::ComGuard`'daki dogru pattern'i buraya da uygula:
```cpp
bool com_ok = SUCCEEDED(hr);
// ... is yapilir ...
if (com_ok) CoUninitialize();
```

**Efor:** Trivial — mevcut dogru pattern'in kopyalanmasi.

---

### I10 — SEH Filtreleri Kritik Istisnalari Yutuyor

**Kaynak:** Fable 5 (6.1) + Opus 4.8 (6.1) — Fable genel SEH leaves'e
odaklanmis (command_router, output_subsystem, metrics_subsystem,
recovery_coordinator), Opus spesifik olarak wasapi_capture'daki
ACCESS_VIOLATION yutulmasina odaklanmis. Birlestirilmis kapsam daha genis.

**Sorun:** `wasapi_capture.cpp` ve `srt_output.cpp` dogru sekilde
`EXCEPTION_STACK_OVERFLOW`/`BREAKPOINT`/`SINGLE_STEP`'i pass-through
yapiyor (`seh_filter`). Ama pipeline tarafindaki SEH leaves (command_router,
output_subsystem, metrics_subsystem, recovery_coordinator) kosulsuz
`EXCEPTION_EXECUTE_HANDLER` kullaniyor — bu, stack overflow'u yutar (guard
page restore edilmez, sonraki overflow kurtarilamaz AV olur) ve
`EXCEPTION_ACCESS_VIOLATION`'i da yutarak bellek bozulmasi sonrasi
calismaya devam eder (Opus: "corrupted state proceeds to encode/network").

**Cozum:**
1. Paylasimli bir `seh_filter(GetExceptionCode())` fonksiyonunu tek bir
   header'a cikar (wasapi_capture.cpp'deki mevcut implementasyonu tasi).
2. Tum SEH leaves bu paylasimli filtreyi kullansin.
3. `EXCEPTION_ACCESS_VIOLATION`'i da `EXCEPTION_CONTINUE_SEARCH` listesine
   ekle (veya minimum `__fastfail` ile hizli sonlandir) — SEH sadece
   *beklenen* driver istisnalarini yakalamali.

**Efor:** Orta — paylasimli header + her SEH leaf'inde tek satir degisikligi,
ama dokunulan dosya sayisi fazla (6+ dosya).

---

### I11 — Cift Consumer Race: Action Queue Iki Yerden Tuketiliyor

**Kaynak:** Opus 4.8 (4.3) — tek kaynak, mimari netlik sorunu.

**Sorun:**
```cpp
// command_router.cpp, action_thread_main(): 100ms poll, rj_action_dequeue
// main_window.cpp, pollHealingActions(): 200ms timer, AYRICA rj_action_dequeue

// Ayni kuyruğu iki bagimsiz tuketici cekiyor — her aksiyon sadece BIRINE
// teslim edilir (kuyruk semantigi geregi), hangisine gittigi race'e bagli.
// Sahiplik tanimli degil.
```

**Cozum:** Tek karar ver:
- **Secenek A:** C++ action thread'i tamamen kaldir, UI zaten kendi
  poll'unu yapiyor.
- **Secenek B:** Rust'tan event/condition-variable ile sinyal — polling'i
  tamamen kaldir, 100ms/200ms latency'sini de ortadan kaldirir.

I1 (rule engine wiring) tamamlanana kadar bu kuyruk zaten hep bos olacagi
icin, I11'i I1 ile AYNI oturumda ele almak mantikli — ikisi de ayni kuyrugun
farkli uclarinda.

**Efor:** Orta — mimari karar + iki dosyada degisiklik.

---

### I12 — MainWindow Yikim Sirasi (GL Widget / Optimizer Race)

**Kaynak:** Fable 5 (1.8) — I6 (Opus) ile ayni kok nedene isaret ediyor,
birlikte ele alinmali.

**Sorun:**
```cpp
// ~MainWindow():
// stopFrameThread() -> ~pipeline_ (once copy_optimizer_.shutdown() cagirilir)
// AMA preview_widget_ hala GL repaint yapiyor olabilir (Qt event loop
// senkron degil), bridge_'e/copy_optimizer_'e referans tutuyor.
```

**Cozum (Fable'in onerdigi sira):**
```cpp
// ~MainWindow icinde:
stopFrameThread();
pipeline_->shutdown();
preview_widget_->setBridge(nullptr);
preview_widget_->setCopyOptimizer(nullptr);  // widget'in GL context'i hala
                                               // acikken referanslari kopar
copy_optimizer_.shutdown();
```

**Bagimlilik:** I6 ile ayni PR.

---

### I13 — Ilk Kare Sira Hatasi (GL, Tamamlanmamis Vulkan Blit'i Okuyabilir)

**Kaynak:** Opus 4.8 (3.2) — tek kaynak, ilk-kullanim/slot-degisim
senaryosunda gercek risk.

**Sorun:** `glWaitSemaphoreEXT` sadece `is_slot_signaled()` true ise
calisiyor; ama bir slot'un ILK kullaniminda sinyal submit sirasinda set
edilip ancak BIR SONRAKI paintGL'de kontrol ediliyor — aradaki frame'de
render, Vulkan'in henuz yazmakta oldugu texture'i okuyabilir.

**Cozum:** Render'in her zaman *tamamlanmis* son kopyayi ornekledigini
garanti et — `current_pool_idx_` render icin `last_used_slot()`'un
*tamamlanmis* halini gostermeli, ya da `is_copy_ready()` render'i gate'lesin.

**Bagimlilik:** I6/I12 ile ayni bolgede calisiyor, ama farkli bug — ayri
PR olarak ele alinabilir.

---

### I14 — rj_metrics_poll Implementasyonu Yok

**Kaynak:** Fable 5 (1.9) — tek kaynak, kullanici-goruntusu etkisi var
(status bar muhtemelen hic guncellenmiyor).

**Sorun:**
```cpp
// ffi_bridge.h:
extern int rj_metrics_poll(RjMetricSample* out);
// main_window.cpp::pollMetrics() bunu her saniye cagiriyor
// Rust tarafinda #[no_mangle] karsiligi yok (yorum "stub" diyor, ama
// gercek implementasyon bulunamadi)
```

**Cozum:** Ya Rust'ta gercek implementasyonu yaz (MetricState'ten en son
ornegi drain et), ya da deklarasyonu ve UI cagri yolunu tamamen kaldir.
Once hangi durumun gecerli oldugunu (link hatasi mi, stub mu) derleme
loguyla dogrula.

**Efor:** Dusuk-Orta — once teshis (build/link kontrolu), sonra fix.

---

## Sprint 3 — Orta: Performans ve Mimari Tutarlilik

---

### I15 — rj_metrics_push Hot-Path Alloc (RT Thread)

**Kaynak:** Fable 5 (2.3, 6.5) + Opus 4.8 (1.2, 2.1) — cift dogrulanmis,
iki ayri acidan (guvenlik: unaligned read; performans: alloc+clone).

**Sorun:** Mutex + JSON format + `String::clone()` + broadcast, ses capture
thread'inden ve SRT'den (saniyede bir) cagriliyor — RT/hot-path'te heap
alloc yasagini ihlal ediyor. Ayrica `unsafe { *sample }` alignment kontrolu
olmadan pointer'i dereference ediyor.

**Cozum:**
1. Alignment: `core::ptr::read_unaligned(sample)` kullan (Opus 1.2).
2. Throttle: JSON formatlama/broadcast'i ring-push'tan ayir — hot path'te
   sadece `state.metric_ring.push(s)` (non-blocking), formatlama/broadcast
   ayri bir async task'ta (16ms tick) yapilsin (Opus 2.1 onerisi).

**Efor:** Orta — ring buffer + ayri drainer task tasarimi (muhtemelen
zaten var olan drainer'a entegre edilebilir, V7'de bahsedilen drainer
altyapisina bakilmali).

---

### I16 — query_gpu_load_pct Hot-Path Alloc

**Kaynak:** Fable 5 (2.2) + Opus 4.8 (4.5) — identik bulgu.

**Sorun:** `std::vector<BYTE> buf(buf_bytes)` her 1Hz pollde yeniden
alloc ediliyor.

**Cozum:** Buffer'i member degiskene tasi, sadece buyudukce resize et.

**Efor:** Trivial.

---

### I17 — Iki Rakip Frame-Pacing Implementasyonu

**Kaynak:** Opus 4.8 (5.3) — tek kaynak, onemli mimari bulgu.

**Sorun:** `FramePacer` (QPC tabanli sleep/spin) ve `DxgiFramePacing`
(DXGI GetFrameStatistics tabanli) es zamanli calisiyor; `run_frame()`
`FramePacer::pace()` cagiriyor AMA DXGI zaten `AcquireNextFrame(timeout=17)`
icinde blokluyor — cift pacing. `main.cpp` yorumunda bile "DXGI zaten
pacing yapiyor" notu var, ama kod hala ikisini de calistiriyor.

**Cozum:** Tek otorite sec. DXGI acquire pacing sagliyorsa,
`FramePacer::pace()` gereksiz spin-wait ekliyor demektir — kaldir veya
DXGI pacing aktifken no-op yap.

**Efor:** Dusuk-Orta — hangi yolun gercekte pacing sagladigini olcerek
dogrulamak gerekiyor (WGC path'inde DXGI'nin devrede olmadigini unutma,
sadece DXGI fallback path'i icin gecerli olabilir).

---

### I18 — wasapi_capture.cpp FFI'yi Dogrudan Cagiriyor (Katman Ihlali)

**Kaynak:** Opus 4.8 (5.2) — tek kaynak, test edilebilirlik sorunu.

**Sorun:** Dusuk seviye ses yakalama kodu `rj_connection_lost`/
`rj_metrics_push`'i dogrudan cagiriyor, digerlerinde kullanilan
subsystem/orchestrator katmanlamasini (`MetricsSubsystem::push`,
`OutputSubsystem`) atliyor. Bu, ses cihaz katmanini orchestrator ABI'sine
sikica baglıyor ve mocklama/test etmeyi imkansiz kiliyor.

**Cozum:** `init()`'e enjekte edilen bir callback uzerinden yonlendir
(mevcut `AudioFrameCallback` pattern'i gibi), pipeline/orchestrator
katmani FFI'yi cagirsin.

**Efor:** Orta — imza degisikligi + cagiran taraflarin guncellenmesi.

---

### I19 — HEALING_MODE Semantigi 4 Katmanda Farkli

**Kaynak:** Fable 5 (5.4) — tek kaynak, I1/I20 ile iliskili (ayni
healing.rs bolgesi).

**Sorun:** `ffi.rs` 0..3 kabul ediyor, `healing.rs::HealingMode` enum'i
sadece 3 varyanta sahip (AutoPilot/CoPilot/ManualAssist), `ffi_bridge.h`
4 taniyor (Auto/CoPilot/Assist/Manual), UI de 4 gosteriyor. Rust'in ic
enum'u mode=2 (Assist) ile mode=3 (Manual) arasini ayirt edemiyor.

**Cozum:** FFI header'da tek kanonik enum, Rust'ta `#[repr(u32)]` ile
birebir yansitilmis + static assert. `HealingMonitor`'un atomic mode'u
tutarli tuketmesini sagla (bkz. I20).

**Bagimlilik:** I1 (rule engine wiring) yapilirken bu enum tutarliligi
zaten gozden gecirilecek — ayni PR'da birlestirilebilir.

---

### I20 — evaluate_adaptive() Donmus self.mode Okuyor

**Kaynak:** Fable 5 (5.5) — I19'un somut sonucu.

**Sorun:** `healing.rs` (~line 290) `self.mode.load()` (constructor'da
donmus deger) okuyor, geri kalan her yer global atomic `HEALING_MODE`'u
okuyor. Sonuc: adaptive katman kullanici `rj_set_healing_mode` ile modu
degistirse bile sonsuza kadar AutoPilot gibi davraniyor.

**Cozum:** `self.mode.load()`'u global atomic okumasiyla degistir (veya
mode degisikliklerini bir channel ile monitor'a ilet).

**Efor:** Trivial — I1 ile ayni PR'da yapilmasi mantikli (ayni fonksiyon
bolgesi, ayni test kapsamı).

---

## Sprint 4 — Dusuk: Temizlik ve Bakim

---

### I21 — Hardcoded C:\reji-studio\ Yollari + Kontrolsuz freopen

**Kaynak:** Fable 5 (5.1) + Opus 4.8 (5.1, 6.6) — cift dogrulanmis.

**Sorun:** `ffi.rs`, `ws_server.rs::log_to_file`, `main.cpp`'te
(`freopen("C:\\reji-studio\\run.log", ...)`, return degeri kontrol
edilmiyor) hardcoded mutlak yol. Son kullanici makinesinde bu dizin
mevcut/yazilabilir olmayabilir; `freopen` basarisiz olursa TUM stderr
tanılama ciktisi sessizce kaybolur.

**Cozum:** `tracing` + configurable file appender'a gecir; yollari
`%LOCALAPPDATA%\Reji\logs\` uzerinden turet (proje zaten
`ShaderCache::get_cache_dir`'de `SHGetFolderPath` kullaniyor, ayni
pattern'i tekrar kullan). `freopen` donus degerini kontrol et.

---

### I22 — Bayat ABI Yorum Satirlari

**Kaynak:** Fable 5 (5.2) + Opus 4.8 (1.1) — identik bulgu, ayni
rakamlar (56B/+51 vs gercek 64B/+55).

**Cozum:** `metrics.rs`'teki cbindgen doc-comment kaynagini duzelt
(ayni bayat "60 byte" basligini tasiyor), `ffi_bridge.h`'i duzelt.
Uzun vadede el yazisi offset yorumlarini tamamen kaldirip sadece
`static_assert`'lere guven.

---

### I23 — Bridge/Optimizer Slot Sayaclari Bagimsiz

**Kaynak:** Fable 5 (3.6) + Opus 4.8 (3.3) — ayni kok nedene farkli
acilardan isaret ediyor.

**Cozum:** Tek dogruluk kaynagi: bridge'in staging/target slot index'ini
`execute_copy`'ye parametre olarak gecir, fiilen kullanilan slot'u geri
dondur; widget GL texture secimi ve semaphore wait icin bu donus degerini
kullansin.

---

### I24 — rj_reload_rules/rj_connection_lost Sinirsiz CStr Okuma

**Kaynak:** Opus 4.8 (6.3) — tek kaynak, dusuk risk (bugun trusted caller)
ama gelecekte WS'e baglanirsa yukselir.

**Cozum:** Sabit izin verilen yol altina sinirla (`~/.reji/rules.json`),
caller-supplied path'i yok say; uzunluk sinirli varyant ekle.

---

### I25 — zig_win32_compat.c Stack Guard Fallback Sabit

**Kaynak:** Fable 5 (6.7) — tek kaynak, dusuk olasilikli senaryo.

**Cozum:** `BCryptGenRandom` basarisiz olursa `__rdtsc()` +
`GetCurrentProcessId()` + stack adresi entropisini XOR'layan bir fallback.

---

### I26 — Demo Fonksiyon Production Crate'te

**Kaynak:** Opus 4.8 (5.5) — tek kaynak, trivial temizlik.

**Cozum:** `lib.rs`'teki `add(u64,u64)` + `it_works` testini sil.

---

### I27 — ITransport SEH Virtual-Call Riski (noexcept ile Saglamlastir)

**[DÜZELTILDI — 2026-07-09]** `ITransport::send`/`shutdown` arayuz imzasina
`noexcept` eklendi (`i_transport.h`); `SrtTransport`/`RtmpTransport`
implementasyonlari `noexcept override` + ic `try{...}catch(...)` sarmalayici
oldu (send → `return false`, shutdown → yut). Boylece exception→bool/void
sozlesmesi tip sistemiyle garanti — yeni implementor atlayamaz. `init`/
`is_connected`'a `noexcept` EKLENMEDI (opsiyoneldi; `init` sicak yol degil,
sozlesmeyi daraltmadik). Dis SEH sarmalayicilar (`pipeline.cpp`/
`output_subsystem.cpp`) DOKUNULMADI — Opus'un "SEH'i leaf'lere it" onerisi
ayri karar. Dogrulama: throw deneyi tekrarlandi — ONCE SEH belirsiz sekilde
yutuyordu, SIMDI noexcept ihlali kesin `std::terminate`'e gidiyor (derleyici
C4297 + `ASSERT_DEATH` PASS ile kanitlandi, sonra geri alindi); build temiz,
`OutputSubsystemTest` 7/7 PASS, `ctest` bilinen 2 disinda yeni kirilma yok.
Ayrinti: SESSION_NOTES 9 Tem. Commit: bkz. asagidaki commit.

**Kaynak:** Fable 5 (6.1) + Opus 4.8 (5.3) — 06.07.2026 taze tarama, bagimsiz
olarak ayni SEH virtual-call kararini elestirdi.

**Sorun:** Faz2/Aşama1'de `SrtTransport`/`RtmpTransport::shutdown()` bir SEH
`__try` blogu icinden cagriliyordu; C++ exception'inin SEH tarafindan
yakalanmasi derleyici/`/EHa` ayarina bagli **belirsiz bir garanti** —
her yeni `ITransport` implementasyonu icin elle tekrar test gerektirir.

**Cozum:** Arayuz sozlesmesine `noexcept` ekleyerek garantiyi tip sistemine
tasi — noexcept ihlali SEH'e ulasmadan `std::terminate`'e gider (net,
ongorulebilir), her implementor exception'i kendi icinde bool/void'e cevirmek
zorunda kalir. SEH mimarisi yeniden duzenlenmedi (kapsam disi).

---

## Uygulama Notlari

- **I1 + I11 + I19 + I20** ayni "self-healing / action pipeline" bolgesini
  kapsiyor (`healing.rs`, `command_router.cpp`, `main_window.cpp`) —
  bunlari tek bir odakli oturumda, sirali olarak (once I1: wiring, sonra
  I19/I20: mode tutarliligi, sonra I11: consumer sahipligi) ele almak,
  ayri ayri dort PR'dan daha az regresyon riski tasir.
- **I6 + I12 + I13** ayni "GL/Vulkan shutdown ve frame sirasi" bolgesini
  kapsiyor — birlikte ele alinmali.
- **09.07.2026 keşfi bu grupla ilgili temel varsayımı çürüttü** — I2/I3/I28-I31
  TEK bir bölge değil, İKİ bağımsız yol (encode: GpuResourceManager, ölü/CPU-fallback;
  preview: capture_dxgi→Vulkan→GL, canlı). Ayrı commit'ler önerilir, tek paket YOK.
  Önerilen gerçek sıra (SESSION_NOTES'taki keşif raporundan):
  1. **I32** (üçlü-free) — izole, düşük risk, en yüksek gerçek-bug önceliği. İLK YAPILACAK.
  2. **I30 → ölü kod temizliği** — `keyed_mutex_display_/encode_/copy_fence_` üyelerini
     `GpuResourceManager`'dan kaldır (kullanılmıyorlar, kafa karıştırıyorlar).
  3. **I28** — muhtemelen kod değişikliği yok, validation-layer ile "gerçekten
     sorun mu" doğrulaması + kasıtlı tasarımın kod yorumunda daha net belgelenmesi.
  4. **I2/I3** — preview yolunun `use_keyed_mutex_=false` fallback senkronizasyon
     doğruluğu, ayrı ve dikkatli bir inceleme gerektiriyor (henüz derinlemesine
     incelenmedi, sadece konumu doğru tespit edildi).
  5. **I29, I31** — kapandı, kod değişikliği gerekmiyor (I29 → I32'ye devretti,
     I31 → defekt yok).
  - _(Eski not, dürüstlük ilkesi gereği korunuyor: 06.07.2026 taze taraması bu alanı
    "tek bölge, TEK oturumda ele alınmalı, önce I30" diye işaretlemişti; 09.07 keşfi
    bu ön-varsayımı yukarıdaki bulgularla revize etti.)_
- **I8** (WS auth), Faz 1 (OBS-WebSocket uyumluluk) calismasiyla ayni dosyada
  (`ws_server.rs`) cakisiyor — iki isin sirasini/koordinasyonunu planla.
- Sprint 1 tamamlaninca yeni bir Fable 5 taramasi (V9) onerilir — ozellikle
  I2/I3/I5 duzeltmeleri Vulkan validation layer ile dogrulanmali.
