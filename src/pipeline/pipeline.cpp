// src/pipeline/pipeline.cpp
//
// Reji Studio Pipeline  DXGI capture  NVENC encode  SRT transport
// Compiler flag REQUIRED: /EHa  (mixed SEH + C++ exception handling)
// Language: C++17
//
// Rules enforced:
//    RAII  no owning raw pointers
//    Every extern-"C" FFI call wrapped in __declspec(noinline) SEH leaf
//    No C++ objects with non-trivial destructors as locals in __try scope
//    Hot-path: no heap allocation
//    All public methods return bool (void prohibited)
//    CoInitializeEx / CoUninitialize paired
//    timeBeginPeriod(1) / timeEndPeriod(1) paired
//    rj_command_drain clamped [0,8]; negative return logged
//    frame_drops delta: exchange(0) after each metrics push
//    std::atomic<SrtOutput*> srt_atomic_ for start/stop_stream thread safety

#include "include/pipeline.h"
#include "include/i_screen_capture.h"
#include "include/frame_profiler.h"
#include "gpu/external_memory_bridge.h"
#include "gpu/vulkan_initializer.h"
#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan.h>
#endif

#ifdef _WIN32
#include "capture/capture_dxgi.h"
#include "capture/capture_dxgi_screen.h"
#include "encode/encode_nvenc.h"
#include "audio/wasapi_capture.h"
#include "output/srt_output.h"
#include "ffi_bridge.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <timeapi.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>

//  FFI struct size verification 
// Natural alignment (no pack pragma) matches Rust #[repr(C)].
// RjMetricSample: 4 + 4(pad) + 8 + 4 + 4 + 4 + 4 + 4(trail-pad) = 40
// RjCommand:      4 + 4(pad) + 8 + 4 + 4                         = 24
// v0.4: RjMetricSample extended to 56 bytes (frame_drop_pct, temps, load, network)
// v0.5: gpu_load_pct eklendi; u64 alignment trailing pad → 64 bytes
static_assert(sizeof(RjMetricSample) == 64, "RjMetricSample ABI drift — expected 64 bytes (v0.5)");
static_assert(sizeof(RjCommand)      == 24, "RjCommand ABI drift");
static_assert(sizeof(RjAction)       == 20, "RjAction ABI drift — expected 20 bytes (v0.4)");

// Global pipeline pointer — set during Pipeline::init(), used by rj_ws_command.
// Atomic to ensure safe read from Tokio worker thread.
static std::atomic<rj::Pipeline*> g_pipeline{nullptr};

namespace {

//  Constants
constexpr uint32_t kMetricMagic    = RJ_METRIC_MAGIC;
constexpr int      kCmdDrainMax    = 8;
constexpr uint32_t kCaptureTimeout = 17;   // ms  60 Hz budget
constexpr int64_t  kResyncFrames   = 4;    // catch-up spiral guard

//  QPC helpers 
inline int64_t qpc_ticks() noexcept {
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart;
}
inline int64_t ticks_to_us(int64_t t, int64_t freq) noexcept {
    return (t * 1'000'000LL) / freq;
}
inline void dbglog(const char* fmt, ...) noexcept {
    char buf[256]; va_list ap; va_start(ap, fmt);
    _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, ap); va_end(ap);
    OutputDebugStringA(buf); OutputDebugStringA("\n");
    fprintf(stderr, "[reji] %s\n", buf); fflush(stderr);
}

//  SEH leaf functions 
// Rules: __declspec(noinline), only POD params, no destructible locals.

__declspec(noinline)
static int seh_command_drain(RjCommand* buf, int max) noexcept {
    __try   { return rj_command_drain(buf, max); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

__declspec(noinline)
static void seh_metrics_push(const RjMetricSample* s) noexcept {
    __try   { rj_metrics_push(s); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline)
static void seh_start_monitor() noexcept {
    __try   { rj_start_monitor(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

__declspec(noinline)
static void seh_connection_lost(const char* reason) noexcept {
    __try   { rj_connection_lost(reason); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

struct SrtSendArgs {
    rj::pipeline::output::SrtOutput* out;
    const uint8_t*                   data;
    size_t                           size;
    int64_t                          pts;
};
__declspec(noinline)
static int seh_srt_send(SrtSendArgs* a) noexcept {
    __try   { return a->out->send_packet(a->data, a->size, a->pts) ? 0 : 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -2; }
}

__declspec(noinline)
static void seh_uninit_com(bool* ok) noexcept {
    __try   { CoUninitialize(); }
    __except(EXCEPTION_EXECUTE_HANDLER) { if (ok) *ok = false; }
}

// Shutdown subsystems via raw pointers  no C++ destructors in scope.
__declspec(noinline)
static bool seh_shutdown_subsystems(
    reji::pipeline::audio::WasapiCapture* audio,
    reji::NvencEncoder*                   enc,
    rj::pipeline::output::SrtOutput*      out) noexcept
{
    bool ok = true;
    __try {
        if (audio) { (void)audio->stop(); (void)audio->shutdown(); }
        if (enc)   { enc->flush(); enc->shutdown(); }
        if (out)   { (void)out->shutdown(); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

//  CPU meter 
class CpuMeter {
public:
    CpuMeter() noexcept {
        SYSTEM_INFO si{}; GetSystemInfo(&si);
        ncpus_ = si.dwNumberOfProcessors ? si.dwNumberOfProcessors : 1u;
    }
    float sample() noexcept {
        FILETIME c, e, k, u, now;
        if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return last_;
        GetSystemTimeAsFileTime(&now);
        uint64_t wall = ft64(now), busy = ft64(k) + ft64(u);
        if (prev_wall_) {
            uint64_t dwall = wall - prev_wall_, dbusy = busy - prev_busy_;
            if (dwall) {
                float pct = float(dbusy) / (float(dwall) * float(ncpus_)) * 100.f;
                last_ = pct < 0.f ? 0.f : pct > 100.f ? 100.f : pct;
            }
        }
        prev_wall_ = wall; prev_busy_ = busy;
        return last_;
    }
private:
    static uint64_t ft64(const FILETIME& f) noexcept {
        return (uint64_t(f.dwHighDateTime) << 32) | f.dwLowDateTime;
    }
    uint64_t prev_wall_ = 0, prev_busy_ = 0;
    uint32_t ncpus_     = 1;
    float    last_      = 0.f;
};

} // anonymous namespace

#endif // _WIN32

namespace rj {

// 
// Pipeline::Impl
// 
#ifdef _WIN32
struct Pipeline::Impl {
    Pipeline::Config cfg{};

    std::unique_ptr<rj::IScreenCapture>                      capture;
    reji::DxgiCapturePipeline*                               capture_dxgi_ = nullptr;
    std::unique_ptr<reji::NvencEncoder>                     encoder;
    std::unique_ptr<reji::pipeline::audio::WasapiCapture>   audio;
    std::unique_ptr<rj::pipeline::output::SrtOutput>        srt;
    std::unique_ptr<rj::MetricsCollector>                   metrics;  // v0.4+

    // v0.5.1: ExternalMemoryBridge for zero-copy D3D11↔Vulkan interop
    std::unique_ptr<rj::pipeline::gpu::ExternalMemoryBridge> ext_bridge;

    // Thread-safe SRT pointer: packet callback (encode thread) vs
    // stop_stream() / shutdown() (potentially another thread).
    std::atomic<rj::pipeline::output::SrtOutput*> srt_atomic{nullptr};

    // v0.4+: Action processing thread (polls rj_action_dequeue)
    std::thread action_processor;
    std::atomic<bool> action_processor_running{false};

    CpuMeter cpu;

    std::atomic<bool>    initialized{false};
    std::atomic<bool>    streaming{false};
    std::atomic<bool>    com_owned{false};
    std::atomic<bool>    timer_set{false};

    int64_t  qpc_freq         = 1;
    int64_t  frame_ticks      = 0;
    int64_t  next_deadline    = 0;
    int64_t  pts_origin       = 0;
    int64_t  last_frame_ticks = 0;
    uint32_t bitrate_kbps     = 0;

    std::atomic<uint32_t> frame_drops{0};
    std::array<RjCommand, kCmdDrainMax> cmd_buf{};

    // C6: SPSC ring buffer — action_processor (producer) → run_frame (consumer)
    struct FrameCmd { int action_type; int32_t param1; };
    static constexpr int kFrameCmdCap = 16;
    std::array<FrameCmd, kFrameCmdCap> frame_cmd_buf_{};
    std::atomic<uint32_t> frame_cmd_head_{0};
    std::atomic<uint32_t> frame_cmd_tail_{0};

    bool push_frame_cmd(const FrameCmd& cmd) noexcept {
        uint32_t head = frame_cmd_head_.load(std::memory_order_relaxed);
        uint32_t next = (head + 1) % kFrameCmdCap;
        if (next == frame_cmd_tail_.load(std::memory_order_acquire)) return false;
        frame_cmd_buf_[head] = cmd;
        frame_cmd_head_.store(next, std::memory_order_release);
        return true;
    }

    void apply_frame_cmd(const FrameCmd& cmd) noexcept {
        if (!encoder) return;
        switch (cmd.action_type) {
            case RJ_ACTION_BITRATE_REDUCE:
            case RJ_ACTION_BITRATE_RECOVER:
                if (cmd.param1 > 0) {
                    (void)encoder->set_bitrate(static_cast<uint32_t>(cmd.param1));
                    bitrate_kbps     = static_cast<uint32_t>(cmd.param1);
                    cfg.bitrate_kbps = static_cast<uint32_t>(cmd.param1);
                }
                break;
            case RJ_ACTION_SCALE_RESOLUTION:
                (void)encoder->set_resolution(cmd.param1 / 1000.0f);
                break;
            case RJ_ACTION_CAP_FPS:
                (void)encoder->set_fps_limit(static_cast<uint32_t>(cmd.param1));
                break;
            default:
                break;
        }
    }

    // Stored so TDR recovery can re-pass it to encoder->init.
    std::function<void(const reji::NvencEncoder::Packet&)> packet_cb;

    // Preview callback  called from run_frame() with CPU-mapped BGRA frame
    Pipeline::PreviewCallback        preview_cb;

    // v0.5.1: D3D11 zero-copy callback - called from run_frame() with staging texture
    Pipeline::D3D11FrameCallback     d3d11_frame_cb;

    // v0.5.1: Cache last frame images for get_last_frame_images() getter
    // E2: atomic — frame thread writes, GL thread reads via get_last_frame_images()
    std::atomic<VkImage> last_staging_vk{nullptr};
    std::atomic<VkImage> last_target_vk{nullptr};

    // WGC path CPU staging — lazily created on first preview frame
    Microsoft::WRL::ComPtr<ID3D11Texture2D> wgc_staging_tex_;

    void apply_command(const RjCommand& c) noexcept {
        switch (c.cmd_type) {
            case RJ_CMD_BITRATE_SET:
                if (encoder && c.param_u32 > 0) {
                    (void)encoder->set_bitrate(c.param_u32);
                    bitrate_kbps     = c.param_u32;
                    cfg.bitrate_kbps = c.param_u32;
                }
                break;
            case RJ_CMD_SCENE_SWITCH: break;  // v0.1 no-op
            case RJ_CMD_PREVIEW_FPS:  break;  // UI side
            default:
                dbglog("[Pipeline] unknown cmd_type=%u", c.cmd_type);
                break;
        }
    }

    // Called from NVENC packet callback (same thread as run_frame).
    // srt_atomic provides acquire/release visibility against stop_stream().
    static void on_packet(const reji::NvencEncoder::Packet& pkt,
                          Impl* self) noexcept {
        static std::atomic<int> pkt_count{0};
        int n = ++pkt_count;
        if (n <= 5 || n % 60 == 0)
            fprintf(stderr, "[NVENC] packet #%d size=%zu pts=%lld keyframe=%d\n",
                    n, pkt.size, (long long)pkt.pts, pkt.is_keyframe ? 1 : 0);
        fflush(stderr);

        if (!self->streaming.load(std::memory_order_acquire)) return;
        auto* out = self->srt_atomic.load(std::memory_order_acquire);
        if (!out) return;
        SrtSendArgs args{out, pkt.data, pkt.size, pkt.pts};
        int rc = seh_srt_send(&args);
        if (rc != 0)
            self->frame_drops.fetch_add(1, std::memory_order_relaxed);
    }

    // Audio callback  v0.1 stub; SRT mux not yet implemented.
    static void on_audio(const float*, uint32_t, uint32_t, uint32_t,
                         int64_t, void*) noexcept {}

    // GPU TDR recovery: query GetDeviceRemovedReason; reinit if device was lost.
    // MUST NOT be called from within a __try block (uses C++ objects).
    // Only applicable to the DXGI capture path; WGC path returns false immediately.
    bool handle_device_lost() {
        if (!capture_dxgi_) {
            // WGC path — capture device lost kontrolü
            ID3D11Device* dev = capture ? capture->d3d_device() : nullptr;
            if (!dev) return false;
            HRESULT reason = dev->GetDeviceRemovedReason();
            if (reason == S_OK) return false;

            dbglog("[Pipeline] WGC device lost: 0x%08lX — reinit",
                   static_cast<unsigned long>(reason));
            seh_connection_lost("wgc-device-lost");

            capture.reset();
            capture = rj::IScreenCapture::create();
            rj::IScreenCapture::Config cap_cfg;
            cap_cfg.timeout_ms          = kCaptureTimeout;
            cap_cfg.allow_cross_adapter = true;
            if (!capture || !capture->init(cap_cfg)) {
                dbglog("[Pipeline] WGC reinit failed");
                capture.reset();
                return false;
            }
            dbglog("[Pipeline] WGC reinit OK");
            return true;
        }

        ID3D11Device* dev = capture_dxgi_->encode_gpu()
                            ? capture_dxgi_->encode_gpu()->d3d_device()
                            : nullptr;
        if (!dev) return false;

        HRESULT reason = dev->GetDeviceRemovedReason();
        if (reason == S_OK) return false;  // transient encode error, not TDR

        dbglog("[Pipeline] TDR device removed reason=0x%08lX",
               static_cast<unsigned long>(reason));
        seh_connection_lost("gpu-device-lost");

        if (encoder) { encoder->flush(); encoder->shutdown(); encoder.reset(); }
        capture.reset();
        capture_dxgi_ = nullptr;

        rj::IScreenCapture::Config cap_cfg;
        cap_cfg.timeout_ms          = kCaptureTimeout;
        cap_cfg.allow_cross_adapter = true;
        capture = rj::IScreenCapture::create();
        if (!capture || !capture->init(cap_cfg)) {
            dbglog("[Pipeline] TDR capture reinit failed");
            capture.reset(); return false;
        }
        auto* dsc = dynamic_cast<reji::DxgiScreenCapture*>(capture.get());
        capture_dxgi_ = dsc ? dsc->pipeline() : nullptr;
        cfg.width  = capture->width();
        cfg.height = capture->height();

        ID3D11Device* encode_device = nullptr;
        if (capture_dxgi_ && capture_dxgi_->encode_gpu()) {
            encode_device = capture_dxgi_->encode_gpu()->d3d_device();
        } else if (capture) {
            encode_device = capture->d3d_device();  // WGC path
        }
        if (!encode_device) {
            dbglog("[Pipeline] TDR: no encode device — encoder reinit skipped");
            return true;
        }

        reji::NvencEncoder::Config enc_cfg;
        enc_cfg.width            = cfg.width;
        enc_cfg.height           = cfg.height;
        enc_cfg.fps_num          = cfg.fps;
        enc_cfg.fps_den          = 1;
        enc_cfg.bitrate_kbps     = bitrate_kbps;
        enc_cfg.max_bitrate_kbps = bitrate_kbps + bitrate_kbps / 4;
        encoder = std::make_unique<reji::NvencEncoder>();
        if (!encoder->init(encode_device, enc_cfg, packet_cb)) {
            dbglog("[Pipeline] TDR encoder reinit failed");
            encoder.reset(); return false;
        }

        dbglog("[Pipeline] TDR recovery complete");
        return true;
    }
};
#else
struct Pipeline::Impl {};
#endif // _WIN32

// 
// Pipeline  public API
// 

Pipeline::Pipeline()  = default;
Pipeline::~Pipeline() { (void)shutdown(); }

#ifndef _WIN32
bool Pipeline::init(const Config&)              { return false; }
bool Pipeline::start_stream()                   { return false; }
bool Pipeline::stop_stream()                    { return false; }
bool Pipeline::is_running() const               { return false; }
bool Pipeline::set_preview_callback(PreviewCallback) { return false; }
bool Pipeline::run_frame()                      { return false; }
bool Pipeline::shutdown()                       { return true;  }
#else

bool Pipeline::init(const Config& cfg_in) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    auto& s = *impl_;

    if (s.initialized.load(std::memory_order_acquire)) {
        dbglog("[Pipeline] init: already initialized");
        return true;
    }
    if (cfg_in.fps == 0) {
        dbglog("[Pipeline] init: fps=0 invalid");
        return false;
    }

    // Initialize profiler
    profiler_ = std::make_unique<rj::FrameProfiler>();

    // Initialize metrics collector (v0.4+ Runtime Adaptation)
    s.metrics = std::make_unique<rj::MetricsCollector>();

    s.cfg                          = cfg_in;
    s.cfg.original_bitrate_kbps    = cfg_in.bitrate_kbps;
    s.bitrate_kbps                 = cfg_in.bitrate_kbps;

    //  COM 
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        dbglog("[Pipeline] COM apartment set by caller  uninit skipped");
        s.com_owned.store(false, std::memory_order_release);
    } else if (FAILED(hr)) {
        dbglog("[Pipeline] CoInitializeEx failed: 0x%08lX", hr);
        return false;
    } else {
        s.com_owned.store(true, std::memory_order_release);
    }

    //  Timer resolution (timeEndPeriod in shutdown) 
    if (timeBeginPeriod(1) == TIMERR_NOERROR)
        s.timer_set.store(true, std::memory_order_release);

    //  QPC 
    LARGE_INTEGER freq{};
    if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
        dbglog("[Pipeline] QueryPerformanceFrequency failed");
        (void)shutdown(); return false;
    }
    s.qpc_freq      = freq.QuadPart;
    s.frame_ticks   = s.qpc_freq / static_cast<int64_t>(cfg_in.fps);
    s.pts_origin    = qpc_ticks();
    s.next_deadline = qpc_ticks() + s.frame_ticks;

    //  IScreenCapture::create() — WGC tercihli, DXGI fallback
    {
        rj::IScreenCapture::Config cap_cfg;
        cap_cfg.timeout_ms          = kCaptureTimeout;
        cap_cfg.allow_cross_adapter = true;
        s.capture = rj::IScreenCapture::create();
        if (!s.capture) {
            dbglog("[Pipeline] IScreenCapture::create() returned null");
            (void)shutdown(); return false;
        }
        if (!s.capture->init(cap_cfg)) {
            dbglog("[Pipeline] IScreenCapture::init failed");
            (void)shutdown(); return false;
        }
        // Cache typed DXGI pointer for encode-specific ops (null when WGC active)
        auto* dsc = dynamic_cast<reji::DxgiScreenCapture*>(s.capture.get());
        s.capture_dxgi_ = dsc ? dsc->pipeline() : nullptr;
    }
    if (s.capture_dxgi_)
        s.capture_dxgi_->setProfiler(profiler_.get());
    // Authoritative dimensions come from the actual display output.
    s.cfg.width  = s.capture->width();
    s.cfg.height = s.capture->height();

    //  ExternalMemoryBridge (v0.5.1 zero-copy D3D11↔Vulkan — DXGI path only)
    if (s.capture_dxgi_) {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        s.capture_dxgi_->set_use_keyed_mutex(vk && vk->use_keyed_mutex());
        fprintf(stderr, "[Pipeline] VulkanInit: device=%p phys=%p\n",
                (void*)(vk ? vk->device() : nullptr),
                (void*)(vk ? vk->physical_device() : nullptr));
        fflush(stderr);
        VkDevice vk_device = vk ? vk->device() : VK_NULL_HANDLE;
        VkPhysicalDevice vk_phys = vk ? vk->physical_device() : VK_NULL_HANDLE;
        s.ext_bridge = std::make_unique<rj::pipeline::gpu::ExternalMemoryBridge>(
            vk_device, vk_phys);
        if (s.ext_bridge) {
            if (!s.ext_bridge->initialize_image_pool(VK_FORMAT_B8G8R8A8_UNORM,
                                                       s.cfg.width, s.cfg.height)) {
                dbglog("[Pipeline] ExternalMemoryBridge::initialize_image_pool failed");
            }
        }
    }

    //  NvencEncoder — DXGI ve WGC path desteklenir
    {
        ID3D11Device* encode_device = nullptr;
        if (s.capture_dxgi_ && s.capture_dxgi_->encode_gpu()) {
            encode_device = s.capture_dxgi_->encode_gpu()->d3d_device();
        } else if (s.capture) {
            encode_device = s.capture->d3d_device();  // WGC: kendi D3D11 device'ı
        }
        if (encode_device) {
            reji::NvencEncoder::Config enc_cfg;
            enc_cfg.width            = s.cfg.width;
            enc_cfg.height           = s.cfg.height;
            enc_cfg.fps_num          = cfg_in.fps;
            enc_cfg.fps_den          = 1;
            enc_cfg.bitrate_kbps     = cfg_in.bitrate_kbps;
            enc_cfg.max_bitrate_kbps = cfg_in.bitrate_kbps + cfg_in.bitrate_kbps / 4;
            s.packet_cb = [&s](const reji::NvencEncoder::Packet& pkt) noexcept {
                Impl::on_packet(pkt, &s);
            };
            s.encoder = std::make_unique<reji::NvencEncoder>();
            if (!s.encoder->init(encode_device, enc_cfg, s.packet_cb)) {
                dbglog("[Pipeline] NvencEncoder::init failed -- running in preview-only mode");
                s.encoder.reset();
            }
        }
    }

    //  WasapiCapture (optional) 
    if (cfg_in.audio_enabled) {
        reji::pipeline::audio::WasapiCapture::Config acfg{};
        acfg.exclusive_mode = false;
        acfg.sample_rate    = 48000;
        acfg.channels       = 2;
        acfg.bit_depth      = 32;
        acfg.buffer_ms      = 50;
        acfg.loopback       = cfg_in.loopback;
        s.audio = std::make_unique<reji::pipeline::audio::WasapiCapture>();
        if (!s.audio->init(acfg, &Impl::on_audio)) {
            dbglog("[Pipeline] WasapiCapture::init failed  audio disabled");
            s.audio.reset();
        }
    }

    //  SrtOutput 
    rj::pipeline::output::SrtOutput::Config scfg{};
    strncpy_s(scfg.host, sizeof(scfg.host), cfg_in.srt_host, sizeof(scfg.host) - 1);
    scfg.port           = cfg_in.srt_port;
    scfg.latency_ms     = 200;
    scfg.bandwidth_kbps = 0;
    scfg.caller_mode    = true;
    s.srt = std::make_unique<rj::pipeline::output::SrtOutput>();
    if (!s.srt->init(scfg)) {
        dbglog("[Pipeline] SrtOutput::init failed -- running without SRT output");
        s.srt.reset();  // SRT olmadan devam et
    }

    //  Rust monitor 

    // v0.2 preview staging — allocate once, no hot-path heap (DXGI only)
    if (s.preview_cb && s.capture_dxgi_) {
        if (!s.capture_dxgi_->init_preview_staging())
            dbglog("[Pipeline] init_preview_staging failed -- preview disabled");
    }
    seh_start_monitor();

    // v0.4+: Start action processor thread (polls rj_action_dequeue)
    s.action_processor_running.store(true, std::memory_order_release);
    s.action_processor = std::thread([this] { action_processor_main(); });

    s.initialized.store(true, std::memory_order_release);
    g_pipeline.store(this, std::memory_order_release);
    dbglog("[Pipeline] init OK %ux%u@%u fps %u kbps audio=%d loopback=%d",
           s.cfg.width, s.cfg.height, cfg_in.fps, cfg_in.bitrate_kbps,
           cfg_in.audio_enabled ? 1 : 0, cfg_in.loopback ? 1 : 0);
    return true;
}

bool Pipeline::start_stream() {
    if (!impl_ || !impl_->initialized.load(std::memory_order_acquire)) return false;
    if (impl_->streaming.exchange(true, std::memory_order_acq_rel)) return true;

    if (!impl_->srt) {
        dbglog("[Pipeline] start_stream: SRT not initialized -- preview-only mode");
        // SRT olmadan streaming flag'ini set et, preview devam etsin
    }

    // Publish SRT pointer before any packet callback can observe it.
    impl_->srt_atomic.store(impl_->srt.get(), std::memory_order_release);
    if (impl_->audio) (void)impl_->audio->start();
    dbglog("[Pipeline] streaming started");
    return true;
}

bool Pipeline::stop_stream() {
    if (!impl_) return false;
    if (!impl_->streaming.exchange(false, std::memory_order_acq_rel)) return true;

    // Null the atomic pointer before any further packets can be sent.
    impl_->srt_atomic.store(nullptr, std::memory_order_release);
    if (impl_->audio) (void)impl_->audio->stop();
    dbglog("[Pipeline] streaming stopped");
    return true;
}

bool Pipeline::is_running() const {
    return impl_ &&
           impl_->initialized.load(std::memory_order_acquire) &&
           impl_->streaming.load(std::memory_order_acquire);
}

bool Pipeline::set_preview_callback(PreviewCallback cb) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->preview_cb = std::move(cb);
    if (impl_->capture_dxgi_) impl_->capture_dxgi_->set_preview_requested(!!impl_->preview_cb);
    fprintf(stderr, "[Pipeline] preview_cb set OK\n"); fflush(stderr);
    return true;
}

bool Pipeline::set_d3d11_frame_callback(D3D11FrameCallback cb) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->d3d11_frame_cb = std::move(cb);
    // init_preview_staging: DXGI capture hazırsa hemen çağır
    if (impl_->capture_dxgi_ && !impl_->capture_dxgi_->shared_texture()) {
        impl_->capture_dxgi_->init_preview_staging();
    }
    // Late-bind Vulkan device to the bridge (Vulkan may not have been ready at init()).
    auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
    if (impl_->ext_bridge && vk && vk->device()) {
        impl_->ext_bridge->set_device(vk->device(), vk->physical_device());
    }
    fprintf(stderr, "[Pipeline] d3d11_frame_cb set OK\n"); fflush(stderr);
    return true;
}

bool Pipeline::notify_vulkan_ready(VkDevice device, VkPhysicalDevice phys_device) {
    if (impl_ && impl_->ext_bridge) {
        impl_->ext_bridge->set_device(device, phys_device);
        fprintf(stderr, "[Pipeline] notify_vulkan_ready: device=%p phys=%p\n",
                (void*)device, (void*)phys_device);
        fflush(stderr);

        // Vulkan device artık hazır — keyed mutex desteğini yeniden değerlendir.
        // İlk init sırasında Vulkan henüz oluşmamış olabilir (device=0x0), bu yüzden
        // use_keyed_mutex_ false kalmış olabilir.
        auto* vk_init = rj::pipeline::gpu::VulkanInitializer::get();
        if (impl_->capture_dxgi_) {
            bool km = vk_init && vk_init->use_keyed_mutex();
            impl_->capture_dxgi_->set_use_keyed_mutex(km);
            fprintf(stderr, "[Pipeline] notify_vulkan_ready: set_use_keyed_mutex=%d\n", (int)km);
            fflush(stderr);
        }

        impl_->ext_bridge->initialize_gl_target_pool(
            VK_FORMAT_B8G8R8A8_UNORM,
            (uint32_t)impl_->cfg.width,
            (uint32_t)impl_->cfg.height
        );
        if (!impl_->ext_bridge->create_gl_sync_semaphore()) {
            fprintf(stderr, "[Pipeline] GL sync semaphore oluşturulamadı\n");
            fflush(stderr);
        }
    }
    return true;
}

bool Pipeline::run_frame() {
    if (!impl_) return false;
    if (!impl_->capture) return false;  // capture yoksa alma
    auto& s = *impl_;

    const int64_t frame_start = qpc_ticks();

    // 1) Command drain  clamp [0,8]; log negative
    int n = seh_command_drain(s.cmd_buf.data(), kCmdDrainMax);
    if (n < 0) {
        dbglog("[Pipeline] rj_command_drain negative: %d", n);
        n = 0;
    } else if (n > kCmdDrainMax) {
        n = kCmdDrainMax;
    }
    for (int i = 0; i < n; ++i) s.apply_command(s.cmd_buf[i]);

    // 1b) C6: Drain SPSC frame commands from action_processor (no lock needed)
    {
        uint32_t tail = s.frame_cmd_tail_.load(std::memory_order_relaxed);
        uint32_t head = s.frame_cmd_head_.load(std::memory_order_acquire);
        while (tail != head) {
            s.apply_frame_cmd(s.frame_cmd_buf_[tail]);
            tail = (tail + 1) % Impl::kFrameCmdCap;
        }
        s.frame_cmd_tail_.store(tail, std::memory_order_release);
    }

    // 2) Capture + encode
    if (s.capture) {
        // DXGI: typed capture_next(); WGC: next_frame() ile handle cast
        rj::CapturedFrame frame{};
        ID3D11Texture2D* tex = nullptr;
        if (s.capture_dxgi_) {
            tex = s.capture_dxgi_->capture_next();
        } else {
            frame = s.capture->next_frame();
            tex = static_cast<ID3D11Texture2D*>(frame.handle);
        }

        static std::atomic<int> null_streak{0};

        if (tex) {
            null_streak.store(0, std::memory_order_relaxed);
            const int64_t pts_us =
                ticks_to_us(frame_start - s.pts_origin, s.qpc_freq);
            if (s.encoder && !s.encoder->encode_frame(tex, pts_us)) {
                s.frame_drops.fetch_add(1, std::memory_order_relaxed);
                // 3) GPU TDR check  outside __try, free to use C++ objects
                (void)s.handle_device_lost();
            }
            // v0.5.1: Zero-copy D3D11 frame callback (GPU-side operations, DXGI only)
            if (s.d3d11_frame_cb) {
                VkImage staging_vk = nullptr;
                VkImage target_vk = nullptr;
                if (s.ext_bridge && s.capture_dxgi_ && s.capture_dxgi_->shared_texture()) {
                    s.ext_bridge->get_frame_images(s.capture_dxgi_->shared_texture(),
                                                    &staging_vk, &target_vk);
#ifdef RJ_DEBUG_VERBOSE
                    fprintf(stderr, "[Pipeline] get_frame_images: staging=%p target=%p\n",
                            staging_vk, target_vk);
                    fflush(stderr);
#endif
                    // v0.5.1: Cache for get_last_frame_images() getter
                    s.last_staging_vk.store(staging_vk, std::memory_order_release);
                    s.last_target_vk.store(target_vk, std::memory_order_release);
                }

                s.d3d11_frame_cb(static_cast<void*>(tex),
                                 (uint32_t)s.capture->width(),
                                 (uint32_t)s.capture->height());
            }

            // v0.2 CPU preview: staging populated in capture_next() (DXGI only)
            if (s.preview_cb && s.capture_dxgi_) {
                const void* data = nullptr; int pitch = 0;
                if (s.capture_dxgi_->map_preview_frame(&data, &pitch)) {
                    static int cnt = 0;
                    if (++cnt == 1)
                        printf("[Preview] First frame: %dx%d pitch=%d\n",
                               (int)s.capture->width(), (int)s.capture->height(), pitch);
                    s.preview_cb(data, (int)s.capture->width(),
                                 (int)s.capture->height(), pitch);
                    s.capture_dxgi_->unmap_preview_frame();
                }
            }

            // WGC path — CPU staging preview
            if (!s.capture_dxgi_ && s.preview_cb) {
                // NVIDIA device'da staging texture oluştur (bir kez)
                if (!s.wgc_staging_tex_) {
                    D3D11_TEXTURE2D_DESC desc{};
                    tex->GetDesc(&desc);
                    desc.Usage          = D3D11_USAGE_STAGING;
                    desc.BindFlags      = 0;
                    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
                    desc.MiscFlags      = 0;
                    ID3D11Device* dev = nullptr;
                    tex->GetDevice(&dev);
                    if (dev) {
                        dev->CreateTexture2D(&desc, nullptr, &s.wgc_staging_tex_);
                        dev->Release();
                    }
                }
                // GPU → staging copy
                if (s.wgc_staging_tex_) {
                    ID3D11Device* dev = nullptr;
                    tex->GetDevice(&dev);
                    if (dev) {
                        ID3D11DeviceContext* ctx = nullptr;
                        dev->GetImmediateContext(&ctx);
                        ctx->CopyResource(s.wgc_staging_tex_.Get(), tex);
                        D3D11_MAPPED_SUBRESOURCE mapped{};
                        if (SUCCEEDED(ctx->Map(s.wgc_staging_tex_.Get(), 0,
                                               D3D11_MAP_READ, 0, &mapped))) {
                            static int wgc_prev_cnt = 0;
                            if (++wgc_prev_cnt <= 3)
                                fprintf(stderr, "[WgcStaging] preview frame #%d %ux%u pitch=%u\n",
                                        wgc_prev_cnt, frame.width, frame.height,
                                        (unsigned)mapped.RowPitch);
                            s.preview_cb(mapped.pData,
                                         static_cast<int>(frame.width),
                                         static_cast<int>(frame.height),
                                         static_cast<int>(mapped.RowPitch));
                            ctx->Unmap(s.wgc_staging_tex_.Get(), 0);
                        }
                        ctx->Release();
                        dev->Release();
                    }
                }
            }

        } else {
            int streak = ++null_streak;
            s.frame_drops.fetch_add(1, std::memory_order_relaxed);

            if (streak >= 60) {
                dbglog("[Pipeline] Capture loss detected (%d frames) — reinit", streak);
                null_streak.store(0, std::memory_order_relaxed);
                (void)s.handle_device_lost();
            }
        }
    }

    // 4) Metrics push  frame_drops as delta
    {
        RjMetricSample m{};
        m.magic_head   = kMetricMagic;
        m.magic_tail   = kMetricMagic;
        m.timestamp_us = static_cast<uint64_t>(
                             ticks_to_us(frame_start, s.qpc_freq));
        m.bitrate_kbps = s.bitrate_kbps;
        {
            auto delta = frame_start - s.last_frame_ticks;
            m.fps_actual = (delta > 0)
                ? std::clamp(
                      static_cast<float>(s.qpc_freq) / static_cast<float>(delta),
                      0.0f, 240.0f)
                : 0.0f;
        }
        m.cpu_percent  = s.cpu.sample();
        m.frame_drops  = s.frame_drops.exchange(0, std::memory_order_acq_rel);

        // v0.4+: Extended metrics from MetricsCollector
        // NOTE: WMI queries run in separate thread; run_frame() only reads snapshot
        if (s.metrics) {
            auto latest = s.metrics->get_latest();
            m.frame_drop_pct     = latest.frame_drop_pct;
            m.gpu_temp_c         = latest.gpu_temp_c;
            m.cpu_temp_c         = latest.cpu_temp_c;
            m.memory_usage_pct   = latest.memory_usage_pct;
            m.cpu_load_pct       = latest.cpu_load_pct;
            m.network_rtt_ms     = latest.network_rtt_ms;
            m.network_loss_pct   = latest.network_loss_pct;
        }

        m.source_id = 0;  // video
        seh_metrics_push(&m);
    }
    s.last_frame_ticks = frame_start;

    // 5) Frame pacing  absolute deadline
    s.next_deadline += s.frame_ticks;
    int64_t now    = qpc_ticks();
    int64_t remain = s.next_deadline - now;

    if (remain < -s.frame_ticks * kResyncFrames) {
        s.next_deadline = now + s.frame_ticks;  // resync  prevent catch-up spiral
    } else if (remain > 0) {
        int64_t remain_us = ticks_to_us(remain, s.qpc_freq);
        if (remain_us > 1500)
            Sleep(static_cast<DWORD>((remain_us - 1000) / 1000));
        while (qpc_ticks() < s.next_deadline)
            YieldProcessor();
    }

    if (s.metrics) s.metrics->poll();

    return true;
}

bool Pipeline::shutdown() {
    if (!impl_) return true;
    g_pipeline.store(nullptr, std::memory_order_release);

    bool ok = true;
    std::call_once(shutdown_once_, [this, &ok]() {
        auto& s = *impl_;

        s.streaming.store(false, std::memory_order_release);
        s.srt_atomic.store(nullptr, std::memory_order_release);

        // v0.4+: Stop action processor thread
        s.action_processor_running.store(false, std::memory_order_release);
        if (s.action_processor.joinable()) {
            s.action_processor.join();
        }

        // Finalize profiler
        if (profiler_) {
            profiler_->finalize();
        }

        // SEH-protected teardown  raw pointers only, no C++ destructors in scope.
        ok = seh_shutdown_subsystems(
            s.audio.get(), s.encoder.get(), s.srt.get());

        // RAII reset outside __try  destructors run safely here.
        s.audio.reset();
        s.srt.reset();
        s.encoder.reset();
        s.capture.reset();

        // B10: Shutdown bridge before VulkanInitializer can release the device.
        // ExternalMemoryBridge holds raw VkDevice/VkImage handles; resetting here
        // while the singleton device is still valid prevents use-after-free.
        if (s.ext_bridge) {
            s.ext_bridge->shutdown();
            s.ext_bridge.reset();
        }

        if (s.timer_set.exchange(false, std::memory_order_acq_rel))
            timeEndPeriod(1);

        // CoUninitialize only if we called CoInitializeEx.
        if (s.com_owned.exchange(false, std::memory_order_acq_rel))
            seh_uninit_com(&ok);

        s.initialized.store(false, std::memory_order_release);
        dbglog(ok ? "[Pipeline] shutdown clean" : "[Pipeline] shutdown SEH fault");
    });
    return ok;
}

bool Pipeline::get_last_frame_images(VkImage* out_staging, VkImage* out_target) {
    if (!impl_ || !out_staging || !out_target) return false;
    // v0.5.1: Return cached frame images from last run_frame()
    *out_staging = impl_->last_staging_vk.load(std::memory_order_acquire);
    *out_target  = impl_->last_target_vk.load(std::memory_order_acquire);
    return *out_staging != nullptr && *out_target != nullptr;
}

uint32_t Pipeline::display_vendor_id() const {
    if (!impl_ || !impl_->capture_dxgi_) return 0;
    const auto& scan = impl_->capture_dxgi_->gpu_scan();
    return scan.count > 0 ? scan.entries[0].vendor_id : 0;
}

rj::pipeline::gpu::ExternalMemoryBridge* Pipeline::get_external_memory_bridge() const {
    if (!impl_) return nullptr;
    return impl_->ext_bridge.get();
}

// v0.4+: Action processor thread main loop
void Pipeline::action_processor_main() {
    // D14: COM init once at thread start — required for WMI/DXGI calls on this thread
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        fprintf(stderr, "[Pipeline] COM init failed: 0x%08X\n", hr);
        fflush(stderr);
    }

    while (impl_->action_processor_running.load(std::memory_order_acquire)) {
        RjAction action{};
        // Poll rj_action_dequeue (FFI call) — non-blocking, returns false if queue empty
        if (rj_action_dequeue(&action)) {
            apply_action(action);
        }
        // Prevent busy-wait: yield briefly if queue is empty
        Sleep(100);  // 100ms poll interval — enqueue_action() kullanılmadığı için sık wake-up gereksiz
    }
    dbglog("[Pipeline] action processor stopped");

    CoUninitialize();  // D14: paired with CoInitializeEx above
}

// v0.4+: Apply a single action from the rule engine.
// C6: All encoder calls are routed through the SPSC frame_cmd queue so
// that set_bitrate/set_resolution/set_fps_limit execute on the frame
// thread (same thread as encode_frame), not on action_processor.
bool Pipeline::apply_action(const RjAction& action) {
    if (!impl_) return false;

    switch (action.action_type) {
        case RJ_ACTION_BITRATE_REDUCE: {
            uint32_t current    = impl_->bitrate_kbps;
            uint32_t new_bitrate = static_cast<uint32_t>(current * 0.85f);
            new_bitrate = (std::max)(new_bitrate, impl_->cfg.min_bitrate_kbps);
            impl_->push_frame_cmd({RJ_ACTION_BITRATE_REDUCE, static_cast<int32_t>(new_bitrate)});
            return true;
        }
        case RJ_ACTION_BITRATE_RECOVER: {
            uint32_t target  = impl_->cfg.original_bitrate_kbps;
            uint32_t current = impl_->bitrate_kbps;
            if (current < target) {
                uint32_t new_bitrate = (std::min)(
                    static_cast<uint32_t>(current * 1.15f),
                    target
                );
                impl_->push_frame_cmd({RJ_ACTION_BITRATE_RECOVER, static_cast<int32_t>(new_bitrate)});
                return true;
            }
            break;
        }
        case RJ_ACTION_SCALE_RESOLUTION:
            impl_->push_frame_cmd({RJ_ACTION_SCALE_RESOLUTION, action.param1});
            return true;
        case RJ_ACTION_CAP_FPS:
            impl_->push_frame_cmd({RJ_ACTION_CAP_FPS, action.param1});
            return true;
        default:
            dbglog("[Pipeline] unknown action_type=%u", action.action_type);
            break;
    }
    return false;
}

#endif // _WIN32

} // namespace rj

// Reverse FFI: Rust WS bridge → C++ pipeline control.
// Called from a Tokio worker thread; Pipeline::start/stop_stream use atomics internally.
extern "C" void rj_ws_command(int cmd) {
    auto* p = g_pipeline.load(std::memory_order_acquire);
    if (!p) return;
    switch (cmd) {
        case 1: (void)p->start_stream(); break;
        case 2: (void)p->stop_stream();  break;
        case 3: /* scene_cut  — MainWindow handles via existing UI path */ break;
        case 4: /* scene_fade — MainWindow handles via existing UI path */ break;
        default: break;
    }
}
