#include "external_memory_bridge.h"
#include "slot_ring.h"  // I23: next_pool_slot (tek round-robin kaynağı)

namespace rj::pipeline::gpu {

ExternalMemoryBridge::ExternalMemoryBridge(
    VkDevice device, VkPhysicalDevice phys) {
    ext_bridge_init(device, phys);
}

ExternalMemoryBridge::~ExternalMemoryBridge() {
    shutdown();
}

void ExternalMemoryBridge::set_device(
    VkDevice device, VkPhysicalDevice phys) {
    ext_bridge_set_device(device, phys);
}

HANDLE ExternalMemoryBridge::export_d3d11_handle(ID3D11Texture2D*) {
    return nullptr;
}

VkImage ExternalMemoryBridge::create_vulkan_image_from_d3d11(
    HANDLE, VkFormat, uint32_t, uint32_t, uint32_t) {
    return nullptr;
}

bool ExternalMemoryBridge::initialize_image_pool(
    VkFormat, uint32_t, uint32_t) {
    return true;
}

VkImage ExternalMemoryBridge::get_pooled_image(uint32_t frame_idx) {
    return ext_bridge_get_pooled_image(frame_idx);
}

bool ExternalMemoryBridge::get_frame_images(
    ID3D11Texture2D* tex,
    VkImage* staging, VkImage* target, uint32_t* out_slot) {
    // J4: round-robin slot artık fonksiyon-yerel static değil, per-instance
    // pool_index_ member'ı. I23 static'i bilinçli korumuştu (kapsamı şişirmemek);
    // üç bağımsız inceleme aynı noktayı işaret edince yeniden değerlendirildi.
    // Tek instance/tek çağıran thread olduğundan davranış birebir aynı (0'dan
    // başlar, aynı next_pool_slot ilerlemesi), ama process-global paylaşım
    // kırılganlığı ortadan kalkar (ikinci bir bridge örneği kendi sayacını tutar).
    // K1: try_lock — tüketici (GL thread) pool'u rebuild ediyorsa BEKLEME, bu kareyi atla.
    // get_frame_images encode/frame-thread'de; blocking lock vkDeviceWaitIdle süresince
    // encode döngüsünü durdururdu (AGENTS.md hot-path no-block + K2 "gerekirse kareyi düşür").
    // Skip yalnız preview-interop'u etkiler; encode yolu ayrıdır. Çağıran false'ta cache'lemez.
    std::unique_lock<std::mutex> lk(gl_pool_mtx_, std::try_to_lock);
    if (!lk.owns_lock()) {
        return false;
    }

    bool ok = ext_bridge_get_frame_images(tex, pool_index_, staging, target);
    if (ok) {
        // I23: bu frame'i üreten slot'u raporla — çağıran (cache→widget→execute_copy)
        // aynı index'i kullanarak bridge image kimliğiyle hizalanır.
        if (out_slot) *out_slot = pool_index_;
        pool_index_ = next_pool_slot(pool_index_);
    }
    return ok;
}

bool ExternalMemoryBridge::initialize_gl_target_pool(
    VkFormat fmt, uint32_t w, uint32_t h) {
    return ext_bridge_init_gl_target_pool(fmt, w, h);
}

bool ExternalMemoryBridge::resize_gl_target_pool(uint32_t w, uint32_t h) {
    // K1: TEK yazar. gl_pool_mtx_ altında (RAII/lock_guard, I9/I10 deseni) — üreticinin
    // (get_frame_images, try_lock) yarı-kurulmuş pool okumasını engeller. İçeride
    // vkDeviceWaitIdle var; tüketici (GL thread, preview-only) burada bloklanabilir.
    // format Zig state'inde saklanır — resize yalnız yeni boyutu alır.
    std::lock_guard<std::mutex> lk(gl_pool_mtx_);
    return ext_bridge_resize_gl_target_pool(w, h);
}

VkImage ExternalMemoryBridge::get_execute_target(uint32_t slot) const {
    // K1: tüketici-thread (GL) okuru; TEK yazar resize_gl_target_pool AYNI thread'de sıralı
    // olduğundan kilit gerekmez. Ofset ((slot+1)%POOL_SIZE) Zig'de tek kaynak.
    return ext_bridge_get_execute_target(slot);
}

HANDLE ExternalMemoryBridge::get_gl_target_handle(uint32_t idx) const {
    return static_cast<HANDLE>(ext_bridge_get_gl_target_handle(idx));
}

VkDeviceSize ExternalMemoryBridge::gl_target_size(uint32_t slot) const {
    return ext_bridge_gl_target_size(slot);
}

bool ExternalMemoryBridge::create_gl_sync_semaphore() {
    return ext_bridge_create_gl_sync_semaphores();
}

HANDLE ExternalMemoryBridge::get_gl_sync_semaphore_handle(
    uint32_t slot) const {
    return static_cast<HANDLE>(ext_bridge_get_gl_sync_handle(slot));
}

VkSemaphore ExternalMemoryBridge::get_gl_sync_semaphore(
    uint32_t slot) const {
    return ext_bridge_get_staging_semaphore(slot);
}

VkDeviceMemory ExternalMemoryBridge::get_staging_memory_for_image(
    VkImage img) const {
    return ext_bridge_get_staging_memory(img);
}

VkDeviceMemory ExternalMemoryBridge::get_shared_texture_memory() const {
    // All pool slots share the same imported D3D11 texture memory; slot 0 is canonical.
    return ext_bridge_get_staging_memory(ext_bridge_get_pooled_image(0));
}

void ExternalMemoryBridge::shutdown() {
    ext_bridge_shutdown();
}

}
