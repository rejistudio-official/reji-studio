#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "reji_constants.h"

namespace rj {

enum class TransportProtocol : uint32_t {
    Srt  = 0,
    Rtmp = 1,   // Faz2/Aşama2.2 — düz rtmp:// (NO_CRYPTO, TLS kararı A)
};

class ITransport {
public:
    struct Config {
        TransportProtocol protocol = TransportProtocol::Srt;
        // Srt:  host = sunucu adı/IP (port ayrı alanda).
        // Rtmp: host = TAM ingest URL'i ("rtmp://host/app/STREAM_KEY"); port kullanılmaz.
        std::string host;
        uint16_t    port           = 9000;
        uint32_t    latency_ms     = rj::constants::kSrtLatencyMs;  // SRT'ye özel
        uint32_t    bandwidth_kbps = 0;   // 0 = sınırsız; SRT'ye özel, RTMP yok sayar
        bool        caller_mode    = true;
    };

    virtual ~ITransport() = default;
    virtual bool init(const Config&)                                    = 0;
    virtual bool send(const uint8_t* data, size_t size, int64_t pts_us) = 0;
    virtual bool is_connected() const                                   = 0;
    virtual void shutdown()                                             = 0;

    static std::unique_ptr<ITransport> create(TransportProtocol proto);
};

} // namespace rj
