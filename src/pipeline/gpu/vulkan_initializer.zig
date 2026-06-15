// src/pipeline/gpu/vulkan_initializer.zig
//
// Faz 2 Pilot — VulkanInitializer Zig iskeleti
// C++ singleton (vulkan_initializer.cpp) için Zig karşılığı.
// Bu dosya yalnızca yapıyı kurar; implementasyon Faz 2'de gelecek.
//
// Derleme:
//   zig build gpu-check -Dvulkan-sdk=C:/VulkanSDK/1.3.290.0

const std = @import("std");
const vk = @cImport(@cInclude("vulkan/vulkan.h"));

// ── Singleton state ───────────────────────────────────────────────────────────

const State = struct {
    instance:               vk.VkInstance        = null,
    physical_device:        vk.VkPhysicalDevice  = null,
    device:                 vk.VkDevice          = null,
    graphics_queue:         vk.VkQueue           = null,
    graphics_queue_family:  u32                  = 0,
    vendor_id:              u32                  = 0,
    use_keyed_mutex:        bool                 = false,
    initialized:            bool                 = false,
};

var state: State = .{};

// ── C++ singleton API karşılığı export'lar ────────────────────────────────────

pub export fn vulkan_init_get() *State {
    return &state;
}

pub export fn vulkan_init_initialize() bool {
    if (state.initialized) return true;
    // TODO: Faz 2 — create_instance, select_device, create_device
    return false;
}

pub export fn vulkan_init_shutdown() void {
    // TODO: Faz 2 — vkDestroyDevice, vkDestroyInstance, debug_messenger
    _ = state;
}

pub export fn vulkan_init_use_keyed_mutex() bool {
    return state.use_keyed_mutex;
}

pub export fn vulkan_init_vendor_id() u32 {
    return state.vendor_id;
}

pub export fn vulkan_init_device() vk.VkDevice {
    return state.device;
}

pub export fn vulkan_init_physical_device() vk.VkPhysicalDevice {
    return state.physical_device;
}

pub export fn vulkan_init_graphics_queue_family() u32 {
    return state.graphics_queue_family;
}

// ── ABI doğrulama ─────────────────────────────────────────────────────────────
// Vulkan dispatchable handle'ları opaque pointer — 64-bit'te *anyopaque boyutunda olmalı.

comptime {
    std.debug.assert(@sizeOf(vk.VkInstance)       == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkPhysicalDevice) == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkDevice)         == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkQueue)          == @sizeOf(*anyopaque));
}
