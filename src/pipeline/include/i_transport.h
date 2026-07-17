#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include "reji_constants.h"

// V9/J2: metrik sink imzası için RjMetricSample (== MetricSample) gerekir. Tam
// tanım ffi_auto.h/ffi_bridge.h'da; burada yalnız pointer için forward-declare —
// bu başlığı FFI/Windows başlıklarından bağımsız (Windows.h çekmez) tutar.
struct MetricSample;

namespace rj {

enum class TransportProtocol : uint32_t {
    Srt  = 0,
    Rtmp = 1,   // Faz2/Aşama2.2 — düz rtmp:// (NO_CRYPTO, TLS kararı A)
};

// V9/J2: I18 FFI-sink deseni (wasapi ConnectionLostCallback/MetricsCallback ile
// aynı sözleşme). Çıkış bileşeni doğrudan ::rj_* çağırmaz; OutputSubsystem bu
// passthrough'ları set eder, transport onları çağırır. Böylece SRT bileşeni
// app-FFI'dan ayrışır (test edilebilir). RtmpTransport bu alanları yok sayar —
// latency_ms/bandwidth_kbps'in "SRT'ye özel, RTMP yok sayar" deseniyle aynı.
using ConnectionLostSink = void (*)(const char* reason, void* user_data);
using MetricsSink        = void (*)(const MetricSample* sample, void* user_data);

class ITransport {
public:
    struct Config {
        TransportProtocol protocol = TransportProtocol::Srt;
        // Srt:  host = sunucu adı/IP (port ayrı alanda), stream_key kullanılmaz.
        // Rtmp: host = sunucu URL'i ("rtmp://host/app" — KEY HARİÇ), stream_key
        //       zorunlu; port kullanılmaz (URL'in içinde).
        std::string host;
        std::string stream_key;
        uint16_t    port           = 9000;
        uint32_t    latency_ms     = rj::constants::kSrtLatencyMs;  // SRT'ye özel
        uint32_t    bandwidth_kbps = 0;   // 0 = sınırsız; SRT'ye özel, RTMP yok sayar
        bool        caller_mode    = true;

        // V9/J2: FFI event sink'leri (SRT kullanır, RTMP yok sayar). OutputSubsystem
        // daima non-null set eder; SRT init null ise başarısız olur (I18 sözleşmesi).
        ConnectionLostSink on_connection_lost = nullptr;
        MetricsSink        on_metrics         = nullptr;
        void*              sink_user_data     = nullptr;
    };

    virtual ~ITransport() = default;
    virtual bool init(const Config&)                                             = 0;
    virtual bool send(const uint8_t* data, size_t size, int64_t pts_us) noexcept = 0;
    virtual bool is_connected() const                                           = 0;
    virtual void shutdown() noexcept                                            = 0;

    // Ses (AAC) yolu — yalnız RTMP/FLV MVP'sinde gerçek. Diğer transport'lar
    // (SRT: konteynersiz ham-ES; ses ayrı tur) varsayılan no-op ile yok sayar,
    // böylece Config'teki latency/bandwidth'in "SRT'ye özel" deseniyle simetrik.
    /// AudioSpecificConfig'i saklar (encoder init sonrası bir kez).
    virtual bool set_audio_config(const uint8_t* /*asc*/, size_t /*len*/) { return false; }
    /// Ham AAC frame'i gönderir; pts_us video ile aynı saati paylaşır (A/V sync).
    virtual bool send_audio(const uint8_t* /*aac*/, size_t /*len*/,
                            int64_t /*pts_us*/) noexcept { return false; }

    static std::unique_ptr<ITransport> create(TransportProtocol proto);
};

} // namespace rj
