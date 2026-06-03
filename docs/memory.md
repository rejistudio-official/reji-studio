# Reji Studio — Proje Hafızası

Mimari kararlar, öğrenilen dersler, tekrar edilmemesi gereken hatalar.
Her oturumda okunmalı.

---

## Mimari Kararlar

### GPU Pipeline
- **RTX 4070 Laptop'ta ekran AMD iGPU'ya bağlı, NVENC NVIDIA dGPU'da.**
  DXGI Desktop Duplication yalnızca AMD adapter'ında çalışır, NVIDIA'da `E_ACCESSDENIED` döner.
  Çözüm: `GpuResourceManager` AMD→NVIDIA `SharedHandle` transferi (cross-adapter path).
  Şu an: `same_adapter_ = true` hardcode (preview-only, cross-adapter aktif değil).

- **`display_vendor_id()`** `gpu_scan_.entries[0]` = display adapter (AMD iGPU).
  `entries[1]` veya sonrası = encode adapter (NVIDIA dGPU).

- **Preview render path seçimi (v0.3):**
  - `CapabilityDetector::detect(vendor_id)` → `RenderProfile{RenderPath, name}`
  - 0x10DE (NVIDIA) → `kNvDxInterop` stub (şimdilik PBO çalışır)
  - Diğer → `kPbo` ping-pong
  - `selectRenderPath()` — pipeline init sonrası bir kez çağrılır, GL thread'de

### FFI Sınırı
- **Her `extern "C"` FFI çağrısı `__declspec(noinline)` SEH leaf function'ına sarılmalı.**
  `pipeline.cpp`'de `seh_command_drain`, `seh_shutdown_subsystems` vb. mevcut.
  `main_window.cpp`'de `rj_command_drain` doğrudan çağrılıyor — SEH wrapper eksik (reliability debt).

- **`rj_command_drain` dönüşü her zaman `[0, max]`'a clamp edilmeli.**
  Güvenlik açığı: `main_window.cpp:312`'de düzeltildi (2026-06-01).
  Pattern: `if (n < 0) n = 0; else if (n > max) n = max;`

- **Blocking FFI çağrısı kesinlikle yasak.** Ring buffer üzerinden async.

### Qt6 Bilinen Sorunlar
- **`QSplitter::setSizes()` + `QOpenGLWidget`** → crash (Qt6.8 + MSVC). `setStretchFactor()` kullan.
- **`statusBar()->showMessage()`** permanent widget'ları gizler. `QLabel` kullan.
- **`setChildrenCollapsible(false)`** + `QOpenGLWidget` → crash. Kaldırıldı.
- **`WIN32_EXECUTABLE ON`** → konsol yok, `printf`/`fprintf(stderr,...)` görünmez.
  Test için: `Start-Process -RedirectStandardError err.log`

### CI / Build
- **`vcvarsall.bat` bu sistemde Windows SDK'yı otomatik bulamıyor.**
  `vswhere.exe` ve `findstr.exe` PATH'te yok.
  Çözüm: INCLUDE/LIB/PATH manuel set edilmeli (bkz. Build Komutları).

- **`&&` operatörü PowerShell'de çalışmaz.**
  GitHub Actions workflow'larında ayrı `run:` adımları kullan.

- **`quality.yml`'de `--enable=all`** çok gürültülü.
  `--enable=warning,performance,portability` kullan.
  `--suppress=missingIncludeSystem` eklenmeli.

### Güvenlik
- **`copy_fence_` (GpuResourceManager)** hiç `CreateQuery` edilmemiş, `nullptr`.
  Cross-adapter path aktif olunca null deref. Reliability debt — DoS kapsamında.

- **`wait_display_gpu_idle()` sonsuz döngü** `DXGI_ERROR_DEVICE_REMOVED` durumunda.
  Reliability debt — cross-adapter aktif olunca düzeltilmeli.

---

## Tekrar Edilmemesi Gereken Hatalar

| Hata | Neden Oldu | Çözüm |
|---|---|---|
| `setSizes()` crash | Qt6.8 + OpenGL widget uyumsuzluğu | `setStretchFactor()` kullan |
| 22 geçici dosya commit'e girdi | `.gitignore` eksikti | v0.2'de temizlendi |
| `WIN32_EXECUTABLE OFF` unutuldu | Debug için açıktı | v0.2'de ON yapıldı |
| `cargo audit` kurulum eksik | `run: cargo audit` doğrudan çalışmaz | `rustsec/audit-check@v2` action kullan |
| `n` clamp yok (FFI dönüşü) | Güvenlik gözden kaçtı | Her FFI dönüşünü clamp et |

---

## PBO Ping-Pong Detayları

```
paintGL() akışı:
  write_idx = pbo_idx
  read_idx  = pbo_idx ^ 1

  Boyut değişimi → her iki PBO orphan, pbo_frame = 0 (guard sıfırla!)
  pbo_size = w * h * 4  (img.sizeInBytes() değil — Qt versiyon farkı var)

  CPU → PBO[write]: glBufferData(data, GL_STREAM_DRAW)  ← async DMA başlar
  PBO[read] → tex:  glTexSubImage2D(nullptr)            ← GPU DMA, önceki frame

  pbo_frame < 1 → read adımı atla (boş PBO okunmaz → ilk kare bozuk olmaz)
  pbo_idx ^= 1
  pbo_frame++

Destructor: glDeleteBuffers(2, pbo)  ← unutulmamalı
```

---

## Açık Reliability Debt

Bu sorunlar güvenlik kapsamında değil, ileride düzeltilmeli:

1. `copy_fence_` asla create edilmemiş → cross-adapter aktifken crash
2. `wait_display_gpu_idle()` `DXGI_ERROR_DEVICE_REMOVED` döngüsü
3. `main_window.cpp::pollMetrics()` SEH wrapper eksik (`seh_command_drain` kullanılmalı)
4. `NV_DX_INTEROP` stub → v0.3'te gerçek `wglDXRegisterObjectNV` implementasyonu

---

## Vendor ID Referansı

| vendor_id | GPU | Render Path |
|---|---|---|
| 0x10DE | NVIDIA | `kNvDxInterop` (stub) |
| 0x1002 | AMD | `kPbo` |
| 0x8086 | Intel | `kPbo` |
| 0x0000 | Bilinmiyor / init yok | `kPbo` |

---

## Self-Healing Modları

Reji Studio'da dört self-healing davranış modu mevcuttur. Kullanıcı rolüne ve tercihine göre seçilir.

| Kullanıcı modu | Self-healing modu | Davranış |
|---|---|---|
| **Başlangıç** | Auto-Pilot | Kritik + Orta önem aksiyonlar tam otomatik gerçekleşir, bildirim gönderilir |
| **Standart** | Co-Pilot | Aksiyonlar kullanıcı onayına sunulur — checkbox listesiyle seçim, seçilmeyenler bildirim gösterir |
| **Uzman** | Assist | Kritik otomatik, orta/düşük aksiyonlar log + bildirim (onay gerekmez) |
| **Uzman** | Manual | Açılışta tek seferlik uyarı dialog, self-healing tamamen devre dışı, sadece log |

**Co-Pilot Aksiyon Örnekleri:**
- Bitrate otomatik düşür (frame drop % > 10)
- Kaynak yeniden bağlan (timeout/disconnect)
- Çözünürlük düşür (GPU stall)
- Encode kalitesi değiştir (thermal throttle)

**Implementasyon:** `src/ui/healing_overlay.cpp` + `src/orchestrator/metrics.rs::AdaptationDecider`

---

## Karar Motoru — 6 Seviye Yol Haritası

Reji Studio'nun donanım, ağ ve bağlam koşullarına göre otomatik uyum sağlama yeteneği.
DeviceProfiler → RuleEngine (Rust) → ActionDispatcher → HealingOverlay (UI)

| Seviye | Versiyon | Kapasite | Detay |
|---|---|---|---|
| **1** | v0.2 ✅ | Hardware Discovery | GPU vendor, VRAM, D3D11 feature level; `CapabilityDetector`, `RenderProfile` |
| **2** | v0.3 ✅ | Capability Detection | OpenGL extensions, render path seçimi (AMD→PBO, NVIDIA→NV_DX_INTEROP stub) |
| **3** | v0.4 | Runtime Adaptation | Frame drop → bitrate düşür; GPU/CPU sıcaklık → kalite ayarı; RTT/jitter izleme; pil durumu; bellek baskısı; disk I/O |
| **4** | v0.5 | Context Awareness | Saat bazlı profil (sabah test, akşam canlı); izleyici sayısı adapt; platform limitleri (Twitch 6000, YouTube 8000); sahne bazlı optimizasyon |
| **5** | v1.0 | Learning System | SQLite oturum geçmişi analizi; donanım yaşlanma tespiti; başarılı config hafızası; anomali tespiti & auto-diagnosis |
| **6** | v2.0 | External Integration | Stream Deck/Loupedeck fiziksel kontrol; OBS/vMix köprüsü; bulut profil sync; webhook (Discord/Slack) |

### Seviye 3 (v0.4) Detayları

**Uyum Mekanizmaları:**
- Frame drop > eşik → bitrate otomatik düşür/yükselt
- GPU sıcaklık > 85°C → encode kalite aşağı çek (full → half → quarter res)
- CPU load > 90% → preview kalite düşür, frame cap (60→30fps)
- Ağ metrikleri (RTT, jitter, paket kaybı) → codec/bitrate ayarı
- Laptop pil modu → CPU/GPU işleri azalt, preview FPS cap
- Disk I/O yüksek (kayıt modu) → buffer size artır, frame skip tolerance

**Impl:** `src/orchestrator/rules.rs` (rule engine), `src/orchestrator/metrics.rs` (AdaptationDecider genişlet)

### Seviye 4 (v0.5) Detayları

**Context-Aware Profiller:**
- Saat: 6-12 → "Morning Test" (low bitrate, high quality)
- Saat: 18-23 → "Evening Live" (high bitrate, motion optimization)
- İzleyici sayısı: 0-10 → "Low", 10-100 → "Medium", 100+ → "High"
- Platform limit: Twitch 6000 kbps max, YouTube 8000 kbps, custom RTMP özel sınır
- Sahne content type: statik/PowerPoint → intra frame artırma; hareketli/kamera → motion estimation

**Impl:** Time-based scheduler, viewer count API poller, platform detection, scene analyzer

### Seviye 5 (v1.0) Detayları

**Öğrenme Sistemi:**
- Her oturum: bitrate, FPS, GPU temp, frame drop → SQLite kaydı
- Trend: 7 gün ortalama, anomali flag (sudden temp spike, frame drop burst)
- Başarılı config: "RTX 4070 + 1080p60 @ 6000kbps, avg temp 72°C" → hafızaya al
- Benzer hardware görüldüğünde otomatik preset uygula
- Anomali: GPU temp anormal yükseliş → "thermal paste degradation?" hint

**Impl:** `src/pipeline/metrics_recorder.cpp` (SQLite), `src/orchestrator/learning.rs` (pattern match)

### Seviye 6 (v2.0) Detayları

**Dış Sistem Entegrasyonu:**
- **Fiziksel Kontrol:** Stream Deck "Bitrate up/down" butonları, Loupedeck fader → bitrate smooth interpolation
- **OBS Köprüsü:** OBS scene switch → Reji profil otomatik sync (encoder settings match)
- **Bulut:** Optional user cloud profile (GitHub gist veya custom server)
- **Webhook:** Critical event → Discord DM ("GPU throttle detected, switching to backup camera")

**Impl:** Named Pipe IPC server, OBS WebSocket client, optional cloud sync, webhook dispatcher

---

## Plugin Güvenliği

### In-Process Plugin Riski (v0.x)
**Sorun:** Şu an plugin'ler C ABI üzerinden in-process çalışıyor.
Hastalıklı veya kötü amaçlı plugin tüm `reji_app.exe` süreci düşürebilir (segfault, infinite loop, memory leak).

**Uzun Vadeli Çözüm:** Extism/WASM Sandbox
- **Kaynağı:** https://github.com/extism/extism
- **Özellikleri:** WASI tabanlı, 12 dil desteği (Rust, Go, Python, C, ...), production (Shopify, Discord)
- **Avantajı:** Plugin kodu process-isolated, sandbox kaçış zor, memory-safe WASM
- **Entegrasyon:** Rust orchestrator'a `extism::PluginManager`, v1.5'te opsiyonel, v2.0'de zorunlu
- **Marketplace:** Ed25519 imza + binary scan + human review (v1.5+)
