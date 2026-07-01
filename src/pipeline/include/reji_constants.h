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

    // HealingOverlay zamanlama sabitleri
    inline constexpr uint32_t kHealingBannerTimeoutMs     = 10000; // otomatik kapanma süresi
    inline constexpr uint32_t kCoPilotApprovalTimeoutMs   = 30000; // Co-Pilot onay timeout

}

// C uyumluluk makrosu — doğrudan C başlıkları kullanan birimler için
#define REJI_POOL_SIZE 3
