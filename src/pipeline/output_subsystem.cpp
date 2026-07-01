// src/pipeline/output_subsystem.cpp
//
// OutputSubsystem implementasyonu. Davranış, Pipeline'ın eski srt/srt_atomic
// koduyla (init/start_stream/stop_stream/on_packet/shutdown) birebir aynıdır
// (Aşama 4 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "output_subsystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace rj {
namespace {

#ifdef _WIN32
// SEH leaf: FFI/native send  __declspec(noinline), __try içinde yok edilebilir local yok.
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
#endif

} // namespace

bool OutputSubsystem::init(const Config& cfg) {
    srt_ = std::make_unique<rj::pipeline::output::SrtOutput>();
    if (!srt_->init(cfg)) {
        srt_.reset();
        return false;
    }
    return true;
}

void OutputSubsystem::set_streaming(bool active) noexcept {
    // Publish srt_ pointer before any packet callback can observe it (start),
    // veya null'la ki sonraki paketler gönderilmesin (stop/shutdown).
    srt_atomic_.store(active ? srt_.get() : nullptr, std::memory_order_release);
}

bool OutputSubsystem::send(const uint8_t* data, size_t size, int64_t pts) noexcept {
    auto* out = srt_atomic_.load(std::memory_order_acquire);
    if (!out) return true;   // aktif çıkış yok — drop sayılmaz
#ifdef _WIN32
    SrtSendArgs args{out, data, size, pts};
    return seh_srt_send(&args) == 0;
#else
    return out->send_packet(data, size, pts);
#endif
}

void OutputSubsystem::shutdown() noexcept {
    // Null the atomic pointer before RAII reset — encode thread güvenliği.
    srt_atomic_.store(nullptr, std::memory_order_release);
    srt_.reset();
}

} // namespace rj
