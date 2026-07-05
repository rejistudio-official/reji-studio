// src/pipeline/encode_subsystem.cpp
//
// EncodeSubsystem implementasyonu. Windows'a özel (audio_subsystem.cpp gibi
// yalnızca WIN32 altında derlenir). Davranış, Pipeline'ın eski encoder/packet_cb
// koduyla (init / apply_frame_cmd / run_frame / handle_device_lost) birebir
// aynıdır (Aşama 6 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "encode_subsystem.h"

namespace rj {

bool EncodeSubsystem::init(ID3D11Device* device, const Config& cfg, PacketCallback cb) {
    packet_cb_ = std::move(cb);           // reinit() için sakla (TDR recovery)
    encoder_   = std::make_unique<reji::NvencEncoder>();
    if (!encoder_->init(device, cfg, packet_cb_)) {
        encoder_.reset();
        return false;
    }
    return true;
}

bool EncodeSubsystem::reinit(ID3D11Device* device, const Config& cfg) {
    // Saklı packet_cb_ ile yeniden kur — callback değişmez (eski TDR reinit bloğu).
    encoder_ = std::make_unique<reji::NvencEncoder>();
    if (!encoder_->init(device, cfg, packet_cb_)) {
        encoder_.reset();
        return false;
    }
    return true;
}

bool EncodeSubsystem::encode_frame(ID3D11Texture2D* tex, int64_t pts_us) {
    // encoder_ yoksa: drop değil (preview-only mod) — eski `s.encoder && ...`
    // koşuluyla aynı gözlemlenebilir davranış (drop tetiklenmez).
    if (!encoder_) return true;
    // request_idr() bekleyen talebi bu karede tüket (Faz2/Aşama2.2).
    const bool force_idr = force_idr_.exchange(false, std::memory_order_acq_rel);
    return encoder_->encode_frame(tex, pts_us, force_idr);
}

bool EncodeSubsystem::set_bitrate(uint32_t kbps) {
    return encoder_ ? encoder_->set_bitrate(kbps) : false;
}

bool EncodeSubsystem::set_resolution(float scale) {
    return encoder_ ? encoder_->set_resolution(scale) : false;
}

bool EncodeSubsystem::set_fps_limit(uint32_t fps) {
    return encoder_ ? encoder_->set_fps_limit(fps) : false;
}

void EncodeSubsystem::flush() {
    if (encoder_) encoder_->flush();
}

void EncodeSubsystem::shutdown() {
    // RAII reset — native NVENC teardown (~NvencEncoder → shutdown → flush+destroy)
    // burada tetiklenir; SEH-leaf'te önceden shutdown edildiyse yıkıcı erken döner.
    encoder_.reset();
}

} // namespace rj
