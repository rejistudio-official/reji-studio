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
#include "include/send_diag.h"
#include "include/command_router.h"
#include "include/bitrate_policy.h"
#include "include/frame_drop_policy.h"
#include "include/frame_repeat_policy.h"
#include "gpu/external_memory_bridge.h"
#include "gpu/vulkan_initializer.h"
#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan.h>
#endif

#ifdef _WIN32
#include "capture/capture_dxgi.h"
#include "capture/capture_dxgi_screen.h"
#include "include/existing_desktop_source.h"   // ISource wiring — CaptureSubsystem'in yerini aldı
#include "include/reinit_trigger_policy.h"     // state() level→edge dönüşümü (60-kare re-arm)
#include "encode/encode_nvenc.h"
#include "include/encode_subsystem.h"
#include "audio/wasapi_capture.h"
#include "include/audio_subsystem.h"
#include "audio/audio_encode_bridge.h"
#include "include/i_transport.h"
#include "include/output_subsystem.h"
#include "include/gpu_interop_subsystem.h"
#include "include/recovery_coordinator.h"
#include "include/seh_filter.h"  // V8/I10: paylaşımlı SEH filtresi
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
    rj::SehCapture cap{};
    __try   { rj_start_monitor(); }
    __except(rj::seh_filter(GetExceptionInformation(), rj::SehSite::StartMonitor, &cap)) {}
    if (cap.fired) rj::seh_report(cap, rj::SehSite::StartMonitor);
}

// seh_connection_lost Aşama 9'da RecoveryCoordinator'a taşındı (tek kullanıcıydı).

__declspec(noinline)
static void seh_uninit_com(bool* ok) noexcept {
    rj::SehCapture cap{};
    __try   { CoUninitialize(); }
    __except(rj::seh_filter(GetExceptionInformation(), rj::SehSite::UninitCom, &cap)) { if (ok) *ok = false; }
    if (cap.fired) rj::seh_report(cap, rj::SehSite::UninitCom);
}

// Shutdown subsystems via raw pointers  no C++ destructors in scope.
__declspec(noinline)
static bool seh_shutdown_subsystems(
    reji::pipeline::audio::WasapiCapture* audio,
    reji::NvencEncoder*                   enc,
    rj::ITransport*                       out) noexcept
{
    bool ok = true;
    rj::SehCapture cap{};
    __try {
        if (audio) { (void)audio->stop(); (void)audio->shutdown(); }
        if (enc)   { enc->flush(); enc->shutdown(); }
        // NOT (Faz2/Aşama1): out->shutdown() artık virtual call — SEH __try içinde
        // MSVC'de yasak değil ama bilinçli bir sapma, bkz. FAZ2_ASAMA1_TALIMAT.md.
        // RtmpTransport eklenince her iki implementasyonun da shutdown()'ının
        // burada güvenle exception fırlatmadığından emin ol.
        if (out)   { (void)out->shutdown(); }
    } __except(rj::seh_filter(GetExceptionInformation(), rj::SehSite::ShutdownSubsys, &cap)) { ok = false; }
    if (cap.fired) rj::seh_report(cap, rj::SehSite::ShutdownSubsys);
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

    // ISource wiring (Faz 0 karar 1, onaylı): orkestratör ExistingDesktopSource
    // tutar; CaptureSubsystem silindi. unique_ptr: adapter Config'i ctor'da alır
    // (recovery reinit'i saklı cfg_ ile source.init()'e iner) — init()'te kurulur.
    std::unique_ptr<ExistingDesktopSource>                 source_;
    // state() level sinyalinin edge'e dönüşümü — frame thread; tek thread.
    ReinitTriggerPolicy                                    reinit_trigger_;
    EncodeSubsystem                                        encode_sub_;  // Aşama 6
    AudioSubsystem                                          audio_sub_;   // Aşama 3
    reji::pipeline::audio::AudioEncodeBridge               audio_bridge_; // Ses Ayarları: capture→AAC→RTMP köprüsü
    OutputSubsystem                                        output_sub_;  // Aşama 4

    // Aşama 7: D3D11↔Vulkan zero-copy interop — ExternalMemoryBridge yaşam döngüsü +
    // son frame VkImage cache'i (eski ext_bridge/last_staging_vk/last_target_vk alanları).
    GpuInteropSubsystem                                     gpu_sub_;     // Aşama 7

    // Aşama 5: Komut/aksiyon yönlendirme — action_processor thread + SPSC ring +
    // her-frame drain (WS port log, command drain, WS drain, SPSC drain).
    CommandRouter command_router_;

    // Aşama 2: Metrics alt sistemi — CpuMeter + MetricsCollector + fps ölçümü
    // (eski cpu, metrics, last_frame_ticks alanları buraya taşındı).
    MetricsSubsystem metrics_sub_;

    // RTMP_DARBOGAZ Faz 1: capture/encode/send süre teşhisi + wire-fps (1Hz
    // stderr). Tek thread (frame thread) — on_packet da aynı thread'de koşar.
    SendDiag send_diag_;
    bool     send_diag_window_open_ = false;
    // Faz 3(b): WGC preview seyreltme sayacı — her 2. tex'li karede emit (30Hz).
    uint32_t preview_seq_ = 0;

    // VFR/CFR Faz 2 (K1): WGC encode girdisinin kalıcı kopyası. WGC pool
    // texture'ı borrowed (null çağrıda bile bırakılıyor — capture_wgc.cpp:171)
    // ve pool pointer'ları dönüşümlü; NVENC tek-slot kayıt cache'i
    // (encode_nvenc.cpp:285) her pointer değişiminde unregister+register
    // yapıyordu. Kalıcı kopya: (a) null tick'te tekrar edilecek son kareyi
    // elde tutar, (b) NVENC kaydını oturum başına bire indirir. DXGI'de
    // gereksiz — GpuResourceManager'ın kalıcı shared texture'ı zaten kararlı.
    Microsoft::WRL::ComPtr<ID3D11Texture2D> repeat_tex_;

    // VFR/CFR Faz 2 (K3): zaman bazlı yedek IDR — gopLength (120 kare) birincil;
    // bu katman yalnız gerçekleşen keyframe kadansı 2 sn'yi aşınca devreye girer
    // (normalde hiç — idrF>0 sürekliyse tekrar mekanizması aksıyor demektir).
    IdrCadence idr_cadence_{};

    // Her iki encode dalının ortak IDR-kadans kontrolü — encode'dan ÖNCE çağrılır
    // ki istek bu karede tüketilsin (encode_subsystem request_idr → FORCEIDR).
    void maybe_force_idr(int64_t pts_us) noexcept {
        if (idr_cadence_.should_force_idr(pts_us)) {
            encode_sub_.request_idr();
            send_diag_.record_idr_fallback();
        }
    }

    // src'yi kalıcı kopyaya çek; kopyayı döndürür. Boyut/format/cihaz
    // değişiminde yeniden yaratır (cihaz kıyası: device-lost recovery yeni WGC
    // cihazı kurar — bayat cihazın texture'ına CopyResource geçersiz olurdu).
    // Başarısızlıkta nullptr — çağıran canlı texture ile encode'a düşer
    // (bugünkü davranış), tekrar o karede mümkün olmaz. CopyResource
    // submit-only: CPU beklemesi yok (preview ringinin dersi).
    ID3D11Texture2D* copy_for_repeat(ID3D11Texture2D* src) noexcept {
        D3D11_TEXTURE2D_DESC want{};
        src->GetDesc(&want);
        Microsoft::WRL::ComPtr<ID3D11Device> dev;
        src->GetDevice(&dev);
        if (!dev) return nullptr;

        if (repeat_tex_) {
            D3D11_TEXTURE2D_DESC have{};
            repeat_tex_->GetDesc(&have);
            Microsoft::WRL::ComPtr<ID3D11Device> have_dev;
            repeat_tex_->GetDevice(&have_dev);
            if (have.Width != want.Width || have.Height != want.Height ||
                have.Format != want.Format || have_dev.Get() != dev.Get())
                repeat_tex_.Reset();
        }
        if (!repeat_tex_) {
            D3D11_TEXTURE2D_DESC desc = want;
            desc.Usage          = D3D11_USAGE_DEFAULT;
            desc.BindFlags      = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = 0;
            desc.MiscFlags      = 0;
            desc.MipLevels      = 1;
            desc.ArraySize      = 1;
            if (FAILED(dev->CreateTexture2D(&desc, nullptr, &repeat_tex_))) {
                fprintf(stderr, "[Pipeline] repeat_tex_ CreateTexture2D FAILED %ux%u\n",
                        want.Width, want.Height);
                return nullptr;
            }
        }
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> ctx;
        dev->GetImmediateContext(&ctx);
        if (!ctx) return nullptr;
        ctx->CopyResource(repeat_tex_.Get(), src);
        return repeat_tex_.Get();
    }

    // QPC delta → µs (SendDiag beslemesi; kırpma güvenli — süreler <1sn).
    uint32_t ticks_us(int64_t dt) const noexcept {
        const int64_t f = pacer_.qpc_freq();
        return f > 0 ? static_cast<uint32_t>((dt * 1'000'000LL) / f) : 0u;
    }

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
    // Yalnızca frame thread yazar/okur. rj_metrics_poll (V8/I14) artık implemente
    // ama agregeli MetricState'ten pull eder; bu seam deterministik per-frame değeri
    // karakterizasyon için ayrı tutar.
    RjMetricSample last_sample_{};

    // L18: WGC yolunda dxgi() null kaldığından vendor/VRAM getter'ları boş
    // kalıyordu — bağımsız tarama init()'te bir kez doldurulur (getter'lar UI
    // thread'den çağrılır; lazy doldurma yarış ve tekrarlı log üretirdi).
    reji::GpuScan fallback_scan_{};

    const reji::GpuScan& current_scan() const {
        if (source_ && source_->dxgi()) return source_->dxgi()->gpu_scan();
        return fallback_scan_;
    }

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
                    // SIYAH_KUTU kök düzeltmesi: konfigüre bitrate durumu Rust
                    // kural katmanına bildirilir — BitrateRecover yalnız gerçek
                    // düşüş varken üretilir (bkz. healing.rs recovery_has_deficit).
                    // bitrate_kbps.store ile AYNI noktada: tüm yollar kapsanır.
                    ::rj_update_bitrate_state(static_cast<uint32_t>(cmd.param1),
                                              cfg.original_bitrate_kbps);
                }
                break;
            case RJ_ACTION_SCALE_RESOLUTION:
            case RJ_ACTION_RESTORE_RESOLUTION: {
                // HP1: restore da buraya gelir (param1=1000 → scale 1.0; set_resolution
                // mutlak olduğundan tam çözünürlüğe döner). HP3: sonuç artık yutulmuyor —
                // başarısızlıkta senkron ERROR log. Bu dal yalnız healing aksiyonu
                // geldiğinde çalışır (her kare değil), hot-path'e yük bindirmez.
                const float scale = cmd.param1 / 1000.0f;
                if (!encode_sub_.set_resolution(scale)) {
                    fprintf(stderr, "[Pipeline] set_resolution(%.3f) FAILED "
                                    "(action_type=%u) — cozunurluk degismedi\n",
                            scale, cmd.action_type);
                }
                break;
            }
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
    // WGC CPU staging texture ISource wiring'de ExistingDesktopSource'a taşındı
    // (source_->emit_wgc_preview — kontrat-dışı adapter yüzeyi).

    void apply_command(const RjCommand& c) noexcept {
        switch (c.cmd_type) {
            case RJ_CMD_BITRATE_SET:
                if (encode_sub_.raw() && c.param_u32 > 0) {
                    (void)encode_sub_.set_bitrate(c.param_u32);
                    bitrate_kbps.store(c.param_u32, std::memory_order_relaxed);
                    // SIYAH_KUTU: predictive/WS komut yolu da bitrate'i değiştirir —
                    // Rust durumu store ile aynı noktada senkron tutulur.
                    ::rj_update_bitrate_state(c.param_u32, cfg.original_bitrate_kbps);
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

        // VFR/CFR Faz 2 (K3): yedek IDR kadansı GERÇEKLEŞEN keyframe'lerle
        // ölçülür — istek kaybolsa da epoch doğru kalır. streaming kontrolünden
        // ÖNCE: encoder init'ten beri keyframe üretir, kadans yayın öncesi de işler.
        if (pkt.is_keyframe) self->idr_cadence_.on_keyframe(pkt.pts);

        if (!self->streaming.load(std::memory_order_acquire)) return;
        // V10/L22: frame_drop_pct beslemesi — send aşamasına ulaşan her paket
        // sayılır, düşen ayrıca drop (pct = drops/attempts, 30s pencere).
        // L21 sonrası send yalnız GERÇEK gönderim hatasında false döner
        // (bağlantı-yokluğu drop sayılmaz) → pct tıkanıklık sinyalidir.
        self->metrics_sub_.record_frame();
        // send() false döndürürse (aktif çıkış vardı ama gönderim başarısız) → drop.
        // RTMP_DARBOGAZ Faz 1: send süresi ölçülür — RTMP_Write bloklaması bu
        // thread'i (= frame thread) durdurur; "yavaş ama başarılı" send hiçbir
        // sayaçta görünmüyordu. wire-fps yalnız başarılı send'i sayar.
        const int64_t send_t0 = qpc_ticks();
        const bool sent = self->output_sub_.send(pkt.data, pkt.size, pkt.pts);
        self->send_diag_.record_send_video(self->ticks_us(qpc_ticks() - send_t0), sent);
        if (!sent) {
            self->frame_drops.fetch_add(1, std::memory_order_relaxed);
            self->metrics_sub_.record_frame_drop();
        }

        // Ses Ayarları: ses ring'ini bu (encode) thread'de drain et → AAC encode
        // + send_audio. Video ile AYNI thread → tek-thread RTMP-yazım invariant'ı
        // korunur (kilit yok). Ses kapalıysa configure edilmemiştir → no-op.
        const int64_t drain_t0 = qpc_ticks();
        self->audio_bridge_.drain(pkt.pts);
        self->send_diag_.record_audio_drain(self->ticks_us(qpc_ticks() - drain_t0));
    }

    // Ses Ayarları: WASAPI capture callback'i (capture supervisor thread) — yalnız
    // ring'e push eder (encode/gönderme YOK). user_data = Impl* (V10/L12: pacer
    // origin'ine erişim gerekir; origin_us init'te sabitlenir, salt-okunur).
    static void on_audio_capture(const float* samples, uint32_t frames, uint32_t channels,
                                 uint32_t sample_rate, int64_t pts_us, void* ud) noexcept {
        auto* self = static_cast<Impl*>(ud);
        // V10/L12: WASAPI pts'i QPC-mutlak, video pts'i pacer-origin'e göreli —
        // muxer tek epoch paylaştığından ses pts'i aynı tabana indirilir.
        const int64_t rebased = reji::pipeline::audio::rebase_audio_pts(
            pts_us, self->pacer_.origin_us());
        self->audio_bridge_.push(samples, frames, channels, sample_rate, rebased);
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
    // Kullanici bitrate'i REDUCE tabaninin (min_bitrate_kbps) altina indirebilir;
    // o durumda apply_action'daki max(new, min) yuzunden REDUCE hic calismazdi.
    // Tabani kullanici bitrate'ine clamp ederek healing'in referans noktasini
    // gecerli tut (Faz 0 bulgusu). Saf/test-edilebilir: bitrate_policy.h.
    s.cfg.min_bitrate_kbps         = reduce_floor_for_target(s.cfg.min_bitrate_kbps,
                                                             cfg_in.bitrate_kbps);
    s.bitrate_kbps.store(cfg_in.bitrate_kbps, std::memory_order_relaxed);
    // SIYAH_KUTU kök düzeltmesi: başlangıç durumu Rust'a bildirilir
    // (current == original → kurtarılacak düşüş yok, recovery üretilmez).
    ::rj_update_bitrate_state(cfg_in.bitrate_kbps, cfg_in.bitrate_kbps);

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

    //  IScreenCapture::create() — WGC tercihli, DXGI fallback (ExistingDesktopSource)
    {
        rj::IScreenCapture::Config cap_cfg;
        cap_cfg.timeout_ms          = kCaptureTimeout;
        cap_cfg.allow_cross_adapter = true;
        // Config ctor'da saklanır — recovery reinit'i (shutdown+init) aynı
        // config'le döner; eski recovery'nin taze-Config kurulumuyla parite.
        s.source_ = std::make_unique<ExistingDesktopSource>(cap_cfg);
        // init(): create + init + dxgi cast tek çağrıda (create-null / init-fail
        // ayrımı tek fatal path'e indi — ikisi de shutdown+false).
        if (!s.source_->init()) {
            dbglog("[Pipeline] IScreenCapture init failed (create/init)");
            (void)shutdown(); return false;
        }
    }
    if (s.source_->dxgi()) {
        s.source_->dxgi()->setProfiler(profiler_.get());
    } else {
        // L18: WGC yolunda DxgiCapturePipeline yok → gpu_scan_ hiç dolmuyordu;
        // vendor/VRAM getter'ları için bağımsız tarama (tek seferlik, init'te).
        reji::DxgiCapturePipeline::scan_gpus_standalone(s.fallback_scan_);
    }
    // Authoritative dimensions come from the actual display output.
    {
        const SourceMetadata md = s.source_->metadata();
        s.cfg.width  = md.width;
        s.cfg.height = md.height;
    }
    s.width.store(s.cfg.width,  std::memory_order_release);   // atomic = runtime kaynağı
    s.height.store(s.cfg.height, std::memory_order_release);

    //  GpuInteropSubsystem (v0.5.1 zero-copy D3D11↔Vulkan — DXGI path only)
    if (s.source_->dxgi()) {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        // Sıkı düğüm: keyed-mutex capture pipeline'ına dokunur — orkestratörde kalır.
        s.source_->dxgi()->set_use_keyed_mutex(vk && vk->use_keyed_mutex());
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
        // Cihaz seçimi eski sırayla aynı: DXGI encode-GPU, yoksa WGC kendi
        // cihazı — metadata().device tam bu seçimi uygular (adapter yorumu).
        ID3D11Device* encode_device = s.source_->metadata().device;
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

    //  ITransport (SrtTransport | RtmpTransport)  OutputSubsystem alt sistemi
    OutputSubsystem::Config scfg{};
    scfg.protocol = static_cast<rj::TransportProtocol>(cfg_in.transport_protocol);
    if (scfg.protocol == rj::TransportProtocol::Rtmp) {
        scfg.host       = cfg_in.rtmp_url;   // sunucu URL'i (Faz2/Aşama2.2)
        scfg.stream_key = cfg_in.rtmp_key;
    } else {
        scfg.host = cfg_in.srt_host;         // std::string ataması (Faz2/Aşama1)
        scfg.port = cfg_in.srt_port;
    }
    scfg.latency_ms     = 200;
    scfg.bandwidth_kbps = 0;
    scfg.caller_mode    = true;
    if (!s.output_sub_.init(scfg)) {
        dbglog("[Pipeline] transport init failed (proto=%u) -- running without stream output",
               cfg_in.transport_protocol);
    }

    //  Ses (AAC) — AudioSubsystem capture + AudioEncodeBridge (MVP: yalnız RTMP).
    //  output_sub_ init'ten SONRA yapılır: bridge send_audio için transport'a bağlı.
    //  Bridge encoder'ı ilk drain'de (encode thread) lazy init eder.
    if (cfg_in.audio_enabled &&
        static_cast<rj::TransportProtocol>(cfg_in.transport_protocol) == rj::TransportProtocol::Rtmp) {
        constexpr uint32_t kAudioSampleRate = 48000;
        constexpr uint32_t kAudioChannels   = 2;
        constexpr uint32_t kAudioBitrateBps = 128000;  // AAC-LC 128 kbps
        s.audio_bridge_.configure(kAudioSampleRate, kAudioChannels, kAudioBitrateBps, &s.output_sub_);

        AudioSubsystem::Config acfg{};
        acfg.exclusive_mode = false;
        acfg.sample_rate    = kAudioSampleRate;
        acfg.channels       = kAudioChannels;
        acfg.bit_depth      = 32;
        acfg.buffer_ms      = 50;
        acfg.loopback       = cfg_in.loopback;
        acfg.device_id      = cfg_in.audio_device_id;  // wchar_t[] → std::wstring (boş = varsayılan)
        // V10/L12: user_data = Impl (bridge değil) — on_audio_capture pacer
        // origin'iyle pts'i rebase edip bridge'e iletir.
        if (!s.audio_sub_.init(acfg, &Impl::on_audio_capture, &s)) {
            dbglog("[Pipeline] WasapiCapture::init failed  audio disabled");
        }
    } else if (cfg_in.audio_enabled) {
        dbglog("[Pipeline] audio_enabled ama transport RTMP degil — ses MVP'de yalniz RTMP, atlaniyor");
    }

    //  Rust monitor 

    // v0.2 preview staging — allocate once, no hot-path heap (DXGI only)
    if (s.preview_cb && s.source_->dxgi()) {
        if (!s.source_->dxgi()->init_preview_staging())
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

    // Faz2/Aşama2.2: akış SPS/PPS + IDR ile başlasın — encoder init'ten beri
    // çalışıyor, ilk IDR/parametre setleri çoktan geçti; transport (özellikle
    // RTMP sequence header) taze IDR olmadan kare gönderemez.
    impl_->encode_sub_.request_idr();

    // Publish transport pointer before any packet callback can observe it.
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
    // source_ init'ten önce null olabilir (set_preview_callback Impl'i kurar).
    if (impl_->source_ && impl_->source_->dxgi())
        impl_->source_->dxgi()->set_preview_requested(!!impl_->preview_cb);
    fprintf(stderr, "[Pipeline] preview_cb set OK\n"); fflush(stderr);
    return true;
}

bool Pipeline::set_d3d11_frame_callback(D3D11FrameCallback cb) {
    if (!impl_) impl_ = std::make_unique<Impl>();
    impl_->d3d11_frame_cb = std::move(cb);
    // init_preview_staging: DXGI capture hazırsa hemen çağır
    if (impl_->source_ && impl_->source_->dxgi() &&
        !impl_->source_->dxgi()->shared_texture()) {
        impl_->source_->dxgi()->init_preview_staging();
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
    if (s.source_ && s.source_->dxgi()) {
        bool km = vk_init && vk_init->use_keyed_mutex();
        s.source_->dxgi()->set_use_keyed_mutex(km);
        fprintf(stderr, "[Pipeline] notify_vulkan_ready: set_use_keyed_mutex=%d\n", (int)km);
        fflush(stderr);
    }
    return true;
}

bool Pipeline::run_frame() {
    if (!impl_) return false;
    // Eski has_capture() eşdeğeri: kaynak kurulu değilse alma
    // (state()==Uninitialized ⇔ capture_ null — adapter kontratı).
    if (!impl_->source_ ||
        impl_->source_->state() == SourceState::Uninitialized) return false;
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
    if (s.source_->state() != SourceState::Uninitialized) {
        // ExistingDesktopSource: DXGI capture_next() / WGC next_frame() dallarını
        // kapsar; handle'da döner (null-streak adapter içinde: geçerli karede
        // sıfırlanır, null'da artar — state() level sinyali üretir).
        // RTMP_DARBOGAZ Faz 1: capture süresi + null/tex ayrımı — H2 (WGC teslimi)
        // hipotezinin verisi. Null iterasyon drop DEĞİL (S1-ek4) ama SendDiag'da
        // sayılır: RTMP_Write bloklarken kaybolan WGC kareleri burada null olarak
        // görünür.
        const int64_t cap_t0 = qpc_ticks();
        const SourceFrame frame = s.source_->next_frame();
        const uint32_t cap_us = s.ticks_us(qpc_ticks() - cap_t0);
        // Level→edge HER karede beslenir: geçerli kare Running'i görüp
        // tetikleyiciyi temizler; yalnız null dalında beslemek bayat re-arm
        // sayacı bırakırdı (reinit_trigger_policy.h testleri bu kalıbı kilitler).
        const bool reinit_edge = s.reinit_trigger_.on_state(s.source_->state());
        ID3D11Texture2D* tex = static_cast<ID3D11Texture2D*>(frame.handle);
        s.send_diag_.record_capture(cap_us, tex != nullptr);

        if (tex) {
            const int64_t pts_us = s.pacer_.pts_us(frame_start);
            // VFR/CFR Faz 2 (K1): encode girdisi kalıcı kopya — hem null tick
            // tekrarının kaynağı hem (WGC'de) NVENC kayıt thrash'inin sonu.
            // İKİ backend'de de kopyalanır: DXGI handle'ı kararlı ama yaşamı
            // GpuResourceManager'a bağlı (reinit'te yenilenir) — kendi kopyamız
            // tekrar dalını backend'den bağımsız güvenli kılar; ölçülen maliyet
            // ~0.0-0.1ms (copy=). Preview ve d3d11_frame_cb canlı texture'la
            // sürer (davranış değişmez).
            ID3D11Texture2D* enc_tex = tex;
            {
                const int64_t copy_t0 = qpc_ticks();
                if (ID3D11Texture2D* copy = s.copy_for_repeat(tex)) enc_tex = copy;
                s.send_diag_.record_copy(s.ticks_us(qpc_ticks() - copy_t0));
            }
            // encode_frame(): encoder yoksa true (no-op) — eski `s.encoder && ...`
            // koşuluyla aynı: yalnızca gerçek encode hatasında drop + TDR recovery.
            // RTMP_DARBOGAZ Faz 1: süre ölçülür — NVENC senkron olduğundan
            // on_packet (send dahil) bu çağrının İÇİNDE koşar; enc-sendV farkı
            // saf encode maliyetini verir (H3).
            s.maybe_force_idr(pts_us);  // K3: yedek IDR (encode'dan önce)
            const int64_t enc_t0 = qpc_ticks();
            const bool enc_ok = s.encode_sub_.encode_frame(enc_tex, pts_us);
            s.send_diag_.record_encode(s.ticks_us(qpc_ticks() - enc_t0));
            // Faz 3: enc mikro-split — saf NVENC beklemesi (encP+lock) ile
            // on_packet toplamı (encCb) ayrışır; keyframe spike'ları lock'ta,
            // send/audio maliyeti encCb'de görünür.
            if (auto* e = s.encode_sub_.raw()) {
                const auto t = e->last_timings();
                s.send_diag_.record_enc_split(t.encode_us, t.lock_us, t.cb_us);
            }
            if (!enc_ok) {
                if (counts_as_frame_drop(FrameOutcome::EncodeFailed))
                    s.frame_drops.fetch_add(1, std::memory_order_relaxed);
                // 3) GPU TDR check  outside __try, free to use C++ objects
                (void)RecoveryCoordinator::handle_device_lost(
                    *s.source_, s.encode_sub_, s.cfg,
                    s.bitrate_kbps.load(std::memory_order_relaxed),
                    s.width, s.height);
            }
            // v0.5.1: Zero-copy D3D11 frame callback (GPU-side operations, DXGI only)
            // get_frame_images + cache GpuInterop'a taşındı; callback'in kendisi burada.
            // Boyutlar: eski capture_sub_.width()/height() çağrılarının karşılığı
            // (metadata() aynı capture_->width()/height()'a delege eder).
            const SourceMetadata md = s.source_->metadata();
            // RTMP_DARBOGAZ Faz 2: d3d11_frame_cb bloğu ölçülür — gpu_sub_
            // interop (get_frame_images try_lock + cache) ve callback'in kendisi
            // (hibrit-GPU çapraz kopya / GL-Vulkan interop) [SendDiag]'da yoktu.
            if (s.d3d11_frame_cb) {
                const int64_t d3d_t0 = qpc_ticks();
                // Guard eskisiyle bire bir: bridge + dxgi pipeline + shared_texture.
                auto* dxgi = s.source_->dxgi();
                if (s.gpu_sub_.raw() && dxgi && dxgi->shared_texture()) {
                    VkImage staging_vk = nullptr;
                    VkImage target_vk = nullptr;
                    uint32_t slot = 0;  // I23: bridge'in kullandığı pool slot'u
                    // K1: get_frame_images false dönebilir (try_lock — tüketici pool'u
                    // rebuild ediyor). O durumda staging/target null kalır; eski geçerli
                    // cache'i null'la EZME — bu preview karesini atla (encode etkilenmez).
                    if (s.gpu_sub_.get_frame_images(dxgi->shared_texture(),
                                                    &staging_vk, &target_vk, &slot)) {
                        s.gpu_sub_.cache_last_images(staging_vk, target_vk, slot);
                    }
                }

                s.d3d11_frame_cb(static_cast<void*>(tex),
                                 md.width, md.height);
                s.send_diag_.record_interop(s.ticks_us(qpc_ticks() - d3d_t0));
            }

            // RTMP_DARBOGAZ Faz 2: preview yolu ölçülür (topolojiye göre biri
            // aktif — DXGI map veya WGC emit; guard'lar birebir korundu, yalnız
            // ortak preview_cb kontrolü dışa alındı).
            if (s.preview_cb) {
                const int64_t prev_t0 = qpc_ticks();

                // v0.2 CPU preview: staging populated in capture_next() (DXGI only)
                if (s.source_->dxgi()) {
                    auto* dxgi = s.source_->dxgi();
                    const void* data = nullptr; int pitch = 0;
                    if (dxgi->map_preview_frame(&data, &pitch)) {
                        static int cnt = 0;
                        if (++cnt == 1)
                            printf("[Preview] First frame: %dx%d pitch=%d\n",
                                   (int)md.width, (int)md.height, pitch);
                        s.preview_cb(data, (int)md.width, (int)md.height, pitch);
                        dxgi->unmap_preview_frame();
                    }
                }

                // WGC path — CPU staging preview (kontrat-dışı adapter yüzeyi).
                // Preview tetikleme kararı (is_wgc + preview_cb var mı) orkestratörde;
                // preview_cb parametre olarak geçilir — kaynak UI'ı bilmez.
                // Faz 3(b): 30Hz seyreltme — preview 60Hz şart değil, kalan
                // memcpy maliyetini yarılar. Faz 3(c): emit artık beklemesiz
                // try-map; hazır değilse false → prev_miss sayılır.
                if (s.source_->is_wgc()) {
                    if ((s.preview_seq_++ & 1u) == 0u) {
                        if (!s.source_->emit_wgc_preview(s.preview_cb, tex,
                                                         frame.width, frame.height))
                            s.send_diag_.record_preview_miss();
                    }
                }

                s.send_diag_.record_preview(s.ticks_us(qpc_ticks() - prev_t0));
            }

        } else {
            // S1-ek4: null frame ("yeni kare yok") drop DEĞİL — sayaç artmaz
            // (frame_drop_policy.h). Boşta bu sayaç predictive healing'i sahte
            // besleyip START'sız recovery banner döngüsü üretiyordu.
            if (counts_as_frame_drop(FrameOutcome::NoNewFrame))
                s.frame_drops.fetch_add(1, std::memory_order_relaxed);
            // VFR/CFR Faz 2 (K2): kopya varsa son kare tekrar encode edilir —
            // CFR üretimi + ses drain kadansı (on_packet → audio_bridge_.drain
            // artık her tick koşar; A5 kapanır). NeedsReinit'te de sürer (yayın
            // kopmasın). Tekrar drop DEĞİL (frame_drop_policy değişmez); yalnız
            // gerçek encode hatası valid-daldaki gibi drop + recovery sayılır.
            bool recovered_this_tick = false;
            if (should_repeat_frame(s.repeat_tex_ != nullptr, /*is_null_tick=*/true)) {
                const int64_t pts_us = s.pacer_.pts_us(frame_start);
                s.maybe_force_idr(pts_us);  // K3: tekrar dalında da kadans korunur
                const int64_t enc_t0 = qpc_ticks();
                const bool enc_ok = s.encode_sub_.encode_frame(s.repeat_tex_.Get(), pts_us);
                s.send_diag_.record_encode(s.ticks_us(qpc_ticks() - enc_t0));
                if (auto* e = s.encode_sub_.raw()) {
                    const auto t = e->last_timings();
                    s.send_diag_.record_enc_split(t.encode_us, t.lock_us, t.cb_us);
                }
                s.send_diag_.record_repeat();
                if (!enc_ok) {
                    if (counts_as_frame_drop(FrameOutcome::EncodeFailed))
                        s.frame_drops.fetch_add(1, std::memory_order_relaxed);
                    (void)RecoveryCoordinator::handle_device_lost(
                        *s.source_, s.encode_sub_, s.cfg,
                        s.bitrate_kbps.load(std::memory_order_relaxed),
                        s.width, s.height);
                    recovered_this_tick = true;  // reinit_edge ile çifte recovery önlenir
                }
            }
            // Null-streak sayacı adapter'da (state() NeedsReinit level sinyali);
            // reinit_trigger_ geçişte bir kez + 60 karede re-arm ile edge üretir
            // — eski handle_null_frame() cadence'ı birebir (Faz 0 karar 3).
            // Gerçek reinit RecoveryCoordinator'a delege edilir (cross-subsystem).
            if (reinit_edge && !recovered_this_tick) {
                dbglog("[Pipeline] Capture loss detected (60 frames) — reinit");
                (void)RecoveryCoordinator::handle_device_lost(
                    *s.source_, s.encode_sub_, s.cfg,
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

    // RTMP_DARBOGAZ Faz 1: 1Hz teşhis satırı (stderr → run.log). [Metrics]
    // satırlarıyla aynı cadence; wire_fps burada — iç fps metriği döngü temposu
    // sayarken bu, hatta GERÇEKTEN yazılan kareyi raporlar.
    {
        const int64_t freq = s.pacer_.qpc_freq();
        const uint64_t now_us = freq > 0
            ? static_cast<uint64_t>((qpc_ticks() * 1'000'000LL) / freq) : 0u;
        if (!s.send_diag_window_open_) {
            s.send_diag_.begin_window(now_us);
            s.send_diag_window_open_ = true;
        }
        // Faz 2: run_frame iş toplamı (frame_start→şimdi; pace + bu flush'ın
        // kendisi HARİÇ). cap+enc+d3d+prev toplamı ile fark, hâlâ ölçülmeyen
        // bölgeyi (ör. command drain, metrics push) gösterir.
        s.send_diag_.record_total(s.ticks_us(qpc_ticks() - frame_start));
        SendDiagStats diag{};
        if (s.send_diag_.maybe_flush(now_us, &diag)) {
            fprintf(stderr, "%s\n", format_send_diag(diag).c_str());
            fflush(stderr);
        }
    }

    // 5) Frame pacing  absolute deadline (FramePacer alt sistemi)
    // Faz 2: pace süresi ölçülür — döngünün 60Hz temposunun kaynağı burası
    // (WGC TryGetNextFrame beklemez; null iterasyonlar pace ile 16.7ms'e
    // tamamlanır). Kayıt flush'tan sonra olduğundan bir sonraki pencereye düşer.
    const int64_t pace_t0 = qpc_ticks();
    s.pacer_.pace();
    s.send_diag_.record_pace(s.ticks_us(qpc_ticks() - pace_t0));

    // J8: metrics_sub_.poll() ARTIK burada değil — PDH/WMI sorguları AGENTS.md
    // gereği frame thread'inde koşmaz, MetricsSubsystem'in kendi 1Hz arka plan
    // thread'ine taşındı. run_frame yalnız build_sample() içinde get_latest()
    // snapshot okur (yukarıda, "4) Metrics push").
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

        // J8: Stop metrics poll thread (running=false + join). MetricsCollector
        // (metrics_) bu join'den SONRA yok edilir → thread'in metrics_->poll()
        // erişimi güvenli. Destructor da çağırır (idempotent).
        s.metrics_sub_.stop();

        // Finalize profiler
        if (profiler_) {
            profiler_->finalize();
        }

        // SEH-protected teardown  raw pointers only, no C++ destructors in scope.
        ok = seh_shutdown_subsystems(
            s.audio_sub_.raw(), s.encode_sub_.raw(), s.output_sub_.raw());

        // RAII reset outside __try  destructors run safely here.
        s.audio_sub_.shutdown();    // audio_ reset (SEH-leaf DIŞINDA)
        // Ses Ayarları: capture durdu (yukarıda) → artık push yok; encoder'ı
        // drain + kapat. Transport zaten null'landı (set_streaming(false)), son
        // AAC frame'leri gönderilmez (shutdown'da kabul edilebilir).
        s.audio_bridge_.shutdown();
        s.output_sub_.shutdown();   // transport_atomic null + transport_ reset (SEH-leaf DIŞINDA)
        s.encode_sub_.shutdown();   // encoder_ reset (SEH-leaf DIŞINDA)
        if (s.source_) s.source_->shutdown();  // capture_ reset + dxgi_ null
        s.repeat_tex_.Reset();  // VFR/CFR Faz 2: kopya, kaynak cihazıyla ölür

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

bool Pipeline::get_last_frame_images(VkImage* out_staging, VkImage* out_target,
                                     uint32_t* out_slot) {
    if (!impl_ || !out_staging || !out_target) return false;
    // v0.5.1: Return cached frame images from last run_frame() (GpuInteropSubsystem).
    // I23: out_slot da taşınır (bridge pool slot'u).
    return impl_->gpu_sub_.get_last_frame_images(out_staging, out_target, out_slot);
}

bool Pipeline::get_last_metric_sample(RjMetricSample* out) const {
    if (!impl_ || !out) return false;
    *out = impl_->last_sample_;
    return impl_->last_sample_.magic_head == kMetricMagic;
}

uint32_t Pipeline::display_vendor_id() const {
    if (!impl_) return 0;
    const auto& scan = impl_->current_scan();
    return scan.count > 0 ? scan.entries[0].vendor_id : 0;
}

uint64_t Pipeline::max_gpu_vram_mb() const {
    if (!impl_) return 0;
    const auto& scan = impl_->current_scan();
    uint64_t max_mb = 0;
    for (uint32_t i = 0; i < scan.count; ++i) {
        if (scan.entries[i].dedicated_vram_mb > max_mb) {
            max_mb = scan.entries[i].dedicated_vram_mb;
        }
    }
    return max_mb;
}

uint32_t Pipeline::max_vram_vendor_id() const {
    if (!impl_) return 0;
    const auto& scan = impl_->current_scan();
    if (scan.count == 0) return 0;
    // max_gpu_vram_mb ile aynı seçim: en büyük adanmış VRAM'li adaptör.
    // Eşitlikte ilk adaptör kazanır (VRAM'ler eşitse vendor seçimi keyfidir).
    uint32_t best = 0;
    for (uint32_t i = 1; i < scan.count; ++i) {
        if (scan.entries[i].dedicated_vram_mb > scan.entries[best].dedicated_vram_mb) {
            best = i;
        }
    }
    return scan.entries[best].vendor_id;
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
            // V10/L11: param1 = profil step_kbps (HP2 sözleşmesi) — artık
            // kullanılıyor; param1<=0 eski %15 fallback (bitrate_policy.h).
            uint32_t new_bitrate = rj::reduce_step_bitrate(current, action.param1);
            new_bitrate = (std::max)(new_bitrate, impl_->cfg.min_bitrate_kbps);
            impl_->command_router_.push_frame_cmd({RJ_ACTION_BITRATE_REDUCE, static_cast<int32_t>(new_bitrate)});
            return true;
        }
        case RJ_ACTION_BITRATE_RECOVER: {
            uint32_t target  = impl_->cfg.original_bitrate_kbps;
            uint32_t current = impl_->bitrate_kbps.load(std::memory_order_relaxed);
            if (current < target) {
                // V10/L11: REDUCE ile simetrik — recovery kuralı da step taşır.
                uint32_t new_bitrate = (std::min)(
                    rj::recover_step_bitrate(current, action.param1),
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
        case RJ_ACTION_RESTORE_RESOLUTION:
            // HP1: eskiden bu case yoktu → default'a düşüp "unknown action_type"
            // loglanıp false dönüyordu (restore hiç encoder'a ulaşmıyordu). Artık
            // frame cmd olarak iletiliyor; param1 = scale×1000 (restore kuralı boş
            // params → 1000 = 1.0 → mutlak set_resolution tam çözünürlüğe döner).
            impl_->command_router_.push_frame_cmd({RJ_ACTION_RESTORE_RESOLUTION, action.param1});
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
