#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include "copy_optimizer.h"
#include "gpu/vulkan_initializer.h"
#include "seh_filter.h"  // V8/I10: paylaşımlı SEH filtresi
#include <cstdio>

#define CHECK_VK(expr) \
    do { \
        VkResult res = (expr); \
        if (res != VK_SUCCESS) { \
            fprintf(stderr, "[GpuCopyOptimizer] VK error: 0x%x at %s:%d\n", res, __FILE__, __LINE__); \
            return false; \
        } \
    } while(0)

bool GpuCopyOptimizer::init(VkDevice device, VkQueue queue, VkPhysicalDevice phys_device,
                            uint32_t queue_family_index,
                            const Config& config) {
    if (!device || !queue || !phys_device) {
        fprintf(stderr, "[GpuCopyOptimizer] Invalid device/queue/phys_device\n");
        return false;
    }

    for (auto& layout : target_layouts_)  layout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (auto& layout : staging_layouts_) layout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (auto& sig : slot_gl_signaled_)   sig = false;

    // V8/I6: re-arm lifecycle guard (supports re-init after a prior shutdown())
    alive_.store(true, std::memory_order_release);

    try {  // C++ exceptions only; Vulkan returns error codes
        device_ = device;
        queue_ = queue;
        phys_device_ = phys_device;
        graphics_queue_family_ = queue_family_index;  // E4

        // Create command pool
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_index;
        if (vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_) != VK_SUCCESS) {
            fprintf(stderr, "[GpuCopyOptimizer] vkCreateCommandPool failed\n");
            fflush(stderr);
            shutdown(); return false;
        }

        // Allocate per-slot command buffer pool (3 slots, round-robin)
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 3;
        if (vkAllocateCommandBuffers(device_, &alloc_info, command_buffers_) != VK_SUCCESS) {
            fprintf(stderr, "[GpuCopyOptimizer] vkAllocateCommandBuffers failed\n");
            fflush(stderr);
            shutdown(); return false;
        }

        // Query blit capability for VK_FORMAT_B8G8R8A8_UNORM
        VkFormatProperties format_props{};
        vkGetPhysicalDeviceFormatProperties(phys_device_, VK_FORMAT_B8G8R8A8_UNORM, &format_props);

        const bool blit_src_supported = (format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
        const bool blit_dst_supported = (format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
        const bool linear_filter_supported = (format_props.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

        use_blit_ = blit_src_supported && blit_dst_supported;

        fprintf(stderr, "[GpuCopyOptimizer] Blit capability: src=%d dst=%d linear=%d -> use_blit=%d\n",
            blit_src_supported, blit_dst_supported, linear_filter_supported, use_blit_);

        // Create timeline semaphore (non-blocking sync)
        VkSemaphoreTypeCreateInfoKHR timeline_info = {};
        timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = 0;

        VkSemaphoreCreateInfo sem_info = {};
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sem_info.pNext = &timeline_info;
        if (vkCreateSemaphore(device_, &sem_info, nullptr, &timeline_semaphore_) != VK_SUCCESS) {
            fprintf(stderr, "[GpuCopyOptimizer] vkCreateSemaphore (timeline) failed\n");
            fflush(stderr);
            shutdown(); return false;
        }
        fprintf(stderr, "[GpuCopyOptimizer] vkCreateSemaphore: timeline_semaphore_=%p\n",
                (void*)timeline_semaphore_);

        // Resolve extension function pointer (must happen after device creation).
        // vkGetSemaphoreCounterValueKHR is NOT part of the Vulkan 1.0 core API
        // (it's from VK_KHR_timeline_semaphore); direct linking will fail at
        // load time on some drivers/builds.
        pfn_get_semaphore_counter_value_ =
            reinterpret_cast<PFN_vkGetSemaphoreCounterValueKHR>(
                vkGetDeviceProcAddr(device_, "vkGetSemaphoreCounterValueKHR"));
        if (!pfn_get_semaphore_counter_value_) {
            fprintf(stderr,
                    "[GpuCopyOptimizer] vkGetDeviceProcAddr failed for "
                    "vkGetSemaphoreCounterValueKHR (VK_KHR_timeline_semaphore missing?)\n");
            fflush(stderr);
            shutdown(); return false;
        }

        pfn_wait_semaphores_ =
            reinterpret_cast<PFN_vkWaitSemaphores>(
                vkGetDeviceProcAddr(device_, "vkWaitSemaphores"));
        if (!pfn_wait_semaphores_) {
            fprintf(stderr, "[GpuCopyOptimizer] vkGetDeviceProcAddr failed for vkWaitSemaphores\n");
            fflush(stderr);
            shutdown(); return false;
        }

        fprintf(stderr, "[GpuCopyOptimizer] Initialized (command pool, buffer, timeline semaphore)\n");
        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during init\n");
        fflush(stderr);
        shutdown(); return false;
    }
}

bool GpuCopyOptimizer::execute_copy(VkImage d3d11_staging_vk,
                                     VkImage vulkan_target,
                                     uint32_t width,
                                     uint32_t height,
                                     uint32_t slot,
                                     VkSemaphore* out_timeline_semaphore,
                                     uint64_t* out_timeline_value,
                                     VkImage* out_target_image,
                                     VkSemaphore gl_sync_sem,
                                     VkDeviceMemory d3d11_staging_memory) {
#ifdef RJ_DEBUG_VERBOSE
    fprintf(stderr, "[GpuCopyOptimizer] execute_copy: device=%p cmd_buf=%p queue=%p sem=%p counter=%llu slot=%u\n",
            (void*)device_, (void*)command_buffers_[slot % POOL_SIZE], (void*)queue_,
            (void*)timeline_semaphore_, (unsigned long long)timeline_counter_, slot);
#endif

    if (!device_ || !command_buffers_[0] || !queue_) {
        fprintf(stderr, "[GpuCopyOptimizer] ABORT: null handle\n");
        return false;
    }

    // I23: slot bridge'ten gelir (0..POOL_SIZE-1). Savunmacı sınır kontrolü —
    // geçersiz slot per-slot dizilerinde (command_buffers_/layout/gl_signal) OOB olurdu.
    if (slot >= POOL_SIZE) {
        fprintf(stderr, "[GpuCopyOptimizer] Invalid slot %u (>= %u)\n", slot, POOL_SIZE);
        return false;
    }

    try {
        // Input validation
        if (!d3d11_staging_vk || !vulkan_target) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid images (staging or target null)\n");
            return false;
        }
        if (width == 0 || height == 0) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid dimensions (%u x %u)\n", width, height);
            return false;
        }
        if (!out_timeline_semaphore || !out_timeline_value || !out_target_image) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid output ptrs\n");
            return false;
        }

        // I23: slot bridge'ten parametre gelir (tek doğruluk kaynağı) — layout tracking,
        //      GL semaphore, keyed mutex ve command buffer aynı index'i paylaşır. Optimizer
        //      artık kendi round-robin sayacını (frame_counter_) sürmüyor.

        // Wait for previous submit to complete before reusing the command buffer.
        // signal_value_for_submit_ holds the timeline value from the last submit.
        // Typically a no-op (GPU finishes well within the frame interval).
        VkCommandBuffer cmd = command_buffers_[slot];

        if (signal_value_for_submit_ > 0 && pfn_get_semaphore_counter_value_ && pfn_wait_semaphores_) {
            uint64_t current_value = 0;
            pfn_get_semaphore_counter_value_(device_, timeline_semaphore_, &current_value);
            if (current_value < signal_value_for_submit_) {
                VkSemaphoreWaitInfo wait_info{};
                wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                wait_info.semaphoreCount = 1;
                wait_info.pSemaphores    = &timeline_semaphore_;
                wait_info.pValues        = &signal_value_for_submit_;
                pfn_wait_semaphores_(device_, &wait_info, UINT64_MAX);
            }
        }

        // Reset command buffer for reuse (safe explicit reset)
        CHECK_VK(vkResetCommandBuffer(cmd, 0));

        // Begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VK(vkBeginCommandBuffer(cmd, &begin_info));

        // ========== LAYOUT TRANSITION 1: Staging → TRANSFER_SRC (external acquire) ==========
        // D2: staging oldLayout always UNDEFINED — D3D11 externally writes this image each frame
        //     via keyed mutex, so Vulkan cannot track its layout between frames.
        // E4: srcAccessMask=0 (keyed mutex handles D3D11→Vulkan sync, no prior Vulkan write)
        //     srcQueueFamilyIndex=EXTERNAL: acquire ownership from D3D11 per VK_KHR_external_memory
        VkImageMemoryBarrier barrier_staging = {};
        barrier_staging.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_staging.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // D3D11 externally written
        barrier_staging.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging.srcAccessMask = 0;  // E4: D3D11 yazdı, keyed mutex sahipliği aktardı
        barrier_staging.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_staging.srcQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;  // E4: external acquire
        barrier_staging.dstQueueFamilyIndex = graphics_queue_family_;
        barrier_staging.image = d3d11_staging_vk;
        barrier_staging.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,  // E4: Vulkan'da önceki iş yok
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_staging);
        // staging_layouts_[slot] stays UNDEFINED — D3D11 retakes image after keyed mutex release

        // ========== LAYOUT TRANSITION 2: Target → TRANSFER_DST ==========
        // D2: per-slot target layout tracking (UNDEFINED on first use, SHADER_READ_ONLY after)
        // Previous submit is waited on (timeline semaphore above), so TOP_OF_PIPE is safe.
        VkImageMemoryBarrier barrier_target = {};
        barrier_target.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_target.oldLayout = target_layouts_[slot];
        barrier_target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_target.srcAccessMask = 0;
        barrier_target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_target.image = vulkan_target;
        barrier_target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_target);

        // ========== BLIT IMAGE ==========
        VkImageBlit blit_region = {};
        blit_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit_region.srcOffsets[0] = {0, 0, 0};
        blit_region.srcOffsets[1] = {(int32_t)width, (int32_t)height, 1};
        blit_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        blit_region.dstOffsets[0] = {0, 0, 0};
        blit_region.dstOffsets[1] = {(int32_t)width, (int32_t)height, 1};

#ifdef RJ_DEBUG_VERBOSE
        fprintf(stderr, "[GpuCopyOptimizer] About to blit: src=%p (TRANSFER_SRC) -> dst=%p (TRANSFER_DST), %u x %u\n",
                (void*)d3d11_staging_vk, (void*)vulkan_target, width, height);
#endif

        if (use_blit_) {
            vkCmdBlitImage(cmd, d3d11_staging_vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vulkan_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &blit_region, VK_FILTER_LINEAR);
        } else {
            // blit desteklenmiyorsa vkCmdCopyImage ile pixel-perfect kopyalama yap
            VkImageCopy copy_region{};
            copy_region.srcSubresource = blit_region.srcSubresource;
            copy_region.dstSubresource = blit_region.dstSubresource;
            copy_region.srcOffset = {0, 0, 0};
            copy_region.dstOffset = {0, 0, 0};
            copy_region.extent = {width, height, 1};
            vkCmdCopyImage(cmd, d3d11_staging_vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           vulkan_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           1, &copy_region);
        }

        // ========== LAYOUT TRANSITION 3: Target TRANSFER_DST → SHADER_READ_ONLY ==========
        VkImageMemoryBarrier barrier_final = {};
        barrier_final.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_final.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_final.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier_final.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_final.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier_final.image = vulkan_target;
        barrier_final.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_final);
        // V8/I5: target_layouts_[slot] ataması submit BAŞARILI olduktan sonraya
        //        taşındı (barrier komutu burada kalır). Bkz. submit sonrası state güncelleme.

        // ========== LAYOUT TRANSITION 4: Staging release → D3D11 (VK_QUEUE_FAMILY_EXTERNAL) ==========
        // E4: Release staging image ownership back to D3D11 after blit.
        //     dstAccessMask=0, dstStageMask=BOTTOM_OF_PIPE: Vulkan makes no further use of this image.
        VkImageMemoryBarrier barrier_staging_release = {};
        barrier_staging_release.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_staging_release.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging_release.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging_release.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_staging_release.dstAccessMask = 0;
        barrier_staging_release.srcQueueFamilyIndex = graphics_queue_family_;
        barrier_staging_release.dstQueueFamilyIndex = VK_QUEUE_FAMILY_EXTERNAL;
        barrier_staging_release.image = d3d11_staging_vk;
        barrier_staging_release.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(cmd,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_staging_release);
        // V8/I5: staging_layouts_[slot] ataması da submit sonrasına taşındı (barrier burada kalır).

        // End command buffer
        CHECK_VK(vkEndCommandBuffer(cmd));

        // ========== SUBMIT WITH TIMELINE + OPTIONAL GL SYNC SIGNAL ==========
        // CRITICAL: Timeline semaphore signal value MUST be > current value!
        timeline_counter_ += FRAME_INCREMENT;
        signal_value_for_submit_ = timeline_counter_;

        VkSemaphore active_gl_sem = gl_sync_sem;  // VK_NULL_HANDLE → GL sync atlanır

        // D12: binary semaphore re-signal koruması — GL henüz tüketmediyse signal atla
        bool will_signal_gl = false;
        if (slot_gl_signaled_[slot].load(std::memory_order_acquire)) {
#ifdef RJ_DEBUG_VERBOSE
            fprintf(stderr, "[CopyOptimizer] slot %u: GL wait bekleniyor, signal atlandı\n", slot);
            fflush(stderr);
#endif
            active_gl_sem = VK_NULL_HANDLE;
        } else if (active_gl_sem != VK_NULL_HANDLE) {
            will_signal_gl = true;
        }

        bool has_gl_sync = (active_gl_sem != VK_NULL_HANDLE);
        uint32_t sem_count = has_gl_sync ? 2u : 1u;

        signal_semaphores_[0] = timeline_semaphore_;
        signal_semaphores_[1] = active_gl_sem;     // pool slot, VK_NULL_HANDLE when unused
        signal_values_[0]     = signal_value_for_submit_;
        signal_values_[1]     = 0;                 // binary semaphore: value ignored

        timeline_submit_info_ = {};
        timeline_submit_info_.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
        timeline_submit_info_.signalSemaphoreValueCount = sem_count;
        timeline_submit_info_.pSignalSemaphoreValues    = signal_values_;

        // B6/D3: Chain keyed mutex acquire/release when D3D11 memory is provided.
        // D3: Store memory handle and struct as member fields — pAcquireSyncs/pReleaseSyncs
        //     point into members so pointers remain valid past async vkQueueSubmit.
        // D11: AMD iGPU'da VK_KHR_win32_keyed_mutex desteklenmeyebilir — use_keyed_mutex() ile kontrol et.
        km_memory_ = d3d11_staging_memory;
        auto* vulkan_init = rj::pipeline::gpu::VulkanInitializer::get();
        bool has_keyed_mutex = (vulkan_init && vulkan_init->use_keyed_mutex()) &&
                               (km_memory_ != VK_NULL_HANDLE);
        if (has_keyed_mutex) {
            keyed_mutex_info_ = {};
            keyed_mutex_info_.sType            = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR;
            keyed_mutex_info_.pNext            = &timeline_submit_info_;  // flat: keyed → timeline
            keyed_mutex_info_.acquireCount     = 1;
            keyed_mutex_info_.pAcquireSyncs    = &km_memory_;
            keyed_mutex_info_.pAcquireKeys     = &km_acquire_key_;
            keyed_mutex_info_.pAcquireTimeouts = &km_timeout_;
            keyed_mutex_info_.releaseCount     = 1;
            keyed_mutex_info_.pReleaseSyncs    = &km_memory_;
            keyed_mutex_info_.pReleaseKeys     = &km_release_key_;
            timeline_submit_info_.pNext        = nullptr;                  // timeline zincir sonu
        } else {
            timeline_submit_info_.pNext = nullptr;
        }

        submit_info_ = {};
        submit_info_.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        // G4: flat chain — keyed_mutex aktifse submit→keyed→timeline, aksi halde submit→timeline
        submit_info_.pNext = has_keyed_mutex
            ? static_cast<const void*>(&keyed_mutex_info_)
            : static_cast<const void*>(&timeline_submit_info_);
        submit_info_.commandBufferCount   = 1;
        submit_info_.pCommandBuffers      = &command_buffers_[slot];
        submit_info_.signalSemaphoreCount = sem_count;
        submit_info_.pSignalSemaphores    = signal_semaphores_;

        VkResult submit_result = vkQueueSubmit(queue_, 1, &submit_info_, VK_NULL_HANDLE);
        if (submit_result != VK_SUCCESS) {
            // H17: submit olmadı — timeline_counter_ geri al, sonraki frame sonsuza beklemesin
            timeline_counter_ -= FRAME_INCREMENT;
            signal_value_for_submit_ = timeline_counter_;
            fprintf(stderr, "[CopyOptimizer] submit failed: %d\n", submit_result);
            return false;
        }
        // Submit başarılı — şimdi state güncelle.
        // V8/I5: layout tracking'i submit BAŞARISINDAN SONRA yaz. Submit başarısızsa
        //        (yukarıdaki return false yolu) image'ler gerçekte transition OLMADIĞINDAN
        //        target_/staging_layouts_ değişmemeli — aksi halde sonraki frame'in barrier'ı
        //        yanlış oldLayout kurar (VUID-VkImageMemoryBarrier-oldLayout riski).
        target_layouts_[slot]  = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // D2: per-slot tracking
        staging_layouts_[slot] = VK_IMAGE_LAYOUT_UNDEFINED;
        if (will_signal_gl) {
            slot_gl_signaled_[slot].store(true, std::memory_order_release);
        }
        // I23: last_used_slot_/frame_counter_ kaldırıldı — slot dışarıdan yönetiliyor.

        // Return outputs
        *out_timeline_semaphore = timeline_semaphore_;
        *out_timeline_value = signal_value_for_submit_;
        *out_target_image = vulkan_target;

#ifdef RJ_DEBUG_VERBOSE
        fprintf(stderr, "[GpuCopyOptimizer] execute_copy: blit submitted, timeline=%llu\n",
                signal_value_for_submit_);
#endif

        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during execute_copy\n");
        return false;
    }
}


bool GpuCopyOptimizer::is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value) {
    // V8/I6: cheap, handle-independent early-out. If shutdown() has begun, device_
    // and pfn_wait_semaphores_ may be mid-teardown; bail before touching them.
    if (!alive_.load(std::memory_order_acquire)) {
        return false;
    }
    if (!device_ || !pfn_wait_semaphores_ || timeline_semaphore == VK_NULL_HANDLE) {
        return false;
    }

    bool result = false;
    rj::SehCapture cap{};
    __try {
        VkSemaphoreWaitInfo wait_info{};
        wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores    = &timeline_semaphore;
        wait_info.pValues        = &expected_value;
        result = (pfn_wait_semaphores_(device_, &wait_info, 0) == VK_SUCCESS);
    } __except (rj::seh_filter(GetExceptionInformation(), rj::SehSite::CopyOptWait, &cap)) {
    }
    if (cap.fired) rj::seh_report(cap, rj::SehSite::CopyOptWait);
    return result;
}

void GpuCopyOptimizer::shutdown() {
    // V8/I6: mark not-alive FIRST so a concurrent is_copy_ready() bails before we
    // null device_ below. Stays false until a subsequent init() re-arms it.
    alive_.store(false, std::memory_order_release);

    if (!device_) {
        fprintf(stderr, "[GpuCopyOptimizer] Device is null, skipping shutdown\n");
        fflush(stderr);
        return;
    }

    rj::SehCapture cap{};
    bool wait_timed_out = false;

    __try {
        // D4: Wait for last submitted GPU work before destroying (5s timeout)
        // D15: Only POD ops and raw Vulkan destroy calls inside __try
        if (signal_value_for_submit_ > 0 && pfn_wait_semaphores_ &&
            timeline_semaphore_ != VK_NULL_HANDLE) {
            VkSemaphoreWaitInfo wait_info{};
            wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
            wait_info.semaphoreCount = 1;
            wait_info.pSemaphores    = &timeline_semaphore_;
            wait_info.pValues        = &signal_value_for_submit_;
            constexpr uint64_t TIMEOUT_5S = 5'000'000'000ULL;
            VkResult wr = pfn_wait_semaphores_(device_, &wait_info, TIMEOUT_5S);
            wait_timed_out = (wr != VK_SUCCESS);  // D15: flag, not fprintf
        }
        if (timeline_semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, timeline_semaphore_, nullptr);
            timeline_semaphore_ = VK_NULL_HANDLE;
        }
        // I23: frame_counter_ kaldırıldı (slot artık dışarıdan yönetiliyor).
        if (command_buffers_[0] != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, command_pool_, 3, command_buffers_);
            for (auto& cb : command_buffers_) cb = VK_NULL_HANDLE;
        }
        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }
        // D15: cleanup_pipeline() moved outside __try — C++ call not allowed in __try
        device_ = VK_NULL_HANDLE;
        queue_ = VK_NULL_HANDLE;
        phys_device_ = VK_NULL_HANDLE;
        timeline_counter_ = 0;
        pfn_get_semaphore_counter_value_ = nullptr;
    } __except (rj::seh_filter(GetExceptionInformation(), rj::SehSite::CopyOptShutdown, &cap)) {
    }

    // D15: All C++ calls and logging outside __try
    if (wait_timed_out) {
        fprintf(stderr, "[GpuCopyOptimizer] shutdown: GPU idle wait timed out\n");
        fflush(stderr);
    }
    if (cap.fired) {
        rj::seh_report(cap, rj::SehSite::CopyOptShutdown);
    } else {
        cleanup_pipeline();
        fprintf(stderr, "[GpuCopyOptimizer] Shutdown complete\n");
        fflush(stderr);
    }

    for (auto& layout : target_layouts_)  layout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (auto& layout : staging_layouts_) layout = VK_IMAGE_LAYOUT_UNDEFINED;
    for (auto& sig : slot_gl_signaled_)   sig = false;
}

void GpuCopyOptimizer::cleanup_pipeline() {
    // Cleanup descriptors, pipeline, layout (Task 1.5)
}
