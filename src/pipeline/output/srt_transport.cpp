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
    return impl_.init(scfg);
}

bool SrtTransport::send(const uint8_t* data, size_t size, int64_t pts_us) {
    return impl_.send_packet(data, size, pts_us);
}

bool SrtTransport::is_connected() const {
    return impl_.is_connected();
}

void SrtTransport::shutdown() {
    (void)impl_.shutdown();
}

}  // namespace rj::pipeline::output
