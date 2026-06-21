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
    gl_target_sizes:    [POOL_SIZE]u64              = [1]u64{0} ** POOL_SIZE,
    gl_sync_semaphores: [POOL_SIZE]vk.VkSemaphore  = [1]vk.VkSemaphore{null} ** POOL_SIZE,
    gl_sync_handles:    [POOL_SIZE]?*anyopaque      = [1]?*anyopaque{null} ** POOL_SIZE,
    cached_texture_ptr: ?*anyopaque                 = null,
    shutdown_called: std.atomic.Value(bool) =
        std.atomic.Value(bool).init(false),
};

var state: State = .{};

// ── Format eşleme ─────────────────────────────────────────────────────────────

fn dxgi_to_vk_format(dxgi: w32.DXGI_FORMAT) vk.VkFormat {
    return switch (dxgi) {
        w32.DXGI_FORMAT_B8G8R8A8_UNORM    => vk.VK_FORMAT_B8G8R8A8_UNORM,
        w32.DXGI_FORMAT_R8G8B8A8_UNORM    => vk.VK_FORMAT_R8G8B8A8_UNORM,
        w32.DXGI_FORMAT_R10G10B10A2_UNORM => vk.VK_FORMAT_A2B10G10R10_UNORM_PACK32,
        else                               => vk.VK_FORMAT_UNDEFINED,
    };
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
        .memoryTypeIndex = 0, // import için 0 yeterli
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
    if (state.device != null) return true; // zaten başlatıldı
    state.device          = device;
    state.physical_device = phys;
    return true;
}

fn create_vulkan_image_from_d3d11(
    tex: ?*anyopaque,
) !ImageResult {
    const d3d_tex: *w32.ID3D11Texture2D =
        @ptrCast(@alignCast(tex orelse return error.NullTexture));

    // D3D11 texture boyut ve format bilgisi
    var desc: w32.D3D11_TEXTURE2D_DESC = undefined;
    d3d_tex.lpVtbl.*.GetDesc.?(d3d_tex, &desc);

    const vk_fmt = dxgi_to_vk_format(desc.Format);
    if (vk_fmt == vk.VK_FORMAT_UNDEFINED) return error.UnsupportedFormat;

    // IDXGIResource üzerinden shared handle al
    var dxgi_res: ?*w32.IDXGIResource = null;
    const hr = d3d_tex.lpVtbl.*.QueryInterface.?(
        d3d_tex,
        &IID_IDXGIResource,
        @ptrCast(&dxgi_res),
    );
    if (hr != 0 or dxgi_res == null) return error.QueryInterfaceFailed;
    defer _ = dxgi_res.?.lpVtbl.*.Release.?(dxgi_res.?);

    var shared_handle: w32.HANDLE = null;
    if (dxgi_res.?.lpVtbl.*.GetSharedHandle.?(dxgi_res.?, &shared_handle) != 0)
        return error.GetSharedHandleFailed;
    if (shared_handle == null) return error.NullSharedHandle;

    // Vulkan image oluştur ve state'e yaz
    const result = try create_vulkan_image(desc.Width, desc.Height, vk_fmt, shared_handle);

    state.width  = desc.Width;
    state.height = desc.Height;
    state.format = vk_fmt;

    return result;
}

fn invalidate_pool() void {
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
    // NT handle'ları kapat
    for (&state.image_pool) |*slot| {
        if (slot.gl_handle) |h| {
            _ = std.os.windows.CloseHandle(h);
            slot.gl_handle = null;
        }
    }
    // GL sync semaphore'ları yok et
    for (&state.gl_sync_semaphores) |*sem| {
        if (sem.* != null) {
            vk.vkDestroySemaphore(state.device, sem.*, null);
            sem.* = null;
        }
    }
    state.cached_texture_ptr = null;
}

pub export fn ext_bridge_shutdown() void {
    // G9 dersi — atomic CAS ile double-shutdown önle
    if (state.shutdown_called.cmpxchgStrong(
            false, true,
            .acq_rel, .acquire) != null) return;

    invalidate_pool();

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
            @ptrCast(tex)) catch {
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
    target_out.*  = state.image_pool[slot].image;
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

// ── ABI doğrulama ─────────────────────────────────────────────────────────────

comptime {
    std.debug.assert(POOL_SIZE == 3);
}
