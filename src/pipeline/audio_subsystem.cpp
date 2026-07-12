// src/pipeline/audio_subsystem.cpp
//
// AudioSubsystem implementasyonu. Windows'a özel (wasapi_capture.cpp gibi yalnızca
// if(WIN32) altında derlenir). Davranış, Pipeline::init/start_stream/stop_stream/
// shutdown'daki eski audio koduyla birebir aynıdır (Aşama 3 saf çıkarma).
#include "audio_subsystem.h"

namespace rj {

void AudioSubsystem::on_audio(const float*, uint32_t, uint32_t, uint32_t,
                              int64_t, void*) noexcept {}

// V8/I18: capture katmanının sink'lerinden gelen çağrıları gerçek FFI'ye
// forward eder — eski doğrudan ::rj_* çağrılarıyla birebir aynı (SEH eklenmez,
// onaylı direkt-passthrough kararı).
void AudioSubsystem::on_connection_lost(const char* reason, void*) noexcept {
    ::rj_connection_lost(reason);
}

void AudioSubsystem::on_metrics(const RjMetricSample* sample, void*) noexcept {
    ::rj_metrics_push(sample);
}

bool AudioSubsystem::init(const Config& cfg, Callback cb) {
    audio_ = std::make_unique<reji::pipeline::audio::WasapiCapture>();
    if (!audio_->init(cfg, cb, nullptr, &on_connection_lost, &on_metrics)) {
        audio_.reset();
        return false;
    }
    return true;
}

bool AudioSubsystem::start() { return audio_ ? audio_->start() : false; }
bool AudioSubsystem::stop()  { return audio_ ? audio_->stop()  : false; }

} // namespace rj
