#pragma once
#include <cstdint>

// ---------------------------------------------------------------------------
// profile_advisor — ilk-kurulum donanım profili önerisi (Stabilite/Performans/
// Verimlilik). Bu başlık yalnız SİNYAL TOPLAMA yüzeyini tanımlar; karar mantığı
// (suggest_profile) ayrı bir adımda (Commit 3) saf/test-edilebilir fonksiyon
// olarak eklenir.
//
// Donanım izolasyonu (CLAUDE.md §2): bu modül GPU/DXGI enumerate ETMEZ. GPU
// vendor/VRAM değerleri UI/pipeline sınırında mevcut `GpuScan`'den (capture_dxgi)
// gelir ve çağıran tarafından geçirilir. Burada yalnız GPU-dışı sistem sorguları
// (RAM, güç kaynağı) yapılır — vendor-spesifik donanım kodu değil.
// ---------------------------------------------------------------------------
namespace reji {

/// İlk-kurulum profil önerisi için toplanan ham donanım sinyalleri (POD).
struct HwSignals {
    uint32_t vendor_id    = 0;      ///< Encode GPU vendor (GpuScan): 0x10DE=NVIDIA, 0x1002=AMD, 0x8086=Intel
    uint64_t vram_mb      = 0;      ///< Encode GPU adanmış VRAM (MB) — GpuScan.dedicated_vram_mb
    uint64_t total_ram_mb = 0;      ///< Toplam fiziksel RAM (MB) — GlobalMemoryStatusEx.ullTotalPhys
    bool     on_battery   = false;  ///< Sistem batarya gücünde mi — GetSystemPowerStatus.ACLineStatus==0
};

/// Önerilen donanım profili. Gömülü kural setleri (:/config/profiles/<id>.json)
/// ve bitrate/FPS preset'iyle eşleşir.
enum class ProfileId { Stability, Performance, Efficiency };

/// Ham sinyallerden önerilen profili türetir. SAF fonksiyon (I/O yok, test edilebilir).
/// İlk-eşleşen üç kural (Faz 1 / onaylanan eşik tablosu):
///   1) batarya gücünde                              → Efficiency (güç önceliği)
///   2) VRAM < kProfileVramLowMb || RAM < kProfileRamLowMb → Stability (marjinal donanım)
///   3) aksi halde                                   → Performance
/// Not: yalnız ÖNERİ üretir; kullanıcı override eder (sessiz uygulama değil).
ProfileId suggest_profile(const HwSignals& s) noexcept;

/// Toplam fiziksel RAM'i MB olarak döndürür (GlobalMemoryStatusEx.ullTotalPhys).
/// Sorgu başarısızsa 0.
uint64_t query_total_ram_mb() noexcept;

/// Sistem batarya gücünde mi? `GetSystemPowerStatus.ACLineStatus == 0` (Offline).
/// AC / batarya yok / bilinmiyor (255) durumunda false — masaüstü/güvenli taraf.
bool query_on_battery() noexcept;

/// GPU vendor/VRAM (çağıran GpuScan'den sağlar) + RAM/batarya (burada sorgulanır)
/// birleştirip `HwSignals` üretir. Bu modül DXGI'ye dokunmaz (izolasyon korunur).
HwSignals collect_hw_signals(uint32_t encode_vendor_id, uint64_t encode_vram_mb) noexcept;

} // namespace reji
