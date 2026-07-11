// src/pipeline/recovery_coordinator.cpp
//
// RecoveryCoordinator implementasyonu. Windows'a özel (capture/encode_subsystem
// gibi yalnızca WIN32 altında derlenir). Davranış, Pipeline::Impl'in eski
// handle_device_lost() koduyla BİREBİR aynıdır — sıra korunmuştur (Aşama 9 saf
// çıkarma). dbglog / seh_connection_lost / kCaptureTimeout, pipeline.cpp'deki
// anon-namespace eşdeğerleriyle aynı (modül bağımsızlığı için burada tanımlı).
#include "recovery_coordinator.h"
#include "capture_subsystem.h"
#include "encode_subsystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#endif

#include <cstdarg>
#include <cstdio>

#include "capture_dxgi.h"   // reji::DxgiCapturePipeline::encode_gpu()->d3d_device()
#include "ffi_bridge.h"     // rj_connection_lost (SEH-leaf içinde)
#include "seh_filter.h"     // V8/I10: paylaşımlı SEH filtresi

namespace rj {
namespace {

// pipeline.cpp anon-namespace ile aynı: DXGI capture timeout (ms, 60 Hz bütçesi).
constexpr uint32_t kCaptureTimeout = 17;

// pipeline.cpp::dbglog ile aynı (OutputDebugStringA + stderr).
inline void dbglog(const char* fmt, ...) noexcept {
    char buf[256]; va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    fprintf(stderr, "[reji] %s\n", buf); fflush(stderr);
}

// SEH leaf: FFI çağrısı __try içinde — __declspec(noinline), yok edilebilir local yok.
__declspec(noinline)
static void seh_connection_lost(const char* reason) noexcept {
    SehCapture cap{};
    __try   { rj_connection_lost(reason); }
    __except(seh_filter(GetExceptionInformation(), SehSite::ConnectionLost, &cap)) {}
    if (cap.fired) seh_report(cap, SehSite::ConnectionLost);
}

} // namespace

bool RecoveryCoordinator::handle_device_lost(
    CaptureSubsystem&           capture,
    EncodeSubsystem&            encode,
    const rj::Pipeline::Config& cfg,
    uint32_t                    bitrate_kbps,
    std::atomic<uint32_t>&      width,
    std::atomic<uint32_t>&      height)
{
    if (!capture.dxgi()) {
        // WGC path — capture device lost kontrolü
        ID3D11Device* dev = capture.d3d_device();
        if (!dev) return false;
        HRESULT reason = dev->GetDeviceRemovedReason();
        if (reason == S_OK) return false;

        dbglog("[Pipeline] WGC device lost: 0x%08lX — reinit",
               static_cast<unsigned long>(reason));
        seh_connection_lost("wgc-device-lost");

        capture.shutdown();
        CaptureSubsystem::Config cap_cfg;
        cap_cfg.timeout_ms          = kCaptureTimeout;
        cap_cfg.allow_cross_adapter = true;
        // Yeni WGC capture recast'te null'a düşer → capture_dxgi_ null kalır
        // (eski davranışla aynı; WGC→DXGI mid-session bir senaryo değil).
        if (!capture.init(cap_cfg)) {
            dbglog("[Pipeline] WGC reinit failed");
            return false;
        }
        dbglog("[Pipeline] WGC reinit OK");
        return true;
    }

    ID3D11Device* dev = capture.dxgi()->encode_gpu()
                        ? capture.dxgi()->encode_gpu()->d3d_device()
                        : nullptr;
    if (!dev) return false;

    HRESULT reason = dev->GetDeviceRemovedReason();
    if (reason == S_OK) return false;  // transient encode error, not TDR

    dbglog("[Pipeline] TDR device removed reason=0x%08lX",
           static_cast<unsigned long>(reason));
    seh_connection_lost("gpu-device-lost");

    encode.shutdown();   // native teardown (~NvencEncoder) + reset
    capture.shutdown();  // capture_ reset + capture_dxgi_ null

    CaptureSubsystem::Config cap_cfg;
    cap_cfg.timeout_ms          = kCaptureTimeout;
    cap_cfg.allow_cross_adapter = true;
    if (!capture.init(cap_cfg)) {
        dbglog("[Pipeline] TDR capture reinit failed");
        return false;
    }
    width.store(capture.width(), std::memory_order_release);
    height.store(capture.height(), std::memory_order_release);

    ID3D11Device* encode_device = nullptr;
    if (capture.dxgi() && capture.dxgi()->encode_gpu()) {
        encode_device = capture.dxgi()->encode_gpu()->d3d_device();
    } else if (capture.has_capture()) {
        encode_device = capture.d3d_device();  // WGC path
    }
    if (!encode_device) {
        dbglog("[Pipeline] TDR: no encode device — encoder reinit skipped");
        return true;
    }

    EncodeSubsystem::Config enc_cfg;
    enc_cfg.width            = width.load(std::memory_order_acquire);
    enc_cfg.height           = height.load(std::memory_order_acquire);
    enc_cfg.fps_num          = cfg.fps;
    enc_cfg.fps_den          = 1;
    const uint32_t bps       = bitrate_kbps;
    enc_cfg.bitrate_kbps     = bps;
    enc_cfg.max_bitrate_kbps = bps + bps / 4;
    // Saklı packet_cb ile yeniden kur (EncodeSubsystem::reinit).
    if (!encode.reinit(encode_device, enc_cfg)) {
        dbglog("[Pipeline] TDR encoder reinit failed");
        return false;
    }

    dbglog("[Pipeline] TDR recovery complete");
    return true;
}

} // namespace rj
