#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

class GpuQueryTiming {
public:
    struct QueryResult {
        uint64_t copy_start_ns = 0;
        uint64_t copy_end_ns = 0;
        uint64_t render_start_ns = 0;
        uint64_t render_end_ns = 0;
        float copy_duration_ms = 0.0f;
        float render_duration_ms = 0.0f;
    };

    GpuQueryTiming() = default;
    ~GpuQueryTiming() = default;

    // Initialize Vulkan timestamp query pool
    bool init(VkDevice device, VkQueue queue, VkPhysicalDevice phys_device);

    // Record a timestamp at this point in command buffer
    bool record_timestamp(VkCommandBuffer cmd, const char* label);

    // Retrieve query results (non-blocking poll)
    // Returns true if results available, false if pending
    bool retrieve_results(QueryResult* out_result);

    // Shutdown
    void shutdown();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool query_pool_ = VK_NULL_HANDLE;

    float timestamp_period_ns_ = 1.0f;  // GPU timestamp frequency (in nanoseconds per tick)

    // Query indices: 0=copy_start, 1=copy_end, 2=render_start, 3=render_end
    static constexpr uint32_t NUM_QUERIES = 4;
    uint64_t query_values_[NUM_QUERIES]{};

    float convert_timestamp_ns_to_ms(uint64_t delta_ns) const;
};
