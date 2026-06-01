#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#include <cstdio>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "ole32.lib")

using Microsoft::WRL::ComPtr;

int main() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    printf("COM: APARTMENTTHREADED\n");

    // Factory
    ComPtr<IDXGIFactory1> factory;
    CreateDXGIFactory1(IID_PPV_ARGS(&factory));

    // İlk adapter
    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapters1(0, &adapter);
    DXGI_ADAPTER_DESC1 desc;
    adapter->GetDesc1(&desc);
    wprintf(L"Adapter: %s\n", desc.Description);

    // D3D11 device
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> ctx;
    D3D_FEATURE_LEVEL fl;
    D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN,
        nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &device, &fl, &ctx);
    printf("Feature level: 0x%X\n", fl);

    // Output
    ComPtr<IDXGIOutput> output;
    adapter->EnumOutputs(0, &output);
    ComPtr<IDXGIOutput1> output1;
    output.As(&output1);

    // Duplication
    ComPtr<IDXGIOutputDuplication> dupl;
    HRESULT hr = output1->DuplicateOutput(device.Get(), &dupl);
    printf("DuplicateOutput: 0x%08lX\n", hr);
    if (FAILED(hr)) { return 1; }

    // Frame al
    printf("Waiting for frames (10 attempts)...\n");
    for (int i = 0; i < 10; i++) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> res;
        hr = dupl->AcquireNextFrame(500, &info, &res);
        printf("Frame %d: hr=0x%08lX AccumFrames=%u\n",
               i, hr, info.AccumulatedFrames);
        if (SUCCEEDED(hr)) {
            dupl->ReleaseFrame();
            printf("SUCCESS -- frame captured!\n");
            break;
        }
    }
    return 0;
}
