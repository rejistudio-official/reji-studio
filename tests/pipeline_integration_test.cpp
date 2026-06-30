#include <gtest/gtest.h>

// REJI_VULKAN_MOCK: Vulkan türlerini void* olarak tanımlar; headless ortamda
// gerçek Vulkan kütüphanesi gerekmez.
#ifndef REJI_VULKAN_MOCK
#define REJI_VULKAN_MOCK 1
#endif

#include "pipeline.h"

// InitShutdownCycle: init → shutdown çifti crash'e yol açmamalı.
// Headless / donanımsız ortamda init() false dönebilir — bu beklenen davranış.
TEST(PipelineIntegration, InitShutdownCycle) {
    auto pipeline = std::make_shared<rj::Pipeline>();
    rj::Pipeline::Config cfg;
    bool ok = pipeline->init(cfg);
    // Platform bağımlı: headless'da false, gerçek donanımda true.
    (void)ok;
    EXPECT_TRUE(pipeline->shutdown());
}

// DoubleShutdownIsSafe: std::call_once koruması sayesinde ikinci çağrı da
// true dönmeli, hiçbir subsystem çift kez kapatılmamalı.
TEST(PipelineIntegration, DoubleShutdownIsSafe) {
    auto pipeline = std::make_shared<rj::Pipeline>();
    rj::Pipeline::Config cfg;
    pipeline->init(cfg);
    EXPECT_TRUE(pipeline->shutdown());
    EXPECT_TRUE(pipeline->shutdown());
}

// RunFrameBeforeInitFails: impl_ henüz oluşturulmamış → run_frame false dönmeli,
// crash veya UB olmamalı.
TEST(PipelineIntegration, RunFrameBeforeInitFails) {
    auto pipeline = std::make_shared<rj::Pipeline>();
    EXPECT_FALSE(pipeline->run_frame());
}

// IsRunningBeforeInit: init ve start_stream çağrılmadan is_running false olmalı.
TEST(PipelineIntegration, IsRunningBeforeInit) {
    auto pipeline = std::make_shared<rj::Pipeline>();
    EXPECT_FALSE(pipeline->is_running());
}

// StopStreamBeforeInit: stop_stream init edilmemiş pipeline'da güvenli olmalı.
TEST(PipelineIntegration, StopStreamBeforeInit) {
    auto pipeline = std::make_shared<rj::Pipeline>();
    EXPECT_FALSE(pipeline->is_running());
    // stop_stream() → false (impl_ null), crash yok
    (void)pipeline->stop_stream();
}
