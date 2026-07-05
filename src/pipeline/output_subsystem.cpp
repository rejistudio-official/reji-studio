// src/pipeline/output_subsystem.cpp
//
// OutputSubsystem implementasyonu. Davranış, Pipeline'ın eski srt/srt_atomic
// koduyla (init/start_stream/stop_stream/on_packet/shutdown) birebir aynıdır
// (Aşama 4 saf çıkarma — baseline_metrics.txt ile doğrulanır; Faz2/Aşama1'de
// somut SrtOutput yerine ITransport soyutlamasına geçildi, davranış korundu).
#include "output_subsystem.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
    __try   { return a->out->send(a->data, a->size, a->pts) ? 0 : 1; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return -2; }
}
#endif

} // namespace

bool OutputSubsystem::init(const Config& cfg) {
    transport_ = rj::ITransport::create();
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

void OutputSubsystem::shutdown() noexcept {
    // Null the atomic pointer before RAII reset — encode thread güvenliği.
    transport_atomic_.store(nullptr, std::memory_order_release);
    transport_.reset();
}

} // namespace rj
