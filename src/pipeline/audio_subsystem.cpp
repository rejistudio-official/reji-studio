// src/pipeline/audio_subsystem.cpp
//
// AudioSubsystem implementasyonu. Windows'a özel (wasapi_capture.cpp gibi yalnızca
// if(WIN32) altında derlenir). Davranış, Pipeline::init/start_stream/stop_stream/
// shutdown'daki eski audio koduyla birebir aynıdır (Aşama 3 saf çıkarma).
#include "audio_subsystem.h"

namespace rj {

void AudioSubsystem::on_audio(const float*, uint32_t, uint32_t, uint32_t,
                              int64_t, void*) noexcept {}

bool AudioSubsystem::init(const Config& cfg, Callback cb) {
    audio_ = std::make_unique<reji::pipeline::audio::WasapiCapture>();
    if (!audio_->init(cfg, cb)) {
        audio_.reset();
        return false;
    }
    return true;
}

bool AudioSubsystem::start() { return audio_ ? audio_->start() : false; }
bool AudioSubsystem::stop()  { return audio_ ? audio_->stop()  : false; }

} // namespace rj
