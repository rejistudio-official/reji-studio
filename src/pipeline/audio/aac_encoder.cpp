// src/pipeline/audio/aac_encoder.cpp
//
// Media Foundation AAC-LC encoder implementasyonu. AAC encoder MFT saf yazilim
// oldugundan (donanim/surucu yok), WASAPI'deki SEH-leaf disiplini burada
// uygulanmaz — tum sinir cagrilari HRESULT ile kontrol edilir (onayli sade
// passthrough; bkz. audio_subsystem.cpp benzeri not). MFT sozlesmesi:
//   1) cikti tipi ONCE (AAC encoder zorunlulugu), sonra girdi tipi (PCM16),
//   2) ProcessInput -> MF_E_NOTACCEPTING olunca ProcessOutput ile drain,
//   3) drain(): DRAIN mesaji + NEED_MORE_INPUT'a kadar ProcessOutput.
#include "aac_encoder.h"
#include "aac_config.h"    // make_audio_specific_config
#include "pcm_convert.h"   // float_to_s16

#include <cstring>

#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <wmcodecdsp.h>    // CLSID_AACMFTEncoder
#include <mmreg.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace reji::pipeline::audio {

namespace {
// 100ns <-> mikrosaniye
inline int64_t us_to_hns(int64_t us) noexcept { return us * 10; }
inline int64_t hns_to_us(int64_t hns) noexcept { return hns / 10; }
constexpr uint32_t kMinOutputBuffer = 8192;  // cbSize=0 durumunda emniyet tamponu
} // namespace

AacEncoder::~AacEncoder() { shutdown(); }

bool AacEncoder::init(const Config& cfg, OutputCallback cb, void* user_data) {
    if (initialized_)               return false;
    if (!cb)                        return false;
    if (cfg.channels < 1 || cfg.channels > 2)             return false;
    if (cfg.sample_rate != 48000 && cfg.sample_rate != 44100) return false;

    cfg_     = cfg;
    sink_    = cb;
    sink_ud_ = user_data;

    // AudioSpecificConfig deterministik (aac_config.h ile testlenen yol).
    auto asc = make_audio_specific_config(cfg.sample_rate, cfg.channels);
    if (!asc.has_value()) return false;
    asc_.assign(asc->begin(), asc->end());

    HRESULT hr = ::MFStartup(MF_VERSION, MFSTARTUP_LITE);
    if (FAILED(hr)) { shutdown(); return false; }
    mf_started_ = true;

    hr = ::CoCreateInstance(CLSID_AACMFTEncoder, nullptr, CLSCTX_INPROC_SERVER,
                            IID_PPV_ARGS(&mft_));
    if (FAILED(hr) || !mft_) { shutdown(); return false; }

    // Stream ID'leri — AAC MFT tek girdi/tek cikti (E_NOTIMPL => 0,0).
    DWORD in_id = 0, out_id = 0;
    hr = mft_->GetStreamIDs(1, &in_id, 1, &out_id);
    if (hr == S_OK) { input_stream_id_ = in_id; output_stream_id_ = out_id; }
    // E_NOTIMPL: varsayilan 0/0 kalir.

    if (!set_output_type()) { shutdown(); return false; }
    if (!set_input_type())  { shutdown(); return false; }

    MFT_OUTPUT_STREAM_INFO osi{};
    hr = mft_->GetOutputStreamInfo(output_stream_id_, &osi);
    if (FAILED(hr)) { shutdown(); return false; }
    output_provides_samples_ =
        (osi.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES |
                        MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)) != 0;
    output_sample_size_ = osi.cbSize > 0 ? osi.cbSize : kMinOutputBuffer;

    if (FAILED(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0)) ||
        FAILED(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0))) {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

bool AacEncoder::set_output_type() {
    ComPtr<IMFMediaType> t;
    if (FAILED(::MFCreateMediaType(&t))) return false;

    const uint32_t bytes_per_sec = cfg_.bitrate_bps / 8;
    HRESULT hr = S_OK;
    hr |= t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    hr |= t->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_AAC);
    hr |= t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr |= t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, cfg_.sample_rate);
    hr |= t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, cfg_.channels);
    hr |= t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, bytes_per_sec);
    hr |= t->SetUINT32(MF_MT_AAC_PAYLOAD_TYPE, 0);  // ham AAC (ADTS/ADIF/LOAS degil)
    hr |= t->SetUINT32(MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x29);  // AAC-LC
    if (FAILED(hr)) return false;

    return SUCCEEDED(mft_->SetOutputType(output_stream_id_, t.Get(), 0));
}

bool AacEncoder::set_input_type() {
    ComPtr<IMFMediaType> t;
    if (FAILED(::MFCreateMediaType(&t))) return false;

    const uint32_t block_align = cfg_.channels * 2;               // S16
    const uint32_t avg_bytes   = cfg_.sample_rate * block_align;
    HRESULT hr = S_OK;
    hr |= t->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    hr |= t->SetGUID(MF_MT_SUBTYPE,    MFAudioFormat_PCM);
    hr |= t->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr |= t->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, cfg_.sample_rate);
    hr |= t->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, cfg_.channels);
    hr |= t->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, block_align);
    hr |= t->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, avg_bytes);
    hr |= t->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
    if (FAILED(hr)) return false;

    return SUCCEEDED(mft_->SetInputType(input_stream_id_, t.Get(), 0));
}

bool AacEncoder::feed_pcm(const int16_t* pcm, uint32_t sample_count, int64_t pts_us) {
    const DWORD bytes = sample_count * sizeof(int16_t);

    ComPtr<IMFMediaBuffer> buf;
    if (FAILED(::MFCreateMemoryBuffer(bytes, &buf))) return false;

    BYTE* dst = nullptr;
    if (FAILED(buf->Lock(&dst, nullptr, nullptr))) return false;
    std::memcpy(dst, pcm, bytes);
    buf->Unlock();
    if (FAILED(buf->SetCurrentLength(bytes))) return false;

    ComPtr<IMFSample> sample;
    if (FAILED(::MFCreateSample(&sample)))     return false;
    if (FAILED(sample->AddBuffer(buf.Get())))  return false;

    const uint32_t frames = sample_count / cfg_.channels;
    sample->SetSampleTime(us_to_hns(pts_us));
    sample->SetSampleDuration(
        static_cast<LONGLONG>(frames) * 10'000'000LL / cfg_.sample_rate);

    HRESULT hr = mft_->ProcessInput(input_stream_id_, sample.Get(), 0);
    if (hr == MF_E_NOTACCEPTING) {
        if (!drain_outputs()) return false;
        hr = mft_->ProcessInput(input_stream_id_, sample.Get(), 0);
    }
    return SUCCEEDED(hr);
}

bool AacEncoder::drain_outputs() {
    for (;;) {
        MFT_OUTPUT_DATA_BUFFER out{};
        out.dwStreamID = output_stream_id_;

        ComPtr<IMFSample>      out_sample;
        ComPtr<IMFMediaBuffer> out_buf;
        if (!output_provides_samples_) {
            if (FAILED(::MFCreateSample(&out_sample)))                    return false;
            if (FAILED(::MFCreateMemoryBuffer(output_sample_size_, &out_buf))) return false;
            if (FAILED(out_sample->AddBuffer(out_buf.Get())))            return false;
            out.pSample = out_sample.Get();
        }

        DWORD status = 0;
        HRESULT hr = mft_->ProcessOutput(0, 1, &out, &status);

        if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
            if (out.pEvents) out.pEvents->Release();
            return true;   // arabellek bosaldi, daha fazla girdi lazim
        }
        if (FAILED(hr)) {
            if (out.pEvents) out.pEvents->Release();
            return false;
        }

        // MFT kendi sample'ini verdiyse onu kullan.
        IMFSample* produced = output_provides_samples_ ? out.pSample : out_sample.Get();
        if (produced) {
            ComPtr<IMFMediaBuffer> pbuf;
            if (SUCCEEDED(produced->ConvertToContiguousBuffer(&pbuf)) && pbuf) {
                BYTE* data = nullptr; DWORD len = 0;
                if (SUCCEEDED(pbuf->Lock(&data, nullptr, &len))) {
                    LONGLONG t_hns = 0;
                    produced->GetSampleTime(&t_hns);
                    if (len > 0 && sink_) sink_(data, len, hns_to_us(t_hns), sink_ud_);
                    pbuf->Unlock();
                }
            }
        }
        // MFT kendi verdiyse sahipligi biz aldik — serbest birak.
        if (output_provides_samples_ && out.pSample) out.pSample->Release();
        if (out.pEvents) out.pEvents->Release();
    }
}

bool AacEncoder::encode(const float* interleaved, uint32_t frames, int64_t pts_us) {
    if (!initialized_ || !interleaved || frames == 0) return false;

    const uint32_t samples = frames * cfg_.channels;
    pcm_scratch_.resize(samples);
    for (uint32_t i = 0; i < samples; ++i)
        pcm_scratch_[i] = float_to_s16(interleaved[i]);

    if (!feed_pcm(pcm_scratch_.data(), samples, pts_us)) return false;
    return drain_outputs();
}

bool AacEncoder::drain() {
    if (!initialized_) return false;
    if (FAILED(mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0))) return false;
    if (FAILED(mft_->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, 0)))        return false;
    return drain_outputs();
}

void AacEncoder::shutdown() noexcept {
    if (mft_) {
        (void)mft_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, 0);
        mft_.Reset();
    }
    if (mf_started_) {
        (void)::MFShutdown();
        mf_started_ = false;
    }
    initialized_ = false;
    sink_        = nullptr;
    sink_ud_     = nullptr;
}

} // namespace reji::pipeline::audio
