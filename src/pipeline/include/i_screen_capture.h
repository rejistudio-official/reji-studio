#pragma once
#include <cstdint>
#include <memory>

#ifdef _WIN32
struct ID3D11Device;
#endif

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

#ifdef _WIN32
    // Returns the D3D11 device used by this capture implementation.
    // DxgiScreenCapture returns its encode-GPU device; WgcScreenCapture its
    // internally created device. Returns nullptr if not applicable.
    virtual ID3D11Device* d3d_device() const noexcept { return nullptr; }
#endif

    static std::unique_ptr<IScreenCapture> create();
};

} // namespace rj
