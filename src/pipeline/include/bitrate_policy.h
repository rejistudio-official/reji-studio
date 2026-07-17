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

} // namespace rj
