## Oturum: 29 Haziran 2026 (Devam)

### Tamamlananlar
- SRT host/port SettingsDialog üzerinden ayarlanabilir, QSettings ile kalıcı
- WGC device lost recovery eklendi (60 frame threshold, IScreenCapture::create() reinit)
- Capture kaybı detection: null_streak atomic, 1 saniye kesintide handle_device_lost() tetikleniyor
- WebSocket kontrol API: axum 0.7, port 7070, ws://host:7070/ws
- HTML kontrol paneli: include_str! ile binary'ye gömülü, http://host:7070/ adresinden erişilebilir
- stream_start / stream_stop WebSocket komutu → rj_ws_command FFI → pipeline.start_stream/stop_stream zinciri çalışıyor
- SRT DLL'leri (srt.dll, libcrypto, libssl) build.py ile otomatik kopyalanıyor

### Açık Kalemler
- scene_cut / scene_fade WebSocket → MainWindow Qt signal bağlantısı (şu an no-op)
- WebSocket metrik push (fps, kbps, drop → tarayıcıya gerçek zamanlı)
- GPU preview path (CPU fallback aktif, GL interop WGC path'te kurulmadı)
- MetricsCollector GPU load C++ toplama tarafı tamamlandı ✅

### Mimari Notlar
- WebSocket Rust orchestrator'da (axum) — Qt thread'i etkilemiyor
- rj_ws_command(int) reverse FFI ile Rust → C++ köprüsü kuruldu
- HTML kontrol paneli mobil tarayıcıdan çalışıyor, native app gereksiz
- Uzun vade: scene komutları için Qt signal emit gerekiyor (QMetaObject::invokeMethod)

---

## Oturum: 29 Haziran 2026

### Tamamlananlar
- FABLE5 V7 H1-H20 tümü doğrulandı ve eksikler giderildi
- Vulkan leak düzeltildi (vkDestroyDevice öncesi 9 nesne temizlenmiyordu)
- WGC (Windows Graphics Capture) backend eklendi — NVIDIA adapter üzerinde
- IScreenCapture factory: WGC destekleniyorsa WGC, yoksa DXGI otomatik seçim
- Her iki panel (preview + program) çalışıyor
- NVENC H264 encode aktif (60fps, 6000kbps, RTX 4070)
- SRT output gerçek implementasyon devreye alındı (stub → gerçek)
- WGC → NVENC → SRT → ffplay uçtan uca test edildi, çalışıyor
- IScreenCapture / IVideoEncoder / ITransport soyutlama interface'leri eklendi
- MetricsCollector: GPU/CPU/RAM PDH üzerinden gerçek veri üretiyor
- Build sistemi: SRT DLL'leri otomatik kopyalanıyor
- same_adapter_ LUID karşılaştırmasıyla düzeltildi (AMD+NVIDIA doğru tespit)
- GL/VK capability detection eklendi
- Repo temizliği: docs/ klasörü, .gitignore güncellendi
- EventBus Lagged (B8) düzeltildi

### Açık Kalemler
- Self-healing recovery kapsamı (device lost, capture kaybı)
- WebSocket kontrol API (mobil/uzaktan kontrol)
- GPU preview path (şu an CPU fallback aktif)
- SRT host/port UI'dan ayarlanabilir olmalı (şu an hardcode 127.0.0.1:9000)
- MetricsCollector::query_gpu_load_pct() C++ toplama tarafı (PDH çalışıyor ✅)

### Mimari Kararlar
- Cross-adapter NT handle sharing AMD+NVIDIA kombinasyonunda çalışmıyor — WGC ile aşıldı
- Timeline semaphore: Vulkan'da YES, GL'de NO (AMD 780M) — binary semaphore path korunuyor
- IScreenCapture/IVideoEncoder/ITransport interface'leri cross-platform hazırlık için eklendi
- Uzun vade: WGC (Windows) + PipeWire (Linux) + ScreenCaptureKit (macOS) + ARM desteği
- Mobil için WebSocket API önerildi (native app yerine)
