// src/pipeline/i_transport.cpp
//
// ITransport::create() faktörü (Faz 2 / Aşama 1) — header'a değil buraya:
// header, somut implementasyonları (SrtTransport) include etmek zorunda kalmasın.
#include "i_transport.h"
#include "output/srt_transport.h"
#include "output/rtmp_transport.h"

namespace rj {

std::unique_ptr<ITransport> ITransport::create(TransportProtocol proto) {
    switch (proto) {
        case TransportProtocol::Rtmp:
            return std::make_unique<rj::pipeline::output::RtmpTransport>();
        case TransportProtocol::Srt:
        default:
            return std::make_unique<rj::pipeline::output::SrtTransport>();
    }
}

} // namespace rj
