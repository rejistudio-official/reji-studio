\# Reji Studio — Fable 5 Kod Tarama Düzeltme Planı



\*\*Tarih:\*\* 10.06.2026  

\*\*Kaynak:\*\* Fable 5 (anthropic/claude-5-fable-20260609) — 60 dosya, 191K token  

\*\*Durum:\*\* Plan aşaması — implementasyon başlamadı



\---



\## Öncelik Matrisi



| # | Sorun | Dosya | Öncelik | Etki | Sprint |

|---|---|---|---|---|---|

| B1 | FFI sembol çakışması (3 yerde aynı fonksiyon) | ffi\_bridge.c + lib.rs + ffi.rs | 🔴 Kritik | Derlenmez / UB | Sprint 1 |

| B2 | `ArrayQueue::pop()` Result değil Option — derlenmez | lib.rs | 🔴 Kritik | Derlenmez | Sprint 1 |

| B3 | `MetricsCollector` frame drop hesabı yanlış | metrics\_collector.cpp | 🔴 Kritik | Yanlış metrik | Sprint 1 |

| B4 | `MainWindow` frame thread race condition | main\_window.cpp | 🔴 Kritik | Use-after-free crash | Sprint 1 |

| B5 | Vulkan/GL implicit sync yok — glWaitSemaphoreEXT eksik | preview\_widget.cpp | 🔴 Kritik | Tearing / stale frame | Sprint 2 |

| B6 | D3D11→Vulkan keyed mutex Acquire/Release eksik | gpu\_resource\_manager.cpp | 🔴 Kritik | Görünmez kare (cross-adapter) | Sprint 2 |

| B7 | `RjAction` ABI uyumsuzluğu (timestamp\_us alanı yok) | lib.rs test kodu | 🟠 Yüksek | Test derlenmez | Sprint 1 |

| B8 | `HealingMonitor::run` — Lagged/Closed recv hatası | healing.rs | 🟠 Yüksek | Busy-loop / mesaj kaybı | Sprint 2 |

| B9 | `rj\_start\_monitor\_impl` mutex poison recovery yok | ffi.rs | 🟠 Yüksek | Rule engine kalıcı kilitli | Sprint 2 |

| B10 | `ExternalMemoryBridge` shutdown sırası (VkDevice önce yıkılabilir) | external\_memory\_bridge.cpp | 🟠 Yüksek | UB / crash on exit | Sprint 2 |

| B11 | `GpuResourceManager::copy\_fence\_` hiç oluşturulmamış | gpu\_resource\_manager.cpp | 🟡 Orta | Cross-adapter aktifken crash | Sprint 3 |

| B12 | `ProgramWidget::beginTransition` — tex swap yarışı | program\_widget.cpp | 🟡 Orta | Yanlış texture upload | Sprint 3 |

| B13 | `initialize\_gl\_target\_pool` kısmi başarısızlıkta sızıntı | external\_memory\_bridge.cpp | 🟡 Orta | Bellek sızıntısı | Sprint 3 |

| B14 | `FrameProfiler` — hot-path mutex + sınırsız push\_back | frame\_profiler.cpp | 🟡 Orta | Proje kuralı ihlali | Sprint 3 |

| B15 | `is\_copy\_ready` — `\_\_try` içinde `fprintf` (SEH kural ihlali) | copy\_optimizer.cpp | 🟡 Orta | Proje kuralı ihlali | Sprint 3 |

| B16 | NT handle çifte kapatma riski (GL + Vulkan) | external\_memory\_bridge.cpp | 🟡 Orta | UB on shutdown | Sprint 3 |

| B17 | `frame\_pacing/gpu\_query\_timing` — anlamsız try/catch | frame\_pacing.cpp + gpu\_query\_timing.cpp | 🔵 Düşük | Ölü kod, yanlış güvenlik hissi | Sprint 4 |

| B18 | Cross-adapter sync tamamen eksik (keyed mutex impl) | gpu\_resource\_manager.cpp | 🔵 Düşük | cross-adapter aktif değilken risksiz | Sprint 4 |



\---



\## Sprint 1 — Derleme ve Kritik Mantık Hataları

\*\*Hedef:\*\* Derleme hatalarını ve veri bozulması risklerini gider  

\*\*Tahmini süre:\*\* 1-2 oturum



\---



\### B1 — FFI Sembol Çakışması



\*\*Sorun:\*\*  

`rj\_metrics\_poll`, `rj\_action\_dequeue` vb. fonksiyonlar 3 ayrı yerde tanımlı:

\- `src/ffi/ffi\_bridge.c` (C stub — her zaman sabit değer döner)

\- `src/orchestrator/lib.rs` (`#\[no\_mangle]`)

\- `src/orchestrator/src/ffi.rs` (`#\[no\_mangle]`)



Statik linkte "duplicate symbol"; dinamik linkte hangi implementasyonun çağrıldığı belirsiz.  

C stub `rj\_metrics\_poll` → 1 döndürürken, Rust → 0 döndürüyor.



\*\*Çözüm:\*\*

1\. `src/ffi/ffi\_bridge.c` içindeki stub'ları ya tamamen kaldır  

&#x20;  ya da `#ifdef RJ\_STUB\_ONLY` koşuluna al

2\. `src/orchestrator/lib.rs` ile `src/orchestrator/src/ffi.rs` arasındaki çakışan sembolleri tespit et — tek kaynak kalmalı

3\. Tercih: `src/orchestrator/src/ffi.rs` canonical, `lib.rs` sadece re-export



\*\*Doğrulama:\*\*  

```cmd

cmake --build build --target reji\_app 2>\&1 | findstr "LNK2005\\|duplicate"

```

Sonuç boş olmalı.



\---



\### B2 — `ArrayQueue::pop()` Yanlış Kullanımı



\*\*Sorun:\*\*  

`src/orchestrator/lib.rs` içinde:

```rust

match action\_queue().pop() {

&#x20;   Ok(action) => { ... }   // YANLIŞ — pop() Option döner, Result değil

&#x20;   Err(\_) => 0,

}

```

`crossbeam::queue::ArrayQueue::pop()` → `Option<T>` döner.  

Bu kod \*\*derlenmez\*\*.



\*\*Çözüm:\*\*

```rust

// lib.rs — DÜZELTME

match action\_queue().pop() {

&#x20;   Some(action) => {

&#x20;       // mevcut işlem

&#x20;       1

&#x20;   }

&#x20;   None => 0,

}

```

Not: `src/orchestrator/src/ffi.rs`'deki versiyon zaten doğru (`if let Some(action)`).  

`lib.rs`'deki versiyonu ya sil ya da düzelt.



\*\*Doğrulama:\*\*  

```cmd

cd src/orchestrator \&\& cargo build 2>\&1 | findstr "error"

```



\---



\### B3 — Frame Drop Hesabı Yanlış



\*\*Sorun:\*\*  

`src/pipeline/metrics\_collector.cpp`, `calculate\_frame\_drop\_pct()`:

```cpp

frame\_drop\_window\_.push\_back(total\_drops\_);  // YANLIŞ: kümülatif değer

```

Pencereye delta değil kümülatif sayaç ekleniyor.  

`accumulate` ile toplam = kümülatiflerin toplamı → her zaman 100'e yakın, anlamsız.



\*\*Çözüm:\*\*

```cpp

// Doğrusu:

uint32\_t delta = total\_drops\_ - prev\_total\_drops\_;

prev\_total\_drops\_ = total\_drops\_;

frame\_drop\_window\_.push\_back(delta);

```

`prev\_total\_drops\_` üye değişkeni eklenmeli (header'a da).



\*\*Doğrulama:\*\*  

Uygulama çalışırken frame drop % değerinin 0-100 arasında makul değerler göstermesi.



\---



\### B4 — Frame Thread Race Condition



\*\*Sorun:\*\*  

`src/ui/main\_window.cpp`, `closeEvent()`:

```cpp

frame\_thread\_->requestInterruption();

frame\_thread\_->wait(1000);  // timeout olursa thread hâlâ çalışıyor

// \~MainWindow → pipeline\_ destructor → shutdown()  ← use-after-free!

```

`wait(1000)` timeout'ta pipeline thread'i hâlâ `run\_frame()` içindeyken  

`Pipeline` yıkılıyor → use-after-free.



Ayrıca worker lambda `deleteLater()` çağırıyor ama event loop dönmüyor → QObject sızıntısı.



\*\*Çözüm:\*\*

```cpp

void MainWindow::closeEvent(QCloseEvent\* event) {

&#x20;   if (frame\_thread\_ \&\& frame\_thread\_->isRunning()) {

&#x20;       frame\_thread\_->requestInterruption();

&#x20;       // Daha uzun bekle — 5 saniye

&#x20;       if (!frame\_thread\_->wait(5000)) {

&#x20;           // Son çare: terminate (güvenli değil ama crash'ten iyi)

&#x20;           frame\_thread\_->terminate();

&#x20;           frame\_thread\_->wait(1000);

&#x20;       }

&#x20;   }

&#x20;   // Worker objesini lambda içinde delete et (deleteLater yerine)

&#x20;   QWidget::closeEvent(event);

}

```

Worker lambda içinde:

```cpp

// deleteLater() yerine:

delete worker;  // lambda sonunda

```



\*\*Doğrulama:\*\*  

Uygulama kapatıldığında crash olmadan temiz kapanma.  

`run.log | findstr "use-after\\|access violation"` → boş.



\---



\### B7 — RjAction ABI Test Kodu Çakışması



\*\*Sorun:\*\*  

`src/orchestrator/lib.rs` test kodunda `timestamp\_us: 0` alanı kullanılıyor  

ama `RjAction` struct'ta bu alan yok → test derlenmez.



\*\*Çözüm:\*\*  

Test kodundaki `timestamp\_us: 0` satırını kaldır.



\*\*Doğrulama:\*\*  

```cmd

cd src/orchestrator \&\& cargo test 2>\&1 | findstr "error"

```



\---



\## Sprint 2 — GPU/Vulkan Senkronizasyon ve Rust Güvenilirlik

\*\*Hedef:\*\* Görüntü kalitesi ve runtime stabilite  

\*\*Tahmini süre:\*\* 2-3 oturum



\---



\### B5 — Vulkan/GL Semaphore Sync Eksik



\*\*Sorun:\*\*  

`src/ui/preview\_widget.cpp`:  

Vulkan blit (`execute\_copy`) tamamlanmadan GL aynı texture'ı okuyor.  

`glWaitSemaphoreEXT` / `GL\_EXT\_semaphore\_win32` import'u yok.  

Vulkan yazma ile GL okuma arasında GPU-side sync yok → tearing, stale frame.



\*\*Çözüm:\*\*

1\. `VK\_KHR\_external\_semaphore\_win32` ile semaphore oluştur ve export et

2\. `GL\_EXT\_semaphore\_win32` ile GL tarafında import et

3\. `paintGL()` başında `glWaitSemaphoreEXT(sem, ...)` çağır

4\. Vulkan submit sonrası semaphore'u signal et



\*\*Gerekli extension'lar:\*\*

\- Vulkan: `VK\_KHR\_external\_semaphore`, `VK\_KHR\_external\_semaphore\_win32`

\- GL: `GL\_EXT\_semaphore`, `GL\_EXT\_semaphore\_win32`



\*\*Doğrulama:\*\*  

`run.log | findstr "GL interop texture"` — her frame görünmeli, tearing yok.



\---



\### B6 — D3D11→Vulkan Keyed Mutex Eksik



\*\*Sorun:\*\*  

`src/pipeline/capture/gpu\_resource\_manager.cpp`:  

`shared\_texture\_` oluşturulurken `D3D11\_RESOURCE\_MISC\_SHARED\_KEYEDMUTEX` flag var  

ama hiçbir yerde `IDXGIKeyedMutex::AcquireSync/ReleaseSync` çağrılmıyor.  

D3D11 capture → `CopyResource` → Vulkan blit arası senkronizasyon boş.



\*\*Çözüm:\*\*

```cpp

// capture\_dxgi.cpp — frame capture sonrası:

IDXGIKeyedMutex\* keyed\_mutex = nullptr;

shared\_texture\_->QueryInterface(\_\_uuidof(IDXGIKeyedMutex), (void\*\*)\&keyed\_mutex);

keyed\_mutex->AcquireSync(0, INFINITE);  // D3D11 yaz

// ... CopyResource ...

keyed\_mutex->ReleaseSync(1);            // Vulkan oku

keyed\_mutex->Release();



// copy\_optimizer.cpp — Vulkan blit öncesi:

// VkWin32KeyedMutexAcquireReleaseInfoKHR ile acquire key=1, release key=0

```



\*\*Doğrulama:\*\*  

Kare rengi doğru görünmeli (siyah kare kalmamalı).



\---



\### B8 — HealingMonitor Lagged/Closed Recv



\*\*Sorun:\*\*  

`src/orchestrator/src/healing.rs`:

```rust

tokio::select! {

&#x20;   biased;

&#x20;   Ok(system) = self.system\_rx.recv() => { ... }  // Err(Lagged) eşleşmez

&#x20;   Ok(media)  = self.media\_rx.recv() => { ... }   // Closed → busy-loop

&#x20;   \_ = ticker.tick() => { ... }

}

```

`Lagged` durumunda mesajlar sessizce kaybolur.  

`Closed` durumunda dal sürekli ready → ticker aç kalır → busy-loop.



\*\*Çözüm:\*\*

```rust

tokio::select! {

&#x20;   biased;

&#x20;   result = self.system\_rx.recv() => {

&#x20;       match result {

&#x20;           Ok(system) => { /\* işle \*/ }

&#x20;           Err(broadcast::error::RecvError::Lagged(n)) => {

&#x20;               warn!("HealingMonitor: system\_rx {} mesaj kaçtı", n);

&#x20;           }

&#x20;           Err(broadcast::error::RecvError::Closed) => {

&#x20;               info!("HealingMonitor: system\_rx kapandı, çıkılıyor");

&#x20;               break;

&#x20;           }

&#x20;       }

&#x20;   }

&#x20;   // media\_rx için aynı pattern

&#x20;   \_ = ticker.tick() => { /\* mevcut kod \*/ }

}

```



\*\*Doğrulama:\*\*  

`run.log | findstr "HealingMonitor"` — Lagged uyarıları görünmeli, crash yok.



\---



\### B9 — Mutex Poison Recovery



\*\*Sorun:\*\*  

`src/orchestrator/src/ffi.rs`:

```rust

state.rule\_engine.lock().unwrap()  // panic → mutex poison → rule engine ölü

```

Panic sonrası mutex kalıcı poison olur, bir daha kilitlenemez.



\*\*Çözüm:\*\*

```rust

// unwrap() yerine:

state.rule\_engine.lock().unwrap\_or\_else(|poisoned| {

&#x20;   warn!("rule\_engine mutex poison — recovering");

&#x20;   poisoned.into\_inner()

})

```



\*\*Doğrulama:\*\*  

Rule reload test edilmeli; panic sonrası reload çalışmaya devam etmeli.



\---



\### B10 — ExternalMemoryBridge Shutdown Sırası



\*\*Sorun:\*\*  

`\~ExternalMemoryBridge()` → `vkFreeMemory(device\_, ...)` ama  

`VulkanInitializer` singleton daha önce `vkDestroyDevice` çağırmış olabilir.  

Yıkım sırası garanti değil → UB.



\*\*Çözüm:\*\*

`Pipeline::shutdown()` içinde bridge'i \*\*açıkça\*\* ve VulkanInitializer'dan \*\*önce\*\* kapat:

```cpp

void Pipeline::Impl::shutdown() {

&#x20;   // 1. Bridge'i önce kapat

&#x20;   if (external\_memory\_bridge\_) {

&#x20;       external\_memory\_bridge\_->shutdown();

&#x20;       external\_memory\_bridge\_.reset();

&#x20;   }

&#x20;   // 2. Sonra Vulkan temizle

&#x20;   vulkan\_initializer\_.reset();

}

```



\*\*Doğrulama:\*\*  

Uygulama kapatıldığında Vulkan validation layer'da  

`VUID-vkDestroyDevice-device-00378` hatası olmamalı.



\---



\## Sprint 3 — Orta Öncelikli Sorunlar

\*\*Hedef:\*\* Bellek güvenliği ve proje kuralı uyumu  

\*\*Tahmini süre:\*\* 1-2 oturum



\---



\### B11 — copy\_fence\_ Null Koruması



\*\*Sorun:\*\*  

`gpu\_resource\_manager.cpp`:  

`copy\_fence\_` (ID3D11Query) hiç `CreateQuery` edilmemiş.  

`wait\_display\_gpu\_idle()` çağrılırsa null deref → crash.



\*\*Çözüm (kısa vadeli):\*\*

```cpp

bool GpuResourceManager::wait\_display\_gpu\_idle() {

&#x20;   if (!copy\_fence\_) {

&#x20;       // Cross-adapter aktif değil, bekle gerekmiyor

&#x20;       return true;

&#x20;   }

&#x20;   // mevcut kod

}

```

Uzun vadeli: `init()` içinde `D3D11\_QUERY\_EVENT` query oluştur.



\---



\### B12 — ProgramWidget Texture Swap Yarışı



\*\*Sorun:\*\*  

`program\_widget.cpp`:  

`beginTransition()` UI thread'den `tex\_a/tex\_b` swap yapıyor.  

`paintGL()` aynı anda `tex\_a`'ya upload yapabilir → yanlış texture yazımı.



\*\*Çözüm:\*\*  

Swap'ı `paintGL()` başına taşı:

```cpp

// beginTransition: sadece flag set et

void ProgramWidget::beginTransition() {

&#x20;   transition\_requested\_ = true;

}



// paintGL başında:

if (transition\_requested\_.exchange(false)) {

&#x20;   std::swap(d\_->tex\_a, d\_->tex\_b);

}

```

`transition\_requested\_` → `std::atomic<bool>`.



\---



\### B13 — GL Target Pool Rollback



\*\*Sorun:\*\*  

`external\_memory\_bridge.cpp`, `initialize\_gl\_target\_pool()`:  

İlk slotlar başarılı, sonraki başarısızsa `return false` ama  

önceki slot'ların image/memory/handle'ları temizlenmiyor.



\*\*Çözüm:\*\*  

Başarısızlıkta rollback:

```cpp

bool ExternalMemoryBridge::initialize\_gl\_target\_pool() {

&#x20;   for (uint32\_t i = 0; i < POOL\_SIZE; ++i) {

&#x20;       if (!create\_gl\_target\_slot(i)) {

&#x20;           // Rollback: 0..i-1 arası temizle

&#x20;           for (uint32\_t j = 0; j < i; ++j) {

&#x20;               destroy\_gl\_target\_slot(j);

&#x20;           }

&#x20;           return false;

&#x20;       }

&#x20;   }

&#x20;   return true;

}

```



\---



\### B14 — FrameProfiler Ring Buffer



\*\*Sorun:\*\*  

`frame\_profiler.cpp`:  

Her frame 6 mutex lock + `push\_back` (sınırsız büyüme).  

Proje kuralı ihlali: "hot-path heap allocation yasak".



\*\*Çözüm:\*\*  

Sabit boyutlu ring buffer:

```cpp

struct FrameProfiler {

&#x20;   static constexpr size\_t MAX\_SAMPLES = 3600;  // 1 dakika @ 60fps

&#x20;   std::array<FrameSample, MAX\_SAMPLES> samples\_;

&#x20;   size\_t head\_ = 0;

&#x20;   size\_t count\_ = 0;

&#x20;   std::mutex mu\_;

&#x20;   

&#x20;   void push(FrameSample s) {

&#x20;       std::lock\_guard<std::mutex> lock(mu\_);

&#x20;       samples\_\[head\_ % MAX\_SAMPLES] = s;

&#x20;       ++head\_;

&#x20;       count\_ = std::min(count\_ + 1, MAX\_SAMPLES);

&#x20;   }

};

```



\---



\### B15 — SEH İçinde fprintf



\*\*Sorun:\*\*  

`copy\_optimizer.cpp`, `\_\_try` bloğu içinde `fprintf` çağrıları var.  

Proje kuralı: "SEH leaf'te yalnız POD, fonksiyon çağrısı yok".



\*\*Çözüm:\*\*  

Log'u SEH dışına taşı:

```cpp

// \_\_try içinde: yalnız işlem, log yok

HRESULT hr = /\* ... \*/;

bool seh\_fired = false;

// \_\_except: seh\_fired = true; set et



// \_\_try/\_\_except DIŞINDA:

if (seh\_fired) {

&#x20;   fprintf(stderr, "\[CopyOptimizer] SEH yakalandı\\n");

&#x20;   fflush(stderr);

}

```



\---



\### B16 — NT Handle Çifte Kapatma



\*\*Sorun:\*\*  

`external\_memory\_bridge.cpp` `shutdown()`:  

`gl\_target\_handles\_` GL'e import edildikten sonra `CloseHandle` ile kapatılıyor.  

GL memory object hâlâ kullanıyor olabilir.



\*\*Çözüm:\*\*  

GL memory object'leri sildikten SONRA handle'ları kapat:

```cpp

void ExternalMemoryBridge::shutdown() {

&#x20;   // 1. GL memory object'leri sil

&#x20;   if (pfn\_DeleteMemoryObjects\_) {

&#x20;       pfn\_DeleteMemoryObjects\_(gl\_memory\_objects\_.size(), gl\_memory\_objects\_.data());

&#x20;   }

&#x20;   // 2. Sonra handle'ları kapat

&#x20;   for (auto\& h : gl\_target\_handles\_) {

&#x20;       if (h) { CloseHandle(h); h = nullptr; }

&#x20;   }

&#x20;   // 3. Vulkan resource'ları sil

&#x20;   // ...

}

```



\---



\## Sprint 4 — Düşük Öncelikli Temizlik

\*\*Hedef:\*\* Kod kalitesi ve ölü kod temizliği  

\*\*Tahmini süre:\*\* 1 oturum



\---



\### B17 — Anlamsız try/catch Kaldır



\*\*Dosyalar:\*\* `frame\_pacing.cpp`, `gpu\_query\_timing.cpp`  

WinAPI/Vulkan çağrıları C++ exception fırlatmaz; `catch(...)` ölü kod.



```cpp

// KALDIR:

try {

&#x20;   hr = device\_->CreateQuery(...);

} catch (...) {

&#x20;   return false;

}



// YERİNE:

hr = device\_->CreateQuery(...);

if (FAILED(hr)) return false;

```



\---



\### B18 — Cross-Adapter Sync Dokümantasyonu



\*\*Sorun:\*\*  

Keyed mutex implementasyonu yarım, `same\_adapter\_ = true` hardcode.  

Şu an risksiz ama ileride aktif edilirse kritik hata.



\*\*Kısa vadeli çözüm:\*\*  

Kodda açık yorum:

```cpp

// TODO(cross-adapter): same\_adapter\_ = false yapılmadan önce:

// 1. D3D11\_RESOURCE\_MISC\_SHARED\_KEYEDMUTEX flag'i doğrula

// 2. IDXGIKeyedMutex::AcquireSync(0)/ReleaseSync(1) implement et

// 3. VkWin32KeyedMutexAcquireReleaseInfoKHR ekle

// 4. copy\_fence\_ (D3D11Query) oluştur

// Referans: docs/cross-adapter-sync.md

```



\---



\## Dosya Düzenleme Sırası (Dependency Graph)



```

Sprint 1 (bağımsız, paralel yapılabilir):

&#x20; B2 → lib.rs (tek satır düzeltme)

&#x20; B7 → lib.rs test kodu (tek satır silme)

&#x20; B3 → metrics\_collector.cpp + .h

&#x20; B1 → ffi\_bridge.c + lib.rs + ffi.rs (dikkatli — sembol silme)

&#x20; B4 → main\_window.cpp



Sprint 2 (B1 tamamlanmadan başlama):

&#x20; B9 → ffi.rs

&#x20; B8 → healing.rs

&#x20; B10 → pipeline.cpp shutdown()

&#x20; B5 → preview\_widget.cpp + external\_memory\_bridge (B10 sonrası)

&#x20; B6 → gpu\_resource\_manager.cpp + capture\_dxgi.cpp



Sprint 3 (B5, B6 sonrası):

&#x20; B11 → gpu\_resource\_manager.cpp

&#x20; B13 → external\_memory\_bridge.cpp

&#x20; B16 → external\_memory\_bridge.cpp (B13 ile birlikte)

&#x20; B12 → program\_widget.cpp + .h

&#x20; B14 → frame\_profiler.cpp + .h

&#x20; B15 → copy\_optimizer.cpp



Sprint 4:

&#x20; B17 → frame\_pacing.cpp + gpu\_query\_timing.cpp

&#x20; B18 → gpu\_resource\_manager.cpp (yorum ekle)

```



\---



\## Build ve Test Komutu (Her Sprint Sonrası)



```cmd

cd C:\\reji-studio

python scripts/build.py --clean

build\\src\\ui\\reji\_app.exe > run.log 2>\&1

type run.log | findstr "ERROR\\|FAILED\\|crash\\|assert\\|VK error\\|validation"

```

Sonuç boş olmalı.



\---



\## Takip



\- \[x] Sprint 1 tamamlandı (B1, B2, B3, B4, B7)

\- \[x] B5 tamamlandı (Vulkan/GL semaphore sync — glWaitSemaphoreEXT + command buffer reuse sync)

\- \[x] B6 tamamlandı (D3D11→Vulkan keyed mutex sync — AcquireSync/ReleaseSync + VkWin32KeyedMutexAcquireReleaseInfoKHR)

\- \[x] B8 tamamlandı (HealingMonitor Lagged/Closed recv hata yönetimi)

\- \[x] B9 tamamlandı (mutex poison recovery — unwrap\_or\_else)

\- \[x] B10 tamamlandı (ExternalMemoryBridge shutdown sırası — VkDevice lifetime garantisi)

\- \[x] Sprint 2 tamamlandı  

\- \[x] B11 tamamlandı (copy\_fence\_ null guard — same-adapter path)

\- \[x] B12 tamamlandı (ProgramWidget tex swap race — atomic transition\_requested\_)

\- \[x] B13 tamamlandı (initialize\_gl\_target\_pool rollback — kısmi başarısızlıkta sızıntı yok)

\- \[x] B14 tamamlandı (FrameProfiler ring buffer — push\_back yerine sabit array)

\- \[x] B15 tamamlandı (SEH içinde fprintf — SehFilter ile dışarı taşındı)

\- \[x] B16 tamamlandı (NT handle sırası — GL memory object silme önce, handle kapatma sonra)

\- \[x] Sprint 3 tamamlandı

\- \[ ] Sprint 4 tamamlandı

\- \[ ] Fable 5 ile ikinci tarama yapıldı (skor iyileşmesi doğrulandı)



\---



\*Bu belge C:\\reji-studio\\docs\\FABLE5\_BUG\_PLAN.md'ye kopyalanmalı ve her sprint sonrası güncellenmeli.\*



