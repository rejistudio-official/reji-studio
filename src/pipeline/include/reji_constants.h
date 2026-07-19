#pragma once
#include <cstdint>

namespace rj::constants {

    // GPU texture pool boyutu — ExternalMemoryBridge, GpuCopyOptimizer ve PreviewWidget
    // bunu round-robin slot sayısı olarak kullanır. Zig tarafı (external_memory_bridge.zig)
    // ayrı bir sabit tutar; değiştirirsen orayı da güncelle (senkronizasyon yorumu orada mevcut).
    inline constexpr int      kGpuPoolSize                = 3;

    // Varsayılan ve azaltılmış encode bitrate (kbps)
    inline constexpr uint32_t kDefaultBitrateKbps         = 6000;
    inline constexpr uint32_t kReducedBitrateKbps         = 3500;

    // UI zamanlama sabitleri
    inline constexpr uint32_t kActionPollIntervalMs        = 200;   // healing action poll (MainWindow)
    inline constexpr uint32_t kSrtLatencyMs               = 200;   // SRT akış gecikmesi
    // Madde 6/A: Ayarlar OK'inden sonra "kaydedildi" durum mesajının görünür kalma
    // süresi; ardından lbl_status_ "Hazır"a döner (kısa süreli bilgilendirme).
    inline constexpr uint32_t kHealingSettingsNotifyMs    = 4000;

    // HealingOverlay zamanlama sabitleri
    inline constexpr uint32_t kHealingBannerTimeoutMs     = 10000; // otomatik kapanma süresi
    inline constexpr uint32_t kCoPilotApprovalTimeoutMs   = 30000; // Co-Pilot onay timeout

    // J7: Keyed-mutex ping-pong anahtarları — D3D11 capture (üretici) ↔ Vulkan
    // copy_optimizer (tüketici) arasında paylaşımlı senkron protokolü. Değer =
    // o anahtarla acquire eden tarafa devredildi anlamına gelir; iki dosya bu
    // değerleri TERS rollerle kullandığından (bir taraftaki acquire = diğerinin
    // release değeri) tek bir yerde tutmak sessiz drift-deadlock riskini önler.
    inline constexpr uint64_t kKeyedMutexKeyD3D11         = 0;     // D3D11'in yazma turu
    inline constexpr uint64_t kKeyedMutexKeyVulkan        = 1;     // Vulkan'ın okuma turu

    // K2: Keyed-mutex hang'ini sınırlayan iki üst-sınır. Steady-state'te (release
    // ~1 frame içinde gelir) hiçbiri tetiklenmez; yalnız device-lost patolojisinde
    // devreye girer. Üçlü savunma-derinliği (bkz. capture_dxgi ReleaseSync kontrolü):
    //   - Vulkan tüketicisinin keyed-mutex acquire üst sınırı (ms). Eskiden UINT32_MAX
    //     (sonsuz) idi → D3D11 release'i hiç gelmezse GPU kuyruğu sonsuz bloke olurdu.
    inline constexpr uint32_t kKeyedMutexAcquireTimeoutMs = 100;
    //   - execute_copy'nin önceki submit'i beklerken üst sınırı (ns). Eskiden UINT64_MAX
    //     (sonsuz) idi → takılı GPU submit'i CPU thread'ini kalıcı dondururdu. 100ms =
    //     ~6 frame@60fps tolerans; aşılırsa kare düşürülür (command buffer reset ÖNCESİ).
    inline constexpr uint64_t kCopyPrevSubmitWaitTimeoutNs = 100'000'000ULL;

}

// C uyumluluk makrosu — doğrudan C başlıkları kullanan birimler için
#define REJI_POOL_SIZE 3
