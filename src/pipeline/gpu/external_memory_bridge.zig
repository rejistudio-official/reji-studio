// src/pipeline/gpu/external_memory_bridge.zig
//
// Faz 2 Pilot — ExternalMemoryBridge Zig iskelet
// C++ implementasyonu (external_memory_bridge.cpp) için Zig karşılığı.
//
// Derleme:
//   zig build ext-bridge-check -Dvulkan-sdk=C:/VulkanSDK/1.4.350.0

const std = @import("std");

const vk = @cImport({
    @cDefine("VK_USE_PLATFORM_WIN32_KHR", "1");
    @cInclude("vulkan/vulkan.h");
});
const w32 = @cImport({
    @cInclude("d3d11_1.h");
    @cInclude("dxgi1_2.h");
});

// ── Sabitler ──────────────────────────────────────────────────────────────────

const POOL_SIZE: u32 = 3;

// ── State ─────────────────────────────────────────────────────────────────────

const PoolSlot = struct {
    image:     vk.VkImage        = null,
    memory:    vk.VkDeviceMemory = null,
    gl_handle: ?*anyopaque       = null,
};

const State = struct {
    device:          vk.VkDevice         = null,
    physical_device: vk.VkPhysicalDevice = null,
    format:          vk.VkFormat         = vk.VK_FORMAT_UNDEFINED,
    width:           u32                 = 0,
    height:          u32                 = 0,
    image_pool:      [POOL_SIZE]PoolSlot = [1]PoolSlot{.{}} ** POOL_SIZE,
    gl_target_sizes: [POOL_SIZE]u64      = [1]u64{0} ** POOL_SIZE,
    cached_texture_ptr: ?*anyopaque      = null,
    shutdown_called: std.atomic.Value(bool) =
        std.atomic.Value(bool).init(false),
};

var state: State = .{};

// ── Export'lar ────────────────────────────────────────────────────────────────

pub export fn ext_bridge_init(
    device: vk.VkDevice,
    phys:   vk.VkPhysicalDevice,
) bool {
    state.device          = device;
    state.physical_device = phys;
    return true;
}

pub export fn ext_bridge_shutdown() void {
    // TODO
}

pub export fn ext_bridge_get_frame_images(
    tex:         ?*anyopaque,
    staging_out: *vk.VkImage,
    target_out:  *vk.VkImage,
) bool {
    _ = tex;
    _ = staging_out;
    _ = target_out;
    return false; // TODO
}

// ── ABI doğrulama ─────────────────────────────────────────────────────────────

comptime {
    std.debug.assert(POOL_SIZE == 3);
}
