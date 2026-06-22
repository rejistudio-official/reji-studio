\# Reji Studio — Yedinci Tarama Duzeltme Plani (V7)



\*\*Tarih:\*\* 21.06.2026

\*\*Kaynaklar:\*\*

&#x20; Opus 4.8    ($1.18) — En kapsamli, 6 kritik yeni bulgu

&#x20; GLM-5.2     ($0.13) — 3-4 gercek bulgu

&#x20; Kimi K2.7   ($0.32) — build.rs eksikligi

&#x20; MiniMax M3  ($0.07) — drainer eksikligi



\*\*Onceki:\*\* FABLE5\_BUG\_PLAN\_V6.md (G1-G13 tamamlandi)



\---



\## Model Karsilastirmasi



| Model    | Maliyet | Yeni Bulgu | Kalite   |

|----------|---------|-----------|----------|

| Opus 4.8 | $1.18   | 8 kritik  | 5/5      |

| GLM-5.2  | $0.13   | 4 gercek  | 4/5      |

| Kimi K2.7| $0.32   | 2 gercek  | 3/5      |

| MiniMax  | $0.07   | 1 gercek  | 2/5      |



\---



\## Oncelik Matrisi



| #   | Kaynak   | Sorun                                              | Dosya                    | Oncelik | Sprint   |

|-----|----------|----------------------------------------------------|--------------------------|---------|----------|

| H1  | Opus     | Tek command buffer SIMULTANEOUS\_USE + reset spec ihlali | copy\_optimizer.cpp  | Kritik  | Sprint 1 |

| H2  | Opus     | Keyed mutex yanlis VkDeviceMemory koruyor — tearing | copy\_optimizer.cpp       | Kritik  | Sprint 1 |

| H3  | Opus     | AMD path cross-API sync tamamen yok                | copy\_optimizer.cpp       | Kritik  | Sprint 1 |

| H4  | Opus+GLM | slot\_gl\_signaled\_ non-atomic cross-thread erisim  | copy\_optimizer.h         | Kritik  | Sprint 1 |

| H5  | GLM      | shared\_handle\_ CloseHandle eksik — handle leak     | gpu\_resource\_manager.cpp | Yuksek  | Sprint 2 |

| H6  | GLM      | Pipeline::shutdown() thread-safe degil            | pipeline.cpp             | Yuksek  | Sprint 2 |

| H7  | Opus     | BGRA/RGBA channel swap — preview renk hatasi       | preview\_widget.cpp       | Yuksek  | Sprint 2 |

| H8  | Opus     | fps\_actual NaN/negatif → MetricState cast UB       | pipeline.cpp + metrics.rs| Yuksek  | Sprint 2 |

| H9  | Kimi     | build.rs ffi.rs'i okumadigi icin RjAction assert eksik | build.rs            | Orta    | Sprint 3 |

| H10 | MiniMax  | Drainer sadece CpuUsage gonderiyor                 | ffi.rs                   | Orta    | Sprint 3 |

| H11 | GLM      | SIMULTANEOUS\_USE\_BIT gereksiz — per-slot cmd buffer| copy\_optimizer.cpp       | Orta    | Sprint 3 |

| H12 | Opus     | compute\_cpu\_percent wall clock NTP jumpa karsi duyarli | wasapi\_capture.cpp  | Dusuk   | Sprint 4 |

| H13 | Opus     | action\_processor thread bos kuyrugu poll ediyor    | pipeline.cpp             | Dusuk   | Sprint 4 |

| H14 | Opus     | SEH filtresi cok genis — stack overflow maskeleniyor| wasapi+srt+pipeline     | Dusuk   | Sprint 4 |

| H15 | Opus     | gl\_sync\_sem\_pool\_ cift — fallback tehlikeli        | copy\_optimizer.cpp       | Yuksek  | Sprint 5 |

| H16 | Kimi+MM  | HealingMonitor ticker starvation (biased select)   | healing.rs               | Yuksek  | Sprint 5 |

| H17 | Kimi     | execute\_copy basarisiz submit sonrasi deadlock     | copy\_optimizer.cpp       | Kritik  | Sprint 5 |

| H18 | Kimi     | WasapiCapture non-atomic members — data race       | wasapi\_capture.h         | Yuksek  | Sprint 5 |

| H19 | Kimi     | GL texture filter eksik → incomplete → siyah ekran | preview\_widget.cpp       | Yuksek  | Sprint 5 |

| H20 | GLM      | VulkanInitializer static destroy order (Zig sonrasi)| vulkan\_initializer.cpp  | Orta    | Sprint 5 |



\---



\## Sprint 1 — Kritik Sync ve Race



\---



\### H1 — Tek Command Buffer SIMULTANEOUS\_USE + Reset



\*\*Kaynak:\*\* Opus 4.8 (C-2), GLM-5.2 (1.2)



\*\*Sorun:\*\*

```cpp

// copy\_optimizer.cpp — tek command\_buffer\_ her frame reset edilip yeniden kullaniliyor

// VK\_COMMAND\_BUFFER\_USAGE\_SIMULTANEOUS\_USE\_BIT ile reset = spec ihlali

// VUID-vkResetCommandBuffer-commandBuffer-00045

```

`SIMULTANEOUS\_USE` birden fazla pending submit icin — ama timeline wait

bir sonraki execute\_copy'de yapiliyor, ayni frame'de degil.



\*\*Cozum:\*\*

Per-slot command buffer pool ekle:

```cpp

// copy\_optimizer.h:

VkCommandBuffer command\_buffers\_\[POOL\_SIZE];



// execute\_copy() icinde:

VkCommandBuffer cmd = command\_buffers\_\[slot];

// O slot'un timeline degerini bekle:

// (submit oncesinde zaten yapiliyor — sadece cmd buffer'i slot'a bagla)

vkResetCommandBuffer(cmd, 0);

vkBeginCommandBuffer(cmd, \&begin\_info\_without\_simultaneous\_use);

```

`VK\_COMMAND\_BUFFER\_USAGE\_SIMULTANEOUS\_USE\_BIT` kaldir.



\---



\### H2 — Keyed Mutex Yanlis VkDeviceMemory



\*\*Kaynak:\*\* Opus 4.8 (V-1)



\*\*Sorun:\*\*

```cpp

// execute\_copy() icinde:

km\_memory\_ = bridge\_->get\_staging\_memory\_for\_image(staging\_vk);

// staging\_vk bridge pool'undan geliyor — shared\_texture\_'dan degil

// Keyed mutex shared\_texture\_'a ait — farkli memory!

// VkWin32KeyedMutexAcquireReleaseInfoKHR yanlis memory'yi koruyor

// Sonuc: D3D11-Vulkan sync yok, torn frames

```



\*\*Cozum:\*\*

Keyed mutex icin dogru memory path:

```cpp

// execute\_copy() icinde:

// shared\_texture\_'dan import edilen VkDeviceMemory kullan:

km\_memory\_ = bridge\_->get\_shared\_texture\_memory();

// Yeni getter gerekiyor: ExternalMemoryBridge'de

// shared\_texture\_'dan import edilen VkDeviceMemory dondursun

```



\---



\### H3 — AMD Path Cross-API Sync Tamamen Yok



\*\*Kaynak:\*\* Opus 4.8 (V-2)



\*\*Sorun:\*\*

AMD'de `use\_keyed\_mutex\_=false` — keyed mutex yok.

`VK\_QUEUE\_FAMILY\_EXTERNAL` acquire barrier var ama

D3D11 yazisinin tamamlandigini garanti eden hicbir primitive yok.

`oldLayout=UNDEFINED` ile discard edilen image D3D11 yazisini kaybedebilir.



\*\*Cozum (minimal):\*\*

D3D11 Flush + CPU fence ile garantile:

```cpp

// capture\_dxgi.cpp capture\_next() — keyed mutex yoksa:

if (!use\_keyed\_mutex\_) {

&#x20;   // D3D11 komutlarinin bitmesini garanti et

&#x20;   display\_ctx\_->d3d\_context()->Flush();

&#x20;   // GPU query ile tamamlanmayi bekle (mevcut copy\_fence\_ benzeri)

}

```

Veya ROADMAP'e "AMD path unsafe" olarak belgele, production'da keyed mutex zorunlu yap.



\---



\### H4 — slot\_gl\_signaled\_ Non-Atomic Cross-Thread



\*\*Kaynak:\*\* Opus 4.8 (V-4)



\*\*Sorun:\*\*

```cpp

// copy\_optimizer.h:

std::array<bool, 3> slot\_gl\_signaled\_{false, false, false};

uint32\_t last\_used\_slot\_ = 0;

```

Frame thread yazar (`execute\_copy`), GL thread okur (`paintGL`).

`bool` ve `uint32\_t` — atomic degil — torn read mumkun.



\*\*Cozum:\*\*

```cpp

// copy\_optimizer.h:

std::array<std::atomic<bool>, 3> slot\_gl\_signaled\_{};

std::atomic<uint32\_t> last\_used\_slot\_{0};



// is\_slot\_signaled():

return slot\_gl\_signaled\_\[slot].load(std::memory\_order\_acquire);



// clear\_gl\_signal():

slot\_gl\_signaled\_\[slot].store(false, std::memory\_order\_release);



// execute\_copy() basarili submit sonrasi:

slot\_gl\_signaled\_\[slot].store(true, std::memory\_order\_release);

last\_used\_slot\_.store(slot, std::memory\_order\_release);



// next\_slot() ve last\_used\_slot():

return last\_used\_slot\_.load(std::memory\_order\_acquire);

```



\---



\## Sprint 2 — Yuksek Oncelikli Duzeltmeler



\---



\### H5 — shared\_handle\_ CloseHandle Eksik



\*\*Kaynak:\*\* GLM-5.2 (2.3)



\*\*Sorun:\*\*

```cpp

// gpu\_resource\_manager.cpp:

HANDLE shared\_handle\_ = nullptr;

// create\_cross\_adapter\_shared(): GetSharedHandle(\&shared\_handle\_)

// shutdown(): shared\_handle\_ = nullptr; // CloseHandle YOK!

```

Legacy shared handle (NT handle degil) CloseHandle gerektiriyor.



\*\*Cozum:\*\*

```cpp

// shutdown() icinde:

if (shared\_handle\_ \&\& shared\_handle\_ != INVALID\_HANDLE\_VALUE) {

&#x20;   CloseHandle(shared\_handle\_);

&#x20;   shared\_handle\_ = nullptr;

}

```



\---



\### H6 — Pipeline::shutdown() Thread-Safe Degil



\*\*Kaynak:\*\* GLM-5.2 (1.4)



\*\*Sorun:\*\*

Iki thread ayni anda `Pipeline::shutdown()` cagirabilir.

`std::once\_flag` yok — double-shutdown race.



\*\*Cozum:\*\*

```cpp

// pipeline.h:

std::once\_flag shutdown\_once\_;



// pipeline.cpp shutdown():

std::call\_once(shutdown\_once\_, \[this]() {

&#x20;   // mevcut shutdown kodu

});

```



\---



\### H7 — BGRA/RGBA Channel Swap Preview'da



\*\*Kaynak:\*\* Opus 4.8 (V-3)



\*\*Sorun:\*\*

Vulkan target: `VK\_FORMAT\_B8G8R8A8\_UNORM` (BGRA)

GL texture: `GL\_RGBA8` ile import ediliyor

`vkCmdBlitImage` channel swizzle yapmaz

Sonuc: preview'da kirmizi/mavi kanallar yer degistirilmis



\*\*Cozum — en kolay:\*\*

Fragment shader'da swizzle ekle:

```glsl

// preview.frag:

fragColor = vec4(texture(tex, uv).bgra); // BGRA → RGBA

```

Veya GL texture'i `GL\_BGRA` internal format ile olustur.



\---



\### H8 — fps\_actual NaN/Negatif Cast



\*\*Kaynak:\*\* Opus 4.8 (C-6)



\*\*Sorun:\*\*

```cpp

// pipeline.cpp CpuMeter:

m.fps\_actual = float(qpc\_freq) / float(frame\_start - last\_frame\_ticks);

// frame\_start - last\_frame\_ticks = 0 veya negatif olabilir ilk frame'de

// NaN veya inf → Rust tarafinda u32 cast = 0 (defined ama yanlis)

```



\*\*Cozum:\*\*

```cpp

auto delta = frame\_start - last\_frame\_ticks;

m.fps\_actual = (delta > 0)

&#x20;   ? std::clamp(float(qpc\_freq) / float(delta), 0.0f, 240.0f)

&#x20;   : 0.0f;

```



Rust tarafinda da dogrulama:

```rust

// metrics.rs update():

let fps = sample.fps\_actual;

if fps.is\_finite() \&\& fps >= 0.0 {

&#x20;   self.fps = fps;

}

```



\---



\## Sprint 3 — Orta Oncelik



\---



\### H9 — build.rs ffi.rs'i Okumadigi icin RjAction Assert Eksik



\*\*Kaynak:\*\* Kimi K2.7



\*\*Sorun:\*\*

```rust

// build.rs check\_abi\_sizes():

// parse\_rust\_size\_asserts("src/orchestrator/src/metrics.rs")

// metrics.rs'de sadece MetricSample asserts var

// RjAction ve RjCommand asserts ffi.rs'de — hic okunmuyor!

```

ABI check RjAction/RjCommand icin kordur.



\*\*Cozum:\*\*

```rust

// build.rs:

// metrics.rs + ffi.rs ikisini de oku:

let rust\_sizes\_metrics = parse\_rust\_size\_asserts(

&#x20;   "src/orchestrator/src/metrics.rs")?;

let rust\_sizes\_ffi = parse\_rust\_size\_asserts(

&#x20;   "src/orchestrator/src/ffi.rs")?;

// Her ikisini birlestir:

rust\_sizes.extend(rust\_sizes\_ffi);

```



\---



\### H10 — Drainer Sadece CpuUsage Gonderiyor



\*\*Kaynak:\*\* MiniMax M3



\*\*Sorun:\*\*

```rust

// ffi.rs drainer loop:

bus\_system.send(SystemEvent::CpuUsage { ratio: sample.cpu\_percent / 100.0 });

// GpuUsage, MemUsage, DiskWarning hic gonderilmiyor

// HealingMonitor GpuUsage>=0.98'de ReduceBitrate tetikliyor

// Ama GpuUsage hic gelmedigi icin bu dal hic ateslenmiyor

```



\*\*Cozum:\*\*

```rust

// ffi.rs drainer:

bus\_system.send(SystemEvent::CpuUsage {

&#x20;   ratio: sample.cpu\_percent / 100.0

});

// GPU metric ekle (simdilik cpu'dan tahmin veya 0):

if sample.gpu\_load\_pct > 0 {

&#x20;   let \_ = bus\_system.send(SystemEvent::GpuUsage {

&#x20;       ratio: sample.gpu\_load\_pct as f32 / 100.0

&#x20;   });

}

```



\---



\### H11 — SIMULTANEOUS\_USE\_BIT Gereksiz



\*\*Kaynak:\*\* GLM-5.2 (1.2) — H1 ile birlikte cozulebilir



\*\*Sorun:\*\*

H1 duzeltmesi (per-slot command buffer) yapilinca

`VK\_COMMAND\_BUFFER\_USAGE\_SIMULTANEOUS\_USE\_BIT` tamamen gereksiz hale gelir.



\*\*Cozum:\*\* H1 ile birlikte — begin\_info'dan kaldir.



\---



\## Sprint 4 — Dusuk Oncelik



\---



\### H12 — compute\_cpu\_percent NTP Jump



\*\*Kaynak:\*\* Opus 4.8 (C-5)



\*\*Sorun:\*\*

`GetSystemTimeAsFileTime` NTP jump'lardan etkileniyor.

Geri giden zaman → `d\_wall` unsigned underflow → garbage CPU%.



\*\*Cozum:\*\*

```cpp

// wasapi\_capture.cpp compute\_cpu\_percent():

// GetSystemTimeAsFileTime yerine:

LARGE\_INTEGER qpc;

QueryPerformanceCounter(\&qpc);

// wall reference olarak QPC kullan

```



\---



\### H13 — action\_processor Bos Kuyrugu Poll Ediyor



\*\*Kaynak:\*\* Opus 4.8 (C-4/P-2)



\*\*Sorun:\*\*

`enqueue\_action()` hicbir yerden cagrilmiyor.

Thread 5ms'de bir bos kuyrugu poll ediyor — wasted wakeup, laptop battery drain.



\*\*Cozum (minimal):\*\*

Sleep'i 100ms'e cikart:

```cpp

// pipeline.cpp action\_processor\_main():

QThread::msleep(100); // 5 → 100ms

```

Veya tamamen kaldir — C4 ile birlikte degerlendirilebilir.



\---



\### H14 — SEH Filtresi Cok Genis



\*\*Kaynak:\*\* Opus 4.8 (S-2)



\*\*Sorun:\*\*

`EXCEPTION\_EXECUTE\_HANDLER` tum exception'lari yakaliyor.

`STATUS\_STACK\_OVERFLOW` yakalanirsa guard page reset edilmiyor.



\*\*Cozum:\*\*

```cpp

// seh\_filter():

LONG seh\_filter(DWORD code) {

&#x20;   // Stack overflow ve critical exception'lari gecir:

&#x20;   if (code == STATUS\_STACK\_OVERFLOW ||

&#x20;       code == EXCEPTION\_BREAKPOINT) {

&#x20;       return EXCEPTION\_CONTINUE\_SEARCH; // gecir

&#x20;   }

&#x20;   return EXCEPTION\_EXECUTE\_HANDLER; // yakala

}

```



\---



\## Sprint 5 — Ek Bulgular (Karsilastirma Analizinden)



\---



\### H15 — gl\_sync\_sem\_pool\_ Cift Semaphore Tehlikesi



\*\*Kaynak:\*\* Opus 4.8 (M-4)



\*\*Sorun:\*\*

```cpp

// GpuCopyOptimizer'da kendi gl\_sync\_sem\_pool\_\[3] var

// ExternalMemoryBridge'de de ayri gl\_sync\_sem\_pool\_\[3] var (GL'e export edilen)

// execute\_copy bridge semaphore'u kullaniyor ama fallback olarak kendi pool'una duser

// GL tarafinda sadece bridge semaphore'lari import edilmis

// Fallback tetiklenirse GL hicbir zaman beklemez → GPU race

```



\*\*Cozum:\*\*

```cpp

// copy\_optimizer.h'den gl\_sync\_sem\_pool\_\[] kaldir

// Her zaman bridge'den gelen semaphore'u kullan:

// execute\_copy(..., VkSemaphore gl\_sync\_sem) — caller saglamali

// Fallback yolu tamamen kaldir

```



\---



\### H16 — HealingMonitor Ticker Starvation



\*\*Kaynak:\*\* Kimi K2.7 + MiniMax M3



\*\*Sorun:\*\*

```rust

// healing.rs run() icinde:

tokio::select! { biased;

&#x20;   result = self.system\_rx.recv() => { ... }

&#x20;   result = self.media\_rx.recv() => { ... }

&#x20;   \_ = ticker.tick() => { ... } // STARVED!

}

// biased: system\_rx surekli mesaj gelirse ticker hic calismaz

// evaluate\_adaptive(), evaluate\_predictive() hic tetiklenmez

// Cooldown recovery, adaptif esik guncelleme durur

```



\*\*Cozum:\*\*

```rust

// Ticker'i ayri task'a tasiy veya fair select kullan:

// Secenek A — biased kaldirilir (fair round-robin):

tokio::select! {

&#x20;   result = self.system\_rx.recv() => { ... }

&#x20;   result = self.media\_rx.recv() => { ... }

&#x20;   \_ = ticker.tick() => { ... }

}



// Secenek B — ticker ayri spawn:

tokio::spawn(async move {

&#x20;   let mut ticker = tokio::time::interval(...);

&#x20;   loop {

&#x20;       ticker.tick().await;

&#x20;       // periodic eval

&#x20;   }

});

```



\---



\### H17 — execute\_copy Basarisiz Submit Sonrasi Deadlock



\*\*Kaynak:\*\* Kimi K2.7



\*\*Sorun:\*\*

```cpp

// copy\_optimizer.cpp execute\_copy():

// F3 ile state corruption duzeltildi ama deadlock durumu kaldi:

//

// submit basarisiz olursa:

//   signal\_value\_for\_submit\_ = timeline\_counter\_ (arttirildi)

//   ama VkQueueSubmit basarisiz — GPU bu degeri hicbir zaman sinyallemez

//

// Bir sonraki execute\_copy():

//   vkWaitSemaphores(signal\_value\_for\_submit\_) // SONSUZA BEKLER

```



\*\*Cozum:\*\*

```cpp

// execute\_copy() basarisiz submit sonrasi:

VkResult result = vkQueueSubmit(...);

if (result != VK\_SUCCESS) {

&#x20;   // timeline\_counter\_'i geri al — submit olmadi

&#x20;   --timeline\_counter\_;

&#x20;   signal\_value\_for\_submit\_ = timeline\_counter\_; // onceki gecerli degere don

&#x20;   fprintf(stderr, "\[CopyOptimizer] submit failed: %d\\n", result);

&#x20;   return false;

}

// Basarili — simdi guncelle

signal\_value\_for\_submit\_ = timeline\_counter\_;

```



\---



\### H18 — WasapiCapture Non-Atomic Members



\*\*Kaynak:\*\* Kimi K2.7



\*\*Sorun:\*\*

```cpp

// wasapi\_capture.h:

int actual\_channels\_ = 0;      // non-atomic

int actual\_sample\_rate\_ = 0;   // non-atomic

int actual\_bits\_ = 0;          // non-atomic

bool actual\_is\_float\_ = false; // non-atomic

uint32\_t buffer\_frames\_ = 0;   // non-atomic



// capture thread yazıyor (open\_device\_and\_init\_engine)

// main thread okuyor (get\_channels(), get\_sample\_rate() vb.)

// Data race — UB

```



\*\*Cozum:\*\*

```cpp

// wasapi\_capture.h:

std::atomic<int> actual\_channels\_{0};

std::atomic<int> actual\_sample\_rate\_{0};

std::atomic<int> actual\_bits\_{0};

std::atomic<bool> actual\_is\_float\_{false};

std::atomic<uint32\_t> buffer\_frames\_{0};

```



\---



\### H19 — GL Texture Min/Mag Filter Eksik



\*\*Kaynak:\*\* Kimi K2.7



\*\*Sorun:\*\*

```cpp

// preview\_widget.cpp GL interop texture olusturulurken:

glBindTexture(GL\_TEXTURE\_2D, gl\_interop\_textures\_\[i]);

glTexStorageMem2DEXT(...);

// glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_MIN\_FILTER, GL\_LINEAR) YOK!

// glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_MAG\_FILTER, GL\_LINEAR) YOK!

// Default min filter: GL\_NEAREST\_MIPMAP\_LINEAR — mipmap olmayan texture = incomplete

// Incomplete texture = siyah ekran render eder

```



\*\*Cozum:\*\*

```cpp

// GL interop texture olusturulduktan sonra:

glBindTexture(GL\_TEXTURE\_2D, gl\_interop\_textures\_\[i]);

glTexStorageMem2DEXT(...);

glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_MIN\_FILTER, GL\_LINEAR);

glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_MAG\_FILTER, GL\_LINEAR);

glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_WRAP\_S, GL\_CLAMP\_TO\_EDGE);

glTexParameteri(GL\_TEXTURE\_2D, GL\_TEXTURE\_WRAP\_T, GL\_CLAMP\_TO\_EDGE);

```



\---



\### H20 — VulkanInitializer Static Destroy Order (Zig Sonrasi Risk)



\*\*Kaynak:\*\* GLM-5.2 (Finding 2.2)



\*\*Sorun:\*\*

Zig migrasyonu oncesinde C++ singleton destructor'i vkDestroyDevice yapıyordu.

Simdi `vulkan\_init\_shutdown()` Zig tarafinda cagiriliyor.

C++ static destruction order TU'lar arasi belirsiz:

```cpp

// vulkan\_initializer.cpp:

VulkanInitializer\* VulkanInitializer::get() {

&#x20;   static VulkanInitializer instance; // static local

&#x20;   return \&instance;

}

// \~VulkanInitializer() → vulkan\_init\_shutdown() → vkDestroyDevice (Zig)

//

// Eger Pipeline baska bir static tarafindan tutuluyorsa

// ve o static once yikilirsa:

// Pipeline::\~Pipeline() → ExternalMemoryBridge::shutdown() → vkDestroyImage(device\_)

// AMA device zaten Zig tarafinda yok edilmis olabilir → UAF

```



\*\*Cozum:\*\*

```cpp

// Pipeline'in statik omur tasimayacagini dokumante et

// Veya VulkanInitializer singleton'ini Pipeline destructor'indan once

// manuel olarak shutdown et:

// main.cpp'de:

//   pipeline.reset(); // once pipeline

//   VulkanInitializer::get()->shutdown(); // sonra vulkan

```



\---



\## Dogrulama



```cmd

cd C:\\reji-studio

python scripts/build.py --clean

build\\src\\ui\\reji\_app.exe --headless --frames 5 2> err.log

type err.log | findstr "VUID\\|ERROR\\|race\\|atomic"

cargo test --manifest-path src/orchestrator/Cargo.toml

just abi-check

```



\---



\## Takip



\- \[ ] Sprint 1 tamamlandi (H1-H4)

\- \[ ] Sprint 2 tamamlandi (H5-H8)

\- \[ ] Sprint 3 tamamlandi (H9-H11)

\- \[ ] Sprint 4 tamamlandi (H12-H14)

\- \[ ] Sprint 5 tamamlandi (H15-H20)

\- \[ ] Sekizinci tarama yapildi



\---



\## Model Performans Notu



```

Opus 4.8   $1.18 — En derin, V-1(keyed mutex memory) + H7(BGRA swap) + H15(cift sem)

&#x20;                   Fable 5'in \~%80'i kalitesinde

GLM-5.2    $0.13 — Surpriz kalite, H20(Zig static order) benzersiz

Kimi K2.7  $0.32 — H17(deadlock), H18(atomic), H19(GL filter) benzersiz

MiniMax    $0.07 — drainer bug, yuzeysel

Toplam     $1.70 — Fable 5 tek taramasinin \~%62'si maliyetle



Ideal kombinasyon: Opus 4.8 + GLM-5.2 = $1.31

&#x20;                  Fable 5'in %80+ kalitesi, %48 maliyeti

```



\---



\*Bu belge C:\\reji-studio\\docs\\FABLE5\_BUG\_PLAN\_V7.md olarak kaydedilmeli.\*



