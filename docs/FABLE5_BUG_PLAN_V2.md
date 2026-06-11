\# Reji Studio — Fable 5 İkinci Tarama Düzeltme Planı (v2)



\*\*Tarih:\*\* 11.06.2026  

\*\*Kaynak:\*\* Fable 5 ikinci tarama — 60 dosya, 203K token, $2.68  

\*\*Önceki:\*\* FABLE5\_BUG\_PLAN.md (B1-B18 tamamlandı)



\---



\## Öncelik Matrisi



| # | Sorun | Dosya | Öncelik | Etki | Sprint |

|---|---|---|---|---|---|

| C1 | `RjMetricSample` ABI uyumsuzluğu (56B vs 40B) | ffi\_bridge.h + metrics.rs | 🔴 Kritik | Self-healing ölü | Sprint 1 |

| C2 | Validation layer release'te zorunlu | vulkan\_initializer.cpp | 🔴 Kritik | SDK'sız sistemde crash | Sprint 1 |

| C3 | `oldLayout=GENERAL` Vulkan spec ihlali | copy\_optimizer.cpp | 🔴 Kritik | Bozuk görüntü / DEVICE\_LOST | Sprint 1 |

| C4 | `srcStage=ALL\_GRAPHICS` + `TRANSFER\_WRITE` | copy\_optimizer.cpp | 🔴 Kritik | Sync garantisi yok | Sprint 1 |

| C5 | 4 ayrı `rj\_command\_drain` tüketicisi | pipeline.cpp + 3 dosya | 🟠 Yüksek | Komutlar kaybolur | Sprint 2 |

| C6 | NVENC thread-safety ihlali | pipeline.cpp | 🟠 Yüksek | Encode UB | Sprint 2 |

| C7 | Binary semaphore yeniden-sinyal ihlali | copy\_optimizer.cpp + preview\_widget.cpp | 🟠 Yüksek | GL/VK sync bozulma | Sprint 2 |

| C8 | Vulkan LUID eşlemesi yok | vulkan\_initializer.cpp | 🟡 Orta | Yanlış GPU seçimi | Sprint 3 |

| C9 | Same-adapter'da iki ayrı D3D11 device | gpu\_resource\_manager.cpp | 🟡 Orta | CopyResource geçersiz | Sprint 3 |



\---



\## Teknik Borç (Build \& ABI)



| # | Görev | Öncelik | Sprint |

|---|---|---|---|

| T1 | cbindgen kur — Rust → C header otomatik üretimi | 🟠 Yüksek | Sprint 1 |

| T2 | scripts/build.py sadeleştir — 357 → \~30 satır | 🟡 Orta | Sprint 2 |

| T3 | justfile ekle — tek komut arayüzü | 🟡 Orta | Sprint 2 |



\---



\## Sprint 1 — Kritik Vulkan + ABI + cbindgen

\*\*Hedef:\*\* Production blocker'ları gider, ABI güvenliğini otomatize et  

\*\*Tahmini süre:\*\* 2-3 oturum



\---



\### C1 — RjMetricSample ABI Uyumsuzluğu



\*\*Sorun:\*\*  

C++ tarafı 56 baytlık `RjMetricSample` gönderiyor, Rust 40 baytlık `MetricSample` okuyor.  

Offset 32'de C++: `frame\_drop\_pct`, Rust: `magic\_tail` — canary kontrolü her zaman başarısız.  

Tüm metrik örnekleri sessizce atılıyor → self-healing zinciri ölü.



\*\*Çözüm:\*\*

```rust

// metrics.rs — C++ layout ile birebir eşleştir

\#\[repr(C)]

pub struct MetricSample {

&#x20;   pub timestamp\_us: u64,      // +0

&#x20;   pub fps: f32,               // +8

&#x20;   pub bitrate\_kbps: u32,      // +12

&#x20;   pub frame\_drop\_pct: f32,    // +16

&#x20;   pub gpu\_temp\_c: f32,        // +20

&#x20;   pub cpu\_load\_pct: f32,      // +24

&#x20;   pub rtt\_ms: f32,            // +28

&#x20;   pub jitter\_ms: f32,         // +32

&#x20;   pub packet\_loss\_pct: f32,   // +36

&#x20;   pub encode\_time\_ms: f32,    // +40

&#x20;   pub copy\_time\_ms: f32,      // +44

&#x20;   pub queue\_depth: u32,       // +48

&#x20;   pub magic\_tail: u32,        // +52  ← 56 bayt toplam

}

```



\*\*Doğrulama:\*\*

```rust

const \_: () = assert!(core::mem::size\_of::<MetricSample>() == 56);

const \_: () = assert!(core::mem::offset\_of!(MetricSample, magic\_tail) == 52);

```



\---



\### C2 — Validation Layer Release'te Zorunlu



\*\*Sorun:\*\*  

`VK\_LAYER\_KHRONOS\_validation` koşulsuz aktif.  

Son kullanıcı makinesinde Vulkan SDK kurulu değilse `vkCreateInstance` → `VK\_ERROR\_LAYER\_NOT\_PRESENT`.



\*\*Çözüm:\*\*

```cpp

// vulkan\_initializer.cpp create\_instance() içinde:

std::vector<const char\*> layers;



\#ifdef NDEBUG

&#x20;   // Release: validation yok, env flag ile açılabilir

&#x20;   if (getenv("RJ\_ENABLE\_VULKAN\_VALIDATION")) {

&#x20;       layers.push\_back("VK\_LAYER\_KHRONOS\_validation");

&#x20;   }

\#else

&#x20;   // Debug: validation varsa ekle, yoksa devam et

&#x20;   layers.push\_back("VK\_LAYER\_KHRONOS\_validation");

\#endif



// Layer varlığını kontrol et, yoksa listeden çıkar

uint32\_t layer\_count = 0;

vkEnumerateInstanceLayerProperties(\&layer\_count, nullptr);

std::vector<VkLayerProperties> available(layer\_count);

vkEnumerateInstanceLayerProperties(\&layer\_count, available.data());



layers.erase(std::remove\_if(layers.begin(), layers.end(),

&#x20;   \[\&](const char\* name) {

&#x20;       bool found = std::any\_of(available.begin(), available.end(),

&#x20;           \[\&](const VkLayerProperties\& p) {

&#x20;               return strcmp(p.layerName, name) == 0;

&#x20;           });

&#x20;       if (!found) fprintf(stderr, "\[Vulkan] Layer %s yok, atlanıyor\\n", name);

&#x20;       return !found;

&#x20;   }), layers.end());

```



\---



\### C3 — oldLayout=GENERAL Vulkan Spec İhlali



\*\*Sorun:\*\*  

İmajlar `UNDEFINED` ile yaratılıyor ama barrier `GENERAL` varsayıyor.  

Her karede layout tracking yok.



\*\*Çözüm:\*\*  

`CopyOptimizer` içinde per-image layout state ekle:

```cpp

struct ImageState {

&#x20;   VkImageLayout staging\_layout = VK\_IMAGE\_LAYOUT\_UNDEFINED;

&#x20;   VkImageLayout target\_layout  = VK\_IMAGE\_LAYOUT\_UNDEFINED;

};

std::array<ImageState, 3> image\_states\_;



// execute\_copy() içinde:

// Staging: UNDEFINED/TRANSFER\_SRC → TRANSFER\_SRC\_OPTIMAL

barrier\_staging.oldLayout = image\_states\_\[slot].staging\_layout;

barrier\_staging.newLayout = VK\_IMAGE\_LAYOUT\_TRANSFER\_SRC\_OPTIMAL;

image\_states\_\[slot].staging\_layout = VK\_IMAGE\_LAYOUT\_TRANSFER\_SRC\_OPTIMAL;



// Target: UNDEFINED/SHADER\_READ\_ONLY → TRANSFER\_DST\_OPTIMAL

barrier\_target.oldLayout = image\_states\_\[slot].target\_layout;

barrier\_target.newLayout = VK\_IMAGE\_LAYOUT\_TRANSFER\_DST\_OPTIMAL;

// Blit sonrası: TRANSFER\_DST → SHADER\_READ\_ONLY\_OPTIMAL

image\_states\_\[slot].target\_layout = VK\_IMAGE\_LAYOUT\_SHADER\_READ\_ONLY\_OPTIMAL;

```



\---



\### C4 — srcStage=ALL\_GRAPHICS + TRANSFER\_WRITE



\*\*Sorun:\*\*  

`VK\_ACCESS\_TRANSFER\_WRITE\_BIT`, `VK\_PIPELINE\_STAGE\_ALL\_GRAPHICS\_BIT` içinde geçersiz.



\*\*Çözüm:\*\*

```cpp

// barrier\_staging srcStage:

// ESKİ: VK\_PIPELINE\_STAGE\_ALL\_GRAPHICS\_BIT

// YENİ:

barrier\_staging.srcStageMask = VK\_PIPELINE\_STAGE\_TRANSFER\_BIT;

barrier\_staging.srcAccessMask = VK\_ACCESS\_TRANSFER\_WRITE\_BIT;

```



\---



\### T1 — cbindgen Kurulumu



\*\*Hedef:\*\* Rust → C header otomatik üretimi, ABI uyumsuzluklarını derleme zamanında yakala.



\*\*Adımlar:\*\*



1\. `src/orchestrator/cbindgen.toml` oluştur:

```toml

language = "C"

include\_guard = "RJ\_FFI\_AUTO\_H"

autogen\_warning = "/\* OTOMATİK ÜRETİLDİ — elle düzenleme \*/"

tab\_width = 4

documentation = true



\[export]

include = \["RjMetricSample", "RjAction", "RjActionType", "RjFrameStats"]

```



2\. `src/orchestrator/build.rs` oluştur:

```rust

fn main() {

&#x20;   let crate\_dir = std::env::var("CARGO\_MANIFEST\_DIR").unwrap();

&#x20;   cbindgen::Builder::new()

&#x20;       .with\_crate(\&crate\_dir)

&#x20;       .with\_config(

&#x20;           cbindgen::Config::from\_file("cbindgen.toml")

&#x20;               .expect("cbindgen.toml okunamadı")

&#x20;       )

&#x20;       .generate()

&#x20;       .expect("cbindgen başarısız")

&#x20;       .write\_to\_file("../../src/ffi/ffi\_auto.h");

}

```



3\. `src/orchestrator/Cargo.toml`'a ekle:

```toml

\[build-dependencies]

cbindgen = "0.27"

```



4\. `src/ffi/ffi\_bridge.h` içinde manuel struct tanımları yerine:

```cpp

\#include "ffi\_auto.h"  // otomatik üretilen

```



5\. `.gitignore`'a ekle:

```

src/ffi/ffi\_auto.h

```



\---



\## Sprint 2 — Komut Yönlendirme + NVENC + Binary Semaphore + Build Sadeleştirme

\*\*Hedef:\*\* Runtime stabilite + geliştirici deneyimi  

\*\*Tahmini süre:\*\* 2-3 oturum



\---



\### C5 — 4 Ayrı rj\_command\_drain Tüketicisi



\*\*Sorun:\*\*  

`rj\_command\_drain` şu yerlerde çağrılıyor:

\- `pipeline.cpp::run\_frame`

\- `wasapi\_capture.cpp::drain\_commands\_nonblocking`

\- `main\_window.cpp::pollMetrics`

\- `srt\_output.cpp::process\_commands`



`RJ\_CMD\_BITRATE\_SET` hangi tüketici önce drain ederse ona gider.



\*\*Çözüm:\*\*  

Tek tüketici: pipeline frame thread.  

Diğer modüllerde `rj\_command\_drain` çağrısını kaldır, pipeline `apply\_command` fan-out yapsın.



\---



\### C6 — NVENC Thread-Safety İhlali



\*\*Sorun:\*\*  

`action\_processor` thread'i `set\_bitrate/set\_resolution` çağırırken  

frame thread `encode\_frame` çalıştırıyor → UB.



\*\*Çözüm:\*\*  

Aksiyonları frame thread komut kuyruğuna yönlendir:

```cpp

// action\_processor yerine:

frame\_command\_queue\_.push(FrameCommand{CMD\_SET\_BITRATE, action.param1});



// run\_frame başında:

FrameCommand cmd;

while (frame\_command\_queue\_.try\_pop(cmd)) {

&#x20;   apply\_frame\_command(cmd);

}

```



\---



\### C7 — Binary Semaphore Yeniden-Sinyal İhlali



\*\*Sorun:\*\*  

Her submit'te aynı binary `gl\_sync\_semaphore\_` sinyalleniyor.  

GL tarafından tüketilmeden tekrar sinyal → spec ihlali.



\*\*Çözüm:\*\*  

Timeline semaphore kullan (zaten `timeline\_semaphore\_` var):

```cpp

// gl\_sync\_semaphore\_ yerine timeline semaphore'u GL'e export et

// paintGL'de timeline değerini bekle (vkGetSemaphoreCounterValue polling)

// veya per-frame binary semaphore pool (3 slot, round-robin)

```



\---



\### T2 — scripts/build.py Sadeleştirme



\*\*Mevcut:\*\* 357 satır, vswhere detection, ninja path discovery, log sistemi  

\*\*Hedef:\*\* \~30 satır, aynı işlev



```python

\#!/usr/bin/env python3

"""Reji Studio build wrapper — VS 2022/2024 + Ninja"""

import subprocess, sys, os, pathlib



def find\_vs():

&#x20;   result = subprocess.run(

&#x20;       \["vswhere", "-latest", "-property", "installationPath"],

&#x20;       capture\_output=True, text=True

&#x20;   )

&#x20;   if result.returncode == 0 and result.stdout.strip():

&#x20;       return result.stdout.strip()

&#x20;   # Fallback: bilinen path'ler

&#x20;   for path in \[

&#x20;       r"C:\\Program Files\\Microsoft Visual Studio\\18\\Community",

&#x20;       r"C:\\Program Files\\Microsoft Visual Studio\\2022\\Community",

&#x20;   ]:

&#x20;       if pathlib.Path(path).exists():

&#x20;           return path

&#x20;   raise RuntimeError("Visual Studio bulunamadı")



def build(target="reji\_app", clean=False):

&#x20;   vs = find\_vs()

&#x20;   vcvars = fr"{vs}\\VC\\Auxiliary\\Build\\vcvars64.bat"

&#x20;   

&#x20;   if clean and pathlib.Path("build").exists():

&#x20;       subprocess.run("rmdir /s /q build", shell=True)

&#x20;   

&#x20;   if not pathlib.Path("build").exists():

&#x20;       subprocess.run(

&#x20;           f'"{vcvars}" > nul \&\& cmake -G Ninja -DCMAKE\_BUILD\_TYPE=Release -B build',

&#x20;           shell=True, check=True

&#x20;       )

&#x20;   

&#x20;   subprocess.run(

&#x20;       f'"{vcvars}" > nul \&\& cmake --build build --target {target}',

&#x20;       shell=True, check=True

&#x20;   )

&#x20;   

&#x20;   # Rust orchestrator

&#x20;   subprocess.run(

&#x20;       \["cargo", "build", "--release"],

&#x20;       cwd="src/orchestrator", check=True

&#x20;   )



if \_\_name\_\_ == "\_\_main\_\_":

&#x20;   import argparse

&#x20;   p = argparse.ArgumentParser()

&#x20;   p.add\_argument("--clean", action="store\_true")

&#x20;   p.add\_argument("--target", default="reji\_app")

&#x20;   args = p.parse\_args()

&#x20;   build(args.target, args.clean)

```



\---



\### T3 — justfile Ekleme



\*\*Kurulum:\*\*

```powershell

\# just kurulumu (winget ile)

winget install Casey.Just

```



\*\*justfile\*\* (C:\\reji-studio\\justfile):

```makefile

\# Reji Studio — Komut Kısayolları



\# Varsayılan: build

default: build



\# Build

build:

&#x20;   python scripts/build.py



\# Temiz build

rebuild:

&#x20;   python scripts/build.py --clean



\# Test

test:

&#x20;   cargo test --manifest-path src/orchestrator/Cargo.toml



\# Çalıştır

run:

&#x20;   build\\src\\ui\\reji\_app.exe 2> err.log



\# Fable 5 tarama

review:

&#x20;   powershell scripts/fable5-review.ps1



\# Modül bazlı tarama

review-gpu:

&#x20;   powershell scripts/fable5-review.ps1 -Module pipeline



review-rust:

&#x20;   powershell scripts/fable5-review.ps1 -Module orchestrator



\# Güvenlik tarama

shield:

&#x20;   npx ecc-agentshield scan



\# Git log

log:

&#x20;   git log --oneline -20

```



\*\*Kullanım:\*\*

```cmd

just build

just test

just run

just review

just shield

```



\---



\## Sprint 3 — GPU Seçimi ve D3D11 Device

\*\*Hedef:\*\* Çift GPU mimarisini doğru yapılandır  

\*\*Tahmini süre:\*\* 1-2 oturum



\---



\### C8 — Vulkan LUID Eşlemesi



\*\*Sorun:\*\*  

İlk graphics-queue'lu cihaz seçiliyor.  

780M+4070 sistemde NVIDIA seçilirse AMD D3D11 texture importu başarısız.



\*\*Çözüm:\*\*

```cpp

// vulkan\_initializer.cpp select\_device() içinde:

// VkPhysicalDeviceIDProperties::deviceLUID ile

// DXGI adapter LUID'ini karşılaştır

VkPhysicalDeviceIDProperties id\_props{};

id\_props.sType = VK\_STRUCTURE\_TYPE\_PHYSICAL\_DEVICE\_ID\_PROPERTIES;

VkPhysicalDeviceProperties2 props2{};

props2.sType = VK\_STRUCTURE\_TYPE\_PHYSICAL\_DEVICE\_PROPERTIES\_2;

props2.pNext = \&id\_props;

vkGetPhysicalDeviceProperties2(device, \&props2);



// DXGI LUID ile karşılaştır

if (memcmp(id\_props.deviceLUID, target\_luid, VK\_LUID\_SIZE) == 0) {

&#x20;   // Bu doğru cihaz

}

```



\---



\### C9 — Same-Adapter'da İki D3D11 Device



\*\*Sorun:\*\*  

`encode\_tex\_` display device'da yaratılıyor ama encode context'iyle kopyalanıyor.



\*\*Çözüm:\*\*  

Same-adapter durumunda tek device:

```cpp

// gpu\_resource\_manager.cpp init() içinde:

if (same\_adapter\_) {

&#x20;   encode\_ctx\_ = display\_ctx\_;  // aynı device paylaş

&#x20;   // encode\_tex\_ display device'da yaratıldığı için geçerli

}

```



\---



\## Dosya Düzenleme Sırası



```

Sprint 1 (bağımsız, paralel):

&#x20; T1 → cbindgen kur (Cargo.toml + build.rs + cbindgen.toml)

&#x20; C1 → metrics.rs struct layout düzelt + const assert

&#x20; C2 → vulkan\_initializer.cpp layer kontrolü

&#x20; C3 + C4 → copy\_optimizer.cpp barrier düzeltmeleri (birlikte)



Sprint 2:

&#x20; C5 → command drain tek tüketici (pipeline.cpp + wasapi + srt + main\_window)

&#x20; C6 → NVENC frame command queue (C5 sonrası)

&#x20; C7 → binary semaphore → timeline/pool

&#x20; T2 → scripts/build.py sadeleştir

&#x20; T3 → justfile ekle + just kur



Sprint 3:

&#x20; C8 → LUID eşlemesi (vulkan\_initializer.cpp)

&#x20; C9 → same-adapter device paylaşımı (gpu\_resource\_manager.cpp)

```



\---



\## Build ve Test Komutu (Her Sprint Sonrası)



```cmd

cd C:\\reji-studio

python scripts/build.py --clean

build\\src\\ui\\reji\_app.exe 2> err.log

type err.log | findstr "ERROR\\|FAILED\\|VUID\\|assert\\|ABI"

cargo test --manifest-path src/orchestrator/Cargo.toml

```



\---



\## Takip



\- \[ ] Sprint 1 tamamlandı (C1, C2, C3, C4, T1)

\- \[x] Sprint 2 tamamlandı (C5, C6, C7, T2, T3)

\- \[ ] Sprint 3 tamamlandı (C8, C9)

\- \[ ] Fable 5 üçüncü tarama yapıldı



\---



\*Bu belge C:\\reji-studio\\docs\\FABLE5\_BUG\_PLAN\_V2.md'ye kopyalanmalı.\*



