// srt_output_stub.cpp — SRT SDK olmadan derlenir, bos implementasyon
#include "srt_output.h"

namespace rj::pipeline::output {

// pImpl stub — Impl tanimli olmali, aksi halde unique_ptr destructor derlenemez
struct SrtOutput::Impl {};

SrtOutput::SrtOutput()  : impl_(nullptr) {}
SrtOutput::~SrtOutput() = default;

bool SrtOutput::init(const Config&)                           { return false; }
bool SrtOutput::send_packet(const uint8_t*, size_t, int64_t) { return false; }
bool SrtOutput::shutdown()                                    { return true;  }
bool SrtOutput::is_connected() const noexcept                 { return false; }
bool SrtOutput::set_bitrate(uint32_t)                         { return false; }

}
