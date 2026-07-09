// src/pipeline/output/srt_transport.h
//
// SrtTransport — ITransport'un SRT implementasyonu (Faz 2 / Aşama 1).
// Kompozisyon ile sarmalama (miras değil): SrtOutput'un test edilmiş koduna
// sıfır risk, yalnızca public arayüzüne delege eder.
//
// SDK var/yok ayrımı SrtOutput seviyesinde çözülür (srt_output.cpp /
// srt_output_stub.cpp) — SrtTransport her iki durumda da aynı şekilde derlenir.
#pragma once
#include "../include/i_transport.h"
#include "srt_output.h"

namespace rj::pipeline::output {

class SrtTransport final : public rj::ITransport {
public:
    bool init(const Config& cfg) override;
    bool send(const uint8_t* data, size_t size, int64_t pts_us) noexcept override;
    bool is_connected() const override;
    void shutdown() noexcept override;

private:
    SrtOutput impl_;
};

}  // namespace rj::pipeline::output
