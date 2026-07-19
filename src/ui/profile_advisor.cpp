#include "profile_advisor.h"

#include "reji_constants.h"

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

ProfileId suggest_profile(const HwSignals& s) noexcept {
    // 1) Güç önceliği: batarya gücündeyse Verimlilik (donanım güçlü olsa bile).
    //    Kullanıcı fişteyken bunu Performans'a override edebilir (öneri, zorunlu değil).
    if (s.on_battery) {
        return ProfileId::Efficiency;
    }
    // 2) Marjinal donanım: düşük VRAM VEYA düşük RAM → Stabilite (güvenli/tutucu).
    //    '<' kesin sınır — tam eşik değeri güçlü sayılır (kural 3'e düşer).
    if (s.vram_mb < rj::constants::kProfileVramLowMb ||
        s.total_ram_mb < rj::constants::kProfileRamLowMb) {
        return ProfileId::Stability;
    }
    // 3) Aksi halde (AC + yeterli VRAM/RAM) → Performans.
    return ProfileId::Performance;
}

ProfilePreset preset_for(ProfileId id) noexcept {
    switch (id) {
        case ProfileId::Performance: return ProfilePreset{12000u, 60u};
        case ProfileId::Stability:   return ProfilePreset{ 6000u, 30u};
        case ProfileId::Efficiency:  return ProfilePreset{ 4500u, 30u};
    }
    return ProfilePreset{12000u, 60u};  // ulaşılmaz — enum tam kapsanır
}

const char* profile_resource_name(ProfileId id) noexcept {
    switch (id) {
        case ProfileId::Performance: return "performance";
        case ProfileId::Stability:   return "stability";
        case ProfileId::Efficiency:  return "efficiency";
    }
    return "performance";  // ulaşılmaz — enum tam kapsanır
}

} // namespace reji
