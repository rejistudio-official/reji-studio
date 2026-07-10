#pragma once

#include "include/reji_constants.h"
#include <vulkan/vulkan.h>
#include <cstdint>
#include <array>
#include <atomic>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan_win32.h>
#endif

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
              uint32_t queue_family_index,
              const Config& config = Config{});

    // Execute GPU-side copy: D3D11 external memory → Vulkan target image
    // Submits compute shader to queue, returns timeline semaphore for async wait
    // Returns false if submission failed
    bool execute_copy(VkImage d3d11_staging_vk,    // D3D11 texture imported as VkImage
                      VkImage vulkan_target,        // Target Vulkan image (OpenGL interop)
                      uint32_t width,
                      uint32_t height,
                      VkSemaphore* out_timeline_semaphore,  // Caller polls this
                      uint64_t* out_timeline_value,         // Value to check
                      VkImage* out_target_image,            // Target image output
                      VkSemaphore gl_sync_sem = VK_NULL_HANDLE,         // B5: optional GL sync
                      VkDeviceMemory d3d11_staging_memory = VK_NULL_HANDLE); // B6: keyed mutex mem

    // Check if copy is ready (non-blocking poll)
    bool is_copy_ready(VkSemaphore timeline_semaphore, uint64_t expected_value);

    // D12: GL semaphore tüketildi — paintGL() içinde glWaitSemaphoreEXT sonrası çağır
    void clear_gl_signal(uint32_t slot) {
        if (slot < rj::constants::kGpuPoolSize) slot_gl_signaled_[slot].store(false, std::memory_order_release);
    }

    // E3: true ise bu slot'ta Vulkan sinyal attı, GL henüz tüketmedi — wait güvenli
    bool is_slot_signaled(uint32_t slot) const {
        return slot < rj::constants::kGpuPoolSize && slot_gl_signaled_[slot].load(std::memory_order_acquire);
    }

    // F8: execute_copy'nin başarıyla kullandığı son slot
    uint32_t last_used_slot() const { return last_used_slot_.load(std::memory_order_acquire); }

    // G2: execute_copy'nin kullanacağı bir sonraki slot (fence wait için)
    uint32_t next_slot() const { return (last_used_slot_.load(std::memory_order_relaxed) + 1) % rj::constants::kGpuPoolSize; }

    // Shutdown and cleanup
    void shutdown();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VkPhysicalDevice phys_device_ = VK_NULL_HANDLE;

    VkCommandPool command_pool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_buffers_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};

    VkSemaphore timeline_semaphore_ = VK_NULL_HANDLE;
    uint64_t timeline_counter_ = 0;

    std::atomic<uint32_t> frame_counter_{0};

    // D2: Per-slot layout tracking — staging always UNDEFINED (D3D11 externally written each frame)
    static constexpr uint32_t POOL_SIZE = rj::constants::kGpuPoolSize;
    std::array<VkImageLayout, POOL_SIZE> staging_layouts_{};  // always UNDEFINED; D3D11 owns between frames
    std::array<VkImageLayout, POOL_SIZE> target_layouts_{};   // UNDEFINED → SHADER_READ_ONLY per slot

    // D12: binary semaphore re-signal koruması — true: GL henüz bu slot'u tüketmedi
    std::array<std::atomic<bool>, POOL_SIZE> slot_gl_signaled_{};
    uint64_t signal_value_for_submit_ = 0;  // Must persist for async vkQueueSubmit (not stack-local)
    VkTimelineSemaphoreSubmitInfoKHR timeline_submit_info_ = {};  // Must persist (submit_info.pNext)
    VkSubmitInfo submit_info_ = {};  // Must persist for reuse
    // B5: multi-semaphore submit arrays (persist across async submit)
    VkSemaphore signal_semaphores_[2] = {};
    uint64_t    signal_values_[2]     = {};
    static constexpr uint64_t FRAME_INCREMENT = 1;
#ifdef _WIN32
    // D3: Keyed mutex info as member — pointers into these fields must outlive vkQueueSubmit
    VkWin32KeyedMutexAcquireReleaseInfoKHR keyed_mutex_info_ = {};
    uint64_t       km_acquire_key_ = 1;
    uint64_t       km_release_key_ = 0;
    uint32_t       km_timeout_     = UINT32_MAX;
    VkDeviceMemory km_memory_      = VK_NULL_HANDLE;
#endif

    // Extension function pointers (loaded once in init()).
    // Both resolved via vkGetDeviceProcAddr rather than called directly.
    PFN_vkGetSemaphoreCounterValueKHR pfn_get_semaphore_counter_value_ = nullptr;
    PFN_vkWaitSemaphores              pfn_wait_semaphores_             = nullptr;

    std::atomic<uint32_t> last_used_slot_{0};

    // V8/I6: Lifecycle guard. shutdown() sets this false BEFORE tearing down
    // device_/pfn_wait_semaphores_. is_copy_ready() checks it first as a cheap,
    // handle-independent early-out. SEH only catches an access violation once a
    // stale/null device_ is dereferenced; if the freed VkDevice memory happens to
    // look valid, the driver call is silent UB that SEH cannot see. The atomic
    // flag closes that window from the caller side without depending on handle state.
    std::atomic<bool> alive_{true};

    bool use_blit_ = true;

    uint32_t dispatch_x_ = 1;
    uint32_t dispatch_y_ = 1;
    uint32_t graphics_queue_family_ = 0;  // E4: external acquire/release barrier için

    void cleanup_pipeline();
};
