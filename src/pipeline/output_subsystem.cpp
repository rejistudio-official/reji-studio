// src/pipeline/output_subsystem.cpp
//
// OutputSubsystem implementasyonu. Davranış, Pipeline'ın eski srt/srt_atomic
// koduyla (init/start_stream/stop_stream/on_packet/shutdown) birebir aynıdır
// (Aşama 4 saf çıkarma — baseline_metrics.txt ile doğrulanır; Faz2/Aşama1'de
// somut SrtOutput yerine ITransport soyutlamasına geçildi, davranış korundu).
#include "output_subsystem.h"
#include "ffi_bridge.h"   // V9/J2: passthrough'lar ::rj_connection_lost/::rj_metrics_push çağırır

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "seh_filter.h"  // V8/I10: paylaşımlı SEH filtresi
#endif

namespace rj {
namespace {

#ifdef _WIN32
// SEH leaf: FFI/native send  __declspec(noinline), __try içinde yok edilebilir local yok.
// NOT (Faz2/Aşama1): out->send() artık virtual call — SEH __try içinde MSVC'de
// yasak değil ama bilinçli bir sapma, bkz. seh_shutdown_subsystems (pipeline.cpp).
struct SrtSendArgs {
    rj::ITransport* out;
    const uint8_t*  data;
    size_t          size;
    int64_t         pts;
};
__declspec(noinline)
static int seh_srt_send(SrtSendArgs* a) noexcept {
    SehCapture cap{}; int rv;
    __try   { rv = a->out->send(a->data, a->size, a->pts) ? 0 : 1; }
    __except(seh_filter(GetExceptionInformation(), SehSite::SrtSend, &cap)) { rv = -2; }
    if (cap.fired) seh_report(cap, SehSite::SrtSend);
    return rv;
}

// Ses gönderimi için video send ile aynı SEH-leaf koruması (native RTMP_Write
// çökme yolu ortak). Ayrı arg tipi; __try içinde yok edilebilir C++ local yok.
struct SrtSendAudioArgs {
    rj::ITransport* out;
    const uint8_t*  data;
    size_t          size;
    int64_t         pts;
};
__declspec(noinline)
static int seh_srt_send_audio(SrtSendAudioArgs* a) noexcept {
    SehCapture cap{}; int rv;
    __try   { rv = a->out->send_audio(a->data, a->size, a->pts) ? 0 : 1; }
    __except(seh_filter(GetExceptionInformation(), SehSite::SrtSend, &cap)) { rv = -2; }
    if (cap.fired) seh_report(cap, SehSite::SrtSend);
    return rv;
}
#endif

} // namespace

// V9/J2: I18 passthrough'ları — çıkış bileşeninin ::rj_* FFI'ya doğrudan
// bağımlılığını buraya (subsystem seam) taşır. AudioSubsystem::on_connection_lost/
// on_metrics ile birebir aynı: yalnızca FFI'ya delege eder, user_data kullanılmaz.
void OutputSubsystem::on_connection_lost(const char* reason, void*) noexcept {
    ::rj_connection_lost(reason);
}

void OutputSubsystem::on_metrics(const MetricSample* sample, void*) noexcept {
    ::rj_metrics_push(sample);
}

bool OutputSubsystem::init(const Config& cfg_in) {
    // FFI sink'lerini enjekte et — SRT bunları çağırır, RTMP yok sayar.
    Config cfg = cfg_in;
    cfg.on_connection_lost = &OutputSubsystem::on_connection_lost;
    cfg.on_metrics         = &OutputSubsystem::on_metrics;
    cfg.sink_user_data     = nullptr;

    transport_ = rj::ITransport::create(cfg.protocol);
    if (!transport_->init(cfg)) {
        transport_.reset();
        return false;
    }
    return true;
}

void OutputSubsystem::set_streaming(bool active) noexcept {
    // Publish transport_ pointer before any packet callback can observe it (start),
    // veya null'la ki sonraki paketler gönderilmesin (stop/shutdown).
    transport_atomic_.store(active ? transport_.get() : nullptr, std::memory_order_release);
}

bool OutputSubsystem::send(const uint8_t* data, size_t size, int64_t pts) noexcept {
    auto* out = transport_atomic_.load(std::memory_order_acquire);
    if (!out) return true;   // aktif çıkış yok — drop sayılmaz
#ifdef _WIN32
    SrtSendArgs args{out, data, size, pts};
    return seh_srt_send(&args) == 0;
#else
    return out->send(data, size, pts);
#endif
}

bool OutputSubsystem::set_audio_config(const uint8_t* asc, size_t len) noexcept {
    // İlk send_audio drain'de çağrılır — o an streaming aktif, transport_atomic_ set.
    auto* out = transport_atomic_.load(std::memory_order_acquire);
    if (!out) return false;
    return out->set_audio_config(asc, len);
}

bool OutputSubsystem::send_audio(const uint8_t* aac, size_t len, int64_t pts) noexcept {
    auto* out = transport_atomic_.load(std::memory_order_acquire);
    if (!out) return true;   // aktif çıkış yok — drop sayılmaz (video ile simetrik)
#ifdef _WIN32
    SrtSendAudioArgs args{out, aac, len, pts};
    return seh_srt_send_audio(&args) == 0;
#else
    return out->send_audio(aac, len, pts);
#endif
}

void OutputSubsystem::shutdown() noexcept {
    // Null the atomic pointer before RAII reset — encode thread güvenliği.
    transport_atomic_.store(nullptr, std::memory_order_release);
    transport_.reset();
}

} // namespace rj
