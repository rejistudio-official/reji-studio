// src/pipeline/include/audio_subsystem.h
//
// AudioSubsystem — WASAPI ses yakalama alt sistemi (Aşama 3'te Pipeline::Impl'den
// çıkarıldı). WasapiCapture yaşam döngüsünü (init/start/stop/teardown) sarmalar.
//
// Windows'a özel: wasapi_capture.h doğrudan <Windows.h> çeker; bu başlık yalnızca
// _WIN32 altında include edilmelidir (pipeline.cpp'nin _WIN32 bloğunda).
//
// SEH NOTU: Cihaz teardown'ı (stop + shutdown) Pipeline::shutdown() içinde
// seh_shutdown_subsystems() SEH-leaf'ine raw() pointer'ı geçilerek yapılır.
// AudioSubsystem::shutdown() yalnızca RAII reset'i (unique_ptr yıkımı) yapar ve
// __try bloğunun DIŞINDA çağrılmalıdır — proje kuralı gereği.
#pragma once
#include <cstdint>
#include <memory>
#include "wasapi_capture.h"   // -I .../audio

namespace rj {

class AudioSubsystem {
public:
    using Config   = reji::pipeline::audio::WasapiCapture::Config;
    using Callback = reji::pipeline::audio::WasapiCapture::AudioFrameCallback;

    // v0.1 audio callback stub (SRT mux henüz yok) — init()'e geçilir.
    static void on_audio(const float*, uint32_t, uint32_t, uint32_t,
                         int64_t, void*) noexcept;

    // WasapiCapture oluşturur + init eder. Başarısızlıkta audio_ reset, false döner.
    bool init(const Config& cfg, Callback cb);

    bool start();   // audio_ yoksa false
    bool stop();    // audio_ yoksa false

    bool is_active() const noexcept { return audio_ != nullptr; }

    // seh_shutdown_subsystems() ile uyum: SEH-leaf'e geçilecek raw pointer.
    reji::pipeline::audio::WasapiCapture* raw() const noexcept { return audio_.get(); }

    // RAII teardown — SEH-leaf (stop/shutdown) DIŞINDA çağrılmalı.
    void shutdown() noexcept { audio_.reset(); }

private:
    std::unique_ptr<reji::pipeline::audio::WasapiCapture> audio_;
};

} // namespace rj
