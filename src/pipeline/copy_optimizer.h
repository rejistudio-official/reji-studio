#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class GpuResourceManager;  // Forward declare

class GpuCopyOptimizer {
public:
    struct Config {
        uint32_t workgroup_x = 8;
        uint32_t workgroup_y = 8;
    };

    GpuCopyOptimizer() = default;
    ~GpuCopyOptimizer() = default;

    // Initialize Vulkan compute pipeline
    // Returns true on success, false if Vulkan unavailable or init failed
    bool init(VkDevice device, VkQueue queue, VkPhysicalDevice phys_device,
              const Config& config = Config{});

    // Execute GPU-side copy: D3D11 external memory → Vulkan target image
    // Submits compute shader to queue, returns timeline semaphore for async wait
    // Returns false if submission failed
    bool execute_copy(VkImage d3d11_staging_vk,    // D3D11 texture imported as VkImage
                      VkImage vulkan_target,        // Target Vulkan image (OpenGL interop)
                      uint32_t width,
                      uint32_t height,
                      VkSemaphore* out_timeline_semaphore,  // Caller polls this
                      uint64_t* out_timeline_value);        // Value to check

    // Check if copy is ready (non-blocking poll)
    bool is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value);

    // Shutdown and cleanup
    void shutdown();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_device_ = VK_NULL_HANDLE;

    VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
    VkPipeline compute_pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_set_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptor_set_ = VK_NULL_HANDLE;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;

    VkSemaphore timeline_semaphore_ = VK_NULL_HANDLE;
    uint64_t timeline_counter_ = 0;
    static constexpr uint64_t FRAME_INCREMENT = 1;

    // Extension function pointer (loaded once in init()).
    // vkGetSemaphoreCounterValueKHR is an extension function; must be resolved
    // via vkGetDeviceProcAddr rather than called directly.
    PFN_vkGetSemaphoreCounterValueKHR pfn_get_semaphore_counter_value_ = nullptr;

    uint32_t dispatch_x_ = 1;
    uint32_t dispatch_y_ = 1;

    bool load_compute_shader(const char* spv_path);
    void cleanup_pipeline();
};
