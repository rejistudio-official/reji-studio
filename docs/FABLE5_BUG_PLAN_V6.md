\# Reji Studio — Cok Modelli Tarama Duzeltme Plani (V6)



\*\*Tarih:\*\* 15.06.2026

\*\*Kaynaklar:\*\*

\- DeepSeek v4 Pro ($0.12) — GL fence slot hazard, bitrate stale config

\- Qwen3.7-Max ($0.19)  — AcquireSync WAIT\_TIMEOUT, GL memory size

\- Kimi K2.7-Code ($0.17) — Uninit members, COLOR\_ATTACHMENT flag, release barrier



\*\*Onceki:\*\* FABLE5\_BUG\_PLAN\_V5.md (F1-F18 tamamlandi)



\---



\## Oncelik Matrisi



| #   | Kaynak        | Sorun                                             | Dosya                          | Oncelik | Sprint   |

|-----|---------------|---------------------------------------------------|--------------------------------|---------|----------|

| G1  | Qwen3.7-Max   | AcquireSync WAIT\_TIMEOUT FAILED() ile kontrol     | capture\_dxgi.cpp               | Kritik  | Sprint 1 |

| G2  | DeepSeek      | GL fence yanlis slot'u bekliyor — GPU hazard      | preview\_widget.cpp             | Kritik  | Sprint 1 |

| G3  | DeepSeek      | Bitrate reduction stale cfg degeri kullanıyor     | pipeline.cpp                   | Kritik  | Sprint 1 |

| G4  | DeepSeek      | VkWin32KeyedMutex pNext chain yanlis siralama     | copy\_optimizer.cpp             | Kritik  | Sprint 1 |

| G5  | Kimi K2.7     | ExternalMemoryBridge uninit members               | external\_memory\_bridge.h       | Kritik  | Sprint 1 |

| G6  | Kimi+Qwen     | GL memory import approximate size — spec ihlali   | preview\_widget.cpp             | Yuksek  | Sprint 2 |

| G7  | Kimi K2.7     | COLOR\_ATTACHMENT usage flag gereksiz              | external\_memory\_bridge.cpp     | Yuksek  | Sprint 2 |

| G8  | Kimi K2.7     | Release barrier GENERAL yerine UNDEFINED          | copy\_optimizer.cpp             | Yuksek  | Sprint 2 |

| G9  | DeepSeek      | ExternalMemoryBridge double-shutdown race         | external\_memory\_bridge.cpp     | Orta    | Sprint 2 |

| G10 | DeepSeek      | MetricsCollector deque hot-path allocation        | metrics\_collector.cpp          | Orta    | Sprint 3 |

| G11 | Kimi K2.7     | \_reserved field isim degisikligi → source\_id      | ffi\_bridge.h + metrics.rs      | Orta    | Sprint 3 |

| G12 | Qwen3.7-Max   | Unreachable dead code acquire() ikinci sifir blok | capture\_dxgi.cpp               | Dusuk   | Sprint 3 |



\---



\## Sprint 1 — Kritik Buglar



\---



\### G1 — AcquireSync WAIT\_TIMEOUT FAILED() ile Kontrol Edilmiyor



\*\*Kaynak:\*\* Qwen3.7-Max



\*\*Sorun:\*\*

```cpp

HRESULT hr = keyed\_mutex\_shared\_->AcquireSync(0, 16);

if (FAILED(hr)) return false; // YANLIS!

```

`WAIT\_TIMEOUT = 0x00000102` — severity bit 0, yani `FAILED()` false doner.

Mutex acquire basarisiz olsa bile `CopyResource` devam eder.

Sonuc: D3D11/Vulkan race condition → corrupted frames, TDR.



\*\*Cozum:\*\*

```cpp

HRESULT hr = keyed\_mutex\_shared\_->AcquireSync(0, 16);

if (hr != S\_OK) { // WAIT\_TIMEOUT ve WAIT\_ABANDONED da yakalar

&#x20;   fprintf(stderr, "\[Capture] KeyedMutex timeout/fail: 0x%08X\\n", hr);

&#x20;   return false;

}

```



\---



\### G2 — GL Fence Yanlis Slot Bekliyor



\*\*Kaynak:\*\* DeepSeek v4 Pro



\*\*Sorun:\*\*

`paintGL()` icinde:

```cpp

// last\_used\_slot() = ONCEKI frame'in slotu

uint32\_t prev\_slot = copy\_optimizer\_->last\_used\_slot();

glClientWaitSync(gl\_draw\_fences\_\[prev\_slot], ...); // onceki slotu bekle



// execute\_copy bir SONRAKI slotu kullanir: (frame\_counter\_+1) % 3

copy\_optimizer\_->execute\_copy(...); // farkli slot!

```

Fence beklenen slot ile execute\_copy'nin kullandigi slot farkli olabilir.

GPU ayni texture'a hem yazar hem okunur → hazard.



\*\*Cozum:\*\*

```cpp

// execute\_copy oncesi hangi slotu kullanacagini sor:

uint32\_t next\_slot = copy\_optimizer\_->next\_slot();

// O slot'un fence'ini bekle:

if (gl\_draw\_fences\_\[next\_slot]) {

&#x20;   glClientWaitSync(gl\_draw\_fences\_\[next\_slot],

&#x20;                    GL\_SYNC\_FLUSH\_COMMANDS\_BIT, 1'000'000);

}

copy\_optimizer\_->execute\_copy(...);

```



```cpp

// copy\_optimizer.h'e ekle:

uint32\_t next\_slot() const {

&#x20;   return (frame\_counter\_ + 1) % POOL\_SIZE;

}

```



\---



\### G3 — Bitrate Reduction Stale Config Kullanıyor



\*\*Kaynak:\*\* DeepSeek v4 Pro



\*\*Sorun:\*\*

```cpp

// pipeline.cpp apply\_action() RJ\_ACTION\_BITRATE\_REDUCE:

uint32\_t new\_bitrate = impl\_->cfg.bitrate\_kbps \* 0.85f;

//                     ^^^^^^^^^^^^^^^^^^^^^^^^^^

//                     cfg.bitrate\_kbps hic guncellenmez!

//                     Healing her reduce'ta orijinale gore hesaplar

//                     Ornek: 6000 → 5100 → 5100\*0.85=4335 OLMALI

//                     Ama: 6000 → 5100 → 6000\*0.85=5100 OLUR (ayni!)

```

Adaptive bitrate hicbir zaman gercekten azalmaz.



\*\*Cozum:\*\*

```cpp

// apply\_action() RJ\_ACTION\_BITRATE\_REDUCE:

// cfg yerine live bitrate kullan:

uint32\_t current = impl\_->bitrate\_kbps; // live deger

uint32\_t new\_bitrate = static\_cast<uint32\_t>(current \* 0.85f);

new\_bitrate = std::max(new\_bitrate, impl\_->cfg.min\_bitrate\_kbps);

apply\_frame\_cmd({RJ\_CMD\_SET\_BITRATE, new\_bitrate});



// RJ\_ACTION\_BITRATE\_RECOVER icin de kontrol et — benzer sorun olabilir

```



\---



\### G4 — VkWin32KeyedMutex pNext Chain Yanlis Siralama



\*\*Kaynak:\*\* DeepSeek v4 Pro



\*\*Sorun:\*\*

```cpp

// Mevcut (yanlis):

keyed\_mutex\_info\_.pNext = nullptr;

timeline\_submit\_info\_.pNext = \&keyed\_mutex\_info\_; // timeline -> keyed

submit\_info\_.pNext = \&timeline\_submit\_info\_;       // submit -> timeline -> keyed

```

Vulkan spec: `VkWin32KeyedMutexAcquireReleaseInfoKHR` yalnizca

`VkSubmitInfo::pNext` zincirinde olmali — nested chain degil.



\*\*Cozum:\*\*

```cpp

// Duzeltilmis flat chain:

timeline\_submit\_info\_.pNext = nullptr;

keyed\_mutex\_info\_.pNext = \&timeline\_submit\_info\_; // keyed -> timeline

submit\_info\_.pNext = \&keyed\_mutex\_info\_;           // submit -> keyed -> timeline

```



\---



\### G5 — ExternalMemoryBridge Uninit Members



\*\*Kaynak:\*\* Kimi K2.7-Code



\*\*Sorun:\*\*

```cpp

// external\_memory\_bridge.h — default initializer yok:

uint32\_t pool\_index\_;         // indeterminate value!

HANDLE cached\_d3d11\_handle\_;  // garbage value!

```

`get\_frame\_images()` ilk cagirildiginda:

\- `pool\_index\_` rastgele deger → `image\_pool\_` out-of-bounds

\- `cached\_d3d11\_handle\_` garbage → yanlis handle-reuse karari



\*\*Cozum:\*\*

```cpp

// external\_memory\_bridge.h:

uint32\_t pool\_index\_{0};

HANDLE   cached\_d3d11\_handle\_{nullptr};

```



\---



\## Sprint 2 — Yuksek Oncelikli Duzeltmeler



\---



\### G6 — GL Memory Import Approximate Size



\*\*Kaynak:\*\* Kimi K2.7-Code + Qwen3.7-Max



\*\*Sorun:\*\*

```cpp

// preview\_widget.cpp:

pfn\_ImportMemoryWin32Handle\_(

&#x20;   gl\_memory\_objects\_\[pool\_idx],

&#x20;   w \* h \* 4,  // YANLIS — driver padding/alignment icermez

&#x20;   GL\_HANDLE\_TYPE\_OPAQUE\_WIN32\_EXT,

&#x20;   handle

);

```

`GL\_EXT\_memory\_object\_win32` spec: boyut tam olarak

`VkMemoryRequirements::size` olmali.

`w\*h\*4` eksik olursa: GL\_INVALID\_VALUE veya sessiz GPU page fault.



\*\*Cozum:\*\*

```cpp

// ExternalMemoryBridge'e per-slot size ekle:

// external\_memory\_bridge.h:

std::array<VkDeviceSize, POOL\_SIZE> gl\_target\_sizes\_{};



// initialize\_gl\_target\_pool() icinde:

VkMemoryRequirements mem\_reqs;

vkGetImageMemoryRequirements(device\_, gl\_target\_images\_\[i], \&mem\_reqs);

gl\_target\_sizes\_\[i] = mem\_reqs.size;



// Getter ekle:

VkDeviceSize gl\_target\_size(uint32\_t slot) const {

&#x20;   return slot < POOL\_SIZE ? gl\_target\_sizes\_\[slot] : 0;

}



// preview\_widget.cpp:

VkDeviceSize exact\_size = bridge\_->gl\_target\_size(pool\_idx);

pfn\_ImportMemoryWin32Handle\_(

&#x20;   gl\_memory\_objects\_\[pool\_idx],

&#x20;   exact\_size,  // tam deger

&#x20;   GL\_HANDLE\_TYPE\_OPAQUE\_WIN32\_EXT,

&#x20;   handle

);

```



\---



\### G7 — COLOR\_ATTACHMENT Usage Flag Gereksiz



\*\*Kaynak:\*\* Kimi K2.7-Code



\*\*Sorun:\*\*

```cpp

// external\_memory\_bridge.cpp create\_vulkan\_image\_from\_d3d11():

image\_info.usage = VK\_IMAGE\_USAGE\_TRANSFER\_SRC\_BIT

&#x20;                | VK\_IMAGE\_USAGE\_TRANSFER\_DST\_BIT

&#x20;                | VK\_IMAGE\_USAGE\_COLOR\_ATTACHMENT\_BIT; // GEREKSIZ

```

Import edilen D3D11 texture sadece transfer source olarak kullanılıyor.

`COLOR\_ATTACHMENT\_BIT` driver'ın tiling/compression secimini kisitlar,

bazi D3D11 import senaryolarinda hata uretebilir.



\*\*Cozum:\*\*

```cpp

image\_info.usage = VK\_IMAGE\_USAGE\_TRANSFER\_SRC\_BIT

&#x20;                | VK\_IMAGE\_USAGE\_TRANSFER\_DST\_BIT;

// COLOR\_ATTACHMENT\_BIT kaldirildi

```



\---



\### G8 — Release Barrier GENERAL Yerine UNDEFINED



\*\*Kaynak:\*\* Kimi K2.7-Code



\*\*Sorun:\*\*

```cpp

// copy\_optimizer.cpp execute\_copy() — blit sonrasi release:

barrier\_staging\_release.newLayout = VK\_IMAGE\_LAYOUT\_GENERAL;

```

D3D11 bir sonraki frame'de texture'i tamamen yeniden yazacak.

GENERAL layout transition gereksiz GPU cycle harcar,

driver compression'i bozabilir.



\*\*Cozum:\*\*

```cpp

// D3D11 icerigi yeniden yazacak — UNDEFINED daha dogru:

barrier\_staging\_release.newLayout = VK\_IMAGE\_LAYOUT\_UNDEFINED;

// Bu driver'a "icerigi at, layout gecisini atla" der

```



\---



\### G9 — ExternalMemoryBridge Double-Shutdown Race



\*\*Kaynak:\*\* DeepSeek v4 Pro



\*\*Sorun:\*\*

`shutdown()` sonu `device\_ = VK\_NULL\_HANDLE` set ediyor (idempotent gibi).

Ama iki thread esit zamanda girerse `vkDeviceWaitIdle` sirasinda

ikincisi `device\_ != VK\_NULL\_HANDLE` gorup devam edebilir.



\*\*Cozum:\*\*

```cpp

// external\_memory\_bridge.h:

std::atomic<bool> shutdown\_called\_{false};



// external\_memory\_bridge.cpp shutdown():

bool expected = false;

if (!shutdown\_called\_.compare\_exchange\_strong(expected, true)) {

&#x20;   return; // Zaten calistirildi

}

// ... geri kalan shutdown kodu

```



\---



\## Sprint 3 — Orta Oncelik ve Temizlik



\---



\### G10 — MetricsCollector Deque Hot-Path Allocation



\*\*Kaynak:\*\* DeepSeek v4 Pro



\*\*Sorun:\*\*

`calculate\_frame\_drop\_pct()` her frame `deque::push\_back` + `pop\_front`

yapıyor — heap allocation/deallocation 60Hz'de.



\*\*Cozum:\*\*

```cpp

// metrics\_collector.h:

// deque yerine sabit boyutlu ring buffer:

static constexpr size\_t WINDOW = 30;

std::array<uint32\_t, WINDOW> drop\_ring\_{};

std::array<uint32\_t, WINDOW> frame\_ring\_{};

size\_t ring\_head\_ = 0;

// push\_back/pop\_front yerine head++ % WINDOW ile yaz

```



\---



\### G11 — \_reserved Field Isim Degisikligi



\*\*Kaynak:\*\* Kimi K2.7-Code



\*\*Sorun:\*\*

`\_reserved` aslinda `source\_id` olarak kullaniliyor (F14 ile).

Ama isim hala `\_reserved` — yaniltici ve kirilgan.



\*\*Cozum:\*\*

```cpp

// ffi\_bridge.h ve ffi\_auto.h:

// \_reserved → source\_id olarak yeniden adlandir

// 0 = video, 1 = audio

uint8\_t source\_id; // eskiden \_reserved



// sizeof\_check.cpp offsetof guncelle

// metrics.rs'de \_reserved → source\_id

// wasapi\_capture.cpp ve pipeline.cpp guncelle

```



\---



\### G12 — Unreachable Dead Code acquire()



\*\*Kaynak:\*\* Qwen3.7-Max



\*\*Sorun:\*\*

```cpp

// capture\_dxgi.cpp acquire():

if (info.AccumulatedFrames == 0) {

&#x20;   duplication\_->ReleaseFrame();

&#x20;   return false; // erken return

}

// Bu blok hicbir zaman ulasilmaz:

if (info.AccumulatedFrames == 0 \&\& ...) { ... } // DEAD CODE

```

D7 ile ilk blok eklendi ama ikinci blok kalmalic.



\*\*Cozum:\*\*

```cpp

// Ulasılamaz ikinci bloku kaldir

// assert(!frame\_held\_) ekle acquire() basina

```



\---



\## Dogrulama



```cmd

cd C:\\reji-studio

python scripts/build.py --clean

build\\src\\ui\\reji\_app.exe 2> err.log

type err.log | findstr "VUID\\|ERROR\\|TIMEOUT\\|hazard"

cargo test --manifest-path src/orchestrator/Cargo.toml

just abi-check

```



\---



\## Takip



\- \[ ] Sprint 1 tamamlandi (G1-G5)

\- \[ ] Sprint 2 tamamlandi (G6-G9)

\- \[ ] Sprint 3 tamamlandi (G10-G12)

\- \[ ] Fable 5 altinci tarama yapildi



\---



\## Model Performans Notu



```

DeepSeek v4 Pro  $0.12 — G2(fence slot), G3(bitrate), G4(pNext) kritik bulgular

Qwen3.7-Max      $0.19 — G1(WAIT\_TIMEOUT) en onemli bulgu

Kimi K2.7-Code   $0.17 — G5(uninit), G7(flag), G8(layout) temiz bulgular

Toplam harcama:  $0.48 — Fable 5 tek taramasinin 1/6'si

Toplam bulgu:    12 bug (5 kritik, 4 yuksek, 3 orta)

```



\---



\*Bu belge C:\\reji-studio\\docs\\FABLE5\_BUG\_PLAN\_V6.md olarak kaydedilmeli.\*



