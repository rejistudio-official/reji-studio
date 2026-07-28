#pragma once
#include <cstdint>
#include <algorithm>

// Bitrate healing politikasının saf (yan-etkisiz) parçaları.
// Pipeline::init'e gömülü kalırsa yalnız karakterizasyon testinin varsayılan
// senaryosuyla dolaylı örtülür; asıl clamp dalı (target < floor) tetiklenmediği
// için sessizce regresyona uğrayabilir. Bu yüzden ayrı/test-edilebilir tutulur
// (bkz. *_for_sample deseni).
namespace rj {

/// REDUCE tabanını (configured_floor = Config::min_bitrate_kbps) kullanıcı
/// hedef bitrate'ine göre geçerli tut.
///
/// apply_action (RJ_ACTION_BITRATE_REDUCE) yeni bitrate'i `max(new, floor)` ile
/// tabanlar. Kullanıcı hedef bitrate'i tabanın altına indirirse (örn. hedef 800,
/// taban 1000) REDUCE hiç ilerleyemez — max daima hedefin üstünde kalır, healing
/// sessizce durur. Tabanı hedefe indirerek REDUCE'un çalışabilmesini garanti et.
///
/// @param configured_floor  Config::min_bitrate_kbps (varsayılan 1000).
/// @param target_kbps       Kullanıcı hedef bitrate'i (Config::bitrate_kbps).
/// @return  min(configured_floor, target_kbps) — taban asla hedefi aşmaz.
inline uint32_t reduce_floor_for_target(uint32_t configured_floor,
                                        uint32_t target_kbps) {
    return (std::min)(configured_floor, target_kbps);
}

/// V10/L11: REDUCE adımı — profil `step_kbps` (param1) artık kullanılır.
/// Kural motoru step'i param1'e taşır (HP2 sözleşmesi: "bitrate aksiyonları:
/// step_kbps, kbps doğrudan"); apply_action bunu yok sayıp sabit %15
/// uyguluyordu → üç profilin mild/high step ayrımı ölü konfigürasyondu.
/// step_kbps <= 0 → %15 fallback (param'sız kural da çalışsın). Taban clamp
/// çağıranda (reduce_floor_for_target ile) kalır.
inline uint32_t reduce_step_bitrate(uint32_t current_kbps, int32_t step_kbps) {
    if (step_kbps > 0) {
        const uint32_t step = static_cast<uint32_t>(step_kbps);
        return current_kbps > step ? current_kbps - step : 0;
    }
    return static_cast<uint32_t>(current_kbps * 0.85f);
}

/// V10/L11: RECOVER adımı — REDUCE ile simetrik (profil recovery kuralı da
/// step_kbps taşır, örn. stability 500). step_kbps <= 0 → %15 artış fallback.
/// Hedef (original) clamp çağıranda kalır.
inline uint32_t recover_step_bitrate(uint32_t current_kbps, int32_t step_kbps) {
    if (step_kbps > 0) return current_kbps + static_cast<uint32_t>(step_kbps);
    return static_cast<uint32_t>(current_kbps * 1.15f);
}

} // namespace rj
