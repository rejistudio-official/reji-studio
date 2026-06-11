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
#include <cstring>
#include <cstdio>

// B15: SEH filter — captures exception code, always executes handler
static DWORD SehFilter(DWORD code, DWORD* out_code) {
    *out_code = code;
    return EXCEPTION_EXECUTE_HANDLER;
}

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

    try {  // C++ exceptions only; Vulkan returns error codes
        device_ = device;
        queue_ = queue;
        phys_device_ = phys_device;

        // Create command pool
        VkCommandPoolCreateInfo pool_info = {};
        pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pool_info.queueFamilyIndex = queue_family_index;
        CHECK_VK(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_));

        // Allocate command buffer
        VkCommandBufferAllocateInfo alloc_info = {};
        alloc_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        alloc_info.commandPool = command_pool_;
        alloc_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc_info.commandBufferCount = 1;
        CHECK_VK(vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer_));

        // Create timeline semaphore (non-blocking sync)
        VkSemaphoreTypeCreateInfoKHR timeline_info = {};
        timeline_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR;
        timeline_info.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
        timeline_info.initialValue = 0;

        VkSemaphoreCreateInfo sem_info = {};
        sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        sem_info.pNext = &timeline_info;
        CHECK_VK(vkCreateSemaphore(device_, &sem_info, nullptr, &timeline_semaphore_));
        fprintf(stderr, "[GpuCopyOptimizer] vkCreateSemaphore: timeline_semaphore_=%p\n",
                (void*)timeline_semaphore_);

        // C7: Create 3-slot binary semaphore pool for GL sync
        VkSemaphoreCreateInfo bin_sem_info{};
        bin_sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (int i = 0; i < 3; ++i) {
            CHECK_VK(vkCreateSemaphore(device_, &bin_sem_info, nullptr, &gl_sync_sem_pool_[i]));
        }
        fprintf(stderr, "[GpuCopyOptimizer] gl_sync_sem_pool: 3 binary semaphores created\n");

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
            return false;
        }

        pfn_wait_semaphores_ =
            reinterpret_cast<PFN_vkWaitSemaphores>(
                vkGetDeviceProcAddr(device_, "vkWaitSemaphores"));
        if (!pfn_wait_semaphores_) {
            fprintf(stderr, "[GpuCopyOptimizer] vkGetDeviceProcAddr failed for vkWaitSemaphores\n");
            return false;
        }

        fprintf(stderr, "[GpuCopyOptimizer] Initialized (command pool, buffer, timeline semaphore)\n");
        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during init\n");
        return false;
    }
}

bool GpuCopyOptimizer::execute_copy(VkImage d3d11_staging_vk,
                                     VkImage vulkan_target,
                                     uint32_t width,
                                     uint32_t height,
                                     VkSemaphore* out_timeline_semaphore,
                                     uint64_t* out_timeline_value,
                                     VkImage* out_target_image,
                                     VkSemaphore gl_sync_sem,
                                     VkDeviceMemory d3d11_staging_memory) {
    fprintf(stderr, "[GpuCopyOptimizer] execute_copy: device=%p cmd_buf=%p queue=%p sem=%p counter=%llu\n",
            (void*)device_, (void*)command_buffer_, (void*)queue_,
            (void*)timeline_semaphore_, (unsigned long long)timeline_counter_);

    if (!device_ || !command_buffer_ || !queue_) {
        fprintf(stderr, "[GpuCopyOptimizer] ABORT: null handle\n");
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

        // D2/D3: Compute round-robin slot once — shared by layout tracking, GL semaphore, keyed mutex.
        // frame_counter_ incremented after use (at submit time).
        const uint32_t slot = frame_counter_ % POOL_SIZE;

        // Wait for previous submit to complete before reusing the command buffer.
        // signal_value_for_submit_ holds the timeline value from the last submit.
        // Typically a no-op (GPU finishes well within the frame interval).
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
        CHECK_VK(vkResetCommandBuffer(command_buffer_, 0));

        // Begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;  // Buffer reused/pending
        CHECK_VK(vkBeginCommandBuffer(command_buffer_, &begin_info));

        // ========== LAYOUT TRANSITION 1: Staging → TRANSFER_SRC ==========
        // D2: staging oldLayout always UNDEFINED — D3D11 externally writes this image each frame
        //     via keyed mutex, so Vulkan cannot track its layout between frames.
        // C4: srcStageMask=TRANSFER_BIT (ALL_GRAPHICS + TRANSFER_WRITE was spec-invalid)
        VkImageMemoryBarrier barrier_staging = {};
        barrier_staging.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_staging.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;  // D3D11 externally written
        barrier_staging.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_staging.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_staging.image = d3d11_staging_vk;
        barrier_staging.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
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

        vkCmdPipelineBarrier(command_buffer_,
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

        fprintf(stderr, "[GpuCopyOptimizer] About to blit: src=%p (TRANSFER_SRC) -> dst=%p (TRANSFER_DST), %u x %u\n",
                (void*)d3d11_staging_vk, (void*)vulkan_target, width, height);

        vkCmdBlitImage(command_buffer_,
                       d3d11_staging_vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       vulkan_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit_region,
                       VK_FILTER_LINEAR);

        // ========== LAYOUT TRANSITION 3: Target TRANSFER_DST → SHADER_READ_ONLY ==========
        VkImageMemoryBarrier barrier_final = {};
        barrier_final.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_final.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_final.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier_final.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_final.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier_final.image = vulkan_target;
        barrier_final.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_final);
        target_layouts_[slot] = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;  // D2: per-slot tracking

        // End command buffer
        CHECK_VK(vkEndCommandBuffer(command_buffer_));

        // ========== SUBMIT WITH TIMELINE + OPTIONAL GL SYNC SIGNAL ==========
        // CRITICAL: Timeline semaphore signal value MUST be > current value!
        timeline_counter_ += FRAME_INCREMENT;
        signal_value_for_submit_ = timeline_counter_;

        // C7: Round-robin slot — uses same slot computed above for layout tracking.
        VkSemaphore active_gl_sem = (gl_sync_sem != VK_NULL_HANDLE)
            ? gl_sync_sem
            : gl_sync_sem_pool_[slot];
        ++frame_counter_;

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
        km_memory_ = d3d11_staging_memory;
        bool has_keyed_mutex = (km_memory_ != VK_NULL_HANDLE);
        if (has_keyed_mutex) {
            keyed_mutex_info_ = {};
            keyed_mutex_info_.sType            = VK_STRUCTURE_TYPE_WIN32_KEYED_MUTEX_ACQUIRE_RELEASE_INFO_KHR;
            keyed_mutex_info_.pNext            = nullptr;
            keyed_mutex_info_.acquireCount     = 1;
            keyed_mutex_info_.pAcquireSyncs    = &km_memory_;
            keyed_mutex_info_.pAcquireKeys     = &km_acquire_key_;
            keyed_mutex_info_.pAcquireTimeouts = &km_timeout_;
            keyed_mutex_info_.releaseCount     = 1;
            keyed_mutex_info_.pReleaseSyncs    = &km_memory_;
            keyed_mutex_info_.pReleaseKeys     = &km_release_key_;
            timeline_submit_info_.pNext        = &keyed_mutex_info_;
        } else {
            timeline_submit_info_.pNext = nullptr;
        }

        submit_info_ = {};
        submit_info_.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info_.pNext = &timeline_submit_info_;
        submit_info_.commandBufferCount   = 1;
        submit_info_.pCommandBuffers      = &command_buffer_;
        submit_info_.signalSemaphoreCount = sem_count;
        submit_info_.pSignalSemaphores    = signal_semaphores_;

        CHECK_VK(vkQueueSubmit(queue_, 1, &submit_info_, VK_NULL_HANDLE));

        // Return outputs
        *out_timeline_semaphore = timeline_semaphore_;
        *out_timeline_value = signal_value_for_submit_;
        *out_target_image = vulkan_target;

        fprintf(stderr, "[GpuCopyOptimizer] execute_copy: blit submitted, timeline=%llu\n",
                signal_value_for_submit_);

        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during execute_copy\n");
        return false;
    }
}

VkSemaphore GpuCopyOptimizer::current_gl_sync_semaphore() const noexcept {
    if (frame_counter_ == 0) return VK_NULL_HANDLE;
    return gl_sync_sem_pool_[(frame_counter_ - 1) % 3];
}

bool GpuCopyOptimizer::is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value) {
    if (!device_ || !pfn_wait_semaphores_ || timeline_semaphore == VK_NULL_HANDLE) {
        return false;
    }

    bool result    = false;
    bool seh_fired = false;
    DWORD seh_code = 0;
    __try {
        VkSemaphoreWaitInfo wait_info{};
        wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
        wait_info.semaphoreCount = 1;
        wait_info.pSemaphores    = &timeline_semaphore;
        wait_info.pValues        = &expected_value;
        result = (pfn_wait_semaphores_(device_, &wait_info, 0) == VK_SUCCESS);
    } __except (SehFilter(GetExceptionCode(), &seh_code)) {
        seh_fired = true;
    }
    if (seh_fired) {
        fprintf(stderr, "[GpuCopyOptimizer] SEH: 0x%08lX\n", seh_code);
        fflush(stderr);
    }
    return result;
}

void GpuCopyOptimizer::shutdown() {
    bool device_null = false;
    bool seh_fired   = false;
    DWORD seh_code   = 0;
    __try {
        device_null = (!device_);
        if (!device_null) {
            // D4: Wait for last submitted GPU work before destroying semaphores (5s timeout)
            if (signal_value_for_submit_ > 0 && pfn_wait_semaphores_ &&
                timeline_semaphore_ != VK_NULL_HANDLE) {
                VkSemaphoreWaitInfo wait_info{};
                wait_info.sType          = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
                wait_info.semaphoreCount = 1;
                wait_info.pSemaphores    = &timeline_semaphore_;
                wait_info.pValues        = &signal_value_for_submit_;
                constexpr uint64_t TIMEOUT_5S = 5'000'000'000ULL;
                VkResult wr = pfn_wait_semaphores_(device_, &wait_info, TIMEOUT_5S);
                if (wr != VK_SUCCESS)
                    fprintf(stderr, "[GpuCopyOptimizer] shutdown: GPU idle wait timed out (0x%x)\n", wr);
            }
            if (timeline_semaphore_ != VK_NULL_HANDLE) {
                vkDestroySemaphore(device_, timeline_semaphore_, nullptr);
                timeline_semaphore_ = VK_NULL_HANDLE;
            }
            // C7: Destroy binary semaphore pool
            for (int i = 0; i < 3; ++i) {
                if (gl_sync_sem_pool_[i] != VK_NULL_HANDLE) {
                    vkDestroySemaphore(device_, gl_sync_sem_pool_[i], nullptr);
                    gl_sync_sem_pool_[i] = VK_NULL_HANDLE;
                }
            }
            frame_counter_ = 0;
            if (command_buffer_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
                vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer_);
                command_buffer_ = VK_NULL_HANDLE;
            }
            if (command_pool_ != VK_NULL_HANDLE) {
                vkDestroyCommandPool(device_, command_pool_, nullptr);
                command_pool_ = VK_NULL_HANDLE;
            }
            cleanup_pipeline();
            device_ = VK_NULL_HANDLE;
            queue_ = VK_NULL_HANDLE;
            phys_device_ = VK_NULL_HANDLE;
            pipeline_layout_ = VK_NULL_HANDLE;
            compute_pipeline_ = VK_NULL_HANDLE;
            descriptor_set_layout_ = VK_NULL_HANDLE;
            descriptor_pool_ = VK_NULL_HANDLE;
            descriptor_set_ = VK_NULL_HANDLE;
            timeline_counter_ = 0;
            pfn_get_semaphore_counter_value_ = nullptr;
        }
    } __except (SehFilter(GetExceptionCode(), &seh_code)) {
        seh_fired = true;
    }
    if (device_null) {
        fprintf(stderr, "[GpuCopyOptimizer] Device is null, skipping shutdown\n");
    } else if (seh_fired) {
        fprintf(stderr, "[GpuCopyOptimizer] SEH exception during shutdown: 0x%08lX\n", seh_code);
    } else {
        fprintf(stderr, "[GpuCopyOptimizer] Shutdown complete\n");
    }
}

bool GpuCopyOptimizer::load_compute_shader(const char* spv_path) {
    // Placeholder: will load SPIR-V from cache (Task 1.5)
    return true;
}

void GpuCopyOptimizer::cleanup_pipeline() {
    // Cleanup descriptors, pipeline, layout (Task 1.5)
}
