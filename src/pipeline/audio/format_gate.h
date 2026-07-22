// src/pipeline/audio/format_gate.h
//
// V10/L10: kanal/örnekleme uyuşmazlığı kapısının saf çekirdeği — header-only,
// link'siz test edilir (asc_state.h / av_sync.h deseni).
//
// Kök neden: AudioEncodeBridge::drain, ring'den gelen chunk'ın gerçek
// (channels, sample_rate) değerlerini yok sayıp encoder'ı configure edilen
// sabit formatla besliyordu. Cihaz mono/44.1kHz verirse encoder yanlış
// interleave/frame sayısıyla SESSİZ bozuk AAC üretir. Bilinen #5 (resampling
// yok) yalnız sample-rate'i kapsıyordu; kanal boyutu ayrı ve sessizdi.
// Model: uyuşmayan chunk encode edilmez, ses yolu güvenli kapatılır (video
// etkilenmez) — bozuk yayın yerine sessizlik + tek satır log.
#pragma once
#include <cstdint>

namespace reji::pipeline::audio {

// drain() her chunk'ta sorar: bu chunk configure edilen formatla encode
// edilebilir mi? Uyuşmazlık = güvenli kapatma sinyali.
inline bool format_matches(uint32_t chunk_channels, uint32_t chunk_sample_rate,
                           uint32_t cfg_channels,   uint32_t cfg_sample_rate) {
    return chunk_channels == cfg_channels && chunk_sample_rate == cfg_sample_rate;
}

} // namespace reji::pipeline::audio
