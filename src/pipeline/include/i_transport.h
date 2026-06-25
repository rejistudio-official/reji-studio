#pragma once
#include <cstdint>
#include <string>

namespace rj {

class ITransport {
public:
    struct Config {
        std::string host;
        uint16_t    port          = 9000;
        uint32_t    latency_ms    = 200;
        bool        caller_mode   = true;
    };

    virtual ~ITransport() = default;
    virtual bool init(const Config&)                                    = 0;
    virtual bool send(const uint8_t* data, size_t size, int64_t pts_us) = 0;
    virtual bool is_connected() const                                   = 0;
    virtual void shutdown()                                             = 0;

    static std::unique_ptr<ITransport> create();
};

} // namespace rj
