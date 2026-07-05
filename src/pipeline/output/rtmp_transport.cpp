// src/pipeline/output/rtmp_transport.cpp
#include "rtmp_transport.h"

// Zig tarafının C ABI'si (zig-out/lib/rtmp_transport_zig.lib —
// `zig build rtmp` üretir; imzalar rtmp_transport.zig ile bire bir).
extern "C" {
void* rj_rtmp_create();
bool  rj_rtmp_init(void* handle, const char* url);
bool  rj_rtmp_send(void* handle, const uint8_t* data, size_t size, int64_t pts_us);
bool  rj_rtmp_is_connected(void* handle);
void  rj_rtmp_shutdown(void* handle);
void  rj_rtmp_destroy(void* handle);
}

namespace rj::pipeline::output {

RtmpTransport::~RtmpTransport() {
    if (handle_) {
        rj_rtmp_destroy(handle_);   // idempotent Close + serbest bırakma
        handle_ = nullptr;
    }
}

bool RtmpTransport::init(const Config& cfg) {
    if (handle_) return false;   // çift init yok (SrtOutput sözleşmesiyle uyumlu)
    if (cfg.host.empty()) return false;
    handle_ = rj_rtmp_create();
    if (!handle_) return false;
    if (!rj_rtmp_init(handle_, cfg.host.c_str())) {
        rj_rtmp_destroy(handle_);
        handle_ = nullptr;
        return false;
    }
    return true;
}

bool RtmpTransport::send(const uint8_t* data, size_t size, int64_t pts_us) {
    if (!handle_) return false;
    return rj_rtmp_send(handle_, data, size, pts_us);
}

bool RtmpTransport::is_connected() const {
    if (!handle_) return false;
    return rj_rtmp_is_connected(handle_);
}

void RtmpTransport::shutdown() {
    // SEH-leaf'ten (seh_shutdown_subsystems) çağrılır — exception fırlatmaz
    // (Zig tarafı hata kodu döner, panik yolu yok; bkz. Faz2/Aşama1 SEH notu).
    if (handle_) rj_rtmp_shutdown(handle_);
}

}  // namespace rj::pipeline::output
