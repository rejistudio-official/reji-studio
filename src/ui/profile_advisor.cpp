#include "profile_advisor.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace reji {
namespace {
constexpr uint64_t kBytesPerMb = 1024ull * 1024ull;
} // namespace

uint64_t query_total_ram_mb() noexcept {
    MEMORYSTATUSEX ms{};
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) {
        return ms.ullTotalPhys / kBytesPerMb;
    }
    return 0;
}

bool query_on_battery() noexcept {
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps)) {
        // ACLineStatus: 0=Offline(batarya), 1=Online(AC), 255=Unknown.
        // Yalnız kesin "Offline" batarya sayılır; Unknown/AC → false
        // (masaüstü varsayımı — güvenli taraf, Verimlilik'i yanlış tetiklemez).
        return sps.ACLineStatus == 0;
    }
    return false;
}

HwSignals collect_hw_signals(uint32_t encode_vendor_id, uint64_t encode_vram_mb) noexcept {
    HwSignals s;
    s.vendor_id    = encode_vendor_id;   // GpuScan'den (donanım izolasyonu korunur)
    s.vram_mb      = encode_vram_mb;     // GpuScan.dedicated_vram_mb
    s.total_ram_mb = query_total_ram_mb();
    s.on_battery   = query_on_battery();
    return s;
}

} // namespace reji
