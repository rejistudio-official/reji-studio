// src/pipeline/gpu/external_memory_bridge.zig
//
// Faz 2 Pilot — ExternalMemoryBridge Zig implementasyonu
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

// UYARI: Bu değer src/pipeline/include/reji_constants.h içindeki
// REJI_POOL_SIZE ve rj::constants::kGpuPoolSize ile senkron olmalı.
// Değiştirirsen copy_optimizer.h ve external_memory_bridge.h'ı da güncelle.
const POOL_SIZE: u32 = 3;

const ImageResult = struct {
    image:  vk.VkImage,
    memory: vk.VkDeviceMemory,
};

// IDXGIResource IID: {035F3AB4-482E-4E50-AAAD-7D841586441A}
const IID_IDXGIResource = w32.GUID{
    .Data1 = 0x035F3AB4,
    .Data2 = 0x482E,
    .Data3 = 0x4E50,
    .Data4 = .{ 0xAA, 0xAD, 0x7D, 0x84, 0x15, 0x86, 0x44, 0x1A },
};
// IDXGIResource1 IID: {30961379-4609-4A41-998E-54FE567EE0C1}
const IID_IDXGIResource1 = w32.GUID{
    .Data1 = 0x30961379,
    .Data2 = 0x4609,
    .Data3 = 0x4A41,
    .Data4 = .{ 0x99, 0x8E, 0x54, 0xFE, 0x56, 0x7E, 0xE0, 0xC1 },
};

// ── State ─────────────────────────────────────────────────────────────────────

const PoolSlot = struct {
    image:  vk.VkImage        = null,
    memory: vk.VkDeviceMemory = null,
};

const State = struct {
    device:          vk.VkDevice         = null,
    physical_device: vk.VkPhysicalDevice = null,
    format:          vk.VkFormat         = vk.VK_FORMAT_UNDEFINED,
    width:           u32                 = 0,
    height:          u32                 = 0,
    image_pool:      [POOL_SIZE]PoolSlot = [1]PoolSlot{.{}} ** POOL_SIZE,
    gl_target_sizes:    [POOL_SIZE]u64                    = [1]u64{0} ** POOL_SIZE,
    gl_target_images:  [POOL_SIZE]vk.VkImage             = [1]vk.VkImage{null} ** POOL_SIZE,
    gl_target_memory:  [POOL_SIZE]vk.VkDeviceMemory      = [1]vk.VkDeviceMemory{null} ** POOL_SIZE,
    gl_target_handles: [POOL_SIZE]?*anyopaque             = [1]?*anyopaque{null} ** POOL_SIZE,
    gl_sync_semaphores: [POOL_SIZE]vk.VkSemaphore        = [1]vk.VkSemaphore{null} ** POOL_SIZE,
    gl_sync_handles:    [POOL_SIZE]?*anyopaque      = [1]?*anyopaque{null} ** POOL_SIZE,
    d3d11_nt_handles:   [POOL_SIZE]?*anyopaque      = [1]?*anyopaque{null} ** POOL_SIZE,
    cached_texture_ptr: ?*anyopaque                 = null,
    shutdown_called: std.atomic.Value(bool) =
        std.atomic.Value(bool).init(false),
};

var state: State = .{};
var ext_bridge_initialized: bool = false;

const PFN_glDeleteMemoryObjects = ?*const fn(
    u32, [*]const u32) callconv(.c) void;

var gl_delete_memory_objects: PFN_glDeleteMemoryObjects = null;
var gl_memory_objects: [POOL_SIZE]u32 = .{0} ** POOL_SIZE;

// ── Format eşleme ─────────────────────────────────────────────────────────────

fn dxgi_to_vk_format(dxgi: w32.DXGI_FORMAT) vk.VkFormat {
    return switch (dxgi) {
        w32.DXGI_FORMAT_B8G8R8A8_UNORM    => vk.VK_FORMAT_B8G8R8A8_UNORM,
        w32.DXGI_FORMAT_R8G8B8A8_UNORM    => vk.VK_FORMAT_R8G8B8A8_UNORM,
        w32.DXGI_FORMAT_R10G10B10A2_UNORM => vk.VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        else                               => vk.VK_FORMAT_UNDEFINED,
    };
}

// ── Yardımcı: Uygun memory type bul ──────────────────────────────────────────

fn find_memory_type(
    mem_reqs: vk.VkMemoryRequirements,
    props: vk.VkMemoryPropertyFlags) u32 {
    var mem_props: vk.VkPhysicalDeviceMemoryProperties = undefined;
    vk.vkGetPhysicalDeviceMemoryProperties(
        state.physical_device, &mem_props);

    var i: u32 = 0;
    while (i < mem_props.memoryTypeCount) : (i += 1) {
        const type_bit = @as(u32, 1) << @intCast(i);
        const type_ok = mem_reqs.memoryTypeBits & type_bit != 0;
        const prop_ok = mem_props.memoryTypes[i].propertyFlags & props == props;
        if (type_ok and prop_ok) return i;
    }
    return 0;
}

// ── Yardımcı: Vulkan image oluştur ────────────────────────────────────────────

fn create_vulkan_image(
    width:  u32,
    height: u32,
    format: vk.VkFormat,
    handle: ?*anyopaque,
) !ImageResult {

    // External memory import info
    var import_info = vk.VkImportMemoryWin32HandleInfoKHR{
        .sType      = vk.VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
        .pNext      = null,
        .handleType = vk.VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
        .handle     = handle,
        .name       = null,
    };

    // Dedicated allocation (image sonra set edilecek)
    var dedicated = vk.VkMemoryDedicatedAllocateInfo{
        .sType  = vk.VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .pNext  = &import_info,
        .image  = null,
        .buffer = null,
    };

    var ext_image_info = vk.VkExternalMemoryImageCreateInfo{
        .sType       = vk.VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext       = null,
        .handleTypes = vk.VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
    };

    var image_info = vk.VkImageCreateInfo{
        .sType                 = vk.VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext                 = &ext_image_info,
        .flags                 = 0,
        .imageType             = vk.VK_IMAGE_TYPE_2D,
        .format                = format,
        .extent                = .{ .width = width, .height = height, .depth = 1 },
        .mipLevels             = 1,
        .arrayLayers           = 1,
        .samples               = vk.VK_SAMPLE_COUNT_1_BIT,
        .tiling                = vk.VK_IMAGE_TILING_OPTIMAL,
        .usage                 = vk.VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode           = vk.VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices   = null,
        .initialLayout         = vk.VK_IMAGE_LAYOUT_UNDEFINED,
    };

    var image: vk.VkImage = null;
    if (vk.vkCreateImage(state.device, &image_info, null, &image) != vk.VK_SUCCESS) {
        return error.ImageCreateFailed;
    }

    // Memory requirements
    var mem_reqs: vk.VkMemoryRequirements = undefined;
    vk.vkGetImageMemoryRequirements(state.device, image, &mem_reqs);
    dedicated.image = image;

    // Allocate
    var alloc_info = vk.VkMemoryAllocateInfo{
        .sType           = vk.VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext           = &dedicated,
        .allocationSize  = mem_reqs.size,
        .memoryTypeIndex = find_memory_type(mem_reqs, vk.VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
    };

    var memory: vk.VkDeviceMemory = null;
    if (vk.vkAllocateMemory(state.device, &alloc_info, null, &memory) != vk.VK_SUCCESS) {
        vk.vkDestroyImage(state.device, image, null);
        return error.MemoryAllocFailed;
    }

    _ = vk.vkBindImageMemory(state.device, image, memory, 0);
    return .{ .image = image, .memory = memory };
}

// ── Export'lar ────────────────────────────────────────────────────────────────

pub export fn ext_bridge_init(
    device: vk.VkDevice,
    phys:   vk.VkPhysicalDevice,
) bool {
    if (device == null or phys == null) return false;
    if (ext_bridge_initialized) {
        std.debug.print("[ExternalMemoryBridge] WARNING: init() called twice — " ++
            "global state will be overwritten, this is not multi-instance safe\n", .{});
        return true; // mevcut idempotency korunuyor
    }
    ext_bridge_initialized = true;
    state.device          = device;
    state.physical_device = phys;
    return true;
}

pub export fn ext_bridge_set_device(
    device: vk.VkDevice,
    phys:   vk.VkPhysicalDevice,
) void {
    state.device          = device;
    state.physical_device = phys;
}

fn create_vulkan_image_from_d3d11(
    tex:  ?*anyopaque,
    slot: u32,
) !ImageResult {
    const d3d_tex: *w32.ID3D11Texture2D =
        @ptrCast(@alignCast(tex orelse return error.NullTexture));

    // D3D11 texture boyut ve format bilgisi
    var desc: w32.D3D11_TEXTURE2D_DESC = undefined;
    d3d_tex.lpVtbl.*.GetDesc.?(d3d_tex, &desc);

    const vk_fmt = dxgi_to_vk_format(desc.Format);
    if (vk_fmt == vk.VK_FORMAT_UNDEFINED) return error.UnsupportedFormat;

    // IDXGIResource1 üzerinden NT handle al (legacy GetSharedHandle yerine)
    var resource1: ?*w32.IDXGIResource1 = null;
    const hr_qi = d3d_tex.lpVtbl.*.QueryInterface.?(
        d3d_tex,
        &IID_IDXGIResource1,
        @ptrCast(&resource1),
    );
    if (hr_qi < 0) return error.QueryInterfaceFailed;
    defer _ = resource1.?.lpVtbl.*.Release.?(resource1.?);

    var nt_handle: ?*anyopaque = null;
    const hr_sh = resource1.?.lpVtbl.*.CreateSharedHandle.?(
        resource1.?,
        null,
        w32.DXGI_SHARED_RESOURCE_READ | w32.DXGI_SHARED_RESOURCE_WRITE,
        null,
        &nt_handle,
    );
    if (hr_sh < 0) return error.SharedHandleFailed;
    if (nt_handle == null) return error.NullSharedHandle;
    // NT handle — invalidate_pool() içinde kapatılacak
    state.d3d11_nt_handles[slot] = nt_handle;

    // Vulkan image oluştur ve state'e yaz
    const result = try create_vulkan_image(desc.Width, desc.Height, vk_fmt, nt_handle);

    state.width  = desc.Width;
    state.height = desc.Height;
    state.format = vk_fmt;

    return result;
}

fn invalidate_pool() void {
    // B16: GL memory object'leri NT handle'lar kapanmadan önce sil
    if (gl_delete_memory_objects) |pfn| {
        pfn(@intCast(POOL_SIZE), &gl_memory_objects);
        gl_memory_objects = .{0} ** POOL_SIZE;
        gl_delete_memory_objects = null;
    }
    // GPU idle bekle — F4 dersi
    if (state.device != null) {
        _ = vk.vkDeviceWaitIdle(state.device);
    }
    // Önce image'lar, sonra memory — E14 dersi
    for (&state.image_pool) |*slot| {
        if (slot.image != null) {
            vk.vkDestroyImage(state.device, slot.image, null);
            slot.image = null;
        }
        if (slot.memory != null) {
            vk.vkFreeMemory(state.device, slot.memory, null);
            slot.memory = null;
        }
    }
    // D3D11 NT handle'larını kapat
    for (&state.d3d11_nt_handles) |*h| {
        if (h.*) |handle| {
            _ = std.os.windows.CloseHandle(handle);
            h.* = null;
        }
    }
    // GL target pool ve sync semaphore'lar D3D11 texture değişimine bağlı değil —
    // bunlar notify_vulkan_ready ile bir kez init edilir, yalnızca shutdown'da temizlenir.
    state.cached_texture_ptr = null;
}

pub export fn ext_bridge_set_gl_memory_objects(
    pfn:     PFN_glDeleteMemoryObjects,
    objects: [*]const u32,
    count:   u32,
) void {
    gl_delete_memory_objects = pfn;
    const n = @min(count, POOL_SIZE);
    for (0..n) |i| {
        gl_memory_objects[i] = objects[i];
    }
}

pub export fn ext_bridge_shutdown() void {
    // G9 dersi — atomic CAS ile double-shutdown önle
    if (state.shutdown_called.cmpxchgStrong(
            false, true,
            .acq_rel, .acquire) != null) return;

    invalidate_pool();

    // GL sync semaphore'lar: önce Win32 handle, sonra VkSemaphore
    for (&state.gl_sync_semaphores, &state.gl_sync_handles) |*sem, *h| {
        if (h.*) |handle| {
            _ = std.os.windows.CloseHandle(handle);
            h.* = null;
        }
        if (sem.* != null) {
            vk.vkDestroySemaphore(state.device, sem.*, null);
            sem.* = null;
        }
    }

    // GL target pool: önce image, sonra memory, sonra NT handle
    for (0..POOL_SIZE) |i| {
        if (state.gl_target_images[i] != null) {
            vk.vkDestroyImage(state.device, state.gl_target_images[i], null);
            state.gl_target_images[i] = null;
        }
        if (state.gl_target_memory[i] != null) {
            vk.vkFreeMemory(state.device, state.gl_target_memory[i], null);
            state.gl_target_memory[i] = null;
        }
        if (state.gl_target_handles[i]) |h| {
            _ = std.os.windows.CloseHandle(h);
            state.gl_target_handles[i] = null;
        }
    }

    state.device          = null;
    state.physical_device = null;
}

pub export fn ext_bridge_get_frame_images(
    tex:         ?*anyopaque,
    slot:        u32,
    staging_out: *vk.VkImage,
    target_out:  *vk.VkImage,
) bool {
    if (state.device == null) return false;
    if (slot >= POOL_SIZE) return false;

    // E5 dersi — texture pointer değişti mi kontrol et
    if (tex != state.cached_texture_ptr) {
        // F4 dersi — invalidate öncesi GPU idle
        invalidate_pool();
        state.cached_texture_ptr = tex;

        // Yeni texture'dan image oluştur
        const result = create_vulkan_image_from_d3d11(
            @ptrCast(tex), slot) catch {
            std.debug.print(
                "[ExtBridgeZig] image create failed\n", .{});
            return false;
        };
        // Tüm slotlara aynı image — C++ versiyonuyla aynı davranış
        for (&state.image_pool, 0..) |*s, i| {
            s.image  = result.image;
            s.memory = result.memory;
            _ = i;
        }
    }

    staging_out.* = state.image_pool[slot].image;
    target_out.*  = state.gl_target_images[(slot + 1) % POOL_SIZE];
    return state.image_pool[slot].image != null;
}

// GL target size getter — G6 dersi
pub export fn ext_bridge_gl_target_size(slot: u32) u64 {
    if (slot >= POOL_SIZE) return 0;
    return state.gl_target_sizes[slot];
}

pub export fn ext_bridge_create_gl_sync_semaphores() bool {
    for (&state.gl_sync_semaphores,
         &state.gl_sync_handles, 0..) |*sem, *handle, i| {
        _ = i;

        var export_info = vk.VkExportSemaphoreCreateInfo{
            .sType = vk.VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
            .pNext = null,
            .handleTypes = vk.VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };
        var sem_info = vk.VkSemaphoreCreateInfo{
            .sType = vk.VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &export_info,
            .flags = 0,
        };
        if (vk.vkCreateSemaphore(state.device,
                &sem_info, null, sem) != vk.VK_SUCCESS) {
            return false;
        }

        // Win32 handle export
        var win32_info = vk.VkSemaphoreGetWin32HandleInfoKHR{
            .sType = vk.VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
            .pNext = null,
            .semaphore = sem.*,
            .handleType = vk.VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };
        // PFN lookup
        const pfn_get = @as(
            vk.PFN_vkGetSemaphoreWin32HandleKHR,
            @ptrCast(vk.vkGetDeviceProcAddr(
                state.device,
                "vkGetSemaphoreWin32HandleKHR")));
        if (pfn_get == null) return false;
        if (pfn_get.?(state.device,
                &win32_info, handle) != vk.VK_SUCCESS) {
            return false;
        }
    }
    std.debug.print(
        "[ExtBridgeZig] GL sync semaphores created\n", .{});
    return true;
}

pub export fn ext_bridge_get_gl_sync_handle(slot: u32) ?*anyopaque {
    if (slot >= POOL_SIZE) return null;
    return state.gl_sync_handles[slot];
}

pub export fn ext_bridge_get_staging_semaphore(slot: u32) vk.VkSemaphore {
    if (slot >= POOL_SIZE) return null;
    return state.gl_sync_semaphores[slot];
}

pub export fn ext_bridge_init_gl_target_pool(
    format: vk.VkFormat,
    width:  u32,
    height: u32,
) bool {
    state.format = format;
    state.width  = width;
    state.height = height;

    // PFN lookup
    const pfn_get_mem = @as(
        vk.PFN_vkGetMemoryWin32HandleKHR,
        @ptrCast(vk.vkGetDeviceProcAddr(
            state.device, "vkGetMemoryWin32HandleKHR")));
    if (pfn_get_mem == null) return false;

    for (0..POOL_SIZE) |i| {
        var export_mem_info = vk.VkExportMemoryAllocateInfo{
            .sType       = vk.VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
            .pNext       = null,
            .handleTypes = vk.VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };
        var ext_img_info = vk.VkExternalMemoryImageCreateInfo{
            .sType       = vk.VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
            .pNext       = null,
            .handleTypes = vk.VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };
        var img_info = vk.VkImageCreateInfo{
            .sType                 = vk.VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext                 = &ext_img_info,
            .flags                 = 0,
            .imageType             = vk.VK_IMAGE_TYPE_2D,
            .format                = format,
            .extent                = .{ .width = width, .height = height, .depth = 1 },
            .mipLevels             = 1,
            .arrayLayers           = 1,
            .samples               = vk.VK_SAMPLE_COUNT_1_BIT,
            .tiling                = vk.VK_IMAGE_TILING_OPTIMAL,
            .usage                 = vk.VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     vk.VK_IMAGE_USAGE_SAMPLED_BIT      |
                                     vk.VK_IMAGE_USAGE_STORAGE_BIT,
            .sharingMode           = vk.VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices   = null,
            .initialLayout         = vk.VK_IMAGE_LAYOUT_UNDEFINED,
        };

        var image: vk.VkImage = null;
        if (vk.vkCreateImage(state.device, &img_info, null, &image) != vk.VK_SUCCESS) {
            // B13 rollback — önceki slotları temizle
            for (0..i) |j| {
                if (state.gl_target_images[j] != null) {
                    vk.vkDestroyImage(state.device, state.gl_target_images[j], null);
                    state.gl_target_images[j] = null;
                }
                if (state.gl_target_memory[j] != null) {
                    vk.vkFreeMemory(state.device, state.gl_target_memory[j], null);
                    state.gl_target_memory[j] = null;
                }
            }
            return false;
        }
        state.gl_target_images[i] = image;

        // Memory requirements ve G6: exact size kaydet
        var mem_reqs: vk.VkMemoryRequirements = undefined;
        vk.vkGetImageMemoryRequirements(state.device, image, &mem_reqs);
        state.gl_target_sizes[i] = mem_reqs.size;

        var dedicated = vk.VkMemoryDedicatedAllocateInfo{
            .sType  = vk.VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .pNext  = &export_mem_info,
            .image  = image,
            .buffer = null,
        };
        var alloc_info = vk.VkMemoryAllocateInfo{
            .sType           = vk.VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext           = &dedicated,
            .allocationSize  = mem_reqs.size,
            .memoryTypeIndex = find_memory_type(mem_reqs, vk.VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
        };

        var memory: vk.VkDeviceMemory = null;
        if (vk.vkAllocateMemory(state.device, &alloc_info, null, &memory) != vk.VK_SUCCESS) {
            return false;
        }
        state.gl_target_memory[i] = memory;
        _ = vk.vkBindImageMemory(state.device, image, memory, 0);

        // NT handle export
        var handle_info = vk.VkMemoryGetWin32HandleInfoKHR{
            .sType      = vk.VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
            .pNext      = null,
            .memory     = memory,
            .handleType = vk.VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
        };
        var handle: ?*anyopaque = null;
        if (pfn_get_mem.?(state.device, &handle_info, &handle) != vk.VK_SUCCESS) {
            return false;
        }
        state.gl_target_handles[i] = handle;
    }

    std.debug.print(
        "[ExtBridgeZig] GL target pool: {}x{}\n",
        .{ width, height });
    return true;
}

pub export fn ext_bridge_get_gl_target_handle(slot: u32) ?*anyopaque {
    if (slot >= POOL_SIZE) return null;
    return state.gl_target_handles[slot];
}

pub export fn ext_bridge_get_gl_target_image(slot: u32) vk.VkImage {
    if (slot >= POOL_SIZE) return null;
    return state.gl_target_images[slot];
}

pub export fn ext_bridge_get_pooled_image(frame_idx: u32) vk.VkImage {
    const slot = frame_idx % POOL_SIZE;
    return state.image_pool[slot].image;
}

pub export fn ext_bridge_get_staging_memory(image: vk.VkImage) vk.VkDeviceMemory {
    for (&state.image_pool) |*slot| {
        if (slot.image == image) return slot.memory;
    }
    return null;
}

// ── ABI doğrulama ─────────────────────────────────────────────────────────────

comptime {
    std.debug.assert(POOL_SIZE == 3);
}
