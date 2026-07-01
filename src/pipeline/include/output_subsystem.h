// src/pipeline/include/output_subsystem.h
//
// OutputSubsystem — SRT çıkış alt sistemi (Aşama 4'te Pipeline::Impl'den çıkarıldı).
// SrtOutput yaşam döngüsü + thread-safe srt_atomic yayınlama/gönderme.
//
// THREAD-SAFETY: send() encode callback (Impl::on_packet, frame thread) tarafından
// çağrılır; set_streaming()/shutdown() ise start/stop_stream/shutdown'dan (başka
// thread olabilir) çağrılır. srt_atomic_ acquire/release görünürlük sağlar —
// stop_stream() pointer'ı null'ladıktan sonra hiçbir paket serbest bırakılmış
// SrtOutput'a gönderilmez.
//
// on_packet "sıkı düğüm"dür (hem Output hem Metrics'e dokunur) — orkestratörde
// (Impl) kalır, yalnızca gönderme kısmını send()'e devreder.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "srt_output.h"   // -I .../output  (cross-platform: Windows.h çekmez)

namespace rj {

class OutputSubsystem {
public:
    using Config = rj::pipeline::output::SrtOutput::Config;

    // SrtOutput oluşturur + init eder. Başarısızlıkta srt_ reset, false döner.
    bool init(const Config& cfg);

    // srt_atomic publish/null: start_stream → true (srt_ yayınla),
    // stop_stream/shutdown → false (null'la). Encode callback'e karşı release.
    void set_streaming(bool active) noexcept;

    // Thread-safe paket gönderimi (on_packet / encode thread'inden).
    // Dönüş: true  = gönderildi VEYA aktif çıkış yok (drop sayılmaz);
    //        false = aktif çıkış vardı ama gönderme BAŞARISIZ (drop sayılır).
    bool send(const uint8_t* data, size_t size, int64_t pts) noexcept;

    bool is_active() const noexcept { return srt_ != nullptr; }

    // seh_shutdown_subsystems() ile uyum: SEH-leaf'e geçilecek raw pointer.
    rj::pipeline::output::SrtOutput* raw() const noexcept { return srt_.get(); }

    // Teardown: önce srt_atomic_ null (encode thread güvenliği), sonra RAII reset.
    // Cihaz shutdown()'ı seh_shutdown_subsystems SEH-leaf'inde yapılır (raw() ile).
    void shutdown() noexcept;

private:
    std::unique_ptr<rj::pipeline::output::SrtOutput> srt_;
    std::atomic<rj::pipeline::output::SrtOutput*>    srt_atomic_{nullptr};
};

} // namespace rj
