#pragma once
#include <cstdint>
#include <functional>
#include <span>

namespace rj {

class IVideoEncoder {
public:
    struct Config {
        uint32_t width        = 1920;
        uint32_t height       = 1080;
        uint32_t fps          = 60;
        uint32_t bitrate_kbps = 6000;
        uint32_t max_bitrate_kbps = 8000;
    };

    struct Packet {
        const uint8_t* data;
        size_t         size;
        int64_t        pts_us;
        bool           keyframe;
    };

    using PacketCallback = std::function<void(const Packet&)>;

    virtual ~IVideoEncoder() = default;
    virtual bool init(void* device, const Config&, PacketCallback) = 0;
    virtual bool encode_frame(void* texture, int64_t pts_us)       = 0;
    virtual bool set_bitrate(uint32_t kbps)                        = 0;
    virtual void flush()                                           = 0;
    virtual void shutdown()                                        = 0;

    static std::unique_ptr<IVideoEncoder> create();
};

} // namespace rj
