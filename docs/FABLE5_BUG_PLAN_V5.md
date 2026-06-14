\# Reji Studio — Fable 5 Besinci Tarama Duzeltme Plani (V5)



\*\*Tarih:\*\* 12.06.2026

\*\*Kaynak:\*\* Fable 5 besinci tarama — 59 dosya, 208K token, $2.73

\*\*Onceki:\*\* FABLE5\_BUG\_PLAN\_V4.md (E1-E18 tamamlandi)



\---



\## Oncelik Matrisi



| #  | Sorun | Dosya | Oncelik | Sprint |

|----|-------|-------|---------|--------|

| F1 | Healing mode enum mismatch — MANUAL=3 C++ vs mode==1 Rust | ffi.rs + healing.rs | Kritik | Sprint 1 |

| F2 | AMD path ReleaseSync(0) acquire edilmeden — D3D11 UB | capture\_dxgi.cpp | Kritik | Sprint 1 |

| F3 | execute\_copy state corruption on submit failure | copy\_optimizer.cpp | Kritik | Sprint 1 |

| F4 | Pool invalidation: VkImage destroy GPU in-flight iken | external\_memory\_bridge.cpp | Kritik | Sprint 1 |

| F5 | WASAPI ReleaseBuffer clamped frame count — AUDCLNT\_E\_INVALID\_SIZE | wasapi\_capture.cpp | Kritik | Sprint 2 |

| F6 | VulkanInitializer::initialize() idempotent degil — double init leak | vulkan\_initializer.cpp | Kritik | Sprint 2 |

| F7 | init\_preview\_staging her frame texture yeniden olusturuyor | capture\_dxgi.cpp | Kritik | Sprint 2 |

| F8 | Frame counter uclu desync — optimizer/preview/bridge | copy\_optimizer.cpp + preview\_widget.cpp | Yuksek | Sprint 2 |

| F9 | HealingOverlay signal birikimi — duplicate emissions | healing\_overlay.cpp | Yuksek | Sprint 3 |

| F10 | NVENC set\_resolution yanlis struct alani | encode\_nvenc.cpp | Yuksek | Sprint 3 |

| F11 | shader\_cache WideCharToMultiByte buffer overflow | shader\_cache.cpp | Yuksek | Sprint 3 |

| F12 | Shader cache FNV1a — driver update sonrasi stale | shader\_cache.cpp | Orta | Sprint 3 |

| F13 | MetricsCollector hic schedule edilmiyor — metrics daima 0 | pipeline.cpp | Orta | Sprint 4 |

| F14 | Audio + video metrikleri ayni ring buffer — karisik | wasapi\_capture.cpp + pipeline.cpp | Orta | Sprint 4 |

| F15 | SRT listener socket replace — yeniden accept imkansiz | srt\_output.cpp | Orta | Sprint 4 |

| F16 | QPC timestamp overflow — 3 hafta uptime sonrasi | frame\_pacing.cpp | Dusuk | Sprint 4 |

| F17 | Debug messenger koşulsuz olusturuluyor — extension yoksa null ptr | vulkan\_initializer.cpp | Dusuk | Sprint 4 |

| F18 | CopyOptimizer init partial failure resource leak | copy\_optimizer.cpp | Dusuk | Sprint 4 |



\---



\## Sprint 1 — Kritik Sync ve State



\---



\### F1 — Healing Mode Enum Mismatch



\*\*Sorun:\*\*

C++ enum: AUTO\_PILOT=0, CO\_PILOT=1, ASSIST=2, MANUAL=3

Rust healing.rs: `if mode == 1` → "Manual mode, skip" yorumu var

Ama mode 1 aslinda CO\_PILOT — yanlis mod durduruyuyor.



\*\*Cozum:\*\*

```rust

// healing.rs — duzeltilmis kontrol:

// 0=AutoPilot, 1=CoPilot, 2=Assist, 3=Manual

if mode == 3 { // Manual — komut uretme

&#x20;   continue;

}

```



```cpp

// ffi\_bridge.h — enum degerlerini belgele:

// RJ\_MODE\_AUTO   = 0

// RJ\_MODE\_COPILOT= 1

// RJ\_MODE\_ASSIST = 2

// RJ\_MODE\_MANUAL = 3

```



\---



\### F2 — AMD Path ReleaseSync Sorunu



\*\*Sorun:\*\*

AMD'de `use\_keyed\_mutex\_=false` iken `capture\_next()`:

`keyed\_mutex\_shared\_->ReleaseSync(0)` — hic acquire edilmeden.

D3D11 semantigi ihlali — UB, cogu durumda DXGI\_ERROR doner.



\*\*Cozum:\*\*

```cpp

// capture\_dxgi.cpp capture\_next():

if (use\_keyed\_mutex\_ \&\& keyed\_mutex\_shared\_) {

&#x20;   HRESULT hr = keyed\_mutex\_shared\_->AcquireSync(0, 16);

&#x20;   if (FAILED(hr)) return false;

&#x20;   display\_ctx\_->d3d\_context()->CopyResource(...);

&#x20;   keyed\_mutex\_shared\_->ReleaseSync(1);

} else {

&#x20;   // Keyed mutex yok — dogrudan kopyala, sync yok

&#x20;   display\_ctx\_->d3d\_context()->CopyResource(...);

}

```



\---



\### F3 — execute\_copy State Corruption



\*\*Sorun:\*\*

`execute\_copy()` submit oncesi:

\- `frame\_counter\_++`

\- `slot\_gl\_signaled\_\[slot] = true`



Submit basarisiz olsa bile state guncelleniyor.

Sonraki frame'de yanlis slot kullaniliyor.



\*\*Cozum:\*\*

```cpp

// copy\_optimizer.cpp execute\_copy():

// State'i submit sonrasi guncelle:

VkResult result = vkQueueSubmit(...);

if (result != VK\_SUCCESS) {

&#x20;   // State'i geri al:

&#x20;   // frame\_counter\_ zaten arttirilmadiysa sorun yok

&#x20;   return false;

}

// Basarili — simdi state'i guncelle:

slot\_gl\_signaled\_\[slot] = true;

frame\_counter\_++;

```



\---



\### F4 — Pool Invalidation GPU In-Flight



\*\*Sorun:\*\*

`get\_frame\_images()` texture degisince pool'u invalidate ediyor.

`invalidate\_pool()` icinde `vkDestroyImage` cagiriliyor.

Ama GPU hala o image'lari kullaniyor olabilir.



\*\*Cozum:\*\*

```cpp

// external\_memory\_bridge.cpp invalidate\_pool():

// Once GPU'nun bitmesini bekle:

if (device\_ != VK\_NULL\_HANDLE) {

&#x20;   vkDeviceWaitIdle(device\_);

}

// Sonra destroy:

for (auto img : image\_pool\_) {

&#x20;   if (img) vkDestroyImage(device\_, img, nullptr);

}

```



\---



\## Sprint 2 — Lifetime ve Init Sorunlari



\---



\### F5 — WASAPI ReleaseBuffer Clamped Count



\*\*Sorun:\*\*

```cpp

uint32\_t frames = std::min(available, kMaxFrames); // clamp

convert\_to\_float(buf, frames, ...);

seh\_release\_buffer(audio\_client\_, frames, 0); // YANLIS — clamped deger

```

`ReleaseBuffer` tam olarak `GetBuffer`'dan gelen deger ile cagrilmali.



\*\*Cozum:\*\*

```cpp

uint32\_t available = 0; // GetBuffer'dan gelen tam deger

// ...

seh\_release\_buffer(audio\_client\_, available, 0); // orijinal deger

// Process etmek istedigimiz miktar ayri degisken:

uint32\_t to\_process = std::min(available, kMaxFrames);

convert\_to\_float(buf, to\_process, ...);

```



\---



\### F6 — VulkanInitializer Double Init Leak



\*\*Sorun:\*\*

`initialize()` iki kez cagilirsa:

\- Eski `VkDevice` destroy edilmiyor

\- Eski `VkInstance` destroy edilmiyor

\- Bridge ve copy\_optimizer stale handle tutuyor



\*\*Cozum:\*\*

```cpp

// vulkan\_initializer.cpp initialize():

if (initialized\_) {

&#x20;   fprintf(stderr, "\[Vulkan] Zaten baslatildi, atlaniyor\\n");

&#x20;   return true;

}

// ... init devam eder

initialized\_ = true;

```



\---



\### F7 — init\_preview\_staging Her Frame



\*\*Sorun:\*\*

`ensure\_preview\_staging()` her frame `preview\_staging\_` null mu diye kontrol ediyor.

Ama `init\_preview\_staging()` `preview\_staging\_`'i set etmiyor — her frame yeniden olusturuyor.

Per-frame texture allocation + handle leak.



\*\*Cozum:\*\*

`init\_preview\_staging()` fonksiyonunun `preview\_staging\_`'i set edip etmedigini kontrol et.

Set etmiyorsa:

```cpp

void DxgiCapturePipeline::init\_preview\_staging() {

&#x20;   if (preview\_staging\_) return; // zaten var

&#x20;   // ... texture olustur

&#x20;   preview\_staging\_ = new\_texture; // SET ET

}

```



\---



\### F8 — Frame Counter Uclu Desync



\*\*Sorun:\*\*

3 ayri frame counter:

\- `GpuCopyOptimizer::frame\_counter\_`

\- `PreviewWidget::frame\_counter\_`

\- `ExternalMemoryBridge::pool\_index\_`



execute\_copy atlandiginda bunlar desync oluyor.

Yanlis slot icin semaphore bekleniyor.



\*\*Cozum:\*\*

Tek kaynak — slot'u `execute\_copy` donus degerinden al:

```cpp

// copy\_optimizer.cpp:

// execute\_copy(..., uint32\_t\* out\_slot)

// \*out\_slot = slot; // kullanilan slot'u doganla



// preview\_widget.cpp:

uint32\_t used\_slot = 0;

if (copy\_optimizer\_->execute\_copy(..., \&used\_slot)) {

&#x20;   current\_pool\_idx\_ = used\_slot; // optimizer'in slot'u

}

```



\---



\## Sprint 3 — NVENC, Shader, Healing



\---



\### F9 — HealingOverlay Signal Birikimi



\*\*Sorun:\*\*

`HealingOverlay` her acildiginda `dataChanged` sinyaline baglanıyor.

Eski baglantılar disconnect edilmiyor.

N. acilista ayni action N kez tetikleniyor.



\*\*Cozum:\*\*

```cpp

// healing\_overlay.cpp — baglantidan once disconnect:

disconnect(model\_, \&HealingModel::dataChanged,

&#x20;          this, \&HealingOverlay::onDataChanged);

connect(model\_, \&HealingModel::dataChanged,

&#x20;       this, \&HealingOverlay::onDataChanged);

// Veya Qt::UniqueConnection kullan:

connect(model\_, \&HealingModel::dataChanged,

&#x20;       this, \&HealingOverlay::onDataChanged,

&#x20;       Qt::UniqueConnection);

```



\---



\### F10 — NVENC set\_resolution Yanlis Struct



\*\*Sorun:\*\*

```cpp

saved\_cfg.encodeWidth = new\_width;   // NV\_ENC\_CONFIG'da bu alan YOK

saved\_cfg.encodeHeight = new\_height; // NV\_ENC\_CONFIG'da bu alan YOK

```

Bu alanlar `NV\_ENC\_INITIALIZE\_PARAMS`'ta. Yanlis struct yaziliyor.



\*\*Cozum:\*\*

```cpp

// encode\_nvenc.cpp set\_resolution():

saved\_init\_.encodeWidth  = new\_width;   // NV\_ENC\_INITIALIZE\_PARAMS

saved\_init\_.encodeHeight = new\_height;

saved\_init\_.maxEncodeWidth  = new\_width;

saved\_init\_.maxEncodeHeight = new\_height;

// reconfig ile uygula

```



\---



\### F11 — Shader Cache Buffer Overflow



\*\*Sorun:\*\*

```cpp

WideCharToMultiByte(..., size, buf, size); // null terminator icin yer yok

```

`size` karakter icin buffer var ama `size` byte yazilıyor — 1 byte taşma.



\*\*Cozum:\*\*

```cpp

WideCharToMultiByte(..., size, buf, size - 1); // null icin yer birak

buf\[size - 1] = '\\0'; // explicit null terminate

```



\---



\### F12 — Shader Cache Driver Update Sonrasi Stale



\*\*Sorun:\*\*

FNV1a hash — driver veya versiyon bilgisi icermiyor.

Driver guncellendikten sonra eski SPIR-V yuklenebilir.



\*\*Cozum:\*\*

```cpp

// shader\_cache.cpp hash'e ekle:

// Driver versiyonu + Vulkan API versiyonu

VkPhysicalDeviceProperties props;

vkGetPhysicalDeviceProperties(phys\_device\_, \&props);

hash ^= fnv1a(props.driverVersion);

hash ^= fnv1a(props.apiVersion);

```



\---



\## Sprint 4 — Metrikler, SRT, Kucuk Duzeltmeler



\---



\### F13 — MetricsCollector Schedule Edilmiyor



\*\*Sorun:\*\*

`MetricsCollector::poll()` hic cagirilmiyor.

`total\_frames\_` daima 0.

`frame\_drop\_pct` daima 0.



\*\*Cozum:\*\*

```cpp

// pipeline.cpp run\_frame() sonunda:

metrics\_collector\_.poll();

```



\---



\### F14 — Audio + Video Metrikleri Karisik



\*\*Sorun:\*\*

Audio ve video metrikleri ayni ring buffer'a push ediyor.

Audio bitrate (3072 kbps) video bitrate'in (6000 kbps) uzerine yazilıyor.

Healing logic yanlis bitrate okuyor.



\*\*Cozum:\*\*

Ayri ring buffer veya `source\_id` field ekle:

```rust

// MetricSample'a ekle:

pub source\_id: u8, // 0=video, 1=audio

// MetricState::update() source\_id'e gore ayir

```



\---



\### F15 — SRT Listener Socket Replace



\*\*Sorun:\*\*

Client baglandiktan sonra listen socket client socket ile replace ediliyor.

Client disconnect sonrasi yeniden accept imkansiz.



\*\*Cozum:\*\*

```cpp

// srt\_output.cpp:

// Listen socket'i ayri tut:

SRTSOCKET listen\_sock\_ = SRT\_INVALID\_SOCK;

SRTSOCKET client\_sock\_ = SRT\_INVALID\_SOCK;

// Client disconnect'te client\_sock\_ kapat, listen\_sock\_ koru

// Yeniden accept icin listen\_sock\_'u kullan

```



\---



\### F16 — QPC Timestamp Overflow



\*\*Sorun:\*\*

`current\_qpc \* 1'000'000ULL` — QPC \~3 haftada UINT64\_MAX'e yaklasir.

Overflow sonrasi timestamp sifirlanir.



\*\*Cozum:\*\*

```cpp

// frame\_pacing.cpp:

// Baslangic QPC'yi cikar, sonra carpma yap:

uint64\_t elapsed = current\_qpc - start\_qpc\_; // kucuk sayi

out\_stats->timestamp\_us = elapsed \* 1'000'000ULL / freq.QuadPart;

```



\---



\### F17 — Debug Messenger Extension Kontrolu



\*\*Sorun:\*\*

Debug messenger koşulsuz olusturuluyor.

`VK\_EXT\_debug\_utils` extension aktif degilse function pointer null.



\*\*Cozum:\*\*

```cpp

// vulkan\_initializer.cpp:

if (debug\_utils\_enabled\_) {

&#x20;   create\_debug\_messenger();

}

```



\---



\### F18 — CopyOptimizer Init Partial Failure Leak



\*\*Sorun:\*\*

`init()` icinde `CHECK\_VK` basarisiz olursa:

Command pool ve semaphore'lar temizlenmiyor.



\*\*Cozum:\*\*

```cpp

// copy\_optimizer.cpp init():

// Basarisizlika temizle:

auto cleanup = \[this]() { shutdown(); };

// RAII veya goto cleanup pattern

if (!create\_command\_pool()) { cleanup(); return false; }

if (!create\_semaphores())   { cleanup(); return false; }

```



\---



\## Build ve Test



```cmd

cd C:\\reji-studio

python scripts/build.py --clean

build\\src\\ui\\reji\_app.exe 2> err.log

type err.log | findstr "VUID\\|ERROR\\|INVALID\\|overflow"

cargo test --manifest-path src/orchestrator/Cargo.toml

just abi-check

```



\---



\## Takip



\- \[x] Sprint 1 tamamlandi (F1-F4)

\- \[x] Sprint 2 tamamlandi (F5-F8)

\- \[x] Sprint 3 tamamlandi (F9-F12)

\- \[x] Sprint 4 tamamlandi (F13-F18)

\- \[ ] Fable 5 altinci tarama yapildi



\---



\*Bu belge C:\\reji-studio\\docs\\FABLE5\_BUG\_PLAN\_V5.md olarak kaydedilmeli.\*



