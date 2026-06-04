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
                            const Config& config) {
    if (!device || !queue || !phys_device) {
        fprintf(stderr, "[GpuCopyOptimizer] Invalid device/queue/phys_device\n");
        return false;
    }

    try {  // C++ exceptions only; Vulkan returns error codes
        device_ = device;
        queue_ = queue;
        phys_device_ = phys_device;

        // Query queue family properties (default to 0 if unavailable)
        uint32_t queue_family_index = 0;
        uint32_t queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(phys_device_, &queue_family_count, nullptr);
        if (queue_family_count > 0) {
            // Use queue family 0 (typically supports graphics + compute)
            queue_family_index = 0;
        }

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
                                     uint64_t* out_timeline_value) {
    if (!device_ || !command_buffer_) {
        return false;
    }

    try {
        if (!out_timeline_semaphore || !out_timeline_value) {
            return false;
        }

        // For now, stub: just return semaphore values
        // Compute shader dispatch will be added after shader compilation (Task 1.5)
        *out_timeline_semaphore = timeline_semaphore_;
        *out_timeline_value = timeline_counter_;

        // TODO: Record compute dispatch in command_buffer_
        // TODO: Submit with timeline semaphore signal

        fprintf(stderr, "[GpuCopyOptimizer] execute_copy stub (values: sem=%p, counter=%llu)\n",
                (void*)timeline_semaphore_, timeline_counter_);

        // Increment timeline counter for next frame
        timeline_counter_ += FRAME_INCREMENT;

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
