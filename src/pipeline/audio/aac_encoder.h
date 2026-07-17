// Reji Studio - Media Foundation AAC-LC ses encoder'i (RTMP/FLV mux icin).
// DERLEYICI: /EHa onerilir (proje geneli). Linker: mfplat, mfuuid,
// wmcodecdspuuid, ole32.
//
// AAC encoder MFT (CLSID_AACMFTEncoder) saf yazilimdir — donanim gerektirmez,
// headless calisir. Girdi: 16-bit PCM (float32 iceride S16'ya cevrilir);
// cikti: ham AAC frame'leri (ADTS'siz, MF_MT_AAC_PAYLOAD_TYPE=0) — FLV AUDIODATA
// icin uygun. Sequence header'in tasidigi AudioSpecificConfig deterministik
// olarak aac_config.h'den uretilir (bkz. make_audio_specific_config).
#pragma once
#include <cstdint>
#include <vector>

#include <Windows.h>
#include <mftransform.h>
#include <wrl/client.h>

namespace reji::pipeline::audio {

class AacEncoder {
public:
    struct Config {
        uint32_t sample_rate;   // 48000 / 44100
        uint32_t channels;      // 1 / 2
        uint32_t bitrate_bps;   // hedef ses bitrate (bit/s), orn. 128000
    };

    // Ham fonksiyon-pointer sink (WASAPI deseni: heap/exception yok). Uretilen
    // her ham AAC frame'i icin cagrilir. pts_us capture'dan gelen zaman damgasi.
    typedef void (*OutputCallback)(const uint8_t* aac, uint32_t len,
                                   int64_t pts_us, void* user_data);

    AacEncoder() = default;
    ~AacEncoder();
    AacEncoder(const AacEncoder&)            = delete;
    AacEncoder& operator=(const AacEncoder&) = delete;

    /// MF'i baslatir, AAC encoder MFT'sini olusturur, cikti+girdi tiplerini
    /// pazarlar ve AudioSpecificConfig'i hazirlar. Basarisizsa false (kaynaklar
    /// geri alinir). @param cb null gecilemez.
    bool init(const Config& cfg, OutputCallback cb, void* user_data);

    /// Interleaved float32 [-1,1] ornekleri S16'ya cevirip MFT'ye verir,
    /// hazir AAC frame'lerini sink'e yayar. @param frames kare (ornek/kanal) sayisi.
    bool encode(const float* interleaved, uint32_t frames, int64_t pts_us);

    /// MFT'yi drain eder — arabellekteki kalan AAC frame'lerini bosaltir.
    bool drain();

    /// FLV sequence header'a konacak AudioSpecificConfig (init sonrasi gecerli).
    const std::vector<uint8_t>& audio_specific_config() const noexcept { return asc_; }

    /// Tum MF kaynaklarini serbest birakir, MFShutdown cagirir.
    void shutdown() noexcept;

private:
    template<class T> using ComPtr = Microsoft::WRL::ComPtr<T>;

    bool set_output_type();
    bool set_input_type();
    bool drain_outputs();                 // ProcessOutput dongusu
    bool feed_pcm(const int16_t* pcm, uint32_t sample_count, int64_t pts_us);

    ComPtr<IMFTransform> mft_;
    OutputCallback       sink_{nullptr};
    void*                sink_ud_{nullptr};
    Config               cfg_{};
    std::vector<uint8_t> asc_;
    std::vector<int16_t> pcm_scratch_;    // float->S16 ara tampon (encode-thread-yerel)
    bool                 mf_started_{false};
    bool                 initialized_{false};
    DWORD                input_stream_id_{0};
    DWORD                output_stream_id_{0};
    bool                 output_provides_samples_{false};
    uint32_t             output_sample_size_{0};
};

} // namespace reji::pipeline::audio
