// src/pipeline/include/encode_subsystem.h
//
// EncodeSubsystem — NVENC video encode alt sistemi (Aşama 6'da Pipeline::Impl'den
// çıkarıldı). NvencEncoder yaşam döngüsünü (init/encode/reconfig/flush/teardown)
// sarmalar ve packet callback'i saklar — TDR recovery reinit'inde aynı callback
// tekrar geçilir.
//
// Windows'a özel: encode_nvenc.h RJ_PLATFORM_WINDOWS altında <d3d11.h> çeker; bu
// başlık yalnızca _WIN32 altında include edilmelidir (pipeline.cpp'nin _WIN32
// bloğunda), audio_subsystem.h ile aynı desende.
//
// SIKI DÜĞÜM NOTU: on_packet (hem OutputSubsystem::send hem Metrics'e ait
// frame_drops'a dokunur) orkestratörde (Impl) kalır. EncodeSubsystem on_packet'i
// bilmez — yalnızca PacketCallback tipini kabul eden init() sunar; callback'in
// içeriği Impl'de tanımlanıp geçilir.
//
// SEH NOTU: Cihaz teardown'ı (flush + shutdown) Pipeline::shutdown() içinde
// seh_shutdown_subsystems() SEH-leaf'ine raw() pointer'ı geçilerek yapılır.
// EncodeSubsystem::shutdown() yalnızca RAII reset'i (unique_ptr yıkımı) yapar ve
// __try bloğunun DIŞINDA çağrılmalıdır — proje kuralı gereği.
#pragma once
#include <cstdint>
#include <memory>
#include <d3d11.h>
#include "encode_nvenc.h"   // -I .../encode

namespace rj {

class EncodeSubsystem {
public:
    using Config         = reji::NvencEncoder::Config;
    using Packet         = reji::NvencEncoder::Packet;
    using PacketCallback = reji::NvencEncoder::PacketCallback;

    // NvencEncoder oluşturur + init eder; cb, reinit() için packet_cb_'de saklanır.
    // Başarısızlıkta encoder_ reset, false döner.
    bool init(ID3D11Device* device, const Config& cfg, PacketCallback cb);

    // TDR recovery: saklı packet_cb_ ile encoder'ı yeniden kurar (callback değişmez).
    // Config Impl tarafından (width/height/fps/bitrate atomik'lerinden) inşa edilir.
    bool reinit(ID3D11Device* device, const Config& cfg);

    // Frame thread'inden çağrılır. encoder_ yoksa true döner (preview-only, drop
    // sayılmaz); aksi halde NvencEncoder::encode_frame sonucunu döner.
    bool encode_frame(ID3D11Texture2D* tex, int64_t pts_us);

    // Runtime reconfig (apply_frame_cmd'den). encoder_ yoksa false.
    bool set_bitrate(uint32_t kbps);
    bool set_resolution(float scale);
    bool set_fps_limit(uint32_t fps);

    // Kalan buffer'ları boşalt (stream sonu). encoder_ yoksa no-op.
    void flush();

    // seh_shutdown_subsystems() ile uyum: SEH-leaf'e geçilecek raw pointer.
    reji::NvencEncoder* raw() const noexcept { return encoder_.get(); }

    // RAII teardown — SEH-leaf (flush/shutdown) DIŞINDA çağrılmalı.
    void shutdown();

private:
    std::unique_ptr<reji::NvencEncoder> encoder_;
    PacketCallback                      packet_cb_;
};

} // namespace rj
