#pragma once
#include <cstdint>
#include <memory>

namespace rj {

struct CapturedFrame {
    enum class HandleType { D3D11, DmaBuf, IOSurface, CpuBuffer };
    HandleType  type;
    void*       handle;
    uint32_t    width;
    uint32_t    height;
    uint64_t    timestamp_us;
};

class IScreenCapture {
public:
    struct Config {
        uint32_t output_index  = 0;
        uint32_t timeout_ms    = 17;
        bool     allow_cross_adapter = true;
    };

    virtual ~IScreenCapture() = default;
    virtual bool          init(const Config&)  = 0;
    virtual CapturedFrame next_frame()         = 0;
    virtual uint32_t      width()  const       = 0;
    virtual uint32_t      height() const       = 0;
    virtual void          shutdown()           = 0;

    static std::unique_ptr<IScreenCapture> create();
};

} // namespace rj
