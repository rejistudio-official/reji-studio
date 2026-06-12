#include "gpu_query_timing.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

bool GpuQueryTiming::init(VkDevice device, VkQueue queue, VkPhysicalDevice phys_device) {
    if (!device || !phys_device) {
        fprintf(stderr, "[GpuQueryTiming] Invalid device or phys_device\n");
        return false;
    }

    device_ = device;

    // Query physical device properties to get timestamp period
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(phys_device, &props);
    timestamp_period_ns_ = props.limits.timestampPeriod;

    fprintf(stderr, "[GpuQueryTiming] Timestamp period: %.2f ns\n", timestamp_period_ns_);

    // Create query pool for 4 timestamps (copy_start, copy_end, render_start, render_end)
    VkQueryPoolCreateInfo pool_info{};
    pool_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    pool_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    pool_info.queryCount = NUM_QUERIES;

    VkResult res = vkCreateQueryPool(device_, &pool_info, nullptr, &query_pool_);
    if (res != VK_SUCCESS) {
        fprintf(stderr, "[GpuQueryTiming] vkCreateQueryPool failed: 0x%x\n", res);
        return false;
    }

    std::memset(query_values_, 0, sizeof(query_values_));

    fprintf(stderr, "[GpuQueryTiming] Initialized with query pool (%u queries)\n", NUM_QUERIES);
    return true;
}

bool GpuQueryTiming::record_timestamp(VkCommandBuffer cmd, const char* label) {
    if (!cmd || !query_pool_) {
        return false;
    }

    // Map label to query index
    uint32_t query_idx = UINT32_MAX;
    if (std::strcmp(label, "copy_start") == 0) query_idx = 0;
    else if (std::strcmp(label, "copy_end") == 0) query_idx = 1;
    else if (std::strcmp(label, "render_start") == 0) query_idx = 2;
    else if (std::strcmp(label, "render_end") == 0) query_idx = 3;
    else {
        fprintf(stderr, "[GpuQueryTiming] Unknown label: %s\n", label);
        return false;
    }

    // Reset all queries at frame start before first write
    if (query_idx == 0) {
        vkCmdResetQueryPool(cmd, query_pool_, 0, NUM_QUERIES);
    }

    vkCmdWriteTimestamp(cmd, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, query_pool_, query_idx);
    return true;
}

bool GpuQueryTiming::retrieve_results(QueryResult* out_result) {
    if (!device_ || !query_pool_ || !out_result) {
        return false;
    }

    // Interleaved layout: [timestamp, availability] per query (WITH_AVAILABILITY_BIT)
    uint64_t buf[NUM_QUERIES * 2]{};
    VkResult res = vkGetQueryPoolResults(
        device_, query_pool_, 0, NUM_QUERIES,
        sizeof(buf), buf, 2 * sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT
    );

    if (res == VK_NOT_READY) {
        return false;
    }

    if (res != VK_SUCCESS) {
        fprintf(stderr, "[GpuQueryTiming] vkGetQueryPoolResults failed: 0x%x\n", res);
        return false;
    }

    // Verify all queries are available and extract timestamps
    for (uint32_t i = 0; i < NUM_QUERIES; ++i) {
        if (!buf[i * 2 + 1]) return false;  // availability == 0: result not written yet
        query_values_[i] = buf[i * 2];
    }

    // Convert raw timestamps to durations
    uint64_t copy_delta = query_values_[1] - query_values_[0];
    uint64_t render_delta = query_values_[3] - query_values_[2];

    out_result->copy_start_ns = query_values_[0] * (uint64_t)timestamp_period_ns_;
    out_result->copy_end_ns = query_values_[1] * (uint64_t)timestamp_period_ns_;
    out_result->render_start_ns = query_values_[2] * (uint64_t)timestamp_period_ns_;
    out_result->render_end_ns = query_values_[3] * (uint64_t)timestamp_period_ns_;

    out_result->copy_duration_ms = convert_timestamp_ns_to_ms(copy_delta * (uint64_t)timestamp_period_ns_);
    out_result->render_duration_ms = convert_timestamp_ns_to_ms(render_delta * (uint64_t)timestamp_period_ns_);

    return true;
}

void GpuQueryTiming::shutdown() {
    if (!device_) {
        fprintf(stderr, "[GpuQueryTiming] Device is null, skipping shutdown\n");
        return;
    }

    if (query_pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, query_pool_, nullptr);
        query_pool_ = VK_NULL_HANDLE;
    }

    device_ = VK_NULL_HANDLE;
    fprintf(stderr, "[GpuQueryTiming] Shutdown complete\n");
}

float GpuQueryTiming::convert_timestamp_ns_to_ms(uint64_t delta_ns) const {
    return delta_ns / 1000000.0f;  // 1 million ns = 1 ms
}
