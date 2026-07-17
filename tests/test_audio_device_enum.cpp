// audio_device_enum entegrasyon testi — WASAPI cihaz enumerasyonu (IMMDevice).
// Donanim gerektirmez (bos liste de gecerli sonuc); headless CI'da cagri
// cokmemeli ve donen her giris iyi-bicimli (id + name dolu) olmali.
// audio_device_enum.cpp dogrudan derlenir (test_seh_filter deseni); ole32 link.
#include <gtest/gtest.h>
#include "audio_device_enum.h"

using reji::pipeline::audio::enumerate_audio_devices;

// Render (loopback) cihazlarini enumerate etmek cokmeden bir liste dondurur;
// her giris bos olmayan id + name tasir.
TEST(AudioDeviceEnumTest, LoopbackEnumerationWellFormed) {
    auto devices = enumerate_audio_devices(/*loopback*/true);
    for (const auto& d : devices) {
        EXPECT_FALSE(d.id.empty())   << "cihaz id bos olmamali";
        EXPECT_FALSE(d.name.empty()) << "cihaz adi bos olmamali";
    }
    SUCCEED();  // bos liste de gecerli (donanimsiz CI)
}

// Capture (mikrofon) enumerasyonu da cokmeden calisir.
TEST(AudioDeviceEnumTest, CaptureEnumerationDoesNotCrash) {
    auto devices = enumerate_audio_devices(/*loopback*/false);
    for (const auto& d : devices) {
        EXPECT_FALSE(d.id.empty());
        EXPECT_FALSE(d.name.empty());
    }
    SUCCEED();
}
