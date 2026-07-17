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

    // V8/I18: WasapiCapture'ın FFI'yi doğrudan çağırmasını engelleyen ince
    // passthrough'lar. Orchestrator-yüzlü bu katman gerçek ::rj_* çağrısını
    // yapar; capture katmanı yalnız sink'e delege eder. Argümanlar birebir
    // forward edilir — davranışsal eşdeğerlik (aynı arg/sıra/koşul) korunur.
    static void on_connection_lost(const char* reason, void* ud) noexcept;
    static void on_metrics(const RjMetricSample* sample, void* ud) noexcept;

    // WasapiCapture oluşturur + init eder. Başarısızlıkta audio_ reset, false döner.
    // @param user_data on_audio callback'ine iletilir (ör. AudioEncodeBridge*).
    bool init(const Config& cfg, Callback cb, void* user_data);

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
