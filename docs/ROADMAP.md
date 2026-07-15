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

> **Kimlik doğrulama ✅ (V8/I8, 2026-07-11):** obs-websocket v5 auth (challenge/salt/
> SHA256, sabit-zamanlı). Parola AYARLIYKEN zorunlu (yanlış→4009, doğrulanmamış
> Request/legacy `{cmd}`→4007); parola YOKken toleranslı davranış bit-aynı. Parola
> SettingsDialog'dan (`rj_set_ws_password`). control.html obs-auth'a yükseltildi.
> **Faz 1 auth "yok" niteliği artık geçersiz** — auth opsiyonel (OBS gibi) ve
> uygulanmış durumda. Tarayıcı/gerçek-istemci davranış onayı kullanıcıda.

## Faz 2 — RTMP Çıkışı

- [x] (Aşama 1) ITransport soyutlaması gerçek: SrtTransport + create() faktörü —
      RtmpTransport öncesi temel (commit 3bd4657)
- [x] ITransport implementasyonu: RtmpTransport (C++ ince sarmalayıcı +
      Zig çekirdek: FLV muxing, happy_eyeballs; yalnız H.264 video — AAC ses
      yolu yok, RTMPS/TLS YOK [karar A: düz rtmp://, NO_CRYPTO])
- [x] librtmp entegrasyonu: OBS librtmp çekirdeği vendorlandı
      (third_party/librtmp, LGPL 2.1, commit 30d3b89b) + zig build rtmp
- [x] SettingsDialog'a RTMP URL/key alanları (protokol seçimi SRT/RTMP,
      key Password echo, QSettings kalıcılığı)
- [ ] Test: Twitch/YouTube'a gerçek yayın testi — YEREL gerçek ingest DOĞRULANDI
      (reji_app gerçek NVENC → ffmpeg RTMP sunucusu: 8 sn, h264 1080p,
      341 kare tam decode); Twitch/YouTube denemesi stream key bekliyor.
      Düz rtmp://'nin platformlarca kabulü ancak bu testle kesinleşir
      (reddedilirse "Aşama 3: RTMPS/mbedTLS" ayrı iş).

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

## Mimari Not — Dağıtık Mimari Hedefi (orta-uzun vade)

Projenin orta-uzun vadeli hedefi arasında dağıtık mimariye geçiş (çoklu
makine üzerinde capture/encode/kontrol ayrımı) var. Bu not, o noktaya
gelindiğinde hangi teknoloji kararının değerlendirileceğini kayda geçirir.

**Kontrol düzlemi (komutlar, durum, metrik, sağlık sinyalleri):**
Düğümler arası iç kontrol trafiği ihtiyacı doğduğunda **gRPC (Rust
`tonic`)** ile mevcut WS/msgpack deseninin (`ws_server.rs`'nin obs-websocket
dışı, iç kullanım için genişletilmiş hali) karşılaştırılması gerekiyor.
Şu an için değerlendirme notları:

- gRPC'nin kazancı: protobuf ile şema evrimi — dağıtık düğümlerde
  versiyon uyuşmazlığı riski, in-process FFI'da (C++↔Rust) olmayan bir
  sorun.
- C++ tarafında `grpc` kütüphanesi ağır bir bağımlılık (protobuf codegen,
  transitive dependency yükü) — mevcut minimal FFI felsefesiyle çelişir.
- Zig tarafında gRPC ekosistemi olgun değil; muhtemelen C++ grpc client'ına
  ek bir C ABI köprüsü (`ffi_bridge`/`external_memory_bridge` tarzı)
  yazmak gerekir.
- Alternatif: mevcut WS/msgpack + oturum-düzeyi auth (I8) + close-code
  altyapısını iç kontrol kanalı olarak genişletmek — daha az yeni bağımlılık,
  msgpack + Rust `serde` ile makul tip güvenliği.

**Medya taşıma — kesin karar, tartışmaya kapalı:**
Kare/ses verisi **her durumda NDI (Faz 4) ve SRT/RTMP'de (Faz 2) kalır,
gRPC'ye taşınmaz.** gRPC streaming teknik olarak mümkün olsa da, NDI'nin
zaten çözdüğü sorunu (codec-farkında, düşük gecikmeli, ağ/donanım optimize
video taşıma) daha kötü yeniden icat etmek olur.

**Şimdi implemente edilmiyor:** Faz 3 (ISource) henüz tek-süreç çoklu-kaynak
aşamasında; gerçek çoklu-makine ihtiyacı yok. Bu not, ihtiyaç doğduğunda
başlangıç noktası olması için kayda geçirilmiştir — YAGNI ilkesi gereği
şimdiden bir prototip veya bağımlılık eklenmemiştir.

## Gelecek Fikir — Makro Motoru (henüz taahhüt edilmedi)

Projenin ilk planlama aşamasından (arşivlenmiş `docs/archive/progress.md`)
taşınan bir fikir — hiç implemente edilmedi, şu an aktif roadmap'in
parçası değil, yalnızca kaybolmaması için kayda geçirildi.

**İçerik:** Kullanıcı tanımlı tetikleyicilerle (hotkey, zamanlayıcı,
event-driven — örn. bir healing aksiyonu tetiklendiğinde) çalışan bir
makro sistemi. Örnek aksiyonlar: sahne değiştir, bitrate ayarla, kayıt
aç/kapat.

**Neden şimdi değil:** Reji Studio'nun kontrol yüzeyi zaten obs-websocket
protokolü (Faz 1) üzerinden Stream Deck/Companion gibi araçlara açık —
bu araçların çoğu zaten kendi makro/tetikleyici sistemlerine sahip. Yeni
bir dahili makro motoru eklemeden önce, mevcut obs-websocket yüzeyinin bu
ihtiyacı zaten karşılayıp karşılamadığı değerlendirilmeli (YAGNI —
V9/J-serisinde defalarca uygulanan ilke).

**Olası tasarım (ileride değerlendirilecek, şimdi karar değil):**
- Tetikleyiciler: hotkey (global), zamanlayıcı, healing-event (RuleEngine
  ile entegrasyon ihtimali — I33'ün action-queue mimarisiyle örtüşebilir,
  ayrı bir mekanizma icat etmeden mevcut event akışına eklenebilir mi
  değerlendirilmeli).
- Kalıcılık: JSON config (`rules.json` deseniyle tutarlı bir format
  düşünülebilir).
- Kapsam: Bu bir "ne zaman" veya "nasıl" kararı değil — yalnızca fikrin
  kaybolmaması için buradaki not. Faz 3 (ISource) veya sonrası bir
  noktada, gerçek kullanıcı talebi/ihtiyacı doğarsa gündeme alınabilir.

## Gelecek Özellikler — Sinerjik Değerlendirme (öncelik sıralı)

Aşağıdaki altı fikir, projenin zaten inşa ettiği mimari varlıklardan
(`RuleEngine`/I33 action-queue, `ITransport` soyutlaması, obs-websocket
protokolü/I8, `MetricState`/I14) yola çıkılarak değerlendirildi — hiçbiri
taahhüt değil, hepsi öncelik sırasına konmuş birer aday. Sıra; maliyet,
bağımlılık ve mevcut altyapıyla örtüşme derecesine göre belirlendi.

### 1. CoPilot aksiyon açıklaması ✅ IMPLEMENTE EDİLDI (2026-07-15)

> **Durum:** `feature/copilot-action-explanation` dalında tamamlandı.
> **Not (maliyet düzeltmesi):** "mevcut event'e bir alan eklemek yeterli"
> varsayımı Faz 0'da kısmen çürütüldü — event üçlüyü taşımıyordu, `rule_id`
> yarı yola kadar gidip FFI öncesi duruyordu, metrik/değer/eşik ise hiç
> yakalanmıyordu. Gerçek maliyet 6 katmana dokunan bir ABI değişikliği
> (`RjActionEvent` 24B→36B, `RJ_FFI_VERSION` 0x00020000) oldu — yine de
> düşük-orta ve mimari olarak temiz (veri üretim anında hazır, sınırdan
> yapılandırılmış/sayısal geçiyor). Bkz. `docs/talimatlar/TALIMAT_OZELLIK1_COPILOT_ACIKLAMA.md`.

**İçerik:** CoPilot'ta bir pending aksiyon (veya AutoPilot/Assist'te
uygulanan bir aksiyon) göründüğünde, hangi kuralın hangi metrik eşiğini
aştığı için tetiklendiğini kullanıcıya göstermek (örn. "GPU sıcaklığı
87°C, eşik 85°C").

**Neden yüksek öncelik:** Healing Plumbing turunda bulduğumuz gerçek bir
sorunu (`gpu_thermal_restore` kuralının metrik-stub yüzünden sessizce
hep-true kalıp anlamsız pending'ler üretmesi) doğrudan hedefliyor —
kullanıcı "neden bu öneriliyor" bilgisine sahip olsa, böyle bir anormali
GUI'de anında fark eder. `ActionEvent` mimarisi (I33) bu bilgiyi zaten
Rust tarafında taşıyor, yalnızca UI'a yansıtılmıyor — yeni mimari
gerekmez, mevcut event'e bir alan eklemek yeterli.

**Bağımlılık:** Yok. **Sinerji:** I33 ile birebir.

### 2. Self-healing durumunu obs-websocket üzerinden dışa açmak (öncelik: yüksek, maliyet: orta)

> **Durum — Aşama A ✅ IMPLEMENTE EDİLDI (2026-07-15):**
> `feature/healing-ws-vendor-events` dalında read-only healing yayını
> tamamlandı. Healing event'leri obs-websocket **VendorEvent** (op 5) olarak
> yayınlanıyor (`vendorName="reji-studio"`): `HealingModeChanged`,
> `HealingActionApplied`, `HealingActionPending`, `HealingActionInvalidated`.
> **Faz 0 bulgusu (talimat varsayımını düzeltti):** Vendor Event mekanizması
> obs-websocket v5'te VAR ama bu projede op 5 Event altyapısı HİÇ kurulmamıştı;
> "I8 zarfını genişlet" alternatifi çıkmaz sokaktı (op'suz metrik gövdesi
> msgpack istemcilerine — Companion — ulaşmıyor). VendorEvent seçildi (protokol-
> idiomatik + msgpack'e ulaşır). **ABI/FFI değişikliği YOK** (Özellik#1'in 6
> katmanlı maliyetinin aksine — `RjActionEvent` yalnız okundu). Auth: yayın
> yalnız doğrulanmış oturuma gider; bu, I8'den beri var olan metrik yayın
> sızıntısını da kapattı ("tek düzeltme, iki kazanım"). **Aşama B (uzaktan
> onay mutasyonu)** ayrı talimata bırakıldı (kademeli güvenlik). Bkz.
> `docs/talimatlar/TALIMAT_OZELLIK2_HEALING_WS.md` + `FFI_CONTRACT.md` "WS Event
> Yüzeyi".

**İçerik:** Healing event'lerini (mod değişimi, pending onay belirdi,
aksiyon uygulandı) WS event olarak yayınlamak — bir Stream Deck/Companion
paneli healing durumunu gösterebilir veya kullanıcı kendi butonuyla
pending'i onaylayabilir.

**Neden yüksek öncelik:** Projenin iki olgun alt sistemini (I8'in
obs-websocket protokol uyumluluğu + I33'ün pending-onay mimarisi)
birleştiriyor — pazarda benzeri az bulunan bir kombinasyon ("self-healing,
endüstri-standart araç zincirinle konuşuyor"). İkisi de zaten kanıtlanmış,
olgun altyapı.

**Bağımlılık:** Yok (I8 ve I33 zaten tamamlandı). **Sinerji:** I8 + I33
kesişimi.

### 3. SQLite healing-log (öncelik: orta-yüksek, maliyet: orta)

**İçerik:** Her healing kararını (hangi kural, hangi metrik değeri, hangi
aksiyon, ne zaman) kalıcı olarak SQLite'a kaydetmek.

**Neden değerli:** V8/V9 boyunca defalarca (J10, Healing Plumbing turu)
"bu kasıtlı mı tutarsız mı" sorusunu yalnızca kod okuyarak çözmek zorunda
kaldık — çalışan sistemde gözlemleyemedik. Kalıcı bir log, bu tür
soruları dakikalar içinde bir SQL sorgusuyla cevaplanabilir hale getirir.
Ayrıca madde 6'nın (kalibre edilmiş eşikler) doğal ön koşulu.

**Dikkat:** Yazma işlemi hot-path'te olmamalı — J8'in dersi (ayrı/batch
mekanizma, AGENTS.md'nin bloklayan-sorgu yasağı) burada da geçerli.

**Bağımlılık:** Yok, ama madde 6'yı besliyor. **Sinerji:** RuleEngine
şeffaflığı + gelecekteki kalibrasyon.

### 4. SRT/RTMP arası otomatik failover (öncelik: orta, maliyet: orta)

**İçerik:** Ağ kalitesi (paket kaybı, RTT — SRT'nin doğal istatistikleri,
zaten `MetricState`'te izleniyor) eşiği aştığında, aktif transport'u
otomatik olarak yedek hedefe (örn. SRT → RTMP) geçirmek.

**Neden değerli:** Faz 2'de kurulan `ITransport` soyutlaması şu an yalnızca
başlangıçta hangi transport'un kullanılacağını seçiyor, runtime geçiş
yok. Üç mevcut parçayı (`ITransport`, `MetricState`, `RuleEngine` action
çerçevesi) birleştirmek, yeni bir mimari icat etmeden gerçek bir
profesyonel-yayıncılık özelliği (otomatik failover) sağlar.

**Maliyet notu:** Transport geçişinin kendisi bir state-machine gerektirir
(hangi anda kesip hangi anda başlatılacağı, izleyici tarafında kesintiyi
en aza indirme) — "orta" tahmini bu karmaşıklığı içeriyor.

**Bağımlılık:** Yok. **Sinerji:** ITransport + MetricState + RuleEngine
üçlüsü.

### 5. Kalibre edilmiş temel çizgi (statik eşikler yerine) (öncelik: orta, maliyet: orta-yüksek)

**İçerik:** Sistem başlangıçta (ilk birkaç dakika) donanıma özgü normal
bitrate/sıcaklık/fps aralığını öğrenip `rules.json`'daki sabit eşikleri
buna göre otomatik kalibre etmek.

**Neden değerli:** J9/J10/Healing Plumbing serisinde tekrar tekrar aynı
bug sınıfıyla karşılaştık — `rules.json`'daki kör sabit eşikler (örn.
`gpu_temp_c < 70`), bir metrik kaynağı stub çıkınca (0 dönünce) tüm
mantığı bozuyor. Kalibrasyon, bu bug sınıfının kökünü kurutur; ayrıca
daha akıllı, donanıma duyarlı bir healing sağlar.

**Bağımlılık:** Madde 3'ün (SQLite log) kalıcılık altyapısıyla doğal
olarak birleşir — ondan önce yapılabilir ama beraber daha güçlü.

**Sinerji:** RuleEngine + SQLite healing-log.

### 6. Sanal kamera + OBS sahne import (öncelik: düşük, Faz 3'e bağımlı — bağımsız planlanmamalı)

**İçerik:** (a) Reji'nin çıktısını DirectShow sanal kamera olarak
Teams/Zoom/OBS'e sunmak. (b) Bir OBS kullanıcısının sahne/kaynak
düzenini (JSON) Reji'ye aktarmak.

**Neden düşük öncelik / Faz 3'e bağımlı:** İkisi de gerçek bir çoklu-kaynak/
sahne-kompozisyon mimarisi gerektiriyor. Şu an Reji'nin "sahne" kavramı
yalnızca obs-websocket uyumluluğu için var (`rj_push_scene_names` gibi),
arkasında gerçek bir kompozisyon mimarisi yok — Faz 3 (ISource) bunu inşa
edecek. Bu iki fikri Faz 3'ten önce bağımsız olarak planlamak yanıltıcı
olur: sanal kamera "bir çıkış sink'i daha" olarak Faz 3 sonrası doğal
oturur; OBS sahne import'un anlamlı bir hedefi (nereye parse edileceği)
Faz 3 olmadan yok.

**Bağımlılık:** Faz 3 (ISource) — henüz başlanmadı.

---

**Kapsam dışı bırakılan/arşivde kalan fikirler (bu değerlendirmenin
parçası ama eklenmedi):** Plugin sandbox (Extism/WASM) — gerçek
üçüncü-taraf plugin kullanım kanıtı olmadan erken, YAGNI. OSD overlay —
düşük öncelik, bağımsız, veri kaynağı (`MetricState`) zaten hazır ama acil
değil; istenirse ayrı eklenebilir.

---

## Farklılaşma Stratejisi — Diğer Yayıncılık Yazılımlarına Karşı

Bu bölüm, tekil özellik maddelerinden farklı: **stratejik konumlandırma**
soruları. Amaç "OBS'in yaptığını doğru yap" değil, "OBS'in yapmadığı/
yapamayacağı neyi yapabiliriz" sorusuna cevap aramak. Üç sütun +
düşük-öncelikli bir gelecek fikri.

### Sütun 1 — "Stream'inizin kara kutusu" (şeffaflık, düşük risk)

**Tez:** Diğer yayın yazılımlarının otomatik özellikleri kara kutu —
bir şey değiştiğinde kullanıcı genelde *neden* olduğunu bilmez. Reji'nin
gerçek bir kural motoru (`RuleEngine`) olduğundan, bu şeffaflığı üç
katmanda sunabiliriz:

- **Anlık açıklama** — ✅ implemente edildi ("Gelecek Özellikler" madde 1,
  CoPilot aksiyon açıklaması).
- **Kalıcı kayıt** — "Gelecek Özellikler" madde 3 (SQLite healing-log).
- **Kalibrasyon** — "Gelecek Özellikler" madde 5 (kalibre edilmiş
  eşikler).

**Neden farklılaştırıcı:** Rakiplerin "otomatik" özellikleri genelde tek
bir if-else; Reji'de denetlenebilir, açıklanabilir bir karar zinciri var.
Bu üç madde zaten roadmap'te — bu sütun onları tek bir marka/vizyon
altında birleştiriyor, yeni bir mühendislik maddesi eklemiyor.

### Sütun 2 — Hibrit-GPU laptop niş konumlandırması (pazarlama + doğrulama)

**Tez:** WGC/DXGI keşfi, keyed-mutex senkronizasyonu, K1-K7 Vulkan/GL
interop turu — bunların hepsi özellikle **AMD iGPU + NVIDIA dGPU'lu
gaming laptop'lar** (ROG/Legion/Predator vb.) için optimize edilmiş
mühendislik. Bu, "genel amaçlı yayın yazılımı" olarak OBS'le rekabet
etmek yerine, **"hibrit-GPU laptop'ta en düşük gecikmeli/en az kare
düşüren yayın yazılımı"** iddiasıyla net bir niş kullanıcı segmentine
hitap edebilir.

**Somut adımlar (mühendislik değil, ölçüm/mesajlaşma):**
- Aynı donanımda Reji vs OBS karşılaştırmalı kare-düşürme/gecikme
  ölçümü (mevcut `MetricState`/`FrameProfiler` altyapısı zaten bu veriyi
  topluyor — yeni bir ölçüm sistemi gerekmez).
- Bu ölçümlerin belgelenmesi (README/marketing materyali) — mühendislik
  kapsamı dışı, ayrı bir görev.

**Öncelik:** Düşük-orta — mühendislik tarafı zaten yapılmış durumda
(I23, K1-K7), yalnızca ölçüm+mesajlaşma eksik.

### Sütun 3 — Paylaşılabilir kural setleri (yeni, somut özellik)

**Tez:** `rules.json` zaten insan-okunur, versiyonlanabilir bir format.
Diğer yazılımlarda "otomatik ayarlar" GUI checkbox'larıdır — versiyonlanamaz,
paylaşılamaz. Reji'nin kural setleri teorik olarak:
- Git'te versiyonlanabilir,
- Topluluk arasında paylaşılabilir ("3070 dizüstü için optimize edilmiş
  kural seti"),
- "Infrastructure as code" mantığının yayıncılığa uygulanması olabilir.

**Somut özellik:** Kural setleri için basit bir dışa aktar/içe aktar
UI'ı (SettingsDialog'a "Kural Setini Paylaş/İçe Aktar" gibi bir seçenek).

**Risk notu:** Plugin sandbox fikrinden (arşivde) temelde farklı ve çok
daha düşük risk — JSON, çalıştırılabilir kod değil, mevcut `rj_reload_rules`
(I24'te sertleştirilmiş) yolunu zaten kullanabilir.

**Bağımlılık:** Yok. Küçük-orta maliyetli, bağımsız bir özellik.

---

## Gelecek Fikir (henüz değerlendirilmedi, yüksek risk/belirsiz talep)

### Uzaktan/işbirlikli prodüksiyon

**Fikir:** "Gelecek Özellikler" madde 2'nin (healing durumunu
obs-websocket'e açmak) bir adım ötesi — bir prodüktörün, yayıncının
fiziksel olarak yanında olmadan, healing pending'lerini uzaktan
izleyip onaylayabildiği bir web dashboard.

**Neden şimdi değil:** OBS/vMix gibi araçlar tek-operatörlü; küçük
prodüksiyon ekipleri (biri kamerada, biri teknik izlemede) için gerçek
bir boşluk olabilir — ama gerçek talep doğrulanmadı. Güvenlik yüzeyi
büyük (I8'deki WS auth işinin genişletilmesi, dışa açık bir dashboard).

**Durum:** Yalnızca fikrin kaybolmaması için kayda geçirildi — aktif
değerlendirme yok, önkoşul yok, taahhüt yok.

## Faz 5 — Zig Global State Tam Çözümü

- [ ] external_memory_bridge.zig — state'i instance-level struct'a taşı
- [ ] vulkan_initializer.zig — aynı
- [ ] C++ tarafında opak pointer API'sine geçiş
- [ ] Çoklu ISource/ITransport senaryosunda test

## Durum Takibi

Her faz tamamlandığında docs/SESSION_NOTES.md'ye özet eklenir, bu dosyada
ilgili checkbox işaretlenir.
