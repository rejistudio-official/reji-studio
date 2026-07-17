// src/pipeline/audio/audio_encode_bridge.h
//
// Ses encode köprüsü — capture thread (üretici) ile encode/output thread
// (tüketici) arasını bağlar. Tasarım notu (Faz 1-ek, onaylı):
//  - on_audio (capture thread) → ring_.push (yalnız kopya, MF/RTMP'ye dokunmaz).
//  - drain (encode thread, her video paketinde) → ring drain → AAC encode →
//    OutputSubsystem::send_audio. Böylece TÜM RTMP yazımları tek thread'de kalır
//    (kilit yok, mevcut invariant korunur).
//  - Encoder MF çağrıları encode thread'inde tutulur: ilk drain'de LAZY init
//    (MFStartup/MFT create/encode aynı thread). Böylece MF thread-affinity riski
//    en aza iner.
//  - A/V drift valfi (I10 deseni): drift eşiği aşarsa dbglog uyarısı.
//  - İzolasyon: encoder init başarısızsa ses kapanır, VIDEO ETKİLENMEZ.
//
// Windows'a özel (AacEncoder = Media Foundation). Yalnız pipeline.cpp'nin _WIN32
// bloğundan kullanılır.
#pragma once
#include <atomic>
#include <cstdint>

#include "audio_ring.h"
#include "aac_encoder.h"
#include "av_sync.h"

namespace rj { class OutputSubsystem; }

namespace reji::pipeline::audio {

class AudioEncodeBridge {
public:
    // Hedef encode parametreleri + çıkış alt sistemi. Yalnız audio_enabled VE
    // RTMP transport'ta çağrılır (MVP). enabled_ set → capture push edebilir.
    void configure(uint32_t sample_rate, uint32_t channels,
                   uint32_t bitrate_bps, rj::OutputSubsystem* out) noexcept;

    // Üretici (capture supervisor thread) — WASAPI on_audio buradan çağırır.
    // Yalnız ring'e kopyalar; encode/gönderme YOK.
    void push(const float* samples, uint32_t frames, uint32_t channels,
              uint32_t sample_rate, int64_t pts_us) noexcept;

    // Tüketici (encode thread) — her video paketinde çağrılır. İlk çağrıda
    // encoder'ı lazy init eder, ring'i drain edip AAC encode + send_audio yapar,
    // A/V drift valfini çalıştırır. @param video_pts_us son video paketinin pts'i.
    void drain(int64_t video_pts_us) noexcept;

    // Encoder'ı drain + kapat (encode thread'inde çağrılmalı — bkz. tasarım notu).
    void shutdown() noexcept;

private:
    static void on_aac(const uint8_t* aac, uint32_t len, int64_t pts_us, void* ud);
    bool ensure_encoder();   // encode-thread-yerel lazy init

    AudioRing            ring_;
    AacEncoder           encoder_;
    rj::OutputSubsystem* out_{nullptr};
    uint32_t             sample_rate_{0};
    uint32_t             channels_{0};
    uint32_t             bitrate_bps_{0};

    std::atomic<bool>    enabled_{false};   // configure sonrası capture push edebilir

    // Aşağıdakiler YALNIZ encode thread'inden erişilir (senkronizasyon gerekmez).
    bool    encoder_ready_{false};
    bool    encoder_failed_{false};
    int64_t last_audio_pts_us_{0};
    int64_t last_drift_warn_ms_{kNoPriorWarn};
};

} // namespace reji::pipeline::audio
