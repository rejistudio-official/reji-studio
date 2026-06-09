#include "copy_optimizer.h"
#include <cstring>
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
                                     VkImage* out_target_image) {
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

        // Reset command buffer for reuse (safe explicit reset)
        CHECK_VK(vkResetCommandBuffer(command_buffer_, 0));

        // Begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = 0;  // No ONE_TIME_SUBMIT_BIT — buffer is reused per frame
        CHECK_VK(vkBeginCommandBuffer(command_buffer_, &begin_info));

        // ========== LAYOUT TRANSITION 1: Staging TRANSFER_SRC → TRANSFER_SRC (buffer reuse) ==========
        VkImageMemoryBarrier barrier_staging = {};
        barrier_staging.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_staging.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_staging.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_staging.image = d3d11_staging_vk;
        barrier_staging.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_staging);

        // ========== LAYOUT TRANSITION 2: Target SHADER_READ → TRANSFER_DST (buffer reuse) ==========
        VkImageMemoryBarrier barrier_target = {};
        barrier_target.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_target.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier_target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_target.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
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

        // End command buffer
        CHECK_VK(vkEndCommandBuffer(command_buffer_));

        // ========== SUBMIT WITH TIMELINE SIGNAL ==========
        uint64_t signal_value = timeline_counter_;

        VkTimelineSemaphoreSubmitInfo timeline_submit_info = {};
        timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
        timeline_submit_info.signalSemaphoreValueCount = 1;
        timeline_submit_info.pSignalSemaphoreValues = &signal_value;

        VkSubmitInfo submit_info = {};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.pNext = &timeline_submit_info;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer_;
        submit_info.signalSemaphoreCount = 1;
        submit_info.pSignalSemaphores = &timeline_semaphore_;

        CHECK_VK(vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE));

        // Return outputs
        *out_timeline_semaphore = timeline_semaphore_;
        *out_timeline_value = signal_value;
        *out_target_image = vulkan_target;

        // Increment for next frame
        timeline_counter_ += FRAME_INCREMENT;

        fprintf(stderr, "[GpuCopyOptimizer] execute_copy: blit submitted, timeline=%llu\n",
                signal_value);

        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during execute_copy\n");
        return false;
    }
}

bool GpuCopyOptimizer::is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value) {
    if (!device_ || !pfn_get_semaphore_counter_value_ ||
        timeline_semaphore == VK_NULL_HANDLE) {
        return false;
    }

    __try {
        uint64_t current_value = 0;
        VkResult res = pfn_get_semaphore_counter_value_(device_, timeline_semaphore, &current_value);
        if (res != VK_SUCCESS) {
            return false;
        }
        return current_value >= expected_value;
    } __except (1) {
        return false;
    }
}

void GpuCopyOptimizer::shutdown() {
    __try {
        if (!device_) {
            fprintf(stderr, "[GpuCopyOptimizer] Device is null, skipping shutdown\n");
            return;
        }

        if (timeline_semaphore_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, timeline_semaphore_, nullptr);
            timeline_semaphore_ = VK_NULL_HANDLE;
        }

        if (command_buffer_ != VK_NULL_HANDLE && command_pool_ != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device_, command_pool_, 1, &command_buffer_);
            command_buffer_ = VK_NULL_HANDLE;
        }

        if (command_pool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, command_pool_, nullptr);
            command_pool_ = VK_NULL_HANDLE;
        }

        cleanup_pipeline();

        // Reset all members
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

        fprintf(stderr, "[GpuCopyOptimizer] Shutdown complete\n");
    } __except (1) {
        fprintf(stderr, "[GpuCopyOptimizer] SEH exception during shutdown\n");
    }
}

bool GpuCopyOptimizer::load_compute_shader(const char* spv_path) {
    // Placeholder: will load SPIR-V from cache (Task 1.5)
    return true;
}

void GpuCopyOptimizer::cleanup_pipeline() {
    // Cleanup descriptors, pipeline, layout (Task 1.5)
}
