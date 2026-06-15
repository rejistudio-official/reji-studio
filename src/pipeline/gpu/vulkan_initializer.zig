// src/pipeline/gpu/vulkan_initializer.zig
//
// Faz 2 Pilot — VulkanInitializer Zig implementasyonu
// C++ singleton (vulkan_initializer.cpp) için Zig karşılığı.
//
// Derleme:
//   zig build gpu-check -Dvulkan-sdk=C:/VulkanSDK/1.4.350.0

const std     = @import("std");
const builtin = @import("builtin");

const vk = @cImport({
    @cDefine("VK_USE_PLATFORM_WIN32_KHR", "1");
    @cInclude("vulkan/vulkan.h");
});

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

// ── create_instance ───────────────────────────────────────────────────────────

fn create_instance() bool {
    const app_info = vk.VkApplicationInfo{
        .sType              = vk.VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext              = null,
        .pApplicationName   = "Reji Studio",
        .applicationVersion = vk.VK_MAKE_VERSION(0, 5, 2),
        .pEngineName        = "RejiEngine",
        .engineVersion      = vk.VK_MAKE_VERSION(0, 5, 2),
        .apiVersion         = vk.VK_API_VERSION_1_3,
    };

    // Validation layer — yalnızca Debug build'de etkin
    const layers = [_][*:0]const u8{
        "VK_LAYER_KHRONOS_validation",
    };
    const layer_count: u32 = if (builtin.mode == .Debug) 1 else 0;

    const extensions = [_][*:0]const u8{
        vk.VK_KHR_SURFACE_EXTENSION_NAME,
        vk.VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
        vk.VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
        vk.VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    };

    const create_info = vk.VkInstanceCreateInfo{
        .sType                   = vk.VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext                   = null,
        .flags                   = 0,
        .pApplicationInfo        = &app_info,
        .enabledLayerCount       = layer_count,
        .ppEnabledLayerNames     = if (layer_count > 0) @ptrCast(&layers) else null,
        .enabledExtensionCount   = @intCast(extensions.len),
        .ppEnabledExtensionNames = @ptrCast(&extensions),
    };

    const result = vk.vkCreateInstance(&create_info, null, &state.instance);
    if (result != vk.VK_SUCCESS) {
        std.debug.print("[VulkanZig] vkCreateInstance failed: {}\n", .{result});
        return false;
    }
    std.debug.print("[VulkanZig] Instance created\n", .{});
    return true;
}

// ── Export'lar ────────────────────────────────────────────────────────────────

pub export fn vulkan_init_get() *State {
    return &state;
}

pub export fn vulkan_init_initialize() bool {
    if (state.initialized) return true;
    if (!create_instance()) return false;
    // TODO: Faz 2 devamı — select_device, create_device
    state.initialized = true;
    return true;
}

pub export fn vulkan_init_shutdown() void {
    if (state.instance != null) {
        vk.vkDestroyInstance(state.instance, null);
        state.instance = null;
    }
    state.initialized = false;
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

comptime {
    std.debug.assert(@sizeOf(vk.VkInstance)       == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkPhysicalDevice) == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkDevice)         == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkQueue)          == @sizeOf(*anyopaque));
}
