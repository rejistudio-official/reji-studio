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

    // Validation layer — Debug build'de etkin; runtime'da availability kontrol edilir
    const layers = [_][*:0]const u8{"VK_LAYER_KHRONOS_validation"};
    var layer_count: u32 = 0;
    if (builtin.mode == .Debug) {
        var avail_count: u32 = 0;
        _ = vk.vkEnumerateInstanceLayerProperties(&avail_count, null);
        var avail_layers: [64]vk.VkLayerProperties = undefined;
        var actual_avail: u32 = @min(avail_count, 64);
        _ = vk.vkEnumerateInstanceLayerProperties(&actual_avail, &avail_layers);
        for (avail_layers[0..actual_avail]) |lp| {
            if (std.mem.eql(u8,
                std.mem.sliceTo(&lp.layerName, 0),
                "VK_LAYER_KHRONOS_validation"))
            {
                layer_count = 1;
                break;
            }
        }
        if (layer_count == 0)
            std.debug.print("[VulkanZig] VK_LAYER_KHRONOS_validation bulunamadi, atlanıyor\n", .{});
    }

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

// ── select_device ─────────────────────────────────────────────────────────────

fn select_device() bool {
    var device_count: u32 = 0;
    _ = vk.vkEnumeratePhysicalDevices(state.instance, &device_count, null);
    if (device_count == 0) {
        std.debug.print("[VulkanZig] No GPU found\n", .{});
        return false;
    }

    var devices: [8]vk.VkPhysicalDevice = undefined;
    var actual: u32 = @min(device_count, 8);
    _ = vk.vkEnumeratePhysicalDevices(state.instance, &actual, &devices);

    var best: vk.VkPhysicalDevice = null;
    var best_score: u32 = 0;

    for (devices[0..actual]) |dev| {
        var props: vk.VkPhysicalDeviceProperties = undefined;
        vk.vkGetPhysicalDeviceProperties(dev, &props);

        var score: u32 = 1;
        if (props.vendorID == 0x1002) score = 100; // AMD iGPU öncelikli
        if (props.deviceType == vk.VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 10;

        if (score > best_score) {
            best_score = score;
            best = dev;
            state.vendor_id = props.vendorID;
        }
    }

    state.physical_device = best;

    var qf_count: u32 = 0;
    vk.vkGetPhysicalDeviceQueueFamilyProperties(best, &qf_count, null);

    var qf_props: [16]vk.VkQueueFamilyProperties = undefined;
    var qf_actual: u32 = @min(qf_count, 16);
    vk.vkGetPhysicalDeviceQueueFamilyProperties(best, &qf_actual, &qf_props);

    for (qf_props[0..qf_actual], 0..) |qf, i| {
        if (qf.queueFlags & vk.VK_QUEUE_GRAPHICS_BIT != 0) {
            state.graphics_queue_family = @intCast(i);
            break;
        }
    }

    std.debug.print("[VulkanZig] Selected: vendorID=0x{X:0>4}\n", .{state.vendor_id});
    return true;
}

// ── check_extension_available ─────────────────────────────────────────────────

fn check_extension_available(name: [*:0]const u8) bool {
    var count: u32 = 0;
    _ = vk.vkEnumerateDeviceExtensionProperties(
        state.physical_device, null, &count, null);
    var props: [256]vk.VkExtensionProperties = undefined;
    var actual: u32 = @min(count, 256);
    _ = vk.vkEnumerateDeviceExtensionProperties(
        state.physical_device, null, &actual, &props);
    for (props[0..actual]) |p| {
        if (std.mem.eql(u8,
            std.mem.sliceTo(&p.extensionName, 0),
            std.mem.sliceTo(name, 0))) return true;
    }
    return false;
}

// ── create_device ─────────────────────────────────────────────────────────────

fn create_device() bool {
    const priority: f32 = 1.0;
    const queue_create_info = vk.VkDeviceQueueCreateInfo{
        .sType            = vk.VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext            = null,
        .flags            = 0,
        .queueFamilyIndex = state.graphics_queue_family,
        .queueCount       = 1,
        .pQueuePriorities = &priority,
    };

    var timeline_features = vk.VkPhysicalDeviceTimelineSemaphoreFeatures{
        .sType             = vk.VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
        .pNext             = null,
        .timelineSemaphore = vk.VK_TRUE,
    };

    // 5 zorunlu + 1 opsiyonel (keyed mutex) — fixed buffer, allocator yok
    var ext_buf: [6][*:0]const u8 = undefined;
    var ext_n: u32 = 0;
    ext_buf[ext_n] = vk.VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME;           ext_n += 1;
    ext_buf[ext_n] = vk.VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;     ext_n += 1;
    ext_buf[ext_n] = vk.VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME;        ext_n += 1;
    ext_buf[ext_n] = vk.VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;  ext_n += 1;
    ext_buf[ext_n] = vk.VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME;        ext_n += 1;
    if (check_extension_available(vk.VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME)) {
        ext_buf[ext_n] = vk.VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME;     ext_n += 1;
        state.use_keyed_mutex = true;
    }

    const device_create_info = vk.VkDeviceCreateInfo{
        .sType                   = vk.VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext                   = &timeline_features,
        .flags                   = 0,
        .queueCreateInfoCount    = 1,
        .pQueueCreateInfos       = &queue_create_info,
        .enabledLayerCount       = 0,
        .ppEnabledLayerNames     = null,
        .enabledExtensionCount   = ext_n,
        .ppEnabledExtensionNames = @ptrCast(&ext_buf),
        .pEnabledFeatures        = null,
    };

    const result = vk.vkCreateDevice(
        state.physical_device,
        &device_create_info,
        null,
        &state.device,
    );
    if (result != vk.VK_SUCCESS) {
        std.debug.print("[VulkanZig] vkCreateDevice failed: {}\n", .{result});
        return false;
    }

    vk.vkGetDeviceQueue(
        state.device,
        state.graphics_queue_family,
        0,
        &state.graphics_queue,
    );

    std.debug.print("[VulkanZig] Device created, keyed_mutex={}\n",
        .{state.use_keyed_mutex});
    return true;
}

// ── Export'lar ────────────────────────────────────────────────────────────────

pub export fn vulkan_init_initialize() bool {
    if (state.initialized) return true;
    if (!create_instance()) return false;
    if (!select_device()) return false;
    if (!create_device()) return false;
    state.initialized = true;
    return true;
}

pub export fn vulkan_init_shutdown() void {
    if (state.device != null) {
        vk.vkDestroyDevice(state.device, null);
        state.device = null;
    }
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

pub export fn vulkan_init_instance() vk.VkInstance {
    return state.instance;
}

pub export fn vulkan_init_graphics_queue() vk.VkQueue {
    return state.graphics_queue;
}

pub export fn vulkan_init_has_extension(name: [*:0]const u8) bool {
    return check_extension_available(name);
}

// ── ABI doğrulama ─────────────────────────────────────────────────────────────

comptime {
    std.debug.assert(@sizeOf(vk.VkInstance)       == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkPhysicalDevice) == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkDevice)         == @sizeOf(*anyopaque));
    std.debug.assert(@sizeOf(vk.VkQueue)          == @sizeOf(*anyopaque));
}
