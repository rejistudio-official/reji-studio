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

    __try {
        device_ = device;
        queue_ = queue;
        phys_device_ = phys_device;

        // Create command pool
        VkCommandPoolCreateInfo pool_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = 0  // Assume queue family 0 (TBD: get from VkQueueFamilyProperties)
        };
        CHECK_VK(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_));

        // Allocate command buffer
        VkCommandBufferAllocateInfo alloc_info{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = command_pool_,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        CHECK_VK(vkAllocateCommandBuffers(device_, &alloc_info, &command_buffer_));

        // Create timeline semaphore (non-blocking sync)
        VkSemaphoreTypeCreateInfoKHR timeline_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO_KHR,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = 0
        };

        VkSemaphoreCreateInfo sem_info{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &timeline_info
        };
        CHECK_VK(vkCreateSemaphore(device_, &sem_info, nullptr, &timeline_semaphore_));

        fprintf(stderr, "[GpuCopyOptimizer] Initialized (command pool, buffer, timeline semaphore)\n");
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "[GpuCopyOptimizer] SEH exception during init\n");
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

    __try {
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

        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fprintf(stderr, "[GpuCopyOptimizer] SEH exception during execute_copy\n");
        return false;
    }
}

bool GpuCopyOptimizer::is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value) {
    if (!device_ || timeline_semaphore == VK_NULL_HANDLE) {
        return false;
    }

    __try {
        uint64_t current_value = 0;
        VkResult res = vkGetSemaphoreCounterValueKHR(device_, timeline_semaphore, &current_value);
        if (res != VK_SUCCESS) {
            return false;
        }
        return current_value >= expected_value;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void GpuCopyOptimizer::shutdown() {
    __try {
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

        fprintf(stderr, "[GpuCopyOptimizer] Shutdown complete\n");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
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
