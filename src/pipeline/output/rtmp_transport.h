// src/pipeline/output/rtmp_transport.h
//
// RtmpTransport — ITransport'un RTMP implementasyonu (Faz2/Aşama2.2).
// SrtTransport ile aynı desen: ince sarmalayıcı; gerçek iş Zig tarafında
// (src/pipeline/rtmp/rtmp_transport.zig, rj_rtmp_* C ABI — librtmp NO_CRYPTO,
// düz rtmp://, FLV muxing dahil).
//
// Config eşlemesi: cfg.host = sunucu URL'i ("rtmp://live.twitch.tv/app" —
// stream key HARİÇ), cfg.stream_key = zorunlu. OBS librtmp modeli: app =
// URL path'inin tamamı, playpath = AddStream ile ayrı (OBS UI Server/Key
// ayrımıyla birebir). port/latency/bandwidth RTMP'de kullanılmaz.
#pragma once
#include "../include/i_transport.h"

namespace rj::pipeline::output {

class RtmpTransport final : public rj::ITransport {
public:
    RtmpTransport() = default;
    ~RtmpTransport() override;

    RtmpTransport(const RtmpTransport&)            = delete;
    RtmpTransport& operator=(const RtmpTransport&) = delete;
    RtmpTransport(RtmpTransport&&)                 = delete;
    RtmpTransport& operator=(RtmpTransport&&)      = delete;

    bool init(const Config& cfg) override;
    bool send(const uint8_t* data, size_t size, int64_t pts_us) noexcept override;
    bool is_connected() const override;
    void shutdown() noexcept override;

    // Ses (AAC/FLV) — MVP'de yalnız RTMP gerçekler (SrtTransport no-op).
    bool set_audio_config(const uint8_t* asc, size_t len) override;
    bool send_audio(const uint8_t* aac, size_t len, int64_t pts_us) noexcept override;

private:
    void* handle_ = nullptr;   // Zig tarafındaki Transport*
};

}  // namespace rj::pipeline::output
