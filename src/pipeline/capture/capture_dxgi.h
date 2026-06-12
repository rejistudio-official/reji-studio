#pragma once
#ifdef RJ_PLATFORM_WINDOWS

#include "gpu_resource_manager.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <wrl/client.h>
#include <d3d11.h>
#include <dxgi1_2.h>

namespace rj { class FrameProfiler; }

namespace reji {

// ---------------------------------------------------------------------------
// GpuScan — inventory of all hardware adapters found at init time
// ---------------------------------------------------------------------------
struct GpuScanEntry {
    wchar_t  description[128];
    uint32_t vendor_id;
    uint64_t dedicated_vram_mb;
};

struct GpuScan {
    GpuScanEntry entries[8];
    uint32_t     count;
};

// ---------------------------------------------------------------------------
// CaptureFrame — one captured desktop frame (non-owning view)
//
// Valid only between DxgiCaptureSession::acquire() and release_frame().
// Do NOT cache or hold across frames.
// ---------------------------------------------------------------------------
struct CaptureFrame {
    ID3D11Texture2D* texture      = nullptr;  ///< on display GPU; valid until release_frame()
    uint32_t         width        = 0;
    uint32_t         height       = 0;
    DXGI_FORMAT      format       = DXGI_FORMAT_UNKNOWN;
    LONGLONG         present_time = 0;        ///< QPC ticks from DXGI_OUTDUPL_FRAME_INFO
};

// ---------------------------------------------------------------------------
// DxgiCaptureSession — wraps IDXGIOutputDuplication for one monitor output
//
// Lifetime rules:
//   - Call acquire() to get a frame; it blocks for at most timeout_ms.
//   - Always call release_frame() after a successful acquire(), even on error paths.
//   - On DXGI_ERROR_ACCESS_LOST (screen lock, DPI change, remote session switch),
//     needs_reinit() returns true. Call reinit() before the next acquire().
// ---------------------------------------------------------------------------
class DxgiCaptureSession {
public:
    struct Config {
        uint32_t output_index = 0;   ///< 0 = primary monitor
        uint32_t timeout_ms   = 17;  ///< per-acquire wait limit (~60 fps)
    };

    DxgiCaptureSession() = default;
    ~DxgiCaptureSession() { shutdown(); }

    DxgiCaptureSession(const DxgiCaptureSession&) = delete;
    DxgiCaptureSession& operator=(const DxgiCaptureSession&) = delete;

    bool init(ID3D11Device* device, IDXGIAdapter* adapter, const Config& cfg);
    void shutdown();

    /// Wait for and latch the next changed desktop frame.
    /// Returns false on timeout (no new frame) or session loss (check needs_reinit()).
    bool acquire(CaptureFrame& out_frame);

    /// Release the current DXGI frame back to the duplication engine.
    /// Must be called after every successful acquire() before the next acquire().
    void release_frame();

    /// Recreate IDXGIOutputDuplication after DXGI_ERROR_ACCESS_LOST.
    bool reinit();

    bool             needs_reinit()   const { return needs_reinit_; }
    DXGI_FORMAT      surface_format() const { return surface_format_; }
    uint32_t         width()          const { return width_; }
    uint32_t         height()         const { return height_; }
    ID3D11Texture2D* last_frame_tex() const { return frame_tex_.Get(); }

    /// Set the FrameProfiler for timing acquire operations.
    /// Profiler is borrowed; caller must manage lifetime.
    void setProfiler(rj::FrameProfiler* profiler) {
        profiler_ = profiler;
    }

private:
    bool create_duplication();

    Microsoft::WRL::ComPtr<IDXGIAdapter>           adapter_;
    Microsoft::WRL::ComPtr<ID3D11Device>           device_;
    Microsoft::WRL::ComPtr<IDXGIOutputDuplication> duplication_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D>        frame_tex_;  ///< ref held during frame

    rj::FrameProfiler* profiler_ = nullptr;  ///< Borrowed reference for timing

    Config      config_;
    DXGI_FORMAT surface_format_ = DXGI_FORMAT_UNKNOWN;
    uint32_t    width_          = 0;
    uint32_t    height_         = 0;
    bool        frame_held_     = false;
    bool        needs_reinit_   = false;
    bool        initialized_    = false;
};

// ---------------------------------------------------------------------------
// DxgiCapturePipeline — high-level desktop capture + cross-adapter transfer
//
// Owns the display GpuContext (AMD iGPU), the encode GpuContext (NVIDIA),
// a DxgiCaptureSession, and a GpuResourceManager.
//
// capture_next() drives the full pipeline:
//   AcquireNextFrame  →  GPU-blit to shared tex  →  return NVENC-ready texture
//
// Usage:
//   DxgiCapturePipeline p;
//   p.init(cfg);
//   while (running) {
//       if (auto* tex = p.capture_next()) {
//           nvenc.submit(tex);
//       }
//   }
// ---------------------------------------------------------------------------
class DxgiCapturePipeline {
public:
    struct Config {
        uint32_t output_index        = 0;     ///< which monitor to capture
        uint32_t timeout_ms          = 17;    ///< per-frame acquire timeout
        bool     allow_cross_adapter = true;  ///< false forces same-adapter path
    };

    DxgiCapturePipeline() = default;
    ~DxgiCapturePipeline() { shutdown(); }

    DxgiCapturePipeline(const DxgiCapturePipeline&) = delete;
    DxgiCapturePipeline& operator=(const DxgiCapturePipeline&) = delete;

    bool init(const Config& cfg);
    void shutdown();

    /// Capture next frame and transfer to encode GPU.
    /// Also copies to preview staging (if init_preview_staging() was called).
    /// Returns NVENC-ready texture on encode GPU, nullptr on timeout or error.
    ID3D11Texture2D* capture_next();

    /// Enumerate all hardware GPU adapters and log their info.
    /// Must be called after CreateDXGIFactory1 succeeds.
    static bool scan_gpus(IDXGIFactory1* factory, GpuScan& out);

    /// Allocate the CPU-readable staging texture for preview.
    /// Call once after init(), before the first run_frame() that uses preview.
    bool init_preview_staging();

    /// D10a: Notify whether a preview callback is registered.
    /// Thread-safe — may be called from any thread.
    void set_preview_requested(bool enabled);

    /// Map the staging texture written by the last capture_next().
    /// Returns false if no frame is pending or Map fails.
    /// Must call unmap_preview_frame() after use.
    bool map_preview_frame(const void** out_data, int* out_pitch);

    /// Unmap the staging texture. Pair with every successful map_preview_frame().
    void unmap_preview_frame();

    GpuContext*      display_gpu()    const { return display_ctx_.get(); }
    GpuContext*      encode_gpu()     const { return encode_ctx_.get(); }
    const GpuScan&   gpu_scan()       const { return gpu_scan_; }
    uint32_t         width()          const;
    uint32_t         height()         const;
    DXGI_FORMAT      surface_format() const;
    bool             is_cross_adapter() const;

    /// v0.5.1: GPU-side shared texture for Vulkan external memory export
    ID3D11Texture2D* shared_texture() const { return shared_texture_.Get(); }

    /// AMD fallback: disable keyed-mutex sync when VK_KHR_win32_keyed_mutex unavailable.
    void set_use_keyed_mutex(bool v) { use_keyed_mutex_ = v; }

    /// Set the FrameProfiler for timing DXGI acquire operations.
    /// Profiler is borrowed; caller must manage lifetime.
    void setProfiler(rj::FrameProfiler* profiler);

private:
    bool find_display_adapter(IDXGIFactory1* factory, IDXGIAdapter** out);
    bool find_encode_adapter(IDXGIFactory1* factory, IDXGIAdapter* display,
                             IDXGIAdapter** out);

    void ensure_preview_staging();  ///< D10a: lazy staging alloc/free per frame

    std::shared_ptr<D3D11GpuContext>     display_ctx_;
    std::shared_ptr<D3D11GpuContext>     encode_ctx_;
    std::unique_ptr<DxgiCaptureSession>  capture_;
    std::unique_ptr<GpuResourceManager>  resource_mgr_;

    rj::FrameProfiler* profiler_ = nullptr;  ///< Borrowed reference from Pipeline

    // v0.5.1: Dual-texture approach for GPU/CPU paths
    Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture_;   ///< GPU-side (DEFAULT + SHARED flags)
    Microsoft::WRL::ComPtr<IDXGIKeyedMutex> keyed_mutex_shared_; ///< B6: sync D3D11 write ↔ Vulkan read
    Microsoft::WRL::ComPtr<ID3D11Texture2D> staging_texture_;  ///< CPU-side (STAGING + CPU_ACCESS_READ)
    Microsoft::WRL::ComPtr<ID3D11Texture2D> preview_staging_;  ///< Legacy preview (deprecated in v0.5.1)
    bool preview_staging_dirty_ = false;
    bool preview_mapped_        = false;

    mutable std::mutex cb_mutex_;       ///< D10a: guards preview_cb_
    bool               preview_cb_ = false;  ///< D10a: true when Pipeline::preview_cb is registered

    GpuScan gpu_scan_{};
    Config  config_;
    bool    initialized_    = false;
    bool    use_keyed_mutex_ = false;
};

} // namespace reji
#endif // RJ_PLATFORM_WINDOWS
