# FFI Contract — Reji Studio

Bu dosya Rust (orchestrator) ↔ C++ (pipeline) arasındaki FFI sınırının resmi sözleşmesidir.
`ffi_auto.h` cbindgen ile otomatik üretilir — bu dosya davranışsal garantileri belgeler,
header'ın kendisini tekrarlamaz.

---

## Genel Prensipler

- **Sadece veri geçer:** FFI sınırından pointer/handle/nesne referansı geçmez.
  PipelineRegistry kaldırıldıktan sonra tüm iletişim kuyruk (ArrayQueue) veya
  doğrudan değer (`int32_t`, `bool`, struct kopyası) üzerinden yapılmaktadır.
- **Panic güvenli:** Tüm `extern "C"` fn'ler `catch_unwind(AssertUnwindSafe(...))` ile
  sarılmıştır. Rust panic'i hiçbir koşulda C++ stack'ine sızmaz.
- **Non-blocking:** FFI fonksiyonları hiçbir koşulda blocking çağrı yapamaz.
  Kuyruk doluysa mesaj düşürülür ve `eprintln!` ile loglanır; çağıran askıda kalmaz.
- **ABI derleme garantisi:** `src/ffi/sizeof_check.cpp` içindeki `static_assert` +
  `offsetof` kontrolleri derleme zamanında tüm struct layout'larını doğrular.
  Rust tarafı bağımsız olarak `const_assert!` ile aynı büyüklükleri kontrol eder
  (`ffi.rs:68-69`).

### İletişim Kanalları

| Kanal | Kapasite | Yön | Öğe türü |
|-------|----------|-----|----------|
| `metric_ring` | 256 slot | C++ → Rust | `MetricSample` |
| `command_queue` | 64 slot | Rust → C++ | `RjCommand` |
| `action_queue` | 64 slot | Rust → C++ | `RjAction` |
| `ws_command_queue` | 32 slot | WS/C++ → C++ (Rust yönetiminde) | `(i32, i32)` cmd+param |

Tüm kuyruklar `crossbeam::ArrayQueue` (lock-free, SPSC güvenli).
Kapasite dolunca mesaj düşürülür; `DROPPED_ACTIONS_COUNT` / `DROPPED_WS_CMDS_COUNT`
atomik sayaçları artırılır ve `[…Queue] FULL` `eprintln!` ile loglanır (v2026-07-01+).

---

## Fonksiyon Listesi

### `rj_start_monitor()`

```c
void rj_start_monitor();
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Qt main thread — uygulama başlangıcında, `SrtOutput::init()` içinden bir kez
- **Davranış:** Tokio runtime, 16 ms'lik metrik drainer task, HealingMonitor, WebSocket
  sunucusu (port 7070 → 7071 → 7072 → 7073 fallback, yalnızca `127.0.0.1`) ve
  WS komutlarını `ws_command_queue`'ya yönlendiren bridge task'ını başlatır.
- **Idempotent:** Evet — `OnceLock::get_or_init` ikinci çağrıyı no-op yapar.
- **Panic:** `catch_unwind` ile sarılı; panic durumunda `[PANIC] rj_start_monitor` stderr'a loglanır, dönüş değeri yok.

---

### `rj_metrics_push(sample)`

```c
void rj_metrics_push(const MetricSample *sample);
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Pipeline frame thread (~60 fps sıcak yol) — `SrtOutput::send_internal()`
- **Davranış:** `sample`'ı `metric_ring`'e yazar (non-blocking push). Başarıda WS
  istemcilerine önceden ayrılmış `ws_json_buf` üzerinden JSON broadcast edilir.
- **Null / canary hatası:** `sample == null` veya `magic_head`/`magic_tail` ≠ `0xEEFF1234`
  ise sessizce atlanır — çağıran bilgilendirilmez.
- **Thread-safety:** `metric_ring` lock-free; `ws_json_buf` için `Mutex<String>` kilitlenir.
- **Kuyruk doluysa:** Drop loglanmaz (metric_ring 256 slot; 60 fps push'ta dolması beklenmez).
- **Panic:** `catch_unwind` ile sarılı.

---

### `rj_metrics_poll(out)` (V8/I14)

```c
__declspec(noinline) int rj_metrics_poll(RjMetricSample *out);
```

- **Yön:** Rust → C++ (pull). UI'ın anlık metrik göstergesi için (`MainWindow::pollMetrics`, Qt timer).
- **Çağıran thread:** UI thread (frame thread DEĞİL — bkz. AGENTS.md YASAK: `run_frame`→poll→WMI).
- **Davranış:** Agregeli `MetricState`'ten (push'un beslediği AYNI otoriter state; WS
  `GetStreamStatus` ile aynı kaynak) bir snapshot üretip `out`'a yazar. Geçici
  `metric_ring`'ten OKUMAZ — o drainer'a aittir, yarışmaz.
- **Dönüş:** `1` → `out` dolduruldu. `0` → yazılmadı (`out == null` veya FFI_STATE init
  değil). C++ tarafı `0`'da UI güncellemesini atlar (`if (rj_metrics_poll(&s) == 0) return;`).
- **Alanlar:** `fps_actual`, `bitrate_kbps` (video), `cpu_percent`, `frame_drops` (kümülatif,
  u32'e truncate), `frame_drop_pct`. MetricState'te tutulmayanlar (gpu/temp/network/mem) 0.
  `source_id=0`, canary alanları geçerli set edilir.
- **Bloklama/WMI:** Yok — yalnız atomik okuma (lock-free, non-blocking).
- **Panic:** `catch_unwind` ile sarılı; panikte `0` döner.
- **Not:** Implementasyon Rust'ta (`orchestrator/src/ffi.rs`); eski Zig stub (V8/I14'te)
  kaldırıldı — aynı sembolü iki statik kütüphanede tanımlamak LNK2005 üretir.

---

### `rj_command_drain(out, max)`

```c
int32_t rj_command_drain(RjCommand *out, int32_t max);
```

- **Yön:** Rust → C++ (C++ kuyruğu boşaltır)
- **Çağıran thread:** Pipeline frame thread — `Pipeline::run_frame()` içinden her frame
- **Davranış:** `command_queue`'dan en fazla `max` adet `RjCommand` çekip `out` buffer'ına yazar.
- **Dönüş değerleri:**

  | Durum | Dönüş |
  |-------|-------|
  | Yazılan komut sayısı | `0`–`max` |
  | `out == null` veya `max ≤ 0` | `-1` |
  | `max > 64` (overflow koruması) | `0` |
  | Panic | `-1` |
  | `FFI_STATE` başlatılmamış | `0` |

- **Güvenlik:** `out` en az `max × sizeof(RjCommand)` (= `max × 24`) byte büyüklüğünde
  olmalıdır. `max > 64` zorunlu olarak reddedilir (`SECURITY FIX`).
- **Panic:** `catch_unwind` ile sarılı; panic → `-1`.

---

### `rj_connection_lost(reason)`

```c
void rj_connection_lost(const char *reason);
```

- **Yön:** C++ → Rust
- **Çağıran thread:** SRT worker thread — `SrtOutput::Impl::worker_loop()` veya `send_internal()`
- **Davranış:** SRT kopuş nedenini event bus'a `MediaEvent::SourceDisconnected { source_id: 0 }`
  olarak iletir. `reason == null` → `"<null>"` kullanılır; geçersiz UTF-8 → `U+FFFD` ile
  değiştirilir (`CStr::to_string_lossy`).
- **Thread-safety:** `broadcast::Sender::send` thread-safe.
- **Panic:** `catch_unwind` ile sarılı.

---

### `rj_pipeline_status()`

```c
int32_t rj_pipeline_status();
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Herhangi — genellikle Qt main thread, diagnostic amaçlı
- **Davranış:** `FFI_STATE` başlatılmışsa `0`, değilse `-1` döner.
- **Thread-safety:** `OnceLock::get()` lock-free, herhangi thread'den güvenli.
- **Panic:** `catch_unwind` ile sarılı; panic → `-1`.

---

### `rj_action_dequeue(out)`

```c
int32_t rj_action_dequeue(RjAction *out);
```

- **Yön:** Rust → C++ (C++ poll yapar)
- **Çağıran thread:** Qt main thread — `MainWindow::pollHealingActions()` içinden (200 ms timer)
- **Davranış:** `action_queue`'dan bir `RjAction` çeker. Varsa `*out`'a kopyalar, `1` döner;
  boşsa `0` döner.
- **`out == null`:** `0` döner, kuyruk dokunulmaz.
- **Thread-safety:** `ArrayQueue::pop()` lock-free.
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### `rj_get_ws_port()`

```c
uint16_t rj_get_ws_port();
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Qt main thread — pipeline init sonrası log/UI için
- **Davranış:** WebSocket sunucusunun gerçek bağlandığı portu döner (`ws_server::actual_port()`).
  Sunucu henüz bind olmadıysa `0`.
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### `rj_ws_command_v2(cmd, param)`

```c
bool rj_ws_command_v2(int32_t cmd, int32_t param);
```

- **Yön:** C++ → `ws_command_queue` (programatik enjeksiyon, WebSocket istemcisi bypaslanır)
- **Çağıran thread:** Herhangi C++ thread
- **Davranış:** `(cmd, param)` çiftini `ws_command_queue`'ya yazar. Başarıda `true`,
  kuyruk doluysa `false` + `DROPPED_WS_CMDS_COUNT++` + log.
- **Cmd değerleri:** `1`=stream_start · `2`=stream_stop · `3`=scene_cut · `4`=scene_fade
- **Panic:** `catch_unwind` ile sarılı; panic → `false`.

---

### `rj_ws_command_dequeue(cmd, param)`

```c
int32_t rj_ws_command_dequeue(int32_t *cmd, int32_t *param);
```

- **Yön:** `ws_command_queue` → C++ (C++ poll yapar)
- **Çağıran thread:** Pipeline frame thread — `Pipeline::run_frame()` içinden her frame
- **Davranış:** `ws_command_queue`'dan bir komut çeker. Varsa `*cmd`/`*param`'a yazar, `1`
  döner; boşsa `0` döner.
- **`cmd == null` veya `param == null`:** `0` döner.
- **Thread-safety:** `ArrayQueue::pop()` lock-free.
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### `rj_action_approve(action_id)`

```c
int32_t rj_action_approve(uint32_t action_id);
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Qt main thread — `HealingOverlay::actionApproved` sinyalinden
- **Davranış:** Co-Pilot modunda aksiyon onayı. **Şu an stub** — her zaman `1` döner.
  Gerçek onay mekanizması implement edilmemiş (TODO).
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### `rj_set_healing_mode(mode)`

```c
bool rj_set_healing_mode(uint32_t mode);
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Qt main thread — `SettingsDialog` değişikliğinde
- **Davranış:** `HEALING_MODE` statik `AtomicU32`'yi günceller (SeqCst).
  `mode > 3` → `false` döner, değer değişmez.

  | Değer | Mod |
  |-------|-----|
  | `0` | AutoPilot — tam otomatik |
  | `1` | CoPilot — onay gerektirir |
  | `2` | Assist — öneri gösterir |
  | `3` | Manual — müdahale yok |

- **Thread-safety:** `AtomicU32::store(SeqCst)` — herhangi thread'den güvenli.
- **Panic:** `catch_unwind` ile sarılı; panic → `false`.

---

### `rj_get_healing_mode()`

```c
uint32_t rj_get_healing_mode();
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Herhangi
- **Davranış:** `HEALING_MODE` atomik değişkenini okur (SeqCst). `0`–`3` arası değer döner.
- **Thread-safety:** `AtomicU32::load(SeqCst)` — herhangi thread'den güvenli.
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### `rj_reload_rules(path)`

```c
int32_t rj_reload_rules(const char *path);
```

- **Yön:** C++ → Rust
- **Çağıran thread:** Qt main thread — kullanıcı ayarları değiştiğinde
- **Davranış:** `path` konumundan yeni `RuleEngine` yükler ve `Mutex<Option<RuleEngine>>`
  içine hot-swap yapar. `path == null` → `%USERPROFILE%\.reji\rules.json` kullanılır.
  Başarıda `1`; parse/IO hatası durumunda `0` (hata loglanır, mevcut motor korunur).
- **Thread-safety:** `Mutex<Option<RuleEngine>>` ile korunur.
- **Panic:** `catch_unwind` ile sarılı; panic → `0`.

---

### Bridge-specific (ffi_bridge.h — cbindgen dışı)

```c
int     rj_metrics_poll(RjMetricSample *out);  // Rust (ffi.rs) — MainWindow::pollMetrics, Qt timer
uint32_t rj_ffi_version(void);                 // Zig (ffi_bridge.zig) — RJ_FFI_VERSION = 0x00010000
```

- `rj_metrics_poll`: V8/I14'te Rust orchestrator'da implemente edildi (yukarıdaki kontrat).
  Eskiden Zig stub'ıydı (daima 0), o kaldırıldı.
- `rj_ffi_version`: Zig'de tanımlı (ABI sürüm sabiti).

---

## Struct ABI Sözleşmesi

Tüm layout garantileri iki katmanda doğrulanır:

1. **Derleme zamanı (C++):** `src/ffi/sizeof_check.cpp` — `static_assert(sizeof(...))` +
   `static_assert(offsetof(...))`. Aynı assert'ler `src/pipeline/pipeline.cpp:60-93` ve
   `src/pipeline/audio/wasapi_capture.h:34-36` içinde de tekrarlanır.
2. **Derleme zamanı (Rust):** `ffi.rs:68-69` — `const_assert!(size_of::<RjAction>() == 20)`
   ve `const_assert!(size_of::<RjCommand>() == 24)`.

### `RjCommand` — 24 byte

```
Rust:  #[repr(C)] struct RjCommand { cmd_type: u32, timestamp_us: u64, param_u32: u32, param_f32: f32 }
C++:   struct RjCommand (ffi_auto.h)
```

| Offset | Alan | Tür | Notlar |
|--------|------|-----|--------|
| +0 | `cmd_type` | `u32` | `RjCommandType` enum: 0=SCENE_SWITCH, 1=BITRATE_SET, 2=PREVIEW_FPS |
| +4 | *(padding)* | 4 byte | u64 hizalaması |
| +8 | `timestamp_us` | `u64` | Unix epoch, mikrosaniye (`now_us()`) |
| +16 | `param_u32` | `u32` | Komuta göre: kbps / fps / sahne no |
| +20 | `param_f32` | `f32` | Komuta göre: fps (float kopyası) veya 0.0 |

### `RjAction` — 20 byte

```
Rust:  #[repr(u32)] enum RjActionType;  #[repr(C)] struct RjAction { id, action_type, param1, param2, canary }
C++:   enum RjActionType : uint32_t;    struct RjAction (ffi_auto.h)
```

| Offset | Alan | Tür | Notlar |
|--------|------|-----|--------|
| +0 | `id` | `u32` | Monoton artan aksiyon kimliği |
| +4 | `action_type` | `u32` | `RjActionType` enum (bkz. aşağı) |
| +8 | `param1` | `i32` | Birincil parametre (kbps, %, fps) |
| +12 | `param2` | `i32` | İkincil parametre |
| +16 | `canary` | `u32` | Bütünlük kontrolü |

**`RjActionType` enum değerleri (`#[repr(u32)]` / `enum … : uint32_t`):**

| Değer | İsim | `param1` anlamı |
|-------|------|-----------------|
| 0 | `BitrateReduce` | Hedef kbps (ör. 3500) |
| 1 | `BitrateRecover` | Hedef kbps (varsayılan 6000) |
| 2 | `ScaleResolution` | Küçültme yüzdesi |
| 3 | `RestoreResolution` | — |
| 4 | `CapFps` | Maksimum FPS |
| 5 | `RestoreFps` | — |
| 6 | `LogOnly` | — (sadece loglama) |

Discriminant değerleri `sizeof_check.cpp:42-48`'deki `static_cast<uint32_t>` assert'leriyle
derleme zamanında doğrulanır.

### `RjMetricSample` / `MetricSample` — 64 byte

```
Rust:  #[repr(C)] struct MetricSample { … }
C++:   typedef MetricSample RjMetricSample;  (ffi_bridge.h)
```

| Offset | Alan | Tür | Notlar |
|--------|------|-----|--------|
| +0 | `magic_head` | `u32` | `0xEEFF1234` — canary |
| +4 | *(padding)* | 4 byte | u64 hizalaması |
| +8 | `timestamp_us` | `u64` | Unix epoch, mikrosaniye |
| +16 | `bitrate_kbps` | `u32` | Anlık SRT bitrate |
| +20 | `fps_actual` | `f32` | Gerçek encode FPS |
| +24 | `cpu_percent` | `f32` | CPU kullanım yüzdesi |
| +28 | `frame_drops` | `u32` | Son periyottaki drop sayısı |
| +32 | `frame_drop_pct` | `u32` | Drop yüzdesi \[0–100\] |
| +36 | `gpu_temp_c` | `i16` | GPU sıcaklığı °C (şu an 0 — stub) |
| +38 | `cpu_temp_c` | `i16` | CPU sıcaklığı °C (şu an 0 — stub) |
| +40 | `memory_usage_pct` | `u32` | Bellek kullanım yüzdesi \[0–100\] |
| +44 | `cpu_load_pct` | `u32` | CPU yük yüzdesi \[0–100\] |
| +48 | `gpu_load_pct` | `u32` | GPU yük yüzdesi \[0–100\] |
| +52 | `network_rtt_ms` | `u16` | RTT milisaniye |
| +54 | `network_loss_pct` | `u8` | Paket kayıp yüzdesi \[0–100\] |
| +55 | `source_id` | `u8` | `0`=video · `1`=audio |
| +56 | `magic_tail` | `u32` | `0xEEFF1234` — canary |
| +60 | *(trailing padding)* | 4 byte | Struct toplam = 64 byte (u64 hizalaması) |

**Canary doğrulama:** `MetricSample::is_valid()` hem `magic_head` hem `magic_tail`'ın
`0xEEFF1234` (= `MetricSample_MAGIC` = `4009693748`) olduğunu kontrol eder.
İkisi de yanlışsa `rj_metrics_push` sample'ı atar ve `warn!` logu yazar.

---

## Başlatma Sırası

```
C++ main()
  └─ Qt app + MainWindow ctor
       ├─ SrtOutput::init()
       │    └─ rj_start_monitor()           ← Tokio + WS sunucu + HealingMonitor başlar
       ├─ Pipeline::init()
       │    └─ WGC/DXGI capture, NVENC init
       └─ frame_thread_->start()            ← Sadece Pipeline::init() başarılıysa
            └─ run_frame() loop
                 ├─ rj_command_drain()      ← Her frame: HealingCommand polling
                 ├─ rj_ws_command_dequeue() ← Her frame: WS→pipeline komut polling
                 └─ rj_metrics_push()       ← ~1 s'de bir (SrtOutput throttle ile)

Qt timer (200 ms)
  └─ MainWindow::pollHealingActions()
       └─ rj_action_dequeue()               ← HealingOverlay için aksiyon polling
```

---

## Bilinen Kısıtlar

| # | Kısıt | Durum |
|---|-------|-------|
| 1 | `FFI_STATE` (`OnceLock`) restart mekanizması yok — process yeniden başlatılmadan sıfırlanamaz | Açık |
| 2 | `action_queue` / `ws_command_queue` dolunca mesaj drop edilir; `DROPPED_*_COUNT` sayacı + `eprintln!` logu mevcut (v2026-07-01+) | Geçici çözüm |
| 3 | `rj_action_approve()` stub — Co-Pilot onay mekanizması implement edilmemiş | Açık |
| 4 | `gpu_temp_c` / `cpu_temp_c` her zaman `0` — thermal query stub | Açık |
| 5 | WS sunucusu yalnızca `127.0.0.1`'e bağlanır; uzak IP desteği yok | Açık |
| 6 | `rj_ws_command_v2` ile programatik enjekte edilen komutlar WebSocket istemcisi log'una düşmez | Açık |
