// src/pipeline/include/gpu_interop_subsystem.h
//
// GpuInteropSubsystem — D3D11↔Vulkan zero-copy interop alt sistemi (Aşama 7'de
// Pipeline::Impl'den çıkarıldı). ExternalMemoryBridge yaşam döngüsünü ve son
// frame VkImage cache'ini (get_last_frame_images getter'ı için) sarmalar.
//
// SIKI DÜĞÜM NOTU: keyed-mutex yeniden değerlendirme (capture_dxgi_->
// set_use_keyed_mutex) capture alt sistemine dokunur — orkestratörde (Impl) kalır,
// bu sınıfa taşınmaz. Aynı şekilde d3d11_frame_cb çağrısının kendisi Impl'de kalır;
// burası yalnızca get_frame_images + cache mantığını sağlar.
//
// THREAD-SAFETY: cache_last_images() frame thread'inden yazar; get_last_frame_images()
// GL thread'inden okur → last_*_vk_ atomic (acquire/release görünürlük).
//
// B10 NOTU: ext_bridge_ ham VkDevice/VkImage handle'ları tutar; VulkanInitializer
// singleton'ı device'ı serbest bırakmadan ÖNCE shutdown()'lanmalı (use-after-free
// önlemi). Bu sıra orkestratör (Pipeline::shutdown) tarafından korunur.
//
// Windows/Vulkan'a özel: external_memory_bridge.h gerçek Vulkan modunda <windows.h>
// ve <vulkan/vulkan.h> çeker; bu başlık yalnızca _WIN32 altında include edilmelidir.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include "../gpu/external_memory_bridge.h"   // ExternalMemoryBridge + Vk* tipleri

namespace rj {

class GpuInteropSubsystem {
public:
    // ExternalMemoryBridge oluşturur + image pool init eder (DXGI zero-copy).
    // device/phys VulkanInitializer'dan orkestratörce çözülüp geçilir; width/height
    // de aynı şekilde. Pool init başarısız olsa bile ext_bridge_ tutulur (eski
    // davranış: yalnızca log); dönüş = pool init sonucu.
    bool init(VkDevice device, VkPhysicalDevice phys, uint32_t width, uint32_t height);

    // Late Vulkan device binding: set_device + GL target pool + sync semaphore.
    // width/height orkestratörden (s.width/height.load()) parametre olarak gelir.
    // ext_bridge_ yoksa no-op.
    void notify_vulkan_ready(VkDevice device, VkPhysicalDevice phys,
                             uint32_t width, uint32_t height);

    // set_d3d11_frame_callback late-bind yolu: yalnızca ext_bridge_ varsa set_device.
    void set_device(VkDevice device, VkPhysicalDevice phys);

    // run_frame D3D11 zero-copy: shared_tex için staging/target VkImage al.
    // I23: out_slot != null ise bu image'leri üreten pool slot'u yazılır.
    // ext_bridge_ yoksa false (out_* dokunulmaz).
    bool get_frame_images(ID3D11Texture2D* shared_tex,
                          VkImage* out_staging, VkImage* out_target,
                          uint32_t* out_slot = nullptr);

    // get_last_frame_images getter'ı için cache (frame thread yazar).
    // I23: slot da cache'lenir → getter aynı slot'u GL thread'e taşır.
    void cache_last_images(VkImage staging, VkImage target, uint32_t slot);

    // GL thread okur (Pipeline::get_last_frame_images). Dönüş: her iki image de non-null.
    // I23: out_slot != null ise cache'lenen pool slot'u yazılır.
    bool get_last_frame_images(VkImage* out_staging, VkImage* out_target,
                               uint32_t* out_slot = nullptr);

    // Pipeline::get_external_memory_bridge + orkestratör guard'ları (keyed-mutex,
    // run_frame zero-copy) için ham pointer. Null = bridge yok.
    rj::pipeline::gpu::ExternalMemoryBridge* raw() const noexcept { return ext_bridge_.get(); }

    // B10: ext_bridge shutdown + reset. VulkanInitializer device release'inden ÖNCE
    // çağrılmalı — sıra orkestratör tarafından korunur.
    void shutdown();

private:
    std::unique_ptr<rj::pipeline::gpu::ExternalMemoryBridge> ext_bridge_;
    std::atomic<VkImage> last_staging_vk_{nullptr};
    std::atomic<VkImage> last_target_vk_{nullptr};
    std::atomic<uint32_t> last_slot_{0};  // I23: cache'lenen frame'in pool slot'u
};

} // namespace rj
