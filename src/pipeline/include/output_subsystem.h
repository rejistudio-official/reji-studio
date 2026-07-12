// src/pipeline/include/output_subsystem.h
//
// OutputSubsystem — çıkış alt sistemi (Aşama 4'te Pipeline::Impl'den çıkarıldı;
// Faz2/Aşama1'de somut SrtOutput yerine ITransport soyutlamasına bağlandı).
// ITransport yaşam döngüsü + thread-safe transport_atomic_ yayınlama/gönderme.
//
// THREAD-SAFETY: send() encode callback (Impl::on_packet, frame thread) tarafından
// çağrılır; set_streaming()/shutdown() ise start/stop_stream/shutdown'dan (başka
// thread olabilir) çağrılır. transport_atomic_ acquire/release görünürlük sağlar —
// stop_stream() pointer'ı null'ladıktan sonra hiçbir paket serbest bırakılmış
// transport'a gönderilmez.
//
// on_packet "sıkı düğüm"dür (hem Output hem Metrics'e dokunur) — orkestratörde
// (Impl) kalır, yalnızca gönderme kısmını send()'e devreder.
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include "i_transport.h"   // -I .../include  (cross-platform: Windows.h çekmez)

namespace rj {

class OutputSubsystem {
public:
    using Config = rj::ITransport::Config;

    // ITransport::create() ile transport oluşturur + init eder.
    // Başarısızlıkta transport_ reset, false döner.
    bool init(const Config& cfg);

    // transport_atomic_ publish/null: start_stream → true (transport_ yayınla),
    // stop_stream/shutdown → false (null'la). Encode callback'e karşı release.
    void set_streaming(bool active) noexcept;

    // Thread-safe paket gönderimi (on_packet / encode thread'inden).
    // Dönüş: true  = gönderildi VEYA aktif çıkış yok (drop sayılmaz);
    //        false = aktif çıkış vardı ama gönderme BAŞARISIZ (drop sayılır).
    bool send(const uint8_t* data, size_t size, int64_t pts) noexcept;

    bool is_active() const noexcept { return transport_ != nullptr; }

    // V9/J2: I18 FFI-sink passthrough'ları (AudioSubsystem::on_connection_lost/
    // on_metrics ile aynı rol). init() bunları cfg'ye enjekte eder; transport
    // doğrudan ::rj_* yerine bu passthrough'ları çağırır. Yalnızca ::rj_*'a delege
    // eder → davranış birebir korunur.
    static void on_connection_lost(const char* reason, void* ud) noexcept;
    static void on_metrics(const MetricSample* sample, void* ud) noexcept;

    // seh_shutdown_subsystems() ile uyum: SEH-leaf'e geçilecek raw pointer.
    rj::ITransport* raw() const noexcept { return transport_.get(); }

    // Teardown: önce transport_atomic_ null (encode thread güvenliği), sonra RAII reset.
    // Cihaz shutdown()'ı seh_shutdown_subsystems SEH-leaf'inde yapılır (raw() ile).
    void shutdown() noexcept;

private:
    std::unique_ptr<rj::ITransport> transport_;
    std::atomic<rj::ITransport*>    transport_atomic_{nullptr};
};

} // namespace rj
