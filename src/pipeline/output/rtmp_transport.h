// src/pipeline/output/rtmp_transport.h
//
// RtmpTransport — ITransport'un RTMP implementasyonu (Faz2/Aşama2.2).
// SrtTransport ile aynı desen: ince sarmalayıcı; gerçek iş Zig tarafında
// (src/pipeline/rtmp/rtmp_transport.zig, rj_rtmp_* C ABI — librtmp NO_CRYPTO,
// düz rtmp://, FLV muxing dahil).
//
// Config eşlemesi: cfg.host = TAM ingest URL'i (örn.
// "rtmp://live.twitch.tv/app/STREAM_KEY"). port/latency/bandwidth RTMP'de
// kullanılmaz (URL'in içinde/ilgisiz).
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
    bool send(const uint8_t* data, size_t size, int64_t pts_us) override;
    bool is_connected() const override;
    void shutdown() override;

private:
    void* handle_ = nullptr;   // Zig tarafındaki Transport*
};

}  // namespace rj::pipeline::output
