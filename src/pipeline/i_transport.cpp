// src/pipeline/i_transport.cpp
//
// ITransport::create() faktörü (Faz 2 / Aşama 1) — header'a değil buraya:
// header, somut implementasyonları (SrtTransport) include etmek zorunda kalmasın.
#include "i_transport.h"
#include "output/srt_transport.h"

namespace rj {

std::unique_ptr<ITransport> ITransport::create() {
    // Şimdilik tek implementasyon. Aşama 2 (RTMP) burayı bir seçim
    // parametresiyle genişletecek — bugün RTMP yok, sadece SRT.
    return std::make_unique<rj::pipeline::output::SrtTransport>();
}

} // namespace rj
