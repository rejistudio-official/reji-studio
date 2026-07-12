// src/pipeline/output/srt_transport.cpp
#include "srt_transport.h"

#include <cstring>

namespace rj::pipeline::output {

bool SrtTransport::init(const Config& cfg) {
    SrtOutput::Config scfg{};
    strncpy_s(scfg.host, sizeof(scfg.host), cfg.host.c_str(), sizeof(scfg.host) - 1);
    scfg.port           = cfg.port;
    scfg.latency_ms     = cfg.latency_ms;
    scfg.bandwidth_kbps = cfg.bandwidth_kbps;
    scfg.caller_mode    = cfg.caller_mode;
    // V9/J2: FFI sink'lerini ITransport::Config'ten SrtOutput'a geçir (OutputSubsystem set etti).
    scfg.on_connection_lost = cfg.on_connection_lost;
    scfg.on_metrics         = cfg.on_metrics;
    scfg.sink_user_data     = cfg.sink_user_data;
    return impl_.init(scfg);
}

bool SrtTransport::send(const uint8_t* data, size_t size, int64_t pts_us) noexcept {
    // noexcept sözleşmesi (V8/I27): olası her exception'ı burada bool'a çevir —
    // ihlali std::terminate'e gider (kesin/öngörülebilir), SEH'in belirsiz
    // yakalamasına güvenmek yerine tip sistemiyle garanti.
    try {
        return impl_.send_packet(data, size, pts_us);
    } catch (...) {
        return false;
    }
}

bool SrtTransport::is_connected() const {
    return impl_.is_connected();
}

void SrtTransport::shutdown() noexcept {
    try {
        (void)impl_.shutdown();
    } catch (...) {
        // shutdown yolu yut: kaynak temizliği en iyi çabayla ilerler
    }
}

}  // namespace rj::pipeline::output
