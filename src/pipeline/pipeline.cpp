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
//    std::atomic<ITransport*> transport_atomic_ for start/stop_stream thread safety

#include "include/pipeline.h"
#include "include/i_screen_capture.h"
#include "include/frame_profiler.h"
#include "include/frame_pacer.h"
#include "include/metrics_subsystem.h"
#include "include/command_router.h"
#include "gpu/external_memory_bridge.h"
#include "gpu/vulkan_initializer.h"
#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan.h>
#endif

#ifdef _WIN32
#include "capture/capture_dxgi.h"
#include "capture/capture_dxgi_screen.h"
#include "include/capture_subsystem.h"
#include "encode/encode_nvenc.h"
#include "include/encode_subsystem.h"
#include "audio/wasapi_capture.h"
#include "include/audio_subsystem.h"
#include "include/i_transport.h"
#include "include/output_subsystem.h"
#include "include/gpu_interop_subsystem.h"
#include "include/recovery_coordinator.h"
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
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <thread>

//  FFI struct size verification 
// Natural alignment (no pack pragma) matches Rust #[repr(C)].
// RjMetricSample: 4 + 4(pad) + 8 + 4 + 4 + 4 + 4 + 4(trail-pad) = 40
// RjCommand:      4 + 4(pad) + 8 + 4 + 4                         = 24
// v0.4: RjMetricSample extended to 56 bytes (frame_drop_pct, temps, load, network)
// v0.5: gpu_load_pct eklendi; u64 alignment trailing pad → 64 bytes
static_assert(sizeof(RjMetricSample) == 64, "RjMetricSample ABI drift — expected 64 bytes (v0.5)");
static_assert(sizeof(RjCommand)      == 24, "RjCommand ABI drift");
static_assert(sizeof(RjAction)       == 20, "RjAction ABI drift — expected 20 bytes (v0.4)");

// RjCommand field offsets
static_assert(offsetof(RjCommand, cmd_type)     ==  0, "RjCommand::cmd_type offset");
static_assert(offsetof(RjCommand, timestamp_us) ==  8, "RjCommand::timestamp_us offset");
static_assert(offsetof(RjCommand, param_u32)    == 16, "RjCommand::param_u32 offset");
static_assert(offsetof(RjCommand, param_f32)    == 20, "RjCommand::param_f32 offset");

// RjAction field offsets
static_assert(offsetof(RjAction, id)          ==  0, "RjAction::id offset");
static_assert(offsetof(RjAction, action_type) ==  4, "RjAction::action_type offset");
static_assert(offsetof(RjAction, param1)      ==  8, "RjAction::param1 offset");
static_assert(offsetof(RjAction, param2)      == 12, "RjAction::param2 offset");
static_assert(offsetof(RjAction, canary)      == 16, "RjAction::canary offset");

// RjMetricSample field offsets
static_assert(offsetof(RjMetricSample, magic_head)       ==  0, "RjMetricSample::magic_head offset");
static_assert(offsetof(RjMetricSample, timestamp_us)     ==  8, "RjMetricSample::timestamp_us offset");
static_assert(offsetof(RjMetricSample, bitrate_kbps)     == 16, "RjMetricSample::bitrate_kbps offset");
static_assert(offsetof(RjMetricSample, fps_actual)       == 20, "RjMetricSample::fps_actual offset");
static_assert(offsetof(RjMetricSample, cpu_percent)      == 24, "RjMetricSample::cpu_percent offset");
static_assert(offsetof(RjMetricSample, frame_drops)      == 28, "RjMetricSample::frame_drops offset");
static_assert(offsetof(RjMetricSample, frame_drop_pct)   == 32, "RjMetricSample::frame_drop_pct offset");
static_assert(offsetof(RjMetricSample, gpu_temp_c)       == 36, "RjMetricSample::gpu_temp_c offset");
static_assert(offsetof(RjMetricSample, cpu_temp_c)       == 38, "RjMetricSample::cpu_temp_c offset");
static_assert(offsetof(RjMetricSample, memory_usage_pct) == 40, "RjMetricSample::memory_usage_pct offset");
static_assert(offsetof(RjMetricSample, cpu_load_pct)     == 44, "RjMetricSample::cpu_load_pct offset");
static_assert(offsetof(RjMetricSample, gpu_load_pct)     == 48, "RjMetricSample::gpu_load_pct offset");
static_assert(offsetof(RjMetricSample, network_rtt_ms)   == 52, "RjMetricSample::network_rtt_ms offset");
static_assert(offsetof(RjMetricSample, network_loss_pct) == 54, "RjMetricSample::network_loss_pct offset");
static_assert(offsetof(RjMetricSample, source_id)        == 55, "RjMetricSample::source_id offset");
static_assert(offsetof(RjMetricSample, magic_tail)       == 56, "RjMetricSample::magic_tail offset");

namespace {

//  Constants
constexpr uint32_t kMetricMagic    = RJ_METRIC_MAGIC;
constexpr uint32_t kCaptureTimeout = 17;   // ms  60 Hz budget

//  QPC helpers 
inline int64_t qpc_ticks() noexcept {
    LARGE_INTEGER c{}; QueryPerformanceCounter(&c); return c.QuadPart;
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
static void seh_start_monitor() noexcept {
    __try   { rj_start_monitor(); }
    __except(EXCEPTION_EXECUTE_HANDLER) {}
}

// seh_connection_lost Aşama 9'da RecoveryCoordinator'a taşındı (tek kullanıcıydı).

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
    rj::ITransport*                       out) noexcept
{
    bool ok = true;
    __try {
        if (audio) { (void)audio->stop(); (void)audio->shutdown(); }
        if (enc)   { enc->flush(); enc->shutdown(); }
        // NOT (Faz2/Aşama1): out->shutdown() artık virtual call — SEH __try içinde
        // MSVC'de yasak değil ama bilinçli bir sapma, bkz. FAZ2_ASAMA1_TALIMAT.md.
        // RtmpTransport eklenince her iki implementasyonun da shutdown()'ının
        // burada güvenle exception fırlatmadığından emin ol.
        if (out)   { (void)out->shutdown(); }
    } __except(EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok;
}

} // anonymous namespace

#endif // _WIN32

namespace rj {

// 
// Pipeline::Impl
// 
#ifdef _WIN32
struct Pipeline::Impl {
    Pipeline::Config cfg{};

    CaptureSubsystem                                       capture_sub_; // Aşama 8
    EncodeSubsystem                                        encode_sub_;  // Aşama 6
    AudioSubsystem                                          audio_sub_;   // Aşama 3
    OutputSubsystem                                         output_sub_;  // Aşama 4

    // Aşama 7: D3D11↔Vulkan zero-copy interop — ExternalMemoryBridge yaşam döngüsü +
    // son frame VkImage cache'i (eski ext_bridge/last_staging_vk/last_target_vk alanları).
    GpuInteropSubsystem                                     gpu_sub_;     // Aşama 7

    // Aşama 5: Komut/aksiyon yönlendirme — action_processor thread + SPSC ring +
    // her-frame drain (WS port log, command drain, WS drain, SPSC drain).
    CommandRouter command_router_;

    // Aşama 2: Metrics alt sistemi — CpuMeter + MetricsCollector + fps ölçümü
    // (eski cpu, metrics, last_frame_ticks alanları buraya taşındı).
    MetricsSubsystem metrics_sub_;

    std::atomic<bool>    initialized{false};
    std::atomic<bool>    streaming{false};
    std::atomic<bool>    com_owned{false};
    std::atomic<bool>    timer_set{false};

    FramePacer pacer_;                 // Aşama 1: QPC/pts/pacing alt sistemi
    std::atomic<uint32_t> bitrate_kbps{0};

    // Authoritative frame dims — capture'dan gelir; recovery (frame thread) yazar,
    // notify_vulkan_ready (başka thread) okur → veri yarışını kapatmak için atomic.
    // cfg.width/height init snapshot olarak kalır; runtime kaynağı bunlardır.
    std::atomic<uint32_t> width{0};
    std::atomic<uint32_t> height{0};

    std::atomic<uint32_t> frame_drops{0};

    // Aşama-0 test seam: son run_frame() metrik örneği (get_last_metric_sample).
    // Yalnızca frame thread yazar/okur; rj_metrics_poll stub olduğu için gerekli.
    RjMetricSample last_sample_{};

    // apply_frame_cmd: SPSC ring'ten tüketilen komutu Encode'a uygular.
    // CommandRouter'a callback olarak geçilir (Impl Encode'a dokunan tarafı tutar).
    void apply_frame_cmd(const CommandRouter::FrameCmd& cmd) noexcept {
        // encoder yoksa no-op (bitrate_kbps de güncellenmez — eski davranış korunur).
        if (!encode_sub_.raw()) return;
        switch (cmd.action_type) {
            case RJ_ACTION_BITRATE_REDUCE:
            case RJ_ACTION_BITRATE_RECOVER:
                if (cmd.param1 > 0) {
                    (void)encode_sub_.set_bitrate(static_cast<uint32_t>(cmd.param1));
                    bitrate_kbps.store(static_cast<uint32_t>(cmd.param1), std::memory_order_relaxed);
                }
                break;
            case RJ_ACTION_SCALE_RESOLUTION:
                (void)encode_sub_.set_resolution(cmd.param1 / 1000.0f);
                break;
            case RJ_ACTION_CAP_FPS:
                (void)encode_sub_.set_fps_limit(static_cast<uint32_t>(cmd.param1));
                break;
            default:
                break;
        }
    }

    // Preview callback  called from run_frame() with CPU-mapped BGRA frame
    Pipeline::PreviewCallback        preview_cb;

    // v0.5.1: D3D11 zero-copy callback - called from run_frame() with staging texture
    Pipeline::D3D11FrameCallback     d3d11_frame_cb;

    // WebSocket scene command callback — invoked from run_frame() ws_command drain for cmd=3/4
    Pipeline::SceneCommandCallback   scene_cmd_cb;

    // Son frame VkImage cache'i Aşama 7'de GpuInteropSubsystem'e taşındı
    // (gpu_sub_.cache_last_images / get_last_frame_images).
    // WGC CPU staging texture Aşama 8b'de CaptureSubsystem'e taşındı
    // (capture_sub_.emit_wgc_preview).

    void apply_command(const RjCommand& c) noexcept {
        switch (c.cmd_type) {
            case RJ_CMD_BITRATE_SET:
                if (encode_sub_.raw() && c.param_u32 > 0) {
                    (void)encode_sub_.set_bitrate(c.param_u32);
                    bitrate_kbps.store(c.param_u32, std::memory_order_relaxed);
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
    // "Sıkı düğüm": hem Output (send) hem Metrics (frame_drops) alt sistemlerine
    // dokunur — bu yüzden orkestratörde kalır; gönderme OutputSubsystem'e devredilir.
    static void on_packet(const reji::NvencEncoder::Packet& pkt,
                          Impl* self) noexcept {
        static std::atomic<int> pkt_count{0};
        int n = ++pkt_count;
        if (n <= 5 || n % 60 == 0)
            fprintf(stderr, "[NVENC] packet #%d size=%zu pts=%lld keyframe=%d\n",
                    n, pkt.size, (long long)pkt.pts, pkt.is_keyframe ? 1 : 0);
        fflush(stderr);

        if (!self->streaming.load(std::memory_order_acquire)) return;
        // send() false döndürürse (aktif çıkış vardı ama gönderim başarısız) → drop.
        if (!self->output_sub_.send(pkt.data, pkt.size, pkt.pts))
            self->frame_drops.fetch_add(1, std::memory_order_relaxed);
    }

    // GPU TDR / capture-loss recovery Aşama 9'da RecoveryCoordinator'a taşındı.
    // run_frame() (frame thread, __try DIŞINDA) doğrudan
    // RecoveryCoordinator::handle_device_lost(...) çağırır.
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
bool Pipeline::init(const Config&)                        { return false; }
bool Pipeline::start_stream()                             { return false; }
bool Pipeline::stop_stream()                              { return false; }
bool Pipeline::is_running() const                         { return false; }
bool Pipeline::set_preview_callback(PreviewCallback)      { return false; }
bool Pipeline::set_scene_command_callback(SceneCommandCallback) { return false; }
void Pipeline::invoke_scene_cmd_(int, uint32_t) noexcept  {}
bool Pipeline::run_frame()                                { return false; }
bool Pipeline::shutdown()                                 { return true;  }
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

    // Initialize metrics subsystem (v0.4+ Runtime Adaptation)
    s.metrics_sub_.init();

    s.cfg                          = cfg_in;
    s.cfg.original_bitrate_kbps    = cfg_in.bitrate_kbps;
    s.bitrate_kbps.store(cfg_in.bitrate_kbps, std::memory_order_relaxed);

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

    //  QPC / frame pacing (FramePacer alt sistemi)
    if (!s.pacer_.init(cfg_in.fps)) {
        dbglog("[Pipeline] FramePacer init failed (QPC unavailable)");
        (void)shutdown(); return false;
    }

    //  IScreenCapture::create() — WGC tercihli, DXGI fallback (CaptureSubsystem)
    {
        rj::IScreenCapture::Config cap_cfg;
        cap_cfg.timeout_ms          = kCaptureTimeout;
        cap_cfg.allow_cross_adapter = true;
        // init(): create + init + dxgi cast tek çağrıda (create-null / init-fail
        // ayrımı tek fatal path'e indi — ikisi de shutdown+false).
        if (!s.capture_sub_.init(cap_cfg)) {
            dbglog("[Pipeline] IScreenCapture init failed (create/init)");
            (void)shutdown(); return false;
        }
    }
    if (s.capture_sub_.dxgi())
        s.capture_sub_.dxgi()->setProfiler(profiler_.get());
    // Authoritative dimensions come from the actual display output.
    s.cfg.width  = s.capture_sub_.width();
    s.cfg.height = s.capture_sub_.height();
    s.width.store(s.cfg.width,  std::memory_order_release);   // atomic = runtime kaynağı
    s.height.store(s.cfg.height, std::memory_order_release);

    //  GpuInteropSubsystem (v0.5.1 zero-copy D3D11↔Vulkan — DXGI path only)
    if (s.capture_sub_.dxgi()) {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        // Sıkı düğüm: keyed-mutex capture pipeline'ına dokunur — orkestratörde kalır.
        s.capture_sub_.dxgi()->set_use_keyed_mutex(vk && vk->use_keyed_mutex());
        fprintf(stderr, "[Pipeline] VulkanInit: device=%p phys=%p\n",
                (void*)(vk ? vk->device() : nullptr),
                (void*)(vk ? vk->physical_device() : nullptr));
        fflush(stderr);
        VkDevice vk_device = vk ? vk->device() : VK_NULL_HANDLE;
        VkPhysicalDevice vk_phys = vk ? vk->physical_device() : VK_NULL_HANDLE;
        // device/phys/width/height çözülüp GpuInterop'a geçilir; pool init fail → log.
        if (!s.gpu_sub_.init(vk_device, vk_phys, s.cfg.width, s.cfg.height)) {
            dbglog("[Pipeline] ExternalMemoryBridge::initialize_image_pool failed");
        }
    }

    //  NvencEncoder — DXGI ve WGC path desteklenir
    {
        ID3D11Device* encode_device = nullptr;
        if (s.capture_sub_.dxgi() && s.capture_sub_.dxgi()->encode_gpu()) {
            encode_device = s.capture_sub_.dxgi()->encode_gpu()->d3d_device();
        } else if (s.capture_sub_.has_capture()) {
            encode_device = s.capture_sub_.d3d_device();  // WGC: kendi D3D11 device'ı
        }
        if (encode_device) {
            reji::NvencEncoder::Config enc_cfg;
            enc_cfg.width            = s.cfg.width;
            enc_cfg.height           = s.cfg.height;
            enc_cfg.fps_num          = cfg_in.fps;
            enc_cfg.fps_den          = 1;
            enc_cfg.bitrate_kbps     = cfg_in.bitrate_kbps;
            enc_cfg.max_bitrate_kbps = cfg_in.bitrate_kbps + cfg_in.bitrate_kbps / 4;
            // packet_cb "sıkı düğüm": Impl::on_packet (Output+Metrics) EncodeSubsystem'e
            // callback olarak geçilir; EncodeSubsystem içeriğini bilmez, yalnızca saklar.
            auto packet_cb = [&s](const reji::NvencEncoder::Packet& pkt) noexcept {
                Impl::on_packet(pkt, &s);
            };
            if (!s.encode_sub_.init(encode_device, enc_cfg, packet_cb)) {
                dbglog("[Pipeline] NvencEncoder::init failed -- running in preview-only mode");
            }
        }
    }

    //  WasapiCapture (optional) — AudioSubsystem alt sistemi
    if (cfg_in.audio_enabled) {
        AudioSubsystem::Config acfg{};
        acfg.exclusive_mode = false;
        acfg.sample_rate    = 48000;
        acfg.channels       = 2;
        acfg.bit_depth      = 32;
        acfg.buffer_ms      = 50;
        acfg.loopback       = cfg_in.loopback;
        if (!s.audio_sub_.init(acfg, &AudioSubsystem::on_audio)) {
            dbglog("[Pipeline] WasapiCapture::init failed  audio disabled");
        }
    }

    //  ITransport (SrtTransport)  OutputSubsystem alt sistemi
    OutputSubsystem::Config scfg{};
    scfg.host           = cfg_in.srt_host;   // std::string ataması (Faz2/Aşama1)
    scfg.port           = cfg_in.srt_port;
    scfg.latency_ms     = 200;
    scfg.bandwidth_kbps = 0;
    scfg.caller_mode    = true;
    if (!s.output_sub_.init(scfg)) {
        dbglog("[Pipeline] SrtOutput::init failed -- running without SRT output");
    }

    //  Rust monitor 

    // v0.2 preview staging — allocate once, no hot-path heap (DXGI only)
    if (s.preview_cb && s.capture_sub_.dxgi()) {
        if (!s.capture_sub_.dxgi()->init_preview_staging())
            dbglog("[Pipeline] init_preview_staging failed -- preview disabled");
    }
    seh_start_monitor();

    // v0.4+: Start action processor thread (CommandRouter alt sistemi).
    // scene_cb geç-bağlanır (invoke_scene_cmd_ çağrı anında scene_cmd_cb'i okur);
    // on_action → apply_action (bitrate/res/fps → SPSC ring push).
    s.command_router_.start(
        [this](int cmd, uint32_t param) { invoke_scene_cmd_(cmd, param); },
        [this](const RjAction& a)       { (void)apply_action(a); });

    s.initialized.store(true, std::memory_order_release);
    dbglog("[Pipeline] init OK %ux%u@%u fps %u kbps audio=%d loopback=%d",
           s.cfg.width, s.cfg.height, cfg_in.fps, cfg_in.bitrate_kbps,
           cfg_in.audio_enabled ? 1 : 0, cfg_in.loopback ? 1 : 0);
    return true;
}

bool Pipeline::start_stream() {
    if (!impl_ || !impl_->initialized.load(std::memory_order_acquire)) return false;
    if (impl_->streaming.exchange(true, std::memory_order_acq_rel)) return true;

    if (!impl_->output_sub_.is_active()) {
        dbglog("[Pipeline] start_stream: SRT not initialized -- preview-only mode");
        // SRT olmadan streaming flag'ini set et, preview devam etsin
    }

    // Publish SRT pointer before any packet callback can observe it.
    impl_->output_sub_.set_streaming(true);
    (void)impl_->audio_sub_.start();
    dbglog("[Pipeline] streaming started");
    return true;
}

bool Pipeline::stop_stream() {
    if (!impl_) return false;
    if (!impl_->streaming.exchange(false, std::memory_order_acq_rel)) return true;

    // Null the atomic pointer before any further packets can be sent.
    impl_->output_sub_.set_streaming(false);
    (void)impl_->audio_sub_.stop();
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
    if (impl_->capture_sub_.dxgi()) impl_->capture_sub_.dxgi()->set_preview_requested(!!impl_->preview_cb);
    fprintf(stderr, "[Pipeline] preview_cb set OK\n"); fflush(stderr);
    return true;
}

bool Pipeline::set_d3d11_frame_callback(D3D11FrameCallback cb) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->d3d11_frame_cb = std::move(cb);
    // init_preview_staging: DXGI capture hazırsa hemen çağır
    if (impl_->capture_sub_.dxgi() && !impl_->capture_sub_.dxgi()->shared_texture()) {
        impl_->capture_sub_.dxgi()->init_preview_staging();
    }
    // Late-bind Vulkan device to the bridge (Vulkan may not have been ready at init()).
    // set_device() bridge yoksa no-op — eski `impl_->ext_bridge &&` guard'ı içeride.
    auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
    if (vk && vk->device()) {
        impl_->gpu_sub_.set_device(vk->device(), vk->physical_device());
    }
    fprintf(stderr, "[Pipeline] d3d11_frame_cb set OK\n"); fflush(stderr);
    return true;
}

bool Pipeline::set_scene_command_callback(SceneCommandCallback cb) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->scene_cmd_cb = std::move(cb);
    return true;
}

void Pipeline::invoke_scene_cmd_(int cmd, uint32_t param) noexcept {
    if (impl_ && impl_->scene_cmd_cb) impl_->scene_cmd_cb(cmd, param);
}

bool Pipeline::notify_vulkan_ready(VkDevice device, VkPhysicalDevice phys_device) {
    if (!impl_) return true;
    auto& s = *impl_;
    // Eski davranış: tüm gövde ext_bridge guard'lıydı — bridge yoksa no-op.
    if (!s.gpu_sub_.raw()) return true;

    // GPU interop: set_device + GL target pool + sync semaphore. width/height
    // atomik'lerden okunup parametre olarak geçilir (Aşama 0 test seam korunur).
    s.gpu_sub_.notify_vulkan_ready(
        device, phys_device,
        s.width.load(std::memory_order_acquire),
        s.height.load(std::memory_order_acquire));

    // Sıkı düğüm: keyed mutex yeniden değerlendirme capture_dxgi_'ye dokunur —
    // GpuInterop'un değil orkestratörün sorumluluğu. Vulkan device artık hazır;
    // ilk init'te device=0x0 olup use_keyed_mutex_ false kalmış olabilir.
    // (ext_bridge GL kurulumundan bağımsız — sıra değişimi güvenli.)
    auto* vk_init = rj::pipeline::gpu::VulkanInitializer::get();
    if (s.capture_sub_.dxgi()) {
        bool km = vk_init && vk_init->use_keyed_mutex();
        s.capture_sub_.dxgi()->set_use_keyed_mutex(km);
        fprintf(stderr, "[Pipeline] notify_vulkan_ready: set_use_keyed_mutex=%d\n", (int)km);
        fflush(stderr);
    }
    return true;
}

bool Pipeline::run_frame() {
    if (!impl_) return false;
    if (!impl_->capture_sub_.has_capture()) return false;  // capture yoksa alma
    auto& s = *impl_;

    const int64_t frame_start = qpc_ticks();

    // 0/1/1a/1b) Komut/aksiyon drain (CommandRouter alt sistemi).
    // Encode'a/state'e dokunan tüm mantık callback ile geçilir — CommandRouter
    // bunları bilmez. ws_cmd 1/2 → start/stop_stream; 3/4 → scene_cb (start()'ta).
    s.command_router_.drain_and_apply(
        [&s](const RjCommand& c)                { s.apply_command(c); },
        [&s](const CommandRouter::FrameCmd& fc) { s.apply_frame_cmd(fc); },
        [this](int ws_cmd) {
            if      (ws_cmd == 1) (void)start_stream();
            else if (ws_cmd == 2) (void)stop_stream();
        });

    // 2) Capture + encode
    if (s.capture_sub_.has_capture()) {
        // CaptureSubsystem: DXGI capture_next() / WGC next_frame() dallarını kapsar;
        // texture handle'da döner (null-streak geçerli frame'de içeride sıfırlanır).
        rj::CapturedFrame frame = s.capture_sub_.next_frame();
        ID3D11Texture2D* tex = static_cast<ID3D11Texture2D*>(frame.handle);

        if (tex) {
            const int64_t pts_us = s.pacer_.pts_us(frame_start);
            // encode_frame(): encoder yoksa true (no-op) — eski `s.encoder && ...`
            // koşuluyla aynı: yalnızca gerçek encode hatasında drop + TDR recovery.
            if (!s.encode_sub_.encode_frame(tex, pts_us)) {
                s.frame_drops.fetch_add(1, std::memory_order_relaxed);
                // 3) GPU TDR check  outside __try, free to use C++ objects
                (void)RecoveryCoordinator::handle_device_lost(
                    s.capture_sub_, s.encode_sub_, s.cfg,
                    s.bitrate_kbps.load(std::memory_order_relaxed),
                    s.width, s.height);
            }
            // v0.5.1: Zero-copy D3D11 frame callback (GPU-side operations, DXGI only)
            // get_frame_images + cache GpuInterop'a taşındı; callback'in kendisi burada.
            if (s.d3d11_frame_cb) {
                // Guard eskisiyle bire bir: bridge + dxgi pipeline + shared_texture.
                auto* dxgi = s.capture_sub_.dxgi();
                if (s.gpu_sub_.raw() && dxgi && dxgi->shared_texture()) {
                    VkImage staging_vk = nullptr;
                    VkImage target_vk = nullptr;
                    s.gpu_sub_.get_frame_images(dxgi->shared_texture(),
                                                &staging_vk, &target_vk);
                    s.gpu_sub_.cache_last_images(staging_vk, target_vk);
                }

                s.d3d11_frame_cb(static_cast<void*>(tex),
                                 (uint32_t)s.capture_sub_.width(),
                                 (uint32_t)s.capture_sub_.height());
            }

            // v0.2 CPU preview: staging populated in capture_next() (DXGI only)
            if (s.preview_cb && s.capture_sub_.dxgi()) {
                auto* dxgi = s.capture_sub_.dxgi();
                const void* data = nullptr; int pitch = 0;
                if (dxgi->map_preview_frame(&data, &pitch)) {
                    static int cnt = 0;
                    if (++cnt == 1)
                        printf("[Preview] First frame: %dx%d pitch=%d\n",
                               (int)s.capture_sub_.width(), (int)s.capture_sub_.height(), pitch);
                    s.preview_cb(data, (int)s.capture_sub_.width(),
                                 (int)s.capture_sub_.height(), pitch);
                    dxgi->unmap_preview_frame();
                }
            }

            // WGC path — CPU staging preview (CaptureSubsystem::emit_wgc_preview).
            // Preview tetikleme kararı (is_wgc + preview_cb var mı) orkestratörde;
            // preview_cb parametre olarak geçilir — subsystem UI'ı bilmez.
            if (s.capture_sub_.is_wgc() && s.preview_cb) {
                s.capture_sub_.emit_wgc_preview(s.preview_cb, tex,
                                                frame.width, frame.height);
            }

        } else {
            s.frame_drops.fetch_add(1, std::memory_order_relaxed);
            // Null-streak sayacı CaptureSubsystem'de; eşiğe (60) ulaşınca true döner.
            // Gerçek reinit RecoveryCoordinator'a delege edilir (cross-subsystem).
            if (s.capture_sub_.handle_null_frame()) {
                dbglog("[Pipeline] Capture loss detected (60 frames) — reinit");
                (void)RecoveryCoordinator::handle_device_lost(
                    s.capture_sub_, s.encode_sub_, s.cfg,
                    s.bitrate_kbps.load(std::memory_order_relaxed),
                    s.width, s.height);
            }
        }
    }

    // 4) Metrics push  frame_drops as delta (MetricsSubsystem alt sistemi)
    {
        // frame_drops atomic Impl'de kalır; delta olarak exchange edilip parametre geçilir.
        const uint32_t drops = s.frame_drops.exchange(0, std::memory_order_acq_rel);
        RjMetricSample m = s.metrics_sub_.build_sample(
            s.bitrate_kbps.load(std::memory_order_relaxed),
            drops, frame_start, s.pacer_.qpc_freq());
        s.metrics_sub_.push(m);
        s.last_sample_ = m;  // Aşama-0 test seam — frame-thread only
    }
    s.metrics_sub_.record_frame_start(frame_start);  // fps ölçümü: bu frame'i sonraki için kaydet

    // 5) Frame pacing  absolute deadline (FramePacer alt sistemi)
    s.pacer_.pace();

    s.metrics_sub_.poll();

    return true;
}

bool Pipeline::shutdown() {
    if (!impl_) return true;

    bool ok = true;
    std::call_once(shutdown_once_, [this, &ok]() {
        auto& s = *impl_;

        s.streaming.store(false, std::memory_order_release);
        s.output_sub_.set_streaming(false);   // transport_atomic null — encode thread güvenliği

        // v0.4+: Stop action processor thread (CommandRouter: running=false + join)
        s.command_router_.stop();

        // Finalize profiler
        if (profiler_) {
            profiler_->finalize();
        }

        // SEH-protected teardown  raw pointers only, no C++ destructors in scope.
        ok = seh_shutdown_subsystems(
            s.audio_sub_.raw(), s.encode_sub_.raw(), s.output_sub_.raw());

        // RAII reset outside __try  destructors run safely here.
        s.audio_sub_.shutdown();    // audio_ reset (SEH-leaf DIŞINDA)
        s.output_sub_.shutdown();   // transport_atomic null + transport_ reset (SEH-leaf DIŞINDA)
        s.encode_sub_.shutdown();   // encoder_ reset (SEH-leaf DIŞINDA)
        s.capture_sub_.shutdown();  // capture_ reset + capture_dxgi_ null

        // B10: Shutdown bridge before VulkanInitializer can release the device.
        // ExternalMemoryBridge holds raw VkDevice/VkImage handles; resetting here
        // while the singleton device is still valid prevents use-after-free.
        // (Sıra korunur: gpu_sub_.shutdown() timer/COM teardown'dan ÖNCE.)
        s.gpu_sub_.shutdown();

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
    // v0.5.1: Return cached frame images from last run_frame() (GpuInteropSubsystem).
    return impl_->gpu_sub_.get_last_frame_images(out_staging, out_target);
}

bool Pipeline::get_last_metric_sample(RjMetricSample* out) const {
    if (!impl_ || !out) return false;
    *out = impl_->last_sample_;
    return impl_->last_sample_.magic_head == kMetricMagic;
}

uint32_t Pipeline::display_vendor_id() const {
    if (!impl_ || !impl_->capture_sub_.dxgi()) return 0;
    const auto& scan = impl_->capture_sub_.dxgi()->gpu_scan();
    return scan.count > 0 ? scan.entries[0].vendor_id : 0;
}

rj::pipeline::gpu::ExternalMemoryBridge* Pipeline::get_external_memory_bridge() const {
    if (!impl_) return nullptr;
    return impl_->gpu_sub_.raw();
}

// v0.4+: Apply a single action from the rule engine.
// C6: All encoder calls are routed through the SPSC frame_cmd queue so
// that set_bitrate/set_resolution/set_fps_limit execute on the frame
// thread (same thread as encode_frame), not on action_processor.
bool Pipeline::apply_action(const RjAction& action) {
    if (!impl_) return false;

    switch (action.action_type) {
        case RJ_ACTION_BITRATE_REDUCE: {
            uint32_t current    = impl_->bitrate_kbps.load(std::memory_order_relaxed);
            uint32_t new_bitrate = static_cast<uint32_t>(current * 0.85f);
            new_bitrate = (std::max)(new_bitrate, impl_->cfg.min_bitrate_kbps);
            impl_->command_router_.push_frame_cmd({RJ_ACTION_BITRATE_REDUCE, static_cast<int32_t>(new_bitrate)});
            return true;
        }
        case RJ_ACTION_BITRATE_RECOVER: {
            uint32_t target  = impl_->cfg.original_bitrate_kbps;
            uint32_t current = impl_->bitrate_kbps.load(std::memory_order_relaxed);
            if (current < target) {
                uint32_t new_bitrate = (std::min)(
                    static_cast<uint32_t>(current * 1.15f),
                    target
                );
                impl_->command_router_.push_frame_cmd({RJ_ACTION_BITRATE_RECOVER, static_cast<int32_t>(new_bitrate)});
                return true;
            }
            break;
        }
        case RJ_ACTION_SCALE_RESOLUTION:
            impl_->command_router_.push_frame_cmd({RJ_ACTION_SCALE_RESOLUTION, action.param1});
            return true;
        case RJ_ACTION_CAP_FPS:
            impl_->command_router_.push_frame_cmd({RJ_ACTION_CAP_FPS, action.param1});
            return true;
        default:
            dbglog("[Pipeline] unknown action_type=%u", action.action_type);
            break;
    }
    return false;
}

#endif // _WIN32

} // namespace rj
