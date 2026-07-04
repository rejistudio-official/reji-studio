\# Reji Studio — Teknik Yol Haritası



\*\*Tarih:\*\* 11.06.2026  

\*\*Versiyon:\*\* v0.5.2  

\*\*Kapsam:\*\* Polyglot Mimari, Zig Migrasyonu, Build Sadeleştirme



\---



\## Mevcut Durum (v0.5.2)



```

Dil Dağılımı:

&#x20; C++    %70.2  — UI (Qt6), GPU pipeline, capture

&#x20; Rust   %20.1  — Orchestrator, self-healing, FFI

&#x20; Python  %4.5  — Build script

&#x20; CMake   %3.0  — Build tanımı

&#x20; C       %1.3  — ABI köprüsü (ffi\_bridge.c)



Tamamlanan:

&#x20; ✅ B1-B18 — İlk Fable 5 tarama düzeltmeleri (18 bug)

&#x20; ✅ C1-C9  — Komut + NVENC + Semaphore + GPU seçimi (9 bug)

&#x20; ✅ D1-D18 — Üçüncü Fable 5 tarama (18 bug)

&#x20; ✅ E1-E18 — Dördüncü Fable 5 tarama (18 bug)

&#x20; ✅ F1-F18 — Beşinci Fable 5 tarama (18 bug)

&#x20; Toplam: 81 bug | Fable 5 tarama sayısı: 5

&#x20; ✅ cbindgen (T1) — Rust → C header otomatik üretim, ABI otomatik kontrol

&#x20; ✅ AgentShield B (85/100)

&#x20; ✅ ECC kurulumu

&#x20; ✅ Validation layer CI entegrasyonu aktif

&#x20; ✅ build.zig pilot tamamlandı

&#x20; ✅ Faz 0 — build.zig pilot (Zig 0.16 uyumlu)

&#x20; ✅ Faz 1 — src/ffi/ Zig'e taşındı, C kaldırıldı

&#x20; ✅ sizeof_check.zig comptime ABI assert



Sonraki Hedef: v0.7 Zig Faz 2 — src/pipeline/gpu/

```



\---



\## Polyglot Mimari İlkeleri



\### 1. Katman → Dil Eşlemesi



| Katman | Şimdi | Hedef (v1.0) | İdeal (v2.0) |

|---|---|---|---|

| UI | C++ (Qt6) | C++ (Qt6/QML) | QML + minimal C++ |

| Orchestrator | Rust | Rust | Rust |

| GPU/Capture/Audio | C++ | Zig | Zig |

| ABI Köprüsü | C (elle) | C (cbindgen) | C (cbindgen) |

| Build | CMake+Python | CMake+Cargo+Zig | build.zig |



\### 2. ABI Kuralları (Zorunlu)



```

✅ Tüm cross-language struct'lar cbindgen ile üretilir

✅ Elle yazılan FFI header yasak

✅ Her struct için sizeof + offsetof assert zorunlu

✅ C ABI'de pointer callback yasak

✅ C ABI'de exception yasak

✅ C ABI'de heap allocation yasak

✅ Her FFI fonksiyonu RjResult döner

```



\### 3. İletişim Kuralları (Zorunlu)



```

✅ Diller arası iletişim: yalnızca C ABI + POD struct

✅ Komut akışı: yalnızca pipeline thread tüketir

✅ GPU kaynakları: yalnızca GPU katmanı yönetir

✅ UI → pipeline: doğrudan erişim yasak

✅ Pipeline → UI: doğrudan erişim yasak

```



\### 4. Ortak Hata Protokolü



```c

/\* ffi\_bridge.h — tüm dillerin anladığı hata tipi \*/

typedef enum {

&#x20;   RJ\_OK              = 0,

&#x20;   RJ\_ERR\_BUSY        = 1,

&#x20;   RJ\_ERR\_INVALID     = 2,

&#x20;   RJ\_ERR\_DEVICE\_LOST = 3,

&#x20;   RJ\_ERR\_TIMEOUT     = 4,

} RjResult;

```



\### 5. ABI Güvenlik Katmanı (Her Dilde)



```cpp

// C++ — sizeof\_check.cpp

static\_assert(sizeof(RjMetricSample) == 56);

static\_assert(offsetof(RjMetricSample, magic\_tail) == 52);

```



```rust

// Rust — metrics.rs

const \_: () = assert!(size\_of::<MetricSample>() == 56);

const \_: () = assert!(offset\_of!(MetricSample, magic\_tail) == 52);

```



```zig

// Zig — gelecekte ffi\_bridge.zig

comptime {

&#x20;   assert(@sizeOf(MetricSample) == 56);

&#x20;   assert(@offsetOf(MetricSample, "magic\_tail") == 52);

}

```



\### 6. SPSC İletişim Şablonu



```

┌──────────┐  write  ┌─────────────┐  read  ┌──────────┐

│ Üretici  │ ──────► │ Ring Buffer │ ──────►│ Tüketici │

│ (Rust)   │         │ (C ABI POD) │        │ (C++)    │

└──────────┘         └─────────────┘        └──────────┘

```



\---



\## Yol Haritası



\---



\### v0.5.2 — Mevcut Sprint'ler (2026 Q2-Q3)



\*\*Sprint 2 — Komut + NVENC + Semaphore + Build\*\*



```

C5 — rj\_command\_drain tek tüketici (pipeline)

C6 — NVENC thread-safety (frame command queue)

C7 — Binary semaphore pool (3 slotlu round-robin)

T2 — scripts/build.py: 357 → \~30 satır

T3 — justfile: build/test/run/review/shield komutları

```



\*\*Sprint 3 — GPU Seçimi\*\*



```

C8 — Vulkan LUID eşlemesi (VkPhysicalDeviceIDProperties)

C9 — Same-adapter tek D3D11 device

```



\*\*Sprint 4 — Altyapı\*\*



```

RjResult hata protokolü standardizasyonu

sizeof\_check.cpp CI entegrasyonu

AGENTS.md dil politikası bölümü

```



\---



\### v0.6 — Zig Faz 0+1: Pilot ve FFI ✅ TAMAMLANDI (15.06.2026)



\*\*GPU (E7):\*\* Cross-adapter NVENC (RTX 4070) — B6 keyed mutex + copy\_fence\_
tamamlanmadan etkinleştirilemez. same\_adapter\_ = true korunuyor — güvenli default.



\*\*Ön koşul:\*\* Zig 1.0 stable (tahmini 2026 sonu)



\*\*✅ Faz 0 — Pilot (Zig 0.16 uyumlu)\*\*

```

✅ build.zig Zig 0.16 API'sine güncellendi (addLibrary, root\_module)

✅ zig build ffi → zig-out/lib/reji\_ffi.lib

✅ ffi\_bridge.zig addObject ile derlemeye entegre edildi

```

\*\*✅ Faz 1 — FFI Katmanı\*\*

```

✅ src/ffi/ffi\_bridge.c → src/ffi/ffi\_bridge.zig (C kaldırıldı)

✅ src/ffi/ffi\_bridge.h — cbindgen üretir (zaten kurulu)

✅ src/ffi/sizeof\_check.zig comptime ABI assert aktif

✅ CMake: IMPORTED STATIC lib → zig-out/lib/reji\_ffi.lib

```

Kazanım:

```

✅ C oranı: %1.3 → %0

✅ comptime ABI doğrulama aktif (zig build abi-check)

✅ build.zig entegrasyonu test edilmiş

```

Sonraki: Faz 2 — src/pipeline/gpu/ (v0.7)



\---



\### v0.7 — Zig Faz 2: GPU Pipeline (2027 Q1, \~1-2 Ay)



\*\*Hedef:\*\* `src/pipeline/gpu/` tamamen Zig



Taşınacak dosyalar (öncelik sırası):



```

1\. external\_memory\_bridge.cpp/.h  → .zig

&#x20;  Kazanım: B10/B13/B16 tipi sorunlar defer ile imkansız



2\. copy\_optimizer.cpp/.h          → .zig

&#x20;  Kazanım: C3/C4 tipi Vulkan barrier hataları

&#x20;           comptime layout state machine ile imkansız



3\. vulkan\_initializer.cpp/.h      → .zig

&#x20;  Kazanım: C2/C8 tipi layer/LUID sorunları

&#x20;           Vulkan-zig spec binding



4\. gpu\_resource\_manager.cpp/.h    → .zig

&#x20;  Kazanım: C9 tipi device sorunları defer ile



5\. frame\_profiler.cpp/.h          → .zig

&#x20;  Kazanım: B14 tipi hot-path allocation

&#x20;           comptime sabit boyutlu buffer



6\. gpu\_query\_timing.cpp/.h        → .zig

&#x20;  Kazanım: B17 tipi try/catch yok, !T ile hata

```



Zig GPU kodu örüntüsü:



```zig

pub const ExternalMemoryBridge = struct {

&#x20;   device: vk.Device,

&#x20;   image\_pool: \[3]PoolSlot,



&#x20;   pub fn deinit(self: \*ExternalMemoryBridge) void {

&#x20;       // B10, B13, B16 — sıra garantili, otomatik

&#x20;       defer for (\&self.image\_pool) |\*s| s.deinit(self.device);

&#x20;       defer vk.destroySemaphore(self.device, self.gl\_sync\_semaphore, null);

&#x20;   }

};

```



Tamamlanan (Faz 2 — vulkan_initializer):

\`\`\`
✅ vulkan_initializer.zig — tam implementasyon
✅ vulkan_init_zig.lib — static lib build hedefi
✅ C++ → Zig entegrasyon (initialize/shutdown delegate)
✅ Runtime doğrulama:
   [VulkanZig] Instance created
   [VulkanZig] Selected: vendorID=0x1002
   [VulkanZig] Device created, keyed_mutex=true
   [Vulkan] Zig init OK, vendor=0x1002
✅ ExternalMemoryBridge 3 image + 3 semaphore OK
✅ 3 headless frame, exit 0
✅ vulkan_initializer.cpp 392 → 54 satır (%86 küçülme)
✅ Dead kod silindi: create_instance, select_device,
   create_device, detect_vendor, check_required_extensions
✅ has_extension() → Zig delegate
✅ Runtime: exit 0, 3 headless frame
\`\`\`

Tamamlanan (Faz 2 — external_memory_bridge):

\`\`\`
✅ external_memory_bridge.zig — tam implementasyon
   - create_vulkan_image (D3D11 NT handle import)
   - initialize_gl_target_pool (B13 rollback dahil)
   - GL sync semaphore export (per-slot Win32 handle)
   - get_frame_images (texture cache + pool invalidation)
   - get_staging_memory reverse lookup
   - shutdown (double-shutdown guard, NT handle cleanup)
   - 5 headless frame, exit 0, VUID yok
\`\`\`

\`\`\`
✅ external_memory_bridge.cpp → Zig delegate
   - 563 → 84 satır (%85 küçülme)
   - ext_bridge_zig.lib CMake'e bağlandı
   - Runtime: [ExtBridgeZig] GL target pool: 1920x1080
   - Runtime: [ExtBridgeZig] GL sync semaphores created
   - 5 headless frame, exit 0, VUID yok

✅ Faz 2 TAMAMLANDI (15.06.2026)
   Toplam küçülme:
     vulkan_initializer.cpp:     392 → 54  satır
     external_memory_bridge.cpp: 563 → 84  satır
     ffi_bridge.c:               silindi
\`\`\`

Sonraki hedef: Faz 3 — copy_optimizer.zig (v0.8)

\`\`\`
  En karmaşık: Vulkan submit, keyed mutex, timeline semaphore,
  GL fence, binary semaphore pool
\`\`\`



\---



\### v0.8 — Zig Faz 3: Capture ve Audio (2027 Q2, \~1-2 Ay)



\*\*Hedef:\*\* `src/pipeline/capture/` ve `src/pipeline/audio/` Zig



```

src/pipeline/capture/capture\_dxgi.cpp/.h    → .zig

src/pipeline/capture/gpu\_resource\_manager   → .zig (zaten v0.7'de)

src/pipeline/audio/wasapi\_capture.cpp/.h    → .zig

```



Zigwin32 gereksinimleri:

```

zigwin32.graphics.dxgi          → DXGI capture

zigwin32.graphics.direct3d11    → D3D11 texture

zigwin32.media.audio            → WASAPI

```



Capture Zig örüntüsü:



```zig

pub const DxgiCapture = struct {

&#x20;   keyed\_mutex: \*dxgi.IDXGIKeyedMutex,



&#x20;   pub fn acquireFrame(self: \*DxgiCapture, timeout\_ms: u32) !Frame {

&#x20;       // B6 — keyed mutex yapısal garanti

&#x20;       \_ = try self.keyed\_mutex.AcquireSync(0, timeout\_ms);

&#x20;       errdefer \_ = self.keyed\_mutex.ReleaseSync(0); // hata durumunda geri ver

&#x20;       defer  \_ = self.keyed\_mutex.ReleaseSync(1);   // başarıda Vulkan'a ver

&#x20;       // ...

&#x20;   }

};

```



\---



\### v1.0 — Release (2027 Q3)



\*\*Dil dağılımı hedefi:\*\*



```

C++    %30  (UI — Qt6 zorunluluğu)

Rust   %20  (Orchestrator — değişmez)

Zig    %45  (GPU + Capture + Audio + FFI)

Python  %2  (veya sıfır — build.zig ile)

CMake   %3  (Qt6 için zorunlu kalır)

```



\*\*Kalite hedefleri:\*\*



```

Fable 5 tarama: kritik bug = 0

Vulkan validation hatası: 0

AgentShield: A (90+)

Rust testleri: 100% pass

ABI assert: her dilde aktif

```



\---



\### v2.0+ — UI Evrimi (2028+, Opsiyonel)



İki seçenek — hangisi seçilirse seçilsin v1.0 bitmeden başlanmaz:



\*\*Seçenek A — QML + Zig (düşük risk)\*\*



```

Qt6 C++ → sadece QML engine + ince köprü

UI      → QML (JavaScript benzeri, öğrenmesi kolay)

Pipeline → tamamen Zig

İletişim → C ABI: QML ↔ Zig

```



\*\*Seçenek B — Dear ImGui + Zig (tam bağımsızlık)\*\*



```

Qt6 tamamen kaldırılır

UI     → Dear ImGui (Zig binding: cimgui)

Render → Vulkan swapchain (zaten mevcut)

Boyut  → \~15MB (Qt6: \~150MB)

```



\*\*Seçenek C — Mevcut Qt6 kalır\*\*



```

v1.0 mimarisi korunur

UI bakımı C++ ile devam eder

Zig yalnızca pipeline katmanında

```



\---



\## Build Sistemi Evrimi



```

Şimdi (v0.5.2):

&#x20; CMakeLists.txt + scripts/build.py + Cargo + CMakePresets.json + .bat

&#x20; → 5 mekanizma, 357 satır Python



v0.5.2 Sprint 2 (T2+T3):

&#x20; scripts/build.py → \~30 satır

&#x20; justfile eklenir

&#x20; → 4 mekanizma ama justfile tek arayüz



v0.6 (Zig Faz 0):

&#x20; build.zig eklenir — C++ dosyalarını da derler

&#x20; → hybrid: build.zig + Cargo + CMake (Qt6 için)



v1.0:

&#x20; build.zig dominant

&#x20; CMake yalnızca Qt6 için

&#x20; Python script silinir

&#x20; → 2 mekanizma: build.zig + CMake(Qt6)



v2.0 (Qt6 kaldırılırsa):

&#x20; build.zig tek sistem

&#x20; → 1 mekanizma

```



\---



\## Schema-First Geliştirme (Uzun Vade)



Şu an cbindgen Rust → C üretimi yapıyor. Uzun vadede tek schema tüm dillere yayılır:



```

schema/reji.fbs  (FlatBuffers schema — tek kaynak)

&#x20;     ↓ flatc otomatik üretim

&#x20;     ├── src/ffi/ffi\_auto.h      (C/C++ için)

&#x20;     ├── src/orchestrator/...    (Rust için)

&#x20;     └── src/pipeline/...        (Zig için)

```



FlatBuffers zero-copy, hot-path için ideal. v1.0 sonrasında değerlendirilebilir.



\---



\## Test Stratejisi



```

Dil-içi:

&#x20; Rust  → cargo test (24/24 ✅)

&#x20; C++   → Google Test (eklenecek)

&#x20; Zig   → zig test (Faz 0'dan itibaren)



ABI sınırı (en kritik):

&#x20; sizeof\_check.cpp  → CI'da zorunlu

&#x20; Rust const\_assert → her build'de

&#x20; Zig comptime      → Faz 0'dan itibaren



Entegrasyon:

&#x20; Pipeline smoke test    (frame üretiliyor mu?)

&#x20; Self-healing zinciri   (C1 fix sonrası)

&#x20; Vulkan validation      (VUID = 0)

&#x20; Fable 5 periyodik tarama (her major versiyon öncesi)

```



\---



\## Özet Zaman Çizelgesi



```

2026 Q2-Q3  v0.5.2  Sprint 2-4 (C5-C9, T2-T3)

2026 Q4     v0.6    Zig Faz 0+1 (FFI, build.zig pilot)

&#x20;            ↑ Zig 1.0 bekleniyor

2027 Q1     v0.7    Zig Faz 2 (GPU pipeline)

2027 Q2     v0.8    Zig Faz 3 (Capture + Audio)

2027 Q3     v1.0    Release — C++/Rust/Zig hybrid

2028+       v2.0    UI evrimi (QML veya ImGui, opsiyonel)

```



\---



\## Karar Noktaları



Her faz başında şu soruları sor:



```

Faz 0 sonunda:

&#x20; → build.zig C++ dosyalarını derleyebildi mi?

&#x20; → ffi\_bridge.zig tüm testleri geçti mi?

&#x20; Hayır → C++ kalır, risk yok



Faz 1 sonunda:

&#x20; → ffi\_bridge.c silindi, C oranı 0 mu?

&#x20; → comptime assert'ler aktif mi?

&#x20; Hayır → ffi\_bridge.c geri gelir



Faz 2 sonunda:

&#x20; → GPU pipeline tüm Fable 5 testlerini geçti mi?

&#x20; → Vulkan validation hatası 0 mı?

&#x20; Hayır → C++ versiyona geri dön



v1.0 sonunda:

&#x20; → UI evrimi başlansın mı?

&#x20; → Qt6 kalsın mı?

&#x20; Karar: performans, ekip, ekosistem durumuna göre

```



\---



*Bu belge C:\reji-studio\docs\ROADMAP.md olarak kaydedilmeli.*
*Her major versiyon sonrası güncellenmeli.*

---

# Modülerlik ve Endüstri Uyumluluğu Yol Haritası

Bu bölüm, Reji Studio'yu tek-kullanımlık bir araçtan modüler, genişlemeye açık
ve endüstri standardı araçlarla uyumlu bir platforma dönüştürme planını tanımlar.

## Faz 0 — Temel Hazırlık

- [x] docs/FFI_CONTRACT.md tamamlandı (2026-07-01)
- [x] Pipeline::Impl God Object refactoring — 9 alt sisteme ayrıldı
      (FramePacer, MetricsSubsystem, AudioSubsystem, OutputSubsystem, CommandRouter,
      EncodeSubsystem, GpuInteropSubsystem, CaptureSubsystem, RecoveryCoordinator),
      sıfır davranışsal regresyonla tamamlandı (2026-07-02)

## Faz 1 — OBS-WebSocket Protokol Uyumluluğu

- [x] obs-websocket v5 protokol alt kümesi araştırması (hangi request/response tipleri gerekli)
- [x] Mevcut ws_server.rs üzerine protokol adaptör katmanı (Aşama 1-5)
- [x] Temel komutlar: GetSceneList, SetCurrentProgramScene, StartStream, StopStream, GetStreamStatus
- [x] Stream Deck veya Companion ile bağlantı doğrulama (JSON VE msgpack modları kütüphane
      seviyesinde canlı doğrulandı — obs-websocket-js json+msgpack varyantları ve simpleobsws
      (msgpack-only) uçtan uca çalışıyor; fiziksel Stream Deck donanımı / gerçek Companion
      kurulumu hâlâ test EDİLMEDİ, mevcut değil)

> **Aşama 7 — msgpack serileştirme ✅ (2026-07-04):** `obswebsocket.msgpack` alt-protokolü tam
> destekli (`rmp-serde` + binary frame + subprotocol seçimi; `WireMode` ile tek mantık/iki kodlama).
> Node-varsayılan obs-websocket-js (Companion'ın bağımlılığı) ve simpleobsws artık bağlanıyor.
> Kalan açık yalnızca fiziksel donanım/gerçek Companion doğrulaması.

## Faz 2 — RTMP Çıkışı

- [ ] ITransport implementasyonu: RtmpTransport
- [ ] librtmp veya benzeri kütüphane entegrasyonu
- [ ] SettingsDialog'a RTMP URL/key alanları
- [ ] Test: Twitch/YouTube'a gerçek yayın testi

## Faz 3 — Çoklu Kaynak Mimarisi (ISource)

- [ ] ISource interface tasarımı (next_frame, metadata, lifecycle)
- [ ] Scene composition — birden fazla ISource'un tek frame'e birleştirilmesi
- [ ] İlk implementasyonlar: WebcamSource (DirectShow/Media Foundation),
      ExistingDesktopSource (mevcut WGC/DXGI'nin ISource'a uyarlanması)
- [ ] UI: Sahne düzenleyici, kaynak ekleme/kaldırma

## Faz 4 — NDI Desteği

- [ ] NDI SDK entegrasyonu
- [ ] NdiInputSource (ISource implementasyonu)
- [ ] NdiOutputTransport (ITransport implementasyonu)
- [ ] Test: vMix/OBS ile NDI üzerinden karşılıklı görüntü alışverişi

## Faz 5 — Zig Global State Tam Çözümü

- [ ] external_memory_bridge.zig — state'i instance-level struct'a taşı
- [ ] vulkan_initializer.zig — aynı
- [ ] C++ tarafında opak pointer API'sine geçiş
- [ ] Çoklu ISource/ITransport senaryosunda test

## Durum Takibi

Her faz tamamlandığında docs/SESSION_NOTES.md'ye özet eklenir, bu dosyada
ilgili checkbox işaretlenir.
