// src/pipeline/include/recovery_coordinator.h
//
// RecoveryCoordinator — GPU TDR / capture-loss recovery koordinasyonu (Aşama 9'da
// Pipeline::Impl::handle_device_lost buraya taşındı).
//
// Bu TAM bir alt sistem DEĞİL — cross-subsystem koordinasyon mantığıdır: Capture ve
// Encode alt sistemleri üzerinde, cihaz kaybından sonra doğru sırayla yeniden kurma
// (capture reset+reinit → dims → encode reinit) orkestrasyonunu yapar. Durum tutmaz
// (stateless static metot); tüm bağımlılıklar parametre olarak alınır.
//
// SEH NOTU: __try bloğunun DIŞINDA çağrılmalıdır (C++ nesneleri kullanır) — eski
// handle_device_lost() ile aynı kısıt. FFI (rj_connection_lost) içeride SEH-leaf
// ile sarmalanır.
//
// Windows'a özel: CaptureSubsystem/EncodeSubsystem ve D3D11 device-removed sorgusu
// Windows'a bağlıdır; bu başlık yalnızca _WIN32 altında include edilmelidir.
#pragma once
#include <atomic>
#include <cstdint>
#include "pipeline.h"   // rj::Pipeline::Config (cfg.fps)

namespace rj {

class CaptureSubsystem;
class EncodeSubsystem;
class ExistingDesktopSource;

class RecoveryCoordinator {
public:
    // GPU TDR / capture-loss recovery. GetDeviceRemovedReason sorgular; gerçek cihaz
    // kaybında capture'ı (+ DXGI path'te encode'u) eski sırayla yeniden kurar:
    //   encode.shutdown() → capture.shutdown() → capture.init() → dims → encode.reinit()
    // width/height = authoritative runtime dims (yalnızca DXGI recovery günceller;
    // notify_vulkan_ready okur). bitrate_kbps: encode reinit için hedef bitrate.
    // Dönüş: recovery yapıldıysa true; transient hata / cihaz sağlamsa false.
    static bool handle_device_lost(
        CaptureSubsystem&           capture,
        EncodeSubsystem&            encode,
        const rj::Pipeline::Config& cfg,
        uint32_t                    bitrate_kbps,
        std::atomic<uint32_t>&      width,
        std::atomic<uint32_t>&      height);

    // ISource wiring overload (Faz 0 karar 2 — somut tip, ISource'a genelleme
    // YAGNI): davranış yukarıdakiyle BİREBİR; reinit Config'i adapter ctor'unda
    // saklı olduğundan yeniden kurulum source.shutdown()+source.init()'e iner.
    // CaptureSubsystem overload'u wiring tamamlanınca silinir (commit 5).
    static bool handle_device_lost(
        ExistingDesktopSource&      source,
        EncodeSubsystem&            encode,
        const rj::Pipeline::Config& cfg,
        uint32_t                    bitrate_kbps,
        std::atomic<uint32_t>&      width,
        std::atomic<uint32_t>&      height);
};

} // namespace rj
