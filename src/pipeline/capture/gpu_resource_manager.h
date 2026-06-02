#pragma once
#ifdef RJ_PLATFORM_WINDOWS

#include <cstdint>
#include <memory>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>

namespace reji {

// ---------------------------------------------------------------------------
// GpuInfo — hardware properties for one physical GPU (filled during init)
// ---------------------------------------------------------------------------
struct GpuInfo {
    wchar_t  description[128];
    uint32_t vendor_id;
    uint64_t dedicated_vram_mb;
    bool     valid;
};

// ---------------------------------------------------------------------------
// GpuContext — abstract GPU adapter + D3D11 device pair
// ---------------------------------------------------------------------------
class GpuContext {
public:
    virtual ~GpuContext() = default;
    virtual ID3D11Device*        d3d_device()   const = 0;
    virtual ID3D11DeviceContext* d3d_context()  const = 0;
    virtual IDXGIAdapter*        dxgi_adapter() const = 0;
    virtual const wchar_t*       description()  const = 0;
};

// ---------------------------------------------------------------------------
// D3D11GpuContext — concrete GpuContext wrapping a single D3D11 device
// ---------------------------------------------------------------------------
class D3D11GpuContext final : public GpuContext {
public:
    D3D11GpuContext() = default;
    ~D3D11GpuContext() { shutdown(); }

    D3D11GpuContext(const D3D11GpuContext&) = delete;
    D3D11GpuContext& operator=(const D3D11GpuContext&) = delete;

    bool init(IDXGIAdapter* adapter);
    void shutdown();

    ID3D11Device*        d3d_device()   const override { return device_.Get(); }
    ID3D11DeviceContext* d3d_context()  const override { return context_.Get(); }
    IDXGIAdapter*        dxgi_adapter() const override { return adapter_.Get(); }
    const wchar_t*       description()  const override { return desc_; }

private:
    Microsoft::WRL::ComPtr<IDXGIAdapter>        adapter_;
    Microsoft::WRL::ComPtr<ID3D11Device>        device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    wchar_t desc_[128] = {};
};

// ---------------------------------------------------------------------------
// GpuResourceManager — cross-adapter VRAM-to-VRAM texture transfer
//
// Topology (RTX 4070 Laptop):
//   display GPU  = AMD iGPU  (screen connected here, DXGI Duplication runs here)
//   encode  GPU  = NVIDIA    (NVENC lives here)
//
// Transfer path (cross-adapter):
//   DXGI frame tex (AMD VRAM)
//       │  CopyResource
//       ▼
//   shared_tex_display_  ─── NT SharedHandle ───►  encode_tex_ (NVIDIA VRAM)
//                                                       │
//                                                    NVENC
//
// Keyed mutex protocol:
//   key=0 → display GPU owns (may write)
//   key=1 → encode  GPU owns (may read)
//
// Same-adapter fallback (single GPU systems):
//   DXGI frame tex  ─── CopyResource ───►  encode_tex_  (same VRAM)
//   No shared handle / keyed mutex overhead.
// ---------------------------------------------------------------------------
class GpuResourceManager {
public:
    GpuResourceManager() = default;
    ~GpuResourceManager() { shutdown(); }

    GpuResourceManager(const GpuResourceManager&) = delete;
    GpuResourceManager& operator=(const GpuResourceManager&) = delete;

    bool init(std::shared_ptr<GpuContext> display_gpu,
              std::shared_ptr<GpuContext> encode_gpu,
              uint32_t width, uint32_t height, DXGI_FORMAT format);

    /// GPU-blit src (on display GPU) into the internal staging texture,
    /// returns a texture on the encode GPU ready for NVENC.
    /// Returns nullptr on failure or keyed-mutex timeout.
    /// Non-reentrant; caller must serialize.
    ID3D11Texture2D* transfer(ID3D11Texture2D* src);

    void shutdown();

    bool same_adapter()   const { return same_adapter_; }
    bool is_initialized() const { return initialized_; }

    const GpuInfo& display_info() const { return display_info_; }
    const GpuInfo& encode_info()  const { return encode_info_; }

private:
    bool create_same_adapter_staging(uint32_t w, uint32_t h, DXGI_FORMAT fmt);
    bool create_cross_adapter_shared(uint32_t w, uint32_t h, DXGI_FORMAT fmt);

    /// Spin-wait for the display GPU to finish executing pending commands.
    /// Required before releasing the keyed mutex to the encode GPU.
    void wait_display_gpu_idle();

    std::shared_ptr<GpuContext> display_gpu_;
    std::shared_ptr<GpuContext> encode_gpu_;

    // Cross-adapter path: shared texture pair (NT handle + keyed mutex)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_tex_display_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_display_;
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_encode_;

    // Encode-side output texture (same-adapter: plain staging; cross-adapter: shared open)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> encode_tex_;

    // GPU event query for CopyResource completion tracking (cross-adapter only)
    Microsoft::WRL::ComPtr<ID3D11Query> copy_fence_;

    GpuInfo display_info_{};
    GpuInfo encode_info_{};

    HANDLE shared_handle_ = nullptr;
    bool   same_adapter_  = false;
    bool   initialized_   = false;
};

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
