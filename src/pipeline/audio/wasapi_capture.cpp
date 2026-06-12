#include "wasapi_capture.h"

#include <Functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <chrono>
#include <cmath>
#include <cstring>

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Avrt.lib")
#pragma comment(lib, "Mmdevapi.lib")

namespace reji::pipeline::audio {

namespace {
constexpr int64_t kSilenceUs        = 500'000;
constexpr int64_t kMetricPeriodUs   = 1'000'000;
constexpr float   kSilenceAmplitude = 1.0f / 32768.0f;

inline int64_t now_us() noexcept {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// SEH filter — yalnızca EXCEPTION_EXECUTE_HANDLER döner.
// Kural: filter içinde C++ nesnesi yaratma/yıkma ve herhangi bir fonksiyon
// çağrısı yasak; filter exception handler'ı seçer, başka iş yapmaz.
extern "C" __declspec(noinline) LONG seh_filter(unsigned /*code*/) noexcept {
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace

// ============================================================================
// SEH leaf fonksiyonlar — yalnızca POD parametre, __declspec(noinline)
// ============================================================================
__declspec(noinline) HRESULT WasapiCapture::seh_get_buffer(
    IAudioCaptureClient* c, BYTE** d, UINT32* n,
    DWORD* f, UINT64* dp, UINT64* qp) noexcept {
    __try { return c->GetBuffer(d, n, f, dp, qp); }
    __except (seh_filter(GetExceptionCode())) { return E_FAIL; }
}

__declspec(noinline) HRESULT WasapiCapture::seh_release_buffer(
    IAudioCaptureClient* c, UINT32 n) noexcept {
    __try { return c->ReleaseBuffer(n); }
    __except (seh_filter(GetExceptionCode())) { return E_FAIL; }
}

__declspec(noinline) HRESULT WasapiCapture::seh_next_packet_size(
    IAudioCaptureClient* c, UINT32* n) noexcept {
    __try { return c->GetNextPacketSize(n); }
    __except (seh_filter(GetExceptionCode())) { return E_FAIL; }
}

__declspec(noinline) HRESULT WasapiCapture::seh_audio_stop(IAudioClient* c) noexcept {
    __try { return c ? c->Stop() : S_OK; }
    __except (seh_filter(GetExceptionCode())) { return E_FAIL; }
}

__declspec(noinline) void WasapiCapture::seh_shutdown_leaf(
    IMMDeviceEnumerator* enm, IMMNotificationClient* nc,
    HANDLE h1, HANDLE h2) noexcept {
    __try {
        if (enm && nc) enm->UnregisterEndpointNotificationCallback(nc);
        if (h1 && h1 != INVALID_HANDLE_VALUE) ::SetEvent(h1);
        if (h2 && h2 != INVALID_HANDLE_VALUE) ::SetEvent(h2);
    } __except (seh_filter(GetExceptionCode())) { }
}

// ============================================================================
// ctor / dtor
// ============================================================================
WasapiCapture::WasapiCapture() = default;
WasapiCapture::~WasapiCapture() { (void)shutdown(); }

// ============================================================================
// init
// ============================================================================
bool WasapiCapture::init(const Config& cfg, AudioFrameCallback fn, void* ud) {
    if (initialized_.load(std::memory_order_acquire)) return false;
    if (!fn)                                          return false;
    if (cfg.channels == 0 || cfg.channels > kMaxChannels)   return false;
    if (cfg.sample_rate < 8000 || cfg.sample_rate > 192000) return false;
    if (cfg.bit_depth != 16 && cfg.bit_depth != 32)         return false;
    if (cfg.buffer_ms == 0 || cfg.buffer_ms > 1000)         return false;

    cfg_         = cfg;
    callback_fn_ = fn;
    callback_ud_ = ud;

    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == RPC_E_CHANGED_MODE) {
        // Başka thread farklı modda init etmiş — non-fatal, COM kullanılabilir.
        com_initialized_.store(false, std::memory_order_release);
    } else if (hr == S_OK || hr == S_FALSE) {
        com_initialized_.store(true, std::memory_order_release);
    } else {
        return false;
    }

    hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            IID_PPV_ARGS(&enumerator_));
    if (FAILED(hr)) { (void)shutdown(); return false; }

    auto* nc = new (std::nothrow) DeviceNotifyClient(this);
    if (!nc) { (void)shutdown(); return false; }
    notify_client_.Attach(nc); // sahipliği ComPtr'a devret (refcount=1)

    hr = enumerator_->RegisterEndpointNotificationCallback(notify_client_.Get());
    if (FAILED(hr)) { (void)shutdown(); return false; }

    wake_event_.reset (::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    audio_event_.reset(::CreateEventW(nullptr, FALSE, FALSE, nullptr));
    if (!wake_event_ || !audio_event_) { (void)shutdown(); return false; }

    stop_thread_.store(false, std::memory_order_release);
    try {
        supervisor_ = std::thread(&WasapiCapture::supervisor_main, this);
    } catch (...) { (void)shutdown(); return false; }

    initialized_.store(true, std::memory_order_release);
    return true;
}

// ============================================================================
// start / stop / is_running / getters
// ============================================================================
bool WasapiCapture::start() {
    if (!initialized_.load(std::memory_order_acquire))              return false;
    if (start_requested_.exchange(true, std::memory_order_acq_rel)) return false;
    ::SetEvent(wake_event_.get());
    return true;
}

bool WasapiCapture::stop() {
    if (!initialized_.load(std::memory_order_acquire))               return false;
    if (!start_requested_.exchange(false, std::memory_order_acq_rel)) return false;
    ::SetEvent(wake_event_.get());
    return true;
}

bool     WasapiCapture::is_running()      const { return running_.load(std::memory_order_acquire); }
uint32_t WasapiCapture::get_sample_rate() const { return actual_sample_rate_; }
uint32_t WasapiCapture::get_channels()    const { return actual_channels_; }

// ============================================================================
// shutdown — SEH ile sarılı
// ============================================================================
bool WasapiCapture::shutdown() {
    bool ok = true;

    // Atomic flag'ler — SEH koruması gerekmez (in-process bellek)
    stop_thread_.store(true, std::memory_order_release);
    start_requested_.store(false, std::memory_order_release);

    // Capture thread'ini uyandır; SetEvent SEH leaf'i içinde korunuyor.
    // enm/nc null → UnregisterEndpointNotificationCallback atlanır.
    seh_shutdown_leaf(nullptr, nullptr, wake_event_.get(), audio_event_.get());

    // joinable() kontrolü sonrası join() fırlatmaz; try/catch gereksiz.
    if (supervisor_.joinable()) supervisor_.join();

    // D16: Null owner_ before unregister — callback artık UAF'a yol açamaz
    if (notify_client_)
        static_cast<DeviceNotifyClient*>(notify_client_.Get())->clear_owner();
    // Notification client unregister + handle sinyalleme — SEH leaf'inde
    seh_shutdown_leaf(enumerator_.Get(), notify_client_.Get(),
                      wake_event_.get(), audio_event_.get());

    // RAII teardown — SEH dışında (C++ nesnesi yıkımı)
    capture_client_.Reset();
    audio_client_.Reset();
    device_.Reset();
    notify_client_.Reset();
    enumerator_.Reset();
    mix_format_.reset();
    wake_event_.reset();
    audio_event_.reset();

    if (com_initialized_.exchange(false, std::memory_order_acq_rel)) {
        ::CoUninitialize();
    }
    initialized_.store(false, std::memory_order_release);
    return ok;
}

// ============================================================================
// Supervisor thread — uzun ömürlü, self-healing
// ============================================================================
void WasapiCapture::supervisor_main() {
    HRESULT hr   = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool t_co_ok = (hr == S_OK || hr == S_FALSE);

    DWORD task_idx = 0;
    HANDLE mmcss = ::AvSetMmThreadCharacteristicsW(L"Pro Audio", &task_idx);
    if (!mmcss) mmcss = ::AvSetMmThreadCharacteristicsW(L"Audio", &task_idx);
    detail::UniqueMmcss mmcss_h(mmcss);
    if (mmcss) ::AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_CRITICAL);

    while (!stop_thread_.load(std::memory_order_acquire)) {
        if (!start_requested_.load(std::memory_order_acquire)) {
            ::WaitForSingleObject(wake_event_.get(), 1000);
            continue;
        }

        if (device_dirty_.exchange(false, std::memory_order_acq_rel)) {
            release_engine();
        }

        if (!engine_active_.load(std::memory_order_acquire)) {
            if (!open_device_and_init_engine()) {
                ::WaitForSingleObject(wake_event_.get(), 500);
                continue;
            }
            if (FAILED(audio_client_->Start())) {
                release_engine();
                ::WaitForSingleObject(wake_event_.get(), 500);
                continue;
            }
            engine_active_.store(true, std::memory_order_release);
            running_.store(true, std::memory_order_release);
        }

        if (!capture_loop()) release_engine();

        if (!start_requested_.load(std::memory_order_acquire)) release_engine();
    }

    release_engine();
    if (t_co_ok) ::CoUninitialize();
}

// ============================================================================
// Engine açma — 3 aşamalı format stratejisi
// ============================================================================
bool WasapiCapture::open_device_and_init_engine() {
    HRESULT hr = enumerator_->GetDefaultAudioEndpoint(
        cfg_.loopback ? eRender : eCapture, eConsole, &device_);
    if (FAILED(hr) || !device_) return false;

    hr = device_->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &audio_client_);
    if (FAILED(hr)) return false;

    WAVEFORMATEXTENSIBLE wfx{};
    wfx.Format.wFormatTag      = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels       = static_cast<WORD>(cfg_.channels);
    wfx.Format.nSamplesPerSec  = cfg_.sample_rate;
    wfx.Format.wBitsPerSample  = static_cast<WORD>(cfg_.bit_depth);
    wfx.Format.nBlockAlign     = (wfx.Format.nChannels * wfx.Format.wBitsPerSample) / 8;
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.cbSize          = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = static_cast<WORD>(cfg_.bit_depth);
    wfx.dwChannelMask = (cfg_.channels == 1)
        ? SPEAKER_FRONT_CENTER
        : (SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT);
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
    if (cfg_.loopback) flags |= AUDCLNT_STREAMFLAGS_LOOPBACK;

    REFERENCE_TIME hns = static_cast<REFERENCE_TIME>(cfg_.buffer_ms) * 10000;

    auto try_init = [&](AUDCLNT_SHAREMODE share, const WAVEFORMATEX* fmt,
                        REFERENCE_TIME buf, REFERENCE_TIME period) -> HRESULT {
        return audio_client_->Initialize(share, flags, buf, period, fmt, nullptr);
    };

    HRESULT      init_hr = E_FAIL;
    const WAVEFORMATEX* used = reinterpret_cast<const WAVEFORMATEX*>(&wfx);

    // Strateji 1: exclusive (istenmiş ve loopback değilse)
    if (cfg_.exclusive_mode && !cfg_.loopback) {
        init_hr = try_init(AUDCLNT_SHAREMODE_EXCLUSIVE, used, hns, hns);
        if (init_hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 frames = 0;
            if (SUCCEEDED(audio_client_->GetBufferSize(&frames))) {
                hns = static_cast<REFERENCE_TIME>(
                    10000.0 * 1000.0 * frames / cfg_.sample_rate + 0.5);
                audio_client_.Reset();
                if (SUCCEEDED(device_->Activate(__uuidof(IAudioClient),
                                                CLSCTX_ALL, nullptr, &audio_client_))) {
                    init_hr = try_init(AUDCLNT_SHAREMODE_EXCLUSIVE, used, hns, hns);
                }
            }
        }
        using_exclusive_ = SUCCEEDED(init_hr);
    }

    // Strateji 2: shared + kullanıcı formatı
    if (FAILED(init_hr)) {
        using_exclusive_ = false;
        if (!audio_client_) {
            if (FAILED(device_->Activate(__uuidof(IAudioClient),
                                         CLSCTX_ALL, nullptr, &audio_client_)))
                return false;
        }
        init_hr = try_init(AUDCLNT_SHAREMODE_SHARED, used, hns, 0);
    }

    // Strateji 3: shared + GetMixFormat (son çare)
    if (FAILED(init_hr)) {
        WAVEFORMATEX* mix = nullptr;
        if (SUCCEEDED(audio_client_->GetMixFormat(&mix)) && mix) {
            mix_format_.reset(mix);
            audio_client_.Reset();
            if (FAILED(device_->Activate(__uuidof(IAudioClient),
                                         CLSCTX_ALL, nullptr, &audio_client_)))
                return false;
            init_hr = try_init(AUDCLNT_SHAREMODE_SHARED, mix_format_.get(), hns, 0);
            used = mix_format_.get();
        }
    }
    if (FAILED(init_hr)) return false;

    // Format tespiti (float vs PCM)
    if (used->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        actual_is_float_ = true;
    } else if (used->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* ex = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(used);
        actual_is_float_ = (ex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
    } else {
        actual_is_float_ = false;
    }
    actual_sample_rate_ = used->nSamplesPerSec;
    actual_channels_    = used->nChannels;
    actual_bits_        = used->wBitsPerSample;

    // Örnekleme hızı uyumsuzluğu uyarısı — resampling v0.2'ye ertelendi.
    if (actual_sample_rate_ != cfg_.sample_rate) {
        OutputDebugStringA("[rj_wasapi] uyari: cihaz sample_rate farklı — "
                           "resampling v0.2'ye ertelendi\n");
    }

    if (FAILED(audio_client_->SetEventHandle(audio_event_.get())))    return false;
    if (FAILED(audio_client_->GetService(IID_PPV_ARGS(&capture_client_)))) return false;
    UINT32 bf = 0;
    if (SUCCEEDED(audio_client_->GetBufferSize(&bf))) buffer_frames_ = bf;

    return true;
}

void WasapiCapture::release_engine() {
    if (audio_client_) (void)seh_audio_stop(audio_client_.Get());
    capture_client_.Reset();
    audio_client_.Reset();
    device_.Reset();
    mix_format_.reset();
    engine_active_.store(false, std::memory_order_release);
    running_.store(false, std::memory_order_release);
}

// ============================================================================
// Cihaz değişimi — IMMNotificationClient'tan tetiklenir
// ============================================================================
void WasapiCapture::on_device_change(const char* reason) {
    device_dirty_.store(true, std::memory_order_release);
    ::rj_connection_lost(reason);
    if (wake_event_) ::SetEvent(wake_event_.get());
}

// ============================================================================
// Capture döngüsü — false dönerse supervisor engine'i yeniden açar
// ============================================================================
bool WasapiCapture::capture_loop() {
    HANDLE evs[2] = { audio_event_.get(), wake_event_.get() };

    while (!stop_thread_.load(std::memory_order_acquire)
        &&  start_requested_.load(std::memory_order_acquire)
        && !device_dirty_.load(std::memory_order_acquire))
    {
        DWORD wr = ::WaitForMultipleObjects(2, evs, FALSE, 500);
        if (wr == WAIT_TIMEOUT) {
            publish_metrics(now_us());
            continue;
        }
        if (wr == WAIT_OBJECT_0 + 1) continue; // wake_event — start/stop/dev-change
        if (wr != WAIT_OBJECT_0)     return false;

        UINT32  pkt = 0;
        HRESULT hr  = seh_next_packet_size(capture_client_.Get(), &pkt);
        while (SUCCEEDED(hr) && pkt > 0) {
            BYTE*  data    = nullptr;
            UINT32 frames  = 0;
            DWORD  fl      = 0;
            UINT64 dev_pos = 0, qpc_100ns = 0;

            hr = seh_get_buffer(capture_client_.Get(), &data, &frames,
                                &fl, &dev_pos, &qpc_100ns);
            if (FAILED(hr)) {
                if (hr == AUDCLNT_E_DEVICE_INVALIDATED)
                    ::rj_connection_lost("audio-device-invalidated");
                return false;
            }

            int64_t pts_us = (qpc_100ns != 0)
                ? static_cast<int64_t>(qpc_100ns / 10)
                : now_us();

            if (frames > kMaxFrames) {
                overrun_count_.fetch_add(1, std::memory_order_relaxed);
                frame_drops_.fetch_add(1, std::memory_order_relaxed);
                frames = kMaxFrames;
            }
            if (actual_channels_ == 0 || actual_channels_ > kMaxChannels) {
                (void)seh_release_buffer(capture_client_.Get(), frames);
                return false;
            }

            convert_to_float(data, frames, fl);
            detect_silence_and_jitter(frames, fl, pts_us);

            if (callback_fn_) {
                callback_fn_(scratch_, frames, actual_channels_,
                             actual_sample_rate_, pts_us, callback_ud_);
            }

            if (FAILED(seh_release_buffer(capture_client_.Get(), frames))) return false;
            hr = seh_next_packet_size(capture_client_.Get(), &pkt);
        }

        publish_metrics(now_us());
    }
    return true;
}

// ============================================================================
// Format dönüşümü — float32 (scratch_ kullanılır, heap YOK)
// ============================================================================
void WasapiCapture::convert_to_float(const BYTE* src, uint32_t frames, DWORD flags) {
    const uint32_t total = frames * actual_channels_;
    if (flags & AUDCLNT_BUFFERFLAGS_SILENT) {
        std::memset(scratch_, 0, total * sizeof(float));
        return;
    }
    if (actual_is_float_) {
        std::memcpy(scratch_, src, total * sizeof(float));
        return;
    }
    if (actual_bits_ == 16) {
        const int16_t* s = reinterpret_cast<const int16_t*>(src);
        constexpr float k = 1.0f / 32768.0f;
        for (uint32_t i = 0; i < total; ++i) scratch_[i] = static_cast<float>(s[i]) * k;
    } else if (actual_bits_ == 32) {
        const int32_t* s = reinterpret_cast<const int32_t*>(src);
        constexpr float k = 1.0f / 2147483648.0f;
        for (uint32_t i = 0; i < total; ++i) scratch_[i] = static_cast<float>(s[i]) * k;
    } else {
        std::memset(scratch_, 0, total * sizeof(float));
    }
}

// ============================================================================
// Sessizlik + RT jitter sezgisi (5 pencerede 3 glitch → uyarı)
// ============================================================================
void WasapiCapture::detect_silence_and_jitter(uint32_t frames, DWORD flags, int64_t now) {
    bool glitch = (flags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) != 0;
    if (glitch) {
        underrun_count_.fetch_add(1, std::memory_order_relaxed);
        ++jitter_window_glitches_;
    }
    if (++jitter_window_count_ >= 5) {
        if (jitter_window_glitches_ >= 3)
            OutputDebugStringA("[rj_wasapi] uyari: RT jitter (5 paket/3 glitch)\n");
        jitter_window_count_   = 0;
        jitter_window_glitches_ = 0;
    }

    float peak = 0.0f;
    const uint32_t total = frames * actual_channels_;
    for (uint32_t i = 0; i < total; ++i) {
        float a = std::fabs(scratch_[i]);
        if (a > peak) peak = a;
    }
    if (peak < kSilenceAmplitude) {
        if (silence_start_us_ == 0) {
            silence_start_us_ = now;
            silence_logged_   = false;
        } else if (!silence_logged_ && (now - silence_start_us_) >= kSilenceUs) {
            silence_logged_ = true;
            OutputDebugStringA("[rj_wasapi] uyari: 500ms+ sessizlik algılandi\n");
        }
    } else {
        silence_start_us_ = 0;
        silence_logged_   = false;
    }
}

// ============================================================================
// CPU örnekleme — GetProcessTimes delta tabanlı, ~1s periyotta çağrılır
// ============================================================================
float WasapiCapture::compute_cpu_percent() noexcept {
    FILETIME ct, et, kt, ut;
    if (!GetProcessTimes(GetCurrentProcess(), &ct, &et, &kt, &ut)) return 0.0f;

    FILETIME wall_ft;
    GetSystemTimeAsFileTime(&wall_ft);

    ULARGE_INTEGER cpu, wall;
    cpu.LowPart  = kt.dwLowDateTime;
    cpu.HighPart = kt.dwHighDateTime;
    ULARGE_INTEGER ut_li;
    ut_li.LowPart  = ut.dwLowDateTime;
    ut_li.HighPart = ut.dwHighDateTime;
    cpu.QuadPart  += ut_li.QuadPart; // kernel + user time toplamı (100ns birim)

    wall.LowPart  = wall_ft.dwLowDateTime;
    wall.HighPart = wall_ft.dwHighDateTime;

    if (wall_prev_.QuadPart == 0) {
        // İlk çağrı — referans al, 0 döndür
        cpu_prev_  = cpu;
        wall_prev_ = wall;
        return 0.0f;
    }

    uint64_t d_cpu  = cpu.QuadPart  - cpu_prev_.QuadPart;
    uint64_t d_wall = wall.QuadPart - wall_prev_.QuadPart;

    cpu_prev_  = cpu;
    wall_prev_ = wall;

    if (d_wall == 0) return 0.0f;

    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    uint32_t cores = si.dwNumberOfProcessors;
    if (cores == 0) cores = 1;

    // d_cpu / (d_wall * cores): tüm CPU kapasitesinin yüzdesi olarak
    return static_cast<float>(d_cpu) /
           static_cast<float>(d_wall * static_cast<uint64_t>(cores)) * 100.0f;
}

// ============================================================================
// Periyodik metrik publish
// ============================================================================
void WasapiCapture::publish_metrics(int64_t now) {
    if (now - last_metric_us_ < kMetricPeriodUs) return;
    last_metric_us_ = now;

    RjMetricSample s{};
    s.magic_head   = RJ_METRIC_MAGIC;
    s.magic_tail   = RJ_METRIC_MAGIC;
    s.timestamp_us = static_cast<uint64_t>(now);
    s.bitrate_kbps = (actual_sample_rate_ * actual_channels_ * 32u) / 1000u;
    s.fps_actual   = (buffer_frames_ > 0)
                   ? static_cast<float>(actual_sample_rate_) / buffer_frames_
                   : 0.0f;
    s.cpu_percent  = compute_cpu_percent();
    s.frame_drops  = frame_drops_.exchange(0, std::memory_order_relaxed);
    ::rj_metrics_push(&s);
}

} // namespace reji::pipeline::audio
