// REJI_VULKAN_MOCK stub'ları — YALNIZ mock derlemesinde kaynak listesine girer
// (src/pipeline/CMakeLists.txt). Mock, gerçek GPU TU'larını (vulkan_initializer,
// external_memory_bridge, gpu_interop_subsystem) dışlar; ama pipeline.cpp ve
// reji_app (run_headless) bu sınıfların sembollerine KOŞULSUZ başvurur — Aşama 7 /
// v0.5.1'de mock tasarımından sonra eklendiler ve exe link'i mock'ta kırılıyordu
// (TALIMAT_CI_CPP_BUILD_ONARIMI Parça 3'te bulunan ek bit-rot katmanı).
//
// Stub semantiği: init/initialize false döner, getter'lar false/no-op — çağıranlar
// "Vulkan hazır değil" yolunu zaten ele alır (gerçek init de çalışma zamanında
// başarısız olabilir), yani mock'ta GPU yolu temiz biçimde kapalı kalır.

#include "../include/gpu_interop_subsystem.h"
#include "vulkan_initializer.h"

namespace rj::pipeline::gpu {

VulkanInitializer* VulkanInitializer::get() {
    static VulkanInitializer instance;
    return &instance;
}

VulkanInitializer::~VulkanInitializer() = default;

bool VulkanInitializer::initialize() { return false; }  // mock: GPU yok

void VulkanInitializer::shutdown() {}

// unique_ptr<ExternalMemoryBridge> dtor'u (GpuInteropSubsystem üyesi) bunu ister;
// ctor stub'lanmaz — mock'ta init() false döndüğünden bridge hiç kurulmaz.
ExternalMemoryBridge::~ExternalMemoryBridge() = default;

}  // namespace rj::pipeline::gpu

namespace rj {

bool GpuInteropSubsystem::init(VkDevice, VkPhysicalDevice, uint32_t, uint32_t) {
    return false;  // mock: bridge kurulmaz, zero-copy yolu kapalı
}

void GpuInteropSubsystem::notify_vulkan_ready(VkDevice, VkPhysicalDevice,
                                              uint32_t, uint32_t) {}

void GpuInteropSubsystem::set_device(VkDevice, VkPhysicalDevice) {}

bool GpuInteropSubsystem::get_frame_images(ID3D11Texture2D*, VkImage*, VkImage*,
                                           uint32_t*) {
    return false;
}

void GpuInteropSubsystem::cache_last_images(VkImage, VkImage, uint32_t) {}

bool GpuInteropSubsystem::get_last_frame_images(VkImage*, VkImage*, uint32_t*) {
    return false;
}

void GpuInteropSubsystem::shutdown() {}

}  // namespace rj
