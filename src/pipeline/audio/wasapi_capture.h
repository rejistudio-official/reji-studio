// Reji Studio - WASAPI Ses Yakalama Modülü
// DERLEYICI ZORUNLU: /EHa  (SEH + C++ exception interop)
// Linker: Ole32.lib, Avrt.lib, Mmdevapi.lib

#pragma once
#include <Windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <wrl/client.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "ffi_bridge.h"

namespace reji::pipeline::audio {

// ABI boyut doğrulaması — Rust #[repr(C)] (naturel hizalanmış) ile eşleşmeli.
// v0.3: RjMetricSample = 40 bytes
// v0.4: RjMetricSample = 56 bytes (extended: frame_drop_pct, gpu_temp_c, cpu_temp_c,
//                                   memory_usage_pct, cpu_load_pct, network_rtt_ms,
//                                   network_loss_pct, reserved)
// Layout (x64 MSVC, #pragma pack(8)):
//   uint32_t magic_head (0) + 4pad + uint64_t timestamp_us (8) + uint32_t bitrate_kbps (16)
//   + float fps_actual (20) + float cpu_percent (24) + uint32_t frame_drops (28)
//   + uint32_t frame_drop_pct (32) + int16_t gpu_temp_c (36) + int16_t cpu_temp_c (38)
//   + uint32_t memory_usage_pct (40) + uint32_t cpu_load_pct (44) + uint16_t network_rtt_ms (48)
//   + uint8_t network_loss_pct (50) + uint8_t reserved (51) + uint32_t magic_tail (52) = 56 bytes
static_assert(sizeof(RjMetricSample) == 56,
              "RjMetricSample ABI drift — expected 56 bytes (v0.4 extended)");
static_assert(sizeof(RjCommand) == 24,
              "RjCommand ABI bozulmus — Rust repr(C) ile eslesmeli");

namespace detail {
struct HandleDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h && h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    }
};
using UniqueHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleDeleter>;

struct MmcssDeleter {
    void operator()(HANDLE h) const noexcept {
        if (h) ::AvRevertMmThreadCharacteristics(h);
    }
};
using UniqueMmcss = std::unique_ptr<std::remove_pointer_t<HANDLE>, MmcssDeleter>;

struct CoTaskMemDeleter {
    void operator()(void* p) const noexcept { if (p) ::CoTaskMemFree(p); }
};
using UniqueWaveFormat = std::unique_ptr<WAVEFORMATEX, CoTaskMemDeleter>;
} // namespace detail

class WasapiCapture {
public:
    struct Config {
        bool     exclusive_mode;   // true=exclusive, false=shared
        uint32_t sample_rate;      // 48000 / 44100
        uint32_t channels;         // 1 / 2
        uint32_t bit_depth;        // 16 / 32
        uint32_t buffer_ms;        // tampon süresi (ms)
        bool     loopback;         // true=sistem sesi (eRender), false=mikrofon
    };

    // std::function yerine ham fonksiyon pointer — hot-path heap tahsis riski yok.
    // user_data ile durum aktarımı sağlanır; callback blocking, allocation-free,
    // exception-free olmalıdır.
    typedef void (*AudioFrameCallback)(
        const float* samples,
        uint32_t     frame_count,
        uint32_t     channels,
        uint32_t     sample_rate,
        int64_t      pts_us,
        void*        user_data
    );

    WasapiCapture();
    ~WasapiCapture();
    WasapiCapture(const WasapiCapture&)            = delete;
    WasapiCapture& operator=(const WasapiCapture&) = delete;

    /// COM altsistemini ayağa kaldırır, cihaz numaralayıcısını oluşturur,
    /// IMMNotificationClient kaydeder ve uzun-ömürlü supervisor thread'ini
    /// başlatır. Yakalama henüz aktif değildir (start() çağrılmalı).
    /// @param fn    Ses karesi callback'i — null geçilemez.
    /// @param ud    Callback'e iletilecek kullanıcı verisi (null kabul edilir).
    /// @return true: hazır; false: hata.
    bool init(const Config& cfg, AudioFrameCallback fn, void* ud = nullptr);

    /// Yakalama oturumunu açar (Audio Engine init + IAudioClient::Start).
    /// Exclusive mode başarısızsa otomatik shared mode fallback yapılır.
    /// @return true: yakalama başladı; false: aksi.
    bool start();

    /// Yakalamayı durdurur (Audio Engine release dahil), ancak supervisor
    /// thread'i ve COM ortamı ayakta kalır — tekrar start() çağrılabilir.
    /// @return true: durdu; false: zaten durmuş veya hata.
    bool stop();

    /// Tüm kaynakları serbest bırakır, supervisor thread'i join eder,
    /// IMMNotificationClient unregister eder, COM'u kapatır.
    /// __try/__except ile sarılıdır; sürücü çökmesi sızdırmaz.
    /// @return true: temiz kapanış; false: SEH yakalandı.
    bool shutdown();

    /// Yakalama döngüsü aktif mi (atomic).
    bool is_running() const;

    /// Aktif örnekleme hızı (Hz). init+start sonrasında geçerli.
    uint32_t get_sample_rate() const;

    /// Aktif kanal sayısı. init+start sonrasında geçerli.
    uint32_t get_channels() const;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;
    friend class DeviceNotifyClient;

    // ---- SEH leaf fonksiyonlar (sadece POD; C++ nesnesi YOK) ----
    static __declspec(noinline) HRESULT seh_get_buffer(
        IAudioCaptureClient*, BYTE**, UINT32*, DWORD*, UINT64*, UINT64*) noexcept;
    static __declspec(noinline) HRESULT seh_release_buffer(
        IAudioCaptureClient*, UINT32) noexcept;
    static __declspec(noinline) HRESULT seh_next_packet_size(
        IAudioCaptureClient*, UINT32*) noexcept;
    static __declspec(noinline) HRESULT seh_audio_stop(IAudioClient*) noexcept;
    static __declspec(noinline) void    seh_shutdown_leaf(
        IMMDeviceEnumerator*, IMMNotificationClient*, HANDLE, HANDLE) noexcept;

    // ---- iç yardımcılar ----
    void    supervisor_main();
    bool    open_device_and_init_engine();
    void    release_engine();
    bool    capture_loop();
    void    convert_to_float(const BYTE* src, uint32_t frames, DWORD flags);
    void    detect_silence_and_jitter(uint32_t frames, DWORD flags, int64_t now_us);
    void    publish_metrics(int64_t now_us);
    void    on_device_change(const char* reason);
    float   compute_cpu_percent() noexcept;   // GetProcessTimes delta tabanlı

    // ---- COM/HANDLE üyeleri (RAII) ----
    ComPtr<IMMDeviceEnumerator>   enumerator_;
    ComPtr<IMMDevice>             device_;
    ComPtr<IAudioClient>          audio_client_;
    ComPtr<IAudioCaptureClient>   capture_client_;
    ComPtr<IMMNotificationClient> notify_client_;
    detail::UniqueHandle          wake_event_;
    detail::UniqueHandle          audio_event_;
    detail::UniqueWaveFormat      mix_format_;

    // ---- thread durumu ----
    std::thread        supervisor_;
    std::atomic<bool>  com_initialized_{false};
    std::atomic<bool>  initialized_{false};
    std::atomic<bool>  start_requested_{false};
    std::atomic<bool>  stop_thread_{false};
    std::atomic<bool>  running_{false};
    std::atomic<bool>  device_dirty_{false};
    std::atomic<bool>  engine_active_{false};

    // ---- runtime config / format ----
    Config             cfg_{};
    AudioFrameCallback callback_fn_{nullptr};
    void*              callback_ud_{nullptr};
    uint32_t           actual_sample_rate_{0};
    uint32_t           actual_channels_{0};
    uint32_t           actual_bits_{0};
    bool               actual_is_float_{false};
    uint32_t           buffer_frames_{0};
    bool               using_exclusive_{false};

    // ---- hot-path scratch (sabit boyutlu, heap YOK) ----
    static constexpr uint32_t kMaxFrames   = 8192;
    static constexpr uint32_t kMaxChannels = 8;
    alignas(64) float scratch_[kMaxFrames * kMaxChannels]{};

    // ---- istatistik / sezgi ----
    std::atomic<uint32_t> overrun_count_{0};
    std::atomic<uint32_t> underrun_count_{0};
    std::atomic<uint32_t> frame_drops_{0};
    uint32_t jitter_window_glitches_{0};
    uint8_t  jitter_window_count_{0};
    int64_t  silence_start_us_{0};
    bool     silence_logged_{false};
    int64_t  last_metric_us_{0};

    // ---- CPU örnekleme durumu (yalnızca capture thread erişir) ----
    ULARGE_INTEGER cpu_prev_{};
    ULARGE_INTEGER wall_prev_{};
};

// ---- IMMNotificationClient gerçeklemesi (cihaz değişikliği) ----
class DeviceNotifyClient : public IMMNotificationClient {
public:
    explicit DeviceNotifyClient(WasapiCapture* o) noexcept : owner_(o) {}

    ULONG  STDMETHODCALLTYPE AddRef()  noexcept override { return ++ref_; }
    ULONG  STDMETHODCALLTYPE Release() noexcept override {
        ULONG r = --ref_; if (r == 0) delete this; return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** ppv) noexcept override {
        if (!ppv) return E_POINTER;
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient*>(this); AddRef(); return S_OK;
        }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow f, ERole r, LPCWSTR) noexcept override {
        (void)f;
        if (r == eConsole && owner_) owner_->on_device_change("default-device-changed");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) noexcept override {
        if (owner_) owner_->on_device_change("device-removed");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD st) noexcept override {
        if (owner_ && (st == DEVICE_STATE_DISABLED  ||
                       st == DEVICE_STATE_NOTPRESENT ||
                       st == DEVICE_STATE_UNPLUGGED))
            owner_->on_device_change("device-state-changed");
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) noexcept override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR, const PROPERTYKEY) noexcept override { return S_OK; }

private:
    std::atomic<ULONG> ref_{1};
    WasapiCapture*     owner_;
};

} // namespace reji::pipeline::audio
