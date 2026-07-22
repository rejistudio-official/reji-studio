// src/pipeline/audio/audio_encode_bridge.cpp
#include "audio_encode_bridge.h"
#include "output_subsystem.h"   // rj::OutputSubsystem::send_audio/set_audio_config

#ifdef _WIN32
#include <windows.h>            // OutputDebugStringA (dbglog eşdeğeri)
#endif

namespace reji::pipeline::audio {

namespace {
inline void dlog(const char* msg) noexcept {
#ifdef _WIN32
    ::OutputDebugStringA(msg);
#else
    (void)msg;
#endif
}
} // namespace

void AudioEncodeBridge::configure(uint32_t sample_rate, uint32_t channels,
                                  uint32_t bitrate_bps, rj::OutputSubsystem* out) noexcept {
    sample_rate_ = sample_rate;
    channels_    = channels;
    bitrate_bps_ = bitrate_bps;
    out_         = out;
    enabled_.store(true, std::memory_order_release);
}

void AudioEncodeBridge::push(const float* samples, uint32_t frames, uint32_t channels,
                             uint32_t sample_rate, int64_t pts_us) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return;
    (void)ring_.push(samples, frames, channels, sample_rate, pts_us);
}

// AAC frame üretildiğinde (encode thread) — çıkışa yönlendir + son ses pts'i izle.
void AudioEncodeBridge::on_aac(const uint8_t* aac, uint32_t len, int64_t pts_us, void* ud) {
    auto* self = static_cast<AudioEncodeBridge*>(ud);
    if (self->out_) (void)self->out_->send_audio(aac, len, pts_us);
    self->last_audio_pts_us_ = pts_us;
}

bool AudioEncodeBridge::ensure_encoder() {
    if (encoder_ready_)  return true;
    if (encoder_failed_) return false;

    AacEncoder::Config cfg{ sample_rate_, channels_, bitrate_bps_ };
    if (!encoder_.init(cfg, &AudioEncodeBridge::on_aac, this)) {
        encoder_failed_ = true;   // ses kapali, video etkilenmez
        dlog("[AudioEncodeBridge] AAC encoder init FAILED — ses devre disi\n");
        return false;
    }
    // AudioSpecificConfig'i transport'a ver (ilk send_audio'dan önce).
    // V10/L6: dönüş asc_sent_'e saklanır — transport o an null'sa (stop_stream
    // yarışı) deneme başarısızdır ve drain sonraki turda yeniden dener; eski
    // (void) hâli encoder_ready_'yi "ASC gitti" sayıp sesi kalıcı öldürüyordu.
    send_asc_if_ready();
    encoder_ready_ = true;
    return true;
}

void AudioEncodeBridge::send_asc_if_ready() noexcept {
    const auto& asc = encoder_.audio_specific_config();
    if (out_ && !asc.empty())
        asc_sent_ = out_->set_audio_config(asc.data(), asc.size());
}

void AudioEncodeBridge::drain(int64_t video_pts_us) noexcept {
    if (!enabled_.load(std::memory_order_acquire)) return;
    if (!ensure_encoder()) return;

    // V10/L6: ASC ilk denemede gidememişse (transport null yarışı) yeniden
    // dene — aksi halde rj_rtmp_send_audio tüm frame'leri kalıcı reddeder.
    if (asc_retry_needed(encoder_ready_, asc_sent_)) send_asc_if_ready();

    // Ring'i boşalt: her PCM chunk'ı AAC'ye kodla (on_aac → send_audio).
    while (ring_.consume([&](const float* s, uint32_t frames, uint32_t /*ch*/,
                            uint32_t /*sr*/, int64_t pts) {
        (void)encoder_.encode(s, frames, pts);
    })) {}

    // A/V drift valfi (I10 deseni) — sessizce kötüleşirse fark edilsin.
    const int64_t drift_ms = (last_audio_pts_us_ - video_pts_us) / 1000;
    const int64_t now_ms   = video_pts_us / 1000;
    if (should_warn_av_drift(drift_ms, now_ms, last_drift_warn_ms_)) {
        last_drift_warn_ms_ = now_ms;
        dlog("[AudioEncodeBridge] uyari: A/V drift esigi asildi (>200ms)\n");
    }
}

void AudioEncodeBridge::shutdown() noexcept {
    enabled_.store(false, std::memory_order_release);
    if (encoder_ready_) {
        (void)encoder_.drain();
        encoder_.shutdown();
        encoder_ready_ = false;
        asc_sent_ = false;  // V10/L6: yeniden init'te ASC baştan gönderilir
    }
}

} // namespace reji::pipeline::audio
