# Reji Studio — Proje Bağlamı

**Son güncelleme:** 2 Temmuz 2026
**Durum:** Faz 0 tamamlandı, Faz 1'e geçiş

---

## Proje Özeti

Açık kaynak canlı yayın/prodüksiyon yazılımı. Windows üzerinde çalışır.

**Stack:**
- C++17 (MSVC) — pipeline (capture/encode/output)
- Rust/Tokio — orchestrator (self-healing, metrikler, WebSocket API)
- Qt6 — UI (preview + program panelleri)
- Vulkan + D3D11 — GPU işlemleri
- Zig — Vulkan bridge (external_memory_bridge.zig, vulkan_initializer.zig)

**Hedef donanım:** AMD Radeon 780M (iGPU/display) + NVIDIA RTX 4070 Laptop (dGPU)

**Repo:** github.com/rejistudio-official/reji-studio

---

## Çalışan Uçtan Uca Zincir
WGC (Windows Graphics Capture, NVIDIA adapter)
→ NVENC H264 encode (60fps, 6000kbps, self-healing ile 3500'e düşebiliyor)
→ SRT output (gerçek implementasyon)
→ ffplay / herhangi bir SRT alıcısı
Paralelde:
→ CPU staging → Qt preview panel (sol)
→ CPU staging → Qt program panel (sağ)
→ WebSocket API (port 7070-7073 fallback) → HTML kontrol paneli
(stream_start/stop, scene_cut/fade, gerçek zamanlı metrikler)

DXGI capture da mevcut, WGC desteklenmediğinde fallback olarak devrede (CPU staging güvenlik ağı ile).

---

## Mimari — Pipeline::Impl Refactoring (TAMAMLANDI)

`Pipeline::Impl` God Object'ti (986 satır, 9 alt sistemi tek struct'ta topluyordu). 
Opus ile 9 aşamalı refactoring yapıldı, sıfır davranışsal regresyonla tamamlandı:

| Aşama | Alt Sistem | Sorumluluk |
|---|---|---|
| 1 | FramePacer | QPC zamanlama, pts hesabı, frame pacing |
| 2 | MetricsSubsystem | CpuMeter, MetricsCollector, fps hesabı |
| 3 | AudioSubsystem | WasapiCapture lifecycle |
| 4 | OutputSubsystem | SRT + srt_atomic (thread-safe fasad) |
| 5 | CommandRouter | Command/WS drain + SPSC ring + action thread |
| 6 | EncodeSubsystem | NVENC lifecycle |
| 7 | GpuInteropSubsystem | ExternalMemoryBridge + D3D11↔Vulkan frame cache |
| 8 | CaptureSubsystem | WGC/DXGI + preview (en düğümlü aşama) |
| 9 | RecoveryCoordinator | TDR/device-lost recovery (stateless, cross-subsystem) |

**Sonuç:**
- `pipeline.cpp`: 986 → 780 satır (−21%)
- `run_frame()`: 234 → 111 satır (−53%)
- `Impl` artık ince orkestratör: 8 alt sistem üyesi + 4 lifecycle flag + indirgenemez 
  cross-cutting state (cfg, width/height atomics, frame_drops, UI callback'leri, 
  sıkı-düğüm applier'ları: on_packet, apply_command, apply_frame_cmd)

**Soyutlama interface'leri (cross-platform/çoklu-backend hazırlığı):**
- `IScreenCapture` — WGC/DXGI seçimi factory pattern ile (`is_supported()` kontrolü)
- `IVideoEncoder` — NVENC implementasyonu, gelecekte AMF/VideoToolbox için hazır
- `ITransport` — SRT implementasyonu, gelecekte RTMP/NDI için hazır

---

## FFI Sınırı (Rust ↔ C++)

Detaylı sözleşme: `docs/FFI_CONTRACT.md`

**Mimari prensip:** FFI'dan sadece veri geçer, pointer/handle geçmez.
- `g_pipeline` global pointer + `PipelineRegistry` (weak_ptr) tamamen kaldırıldı
- Yerine: lock-free SPSC kuyruklar (`action_queue`, `ws_command_queue`)
- 13 `extern "C"` fonksiyon, hepsi `catch_unwind` ile korunuyor (panic C++'a sızmıyor)
- `RjCommand` (24B), `RjAction` (20B), `RjMetricSample` (64B) — offsetof static_assert 
  ile 25 alan derleme zamanında doğrulanıyor
- `ffi_auto.h` cbindgen ile tamamen otomatik üretiliyor (manuel drift riski yok)
- Backpressure logging: kuyruk dolduğunda sessizce kaybetmek yerine loglanıyor + sayaç

---

## Self-Healing Sistemi

Kural motoru (`rules.rs`), JSON/TOML formatında, hot-reload destekli.

- OR (`||`) ve AND (`&&`) koşul kombinasyonu: `"cpu_load_pct > 80 || gpu_load_pct > 85"`
- Hysteresis — aynı kural belirli süre içinde tekrar tetiklenmiyor
- Çakışma çözümü — BitrateReduce + BitrateRecover aynı anda tetiklenirse öncelik sırası uygulanıyor
- `HealingOverlay` UI bağlantısı kuruldu — Co-Pilot modunda kullanıcı onay akışı çalışıyor
- Metrikler: CPU/GPU/RAM gerçek PDH sorgularıyla besleniyor (`MetricsCollector`)

Örnek kural dosyası: `~/.reji/rules.json`

---

## Kontrol Arayüzleri

**Qt UI:** Ana pencere, sahne listesi, CUT/FADE geçişleri, ayarlar dialogu (SRT host/port, healing modu)

**WebSocket API** (Rust, axum):
- Port 7070 varsayılan, 7071-7073 fallback (port çakışmasında otomatik dener)
- `ws://host:port/ws` — komut gönder (`stream_start`, `stream_stop`, `scene_cut`, `scene_fade`)
- `http://host:port/` — HTML kontrol paneli (mobil tarayıcıdan erişilebilir)
- Gerçek zamanlı metrik push: fps, kbps, drop, CPU, GPU

---

## Büyük Kararlar (Kronolojik Özet)

1. **DXGI → WGC geçişi** — AMD+NVIDIA cross-adapter NT handle sharing çalışmıyordu 
   (`E_INVALIDARG`). Windows Graphics Capture API'ye geçildi, NVIDIA adapter üzerinde 
   çalıştırılarak sorun OS seviyesinde çözüldü. DXGI fallback olarak korundu.

2. **NVENC API version negotiation** — SDK 13.1 header'ı driver'ın desteklediği 13.0 
   formatına düşürülerek `NV_ENC_ERR_INVALID_VERSION` hatası çözüldü.

3. **SRT gerçek implementasyon** — stub'dan gerçek SRT'ye geçildi, vcpkg DLL bağımlılıkları 
   (`srt.dll`, `libcrypto-3-x64.dll`, `libssl-3-x64.dll`) build.py'ye otomatik kopyalama 
   eklendi.

4. **FFI mimarisi — tam veri-tabanlı geçiş** — Önce `g_pipeline` → `PipelineRegistry` 
   (weak_ptr) ile UAF riski giderildi, sonra bu da gereksiz hale geldi ve tamamen kaldırıldı: 
   artık FFI sınırından sadece kuyruk üzerinden veri geçiyor, hiçbir pointer/handle yok.

5. **Pipeline::Impl God Object refactoring** — 9 aşamalı, Opus ile yürütülen büyük refactoring. 
   Her aşamada karakterizasyon testi + baseline karşılaştırması ile sıfır regresyon garantisi.

6. **Dört model code review turu** — Claude Opus 4.8, Sonnet 4.6, GLM 5.2, Sonnet 5 (2 deneme, 
   ikisi de boş/başarısız yanıt döndü — provider tarafı sorun). 12+ kritik/orta bulgu düzeltildi: 
   Vulkan blit capability, cross-adapter CPU fallback, HealingOverlay wiring, NT handle leak, 
   stack canary, frame thread busy-loop, SrtOutput çift shutdown, AMD GPU sync, vb.

---

## Yol Haritası — 5 Faz

Detaylar: `docs/ROADMAP.md`, Linear (Reji Studio takımı, REJ-5 ila REJ-13)

| Faz | Konu | Durum |
|---|---|---|
| **0** | Temel Hazırlık (FFI_CONTRACT + God Object refactoring) | ✅ **TAMAMLANDI** |
| 1 | OBS-WebSocket Protokol Uyumluluğu | 🔜 Sıradaki |
| 2 | RTMP Çıkışı | Backlog |
| 3 | Çoklu Kaynak Mimarisi (ISource) | Backlog |
| 4 | NDI Desteği | Backlog |
| 5 | Zig Global State Tam Çözümü | Kısmen (geçici double-init uyarısı var) |

### Faz 1 — Sıradaki Adımlar

OBS-WebSocket protokol uyumluluğu, Stream Deck/Companion ekosistemiyle anında uyum sağlar.

1. obs-websocket v5 protokol alt kümesi araştırması (hangi request/response tipleri gerekli)
2. Mevcut `ws_server.rs` üzerine protokol adaptör katmanı
3. Temel komutlar: `GetSceneList`, `SetCurrentProgramScene`, `StartStream`, `StopStream`, 
   `GetStreamStatus`
4. Test: Stream Deck veya Companion ile bağlantı doğrulama

Not: Reji'nin kendi WebSocket API'si zaten çalışıyor — bu faz üzerine bir uyumluluk 
adaptör katmanı ekler, mevcut API'yi değiştirmez.

---

## Dış Araçlar

- **Linear** — linear.app/reji-studio — issue/sprint takibi (REJ-5 ila REJ-13)
- **Notion** — "Reji Studio — Proje Merkezi" sayfası — genel proje referansı
- **Todoist** — "Reji Studio" projesi — günlük açık kalem takibi
- **GitHub** — Claude Code üzerinden yönetiliyor (resmi connector henüz yok)
- ~~Telegram bridge~~ — kaldırıldı (kullanılmıyor)

---

## Bilinen Açık Kalemler / Teknik Borç

- `rj_action_approve` şu an stub, her zaman `1` dönüyor — gerçek implementasyon gerekebilir
- Zig modül-global state (`external_memory_bridge.zig`, `vulkan_initializer.zig`) — 
  çoklu instance senaryosunda gerçek çözüm gerekiyor (Faz 5)
- Audio metrikleri izlenmiyor (WASAPI tarafı self-healing için kör nokta)
- `default_mode` alanı `rules.json`'da parse ediliyor ama kullanılmıyor

---

## Kaynak Dosyalar

- `docs/CONTEXT.md` — bu dosya
- `docs/SESSION_NOTES.md` — kronolojik oturum geçmişi, detaylı
- `docs/ROADMAP.md` — Zig migration planı + modülerlik/endüstri uyumluluğu 5 fazı
- `docs/FFI_CONTRACT.md` — Rust/C++ FFI sınırının resmi sözleşmesi
