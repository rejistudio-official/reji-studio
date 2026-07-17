// src/pipeline/audio/audio_device_enum.cpp
#include "audio_device_enum.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmdeviceapi.h>
#include <Functiondiscoverykeys_devpkey.h>  // PKEY_Device_FriendlyName
#include <propidl.h>                          // PROPVARIANT, PropVariantClear
#include <wrl/client.h>

#pragma comment(lib, "ole32.lib")

namespace reji::pipeline::audio {

using Microsoft::WRL::ComPtr;

std::vector<AudioDeviceInfo> enumerate_audio_devices(bool loopback) {
    std::vector<AudioDeviceInfo> out;

    // COM init: MTA dene. RPC_E_CHANGED_MODE -> thread zaten (ör. Qt STA) init
    // etmis, COM kullanilabilir ama biz uninit ETMEYIZ (dengeyi bozmayalim).
    const HRESULT co = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool need_uninit = (co == S_OK || co == S_FALSE);
    if (co != S_OK && co != S_FALSE && co != RPC_E_CHANGED_MODE) {
        return out;  // COM gercekten kullanilamaz
    }

    {
        ComPtr<IMMDeviceEnumerator> enumr;
        if (SUCCEEDED(::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                         CLSCTX_ALL, IID_PPV_ARGS(&enumr))) && enumr) {
            const EDataFlow flow = loopback ? eRender : eCapture;
            ComPtr<IMMDeviceCollection> coll;
            if (SUCCEEDED(enumr->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &coll)) && coll) {
                UINT count = 0;
                coll->GetCount(&count);
                for (UINT i = 0; i < count; ++i) {
                    ComPtr<IMMDevice> dev;
                    if (FAILED(coll->Item(i, &dev)) || !dev) continue;

                    AudioDeviceInfo info;
                    LPWSTR id = nullptr;
                    if (SUCCEEDED(dev->GetId(&id)) && id) {
                        info.id = id;
                        ::CoTaskMemFree(id);
                    }
                    if (info.id.empty()) continue;  // id'siz cihaz secilemez

                    ComPtr<IPropertyStore> props;
                    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &props)) && props) {
                        PROPVARIANT pv;
                        PropVariantInit(&pv);
                        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &pv)) &&
                            pv.vt == VT_LPWSTR && pv.pwszVal) {
                            info.name = pv.pwszVal;
                        }
                        PropVariantClear(&pv);
                    }
                    if (info.name.empty()) info.name = L"(bilinmeyen cihaz)";
                    out.push_back(std::move(info));
                }
            }
        }
    }  // ComPtr'lar CoUninitialize'dan ONCE serbest

    if (need_uninit) ::CoUninitialize();
    return out;
}

} // namespace reji::pipeline::audio
