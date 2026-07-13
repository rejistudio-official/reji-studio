// src/pipeline/gpu_interop_subsystem.cpp
//
// GpuInteropSubsystem implementasyonu. Gerçek Vulkan gerektirir (external_memory_
// bridge.cpp gibi yalnızca NOT REJI_VULKAN_MOCK altında derlenir). Davranış,
// Pipeline'ın eski ext_bridge / last_*_vk koduyla (init / notify_vulkan_ready /
// run_frame zero-copy / get_last_frame_images / shutdown) birebir aynıdır
// (Aşama 7 saf çıkarma — baseline_metrics.txt ile doğrulanır).
#include "gpu_interop_subsystem.h"

#include <cstdio>

namespace rj {

bool GpuInteropSubsystem::init(VkDevice device, VkPhysicalDevice phys,
                               uint32_t width, uint32_t height) {
    ext_bridge_ = std::make_unique<rj::pipeline::gpu::ExternalMemoryBridge>(device, phys);
    // Pool init başarısız olsa bile bridge tutulur (eski davranış: yalnızca log).
    // Dönüş orkestratöre bırakılır — dbglog pipeline.cpp anonim namespace'inde.
    return ext_bridge_->initialize_image_pool(VK_FORMAT_B8G8R8A8_UNORM, width, height);
}

void GpuInteropSubsystem::notify_vulkan_ready(VkDevice device, VkPhysicalDevice phys,
                                              uint32_t width, uint32_t height) {
    if (!ext_bridge_) return;
    ext_bridge_->set_device(device, phys);
    fprintf(stderr, "[Pipeline] notify_vulkan_ready: device=%p phys=%p\n",
            (void*)device, (void*)phys);
    fflush(stderr);

    ext_bridge_->initialize_gl_target_pool(VK_FORMAT_B8G8R8A8_UNORM, width, height);
    if (!ext_bridge_->create_gl_sync_semaphore()) {
        fprintf(stderr, "[Pipeline] GL sync semaphore oluşturulamadı\n");
        fflush(stderr);
    }
}

void GpuInteropSubsystem::set_device(VkDevice device, VkPhysicalDevice phys) {
    if (ext_bridge_) ext_bridge_->set_device(device, phys);
}

bool GpuInteropSubsystem::get_frame_images(ID3D11Texture2D* shared_tex,
                                           VkImage* out_staging, VkImage* out_target,
                                           uint32_t* out_slot) {
    if (!ext_bridge_) return false;
    bool ok = ext_bridge_->get_frame_images(shared_tex, out_staging, out_target, out_slot);
#ifdef RJ_DEBUG_VERBOSE
    fprintf(stderr, "[Pipeline] get_frame_images: staging=%p target=%p\n",
            *out_staging, *out_target);
    fflush(stderr);
#endif
    return ok;
}

void GpuInteropSubsystem::cache_last_images(VkImage staging, VkImage target, uint32_t slot) {
    // I23: slot'u image'lerden ÖNCE yaz — get_last_frame_images image non-null
    // gördüğünde slot da görünür olsun (release zinciri last_target_vk_ store'unda kapanır).
    last_slot_.store(slot, std::memory_order_relaxed);
    last_staging_vk_.store(staging, std::memory_order_release);
    last_target_vk_.store(target, std::memory_order_release);
}

bool GpuInteropSubsystem::get_last_frame_images(VkImage* out_staging, VkImage* out_target,
                                                uint32_t* out_slot) {
    *out_staging = last_staging_vk_.load(std::memory_order_acquire);
    *out_target  = last_target_vk_.load(std::memory_order_acquire);
    if (out_slot) *out_slot = last_slot_.load(std::memory_order_relaxed);
    return *out_staging != nullptr && *out_target != nullptr;
}

void GpuInteropSubsystem::shutdown() {
    // B10: bridge, VulkanInitializer device release'inden önce yıkılır (use-after-free
    // önlemi). Çağrı sırası orkestratörde korunur.
    if (ext_bridge_) {
        ext_bridge_->shutdown();
        ext_bridge_.reset();
    }
    // J13: ext_bridge_->shutdown() cache'lenen VkImage'ları (image_pool /
    // gl_target_images) yok eder; last_*_vk_ aksi halde dangling handle tutar.
    // get_last_frame_images() bunları raw()/bridge-alive guard'ı OLMADAN okur
    // (get_frame_images yazma yolunun aksine) → savunma-derinliği için burada
    // sıfırla. Normal teardown'da tek okuyucu (frame thread) zaten join edilmiş
    // olur, ama header'ın belgelediği GL-thread okuyucusu bağlanırsa bu boşluk
    // aktifleşir; şimdi kapatıyoruz.
    last_staging_vk_.store(nullptr, std::memory_order_release);
    last_target_vk_.store(nullptr, std::memory_order_release);
    last_slot_.store(0, std::memory_order_relaxed);
}

} // namespace rj
