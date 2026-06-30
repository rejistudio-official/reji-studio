// Force discrete GPU on Optimus / hybrid graphics systems
#include <windows.h>
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 1; }
extern "C" { __declspec(dllexport) DWORD AmdPowerXpressRequestHighPerformance = 1; }

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "../pipeline/include/pipeline.h"
#include "../pipeline/gpu/vulkan_initializer.h"

// ---------------------------------------------------------------------------
// Headless mode — CI Vulkan VUID taramasi, UI acilmadan pipeline calistirir.
// Usage: reji_app --headless [--frames N]   (varsayilan N=10)
// Stderr -> vk_log.txt; CI: findstr "VUID" && exit 1 || exit 0
// ---------------------------------------------------------------------------
static int run_headless(int frames) {
    fprintf(stderr, "[headless] %d-frame Vulkan validation run\n", frames);
    fflush(stderr);

    auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
    if (!vk->initialize()) {
        fprintf(stderr, "[headless] Vulkan unavailable -- no VUID check possible, exiting 0\n");
        fflush(stderr);
        return 0;
    }

    auto pipeline = std::make_shared<rj::Pipeline>();
    rj::Pipeline::Config cfg;
    cfg.audio_enabled = false;

    if (!pipeline->init(cfg)) {
        fprintf(stderr, "[headless] Pipeline::init failed, exiting 0\n");
        fflush(stderr);
        return 0;
    }

    pipeline->notify_vulkan_ready(vk->device(), vk->physical_device());

    for (int i = 0; i < frames; ++i) {
        pipeline->run_frame();
    }

    // H20: Shutdown order matters — pipeline MUST be torn down before
    // VulkanInitializer so ExternalMemoryBridge can call vkDestroyImage
    // while the Zig-owned VkDevice is still alive.
    pipeline->shutdown();
    // Explicit shutdown before the function-local static destructor fires
    // at program exit, enforcing: pipeline → VulkanInitializer.
    vk->shutdown();
    fprintf(stderr, "[headless] Done (%d frames)\n", frames);
    fflush(stderr);
    return 0;
}

#ifdef QT6_AVAILABLE
#include "main_window.h"
#include <QApplication>
#include <QSurfaceFormat>
#endif

int main(int argc, char* argv[]) {
    freopen("C:\\reji-studio\\run.log", "w", stderr);
    // --headless / --frames argümanlarını Qt başlamadan önce ayrıştır
    bool headless = false;
    int  frames   = 10;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            frames = atoi(argv[++i]);
            if (frames <= 0) frames = 10;
        }
    }

    if (headless) {
        return run_headless(frames);
    }

#ifdef QT6_AVAILABLE
    QApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setApplicationName("Reji Studio");
    QApplication::setOrganizationName("RejiStudio");
    QApplication::setApplicationVersion("0.1.0");

    MainWindow w;
    w.show();

    return app.exec();
#else
    std::fputs("Reji Studio: Qt6 bulunamadi, UI devre disi.\n", stderr);
    return 1;
#endif
}
