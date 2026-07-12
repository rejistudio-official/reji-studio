#pragma once

#include <cstdint>
#include <cstddef>
#include <atomic>
#include <memory>

// V9/J2: metrik sink imzası için MetricSample (== RjMetricSample) forward-decl.
struct MetricSample;

namespace rj::pipeline::output {

enum class RjError : int {
    Ok               = 0,
    InitFailed       = 1,
    ConnectFailed    = 2,
    SendFailed       = 3,
    NotConnected     = 4,
    InvalidArgument  = 5,
    AlreadyInit      = 6,
    Unknown          = -1
};

class SrtOutput {
public:
    struct Config {
        char     host[256];
        uint16_t port;
        uint32_t latency_ms;     // SRT latency, varsayılan 200 ms
        uint32_t bandwidth_kbps; // 0 = sınırsız
        bool     caller_mode;    // true=caller, false=listener

        // V9/J2: I18 FFI-sink'leri (SrtTransport, ITransport::Config'ten kopyalar).
        // init() ikisinin de non-null olmasını ister; doğrudan ::rj_* yerine çağrılır.
        void (*on_connection_lost)(const char* reason, void* ud) = nullptr;
        void (*on_metrics)(const MetricSample* sample, void* ud)  = nullptr;
        void*  sink_user_data                                    = nullptr;
    };

    SrtOutput();
    ~SrtOutput();

    SrtOutput(const SrtOutput&)            = delete;
    SrtOutput& operator=(const SrtOutput&) = delete;
    SrtOutput(SrtOutput&&)                 = delete;
    SrtOutput& operator=(SrtOutput&&)      = delete;

    bool init(const Config& cfg);
    bool send_packet(const uint8_t* data, size_t size, int64_t pts);
    bool is_connected() const noexcept;
    bool set_bitrate(uint32_t kbps);
    bool shutdown();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rj::pipeline::output
