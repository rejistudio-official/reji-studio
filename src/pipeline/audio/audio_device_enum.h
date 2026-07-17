// src/pipeline/audio/audio_device_enum.h
//
// WASAPI ses cihazi enumerasyonu — Ses Ayarlari UI'daki cihaz dropdown'i besler.
// Capture'dan (wasapi_capture) bagimsiz saf enumerasyon; running bir engine
// gerektirmez, bu yuzden UI thread'inden dogrudan cagrilabilir. Donen cihaz
// id'si (IMMDevice::GetId) WasapiCapture::Config.device_id'ye verilerek belirli
// cihaz secilir (bos = sistem varsayilani, eski davranis).
#pragma once
#include <string>
#include <vector>

namespace reji::pipeline::audio {

struct AudioDeviceInfo {
    std::wstring id;    // IMMDevice::GetId — GetDevice(id) ile acilir
    std::wstring name;  // PKEY_Device_FriendlyName (kullaniciya gosterilir)
};

// Aktif (DEVICE_STATE_ACTIVE) ses endpoint'lerini listeler.
//   loopback=true  -> eRender  (sistem sesi / hoparlor — loopback capture)
//   loopback=false -> eCapture (mikrofon)
// COM'u kendi icinde yonetir (init/uninit dengeli). Cihaz yoksa bos liste doner
// (hata degil — donanimsiz ortam gecerli).
std::vector<AudioDeviceInfo> enumerate_audio_devices(bool loopback);

} // namespace reji::pipeline::audio
