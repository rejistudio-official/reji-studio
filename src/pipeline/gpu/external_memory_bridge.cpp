#include "external_memory_bridge.h"
#include <cstdio>

#ifndef REJI_VULKAN_MOCK
#endif

namespace rj::pipeline::gpu {

ExternalMemoryBridge::ExternalMemoryBridge(VkDevice device, VkPhysicalDevice physical_device)
    : device_(device), physical_device_(physical_device), format_(VK_FORMAT_UNDEFINED),
      width_(0), height_(0), pool_index_(0), cached_d3d11_handle_(nullptr),
      cached_texture_ptr_(nullptr) {}

void ExternalMemoryBridge::set_device(VkDevice device, VkPhysicalDevice phys_device) {
  device_ = device;
  physical_device_ = phys_device;
  fprintf(stderr, "[ExternalMemoryBridge] set_device: device=%p phys=%p\n",
          (void*)device_, (void*)physical_device_);
  fflush(stderr);
}

HANDLE ExternalMemoryBridge::export_d3d11_handle(ID3D11Texture2D* staging_texture) {
  if (!staging_texture) {
    fprintf(stderr, "[ExternalMemoryBridge] staging_texture is null\n");
    fflush(stderr);
    return nullptr;
  }

#ifdef REJI_VULKAN_MOCK
  fprintf(stderr, "[ExternalMemoryBridge] Mock mode: skipping handle export\n");
  fflush(stderr);
  return nullptr;
#endif

  Microsoft::WRL::ComPtr<IDXGIResource1> dxgi_resource;
  HRESULT hr = staging_texture->QueryInterface(__uuidof(IDXGIResource1), (void**)&dxgi_resource);
  if (FAILED(hr)) {
    fprintf(stderr, "[ExternalMemoryBridge] Failed to query IDXGIResource1: 0x%x\n", hr);
    fflush(stderr);
    return nullptr;
  }

  HANDLE nt_handle = nullptr;
  hr = dxgi_resource->CreateSharedHandle(
    nullptr,
    DXGI_SHARED_RESOURCE_READ,
    nullptr,
    &nt_handle
  );

  if (FAILED(hr)) {
    fprintf(stderr, "[ExternalMemoryBridge] CreateSharedHandle failed: 0x%x\n", hr);
    fflush(stderr);
    return nullptr;
  }

  fprintf(stderr, "[ExternalMemoryBridge] Exported NT handle: %p\n", nt_handle);
  fflush(stderr);
  return nt_handle;
}

VkImage ExternalMemoryBridge::create_vulkan_image_from_d3d11(
    HANDLE d3d11_handle,
    VkFormat format,
    uint32_t width,
    uint32_t height,
    uint32_t pool_idx) {

  if (!d3d11_handle) {
    fprintf(stderr, "[ExternalMemoryBridge] d3d11_handle is null\n");
    fflush(stderr);
    return nullptr;
  }

#ifdef REJI_VULKAN_MOCK
  fprintf(stderr, "[ExternalMemoryBridge] Mock mode: skipping Vulkan image creation\n");
  fflush(stderr);
  return nullptr;
#endif

  VkExternalMemoryImageCreateInfo ext_img_info{};
  ext_img_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  ext_img_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

  VkImageCreateInfo img_info{};
  img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  img_info.pNext = &ext_img_info;
  img_info.imageType = VK_IMAGE_TYPE_2D;
  img_info.format = format;
  img_info.extent = {width, height, 1};
  img_info.mipLevels = 1;
  img_info.arrayLayers = 1;
  img_info.samples = VK_SAMPLE_COUNT_1_BIT;
  img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  img_info.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage vk_img = nullptr;
  VkResult result = vkCreateImage(device_, &img_info, nullptr, &vk_img);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[ExternalMemoryBridge] vkCreateImage failed: %d\n", result);
    fflush(stderr);
    return nullptr;
  }

  VkMemoryRequirements mem_reqs;
  vkGetImageMemoryRequirements(device_, vk_img, &mem_reqs);

  VkImportMemoryWin32HandleInfoKHR import_info{};
  import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
  import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  import_info.handle = d3d11_handle;

  VkMemoryDedicatedAllocateInfo dedicated_alloc{};
  dedicated_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated_alloc.image = vk_img;
  dedicated_alloc.pNext = &import_info;

  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.pNext = &dedicated_alloc;
  alloc_info.allocationSize = mem_reqs.size;

  VkPhysicalDeviceMemoryProperties mem_props;
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

  uint32_t mem_type_idx = 0;
  bool found = false;
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; ++i) {
    if ((mem_reqs.memoryTypeBits & (1 << i)) &&
        (mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      mem_type_idx = i;
      found = true;
      break;
    }
  }

  if (!found) {
    fprintf(stderr, "[ExternalMemoryBridge] No suitable memory type found\n");
    fflush(stderr);
    vkDestroyImage(device_, vk_img, nullptr);
    return nullptr;
  }

  alloc_info.memoryTypeIndex = mem_type_idx;

  VkDeviceMemory vk_mem = nullptr;
  result = vkAllocateMemory(device_, &alloc_info, nullptr, &vk_mem);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[ExternalMemoryBridge] vkAllocateMemory failed: %d\n", result);
    fflush(stderr);
    vkDestroyImage(device_, vk_img, nullptr);
    return nullptr;
  }

  pool_memory_[pool_idx] = vk_mem;

  result = vkBindImageMemory(device_, vk_img, vk_mem, 0);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[ExternalMemoryBridge] vkBindImageMemory failed: %d\n", result);
    fflush(stderr);
    vkFreeMemory(device_, vk_mem, nullptr);
    vkDestroyImage(device_, vk_img, nullptr);
    return nullptr;
  }

  fprintf(stderr, "[ExternalMemoryBridge] Created Vulkan image from D3D11 handle\n");
  fflush(stderr);
  return vk_img;
}

bool ExternalMemoryBridge::initialize_image_pool(VkFormat format, uint32_t width, uint32_t height) {
  format_ = format;
  width_ = width;
  height_ = height;

  image_pool_.resize(POOL_SIZE);
  pool_memory_.resize(POOL_SIZE);

  fprintf(stderr, "[ExternalMemoryBridge] Image pool initialized (size: %d, %ux%u)\n",
          POOL_SIZE, width, height);
  fflush(stderr);

  return true;
}

bool ExternalMemoryBridge::initialize_gl_target_pool(
    VkFormat format, uint32_t width, uint32_t height) {

  gl_target_pool_.resize(POOL_SIZE);
  gl_target_memory_.resize(POOL_SIZE);

  // B13: rollback helper — frees already-created slots [0, count) on partial failure
  auto rollback = [&](uint32_t count) {
    for (uint32_t j = 0; j < count; ++j) {
      if (gl_target_pool_[j]) {
        vkDestroyImage(device_, gl_target_pool_[j], nullptr);
        gl_target_pool_[j] = nullptr;
      }
      if (gl_target_memory_[j]) {
        vkFreeMemory(device_, gl_target_memory_[j], nullptr);
        gl_target_memory_[j] = nullptr;
      }
      if (gl_target_handles_[j]) {
        CloseHandle(gl_target_handles_[j]);
        gl_target_handles_[j] = nullptr;
      }
    }
  };

  for (uint32_t i = 0; i < POOL_SIZE; ++i) {
    VkExternalMemoryImageCreateInfo ext_img_info{};
    ext_img_info.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    ext_img_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkImageCreateInfo img_info{};
    img_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    img_info.pNext = &ext_img_info;
    img_info.imageType = VK_IMAGE_TYPE_2D;
    img_info.format = format;
    img_info.extent = {width, height, 1};
    img_info.mipLevels = 1;
    img_info.arrayLayers = 1;
    img_info.samples = VK_SAMPLE_COUNT_1_BIT;
    img_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    img_info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;  // GL texture sampling
    img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkImage vk_img = nullptr;
    VkResult result = vkCreateImage(device_, &img_info, nullptr, &vk_img);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkCreateImage (GL target) failed: %d (slot %u)\n",
              result, i);
      fflush(stderr);
      rollback(i);
      return false;
    }

    VkMemoryRequirements mem_reqs;
    vkGetImageMemoryRequirements(device_, vk_img, &mem_reqs);
    gl_target_sizes_[i] = mem_reqs.size;  // G6: exact size for GL import

    VkExportMemoryAllocateInfo export_info{};
    export_info.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    VkMemoryDedicatedAllocateInfo dedicated_alloc{};
    dedicated_alloc.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_alloc.image = vk_img;
    dedicated_alloc.pNext = &export_info;

    VkMemoryAllocateInfo alloc_info{};
    alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    alloc_info.pNext = &dedicated_alloc;
    alloc_info.allocationSize = mem_reqs.size;

    VkPhysicalDeviceMemoryProperties mem_props;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &mem_props);

    uint32_t mem_type_idx = 0;
    bool found = false;
    for (uint32_t j = 0; j < mem_props.memoryTypeCount; ++j) {
      if ((mem_reqs.memoryTypeBits & (1 << j)) &&
          (mem_props.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
        mem_type_idx = j;
        found = true;
        break;
      }
    }

    if (!found) {
      fprintf(stderr, "[ExternalMemoryBridge] No suitable memory type (GL target, slot %u)\n", i);
      fflush(stderr);
      vkDestroyImage(device_, vk_img, nullptr);
      rollback(i);
      return false;
    }

    alloc_info.memoryTypeIndex = mem_type_idx;

    VkDeviceMemory vk_mem = nullptr;
    result = vkAllocateMemory(device_, &alloc_info, nullptr, &vk_mem);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkAllocateMemory (GL target) failed: %d (slot %u)\n",
              result, i);
      fflush(stderr);
      vkDestroyImage(device_, vk_img, nullptr);
      rollback(i);
      return false;
    }

    gl_target_memory_[i] = vk_mem;

    result = vkBindImageMemory(device_, vk_img, vk_mem, 0);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkBindImageMemory (GL target) failed: %d (slot %u)\n",
              result, i);
      fflush(stderr);
      vkFreeMemory(device_, vk_mem, nullptr);
      vkDestroyImage(device_, vk_img, nullptr);
      rollback(i);
      return false;
    }

    VkMemoryGetWin32HandleInfoKHR handle_info{};
    handle_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    handle_info.memory = vk_mem;
    handle_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    auto pfn_vkGetMemoryWin32HandleKHR =
        reinterpret_cast<PFN_vkGetMemoryWin32HandleKHR>(
            vkGetDeviceProcAddr(device_, "vkGetMemoryWin32HandleKHR"));
    if (!pfn_vkGetMemoryWin32HandleKHR) {
      fprintf(stderr, "[ExternalMemoryBridge] vkGetMemoryWin32HandleKHR not available\n");
      fflush(stderr);
      vkFreeMemory(device_, vk_mem, nullptr);
      vkDestroyImage(device_, vk_img, nullptr);
      rollback(i);
      return false;
    }
    result = pfn_vkGetMemoryWin32HandleKHR(device_, &handle_info, &gl_target_handles_[i]);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkGetMemoryWin32HandleKHR failed: %d (slot %u)\n",
              result, i);
      fflush(stderr);
      vkFreeMemory(device_, vk_mem, nullptr);
      vkDestroyImage(device_, vk_img, nullptr);
      rollback(i);
      return false;
    }

    gl_target_pool_[i] = vk_img;

    fprintf(stderr, "[ExternalMemoryBridge] GL target slot %u: image=%p, handle=%p\n",
            i, (void*)vk_img, gl_target_handles_[i]);
    fflush(stderr);
  }

  fprintf(stderr, "[ExternalMemoryBridge] GL target pool initialized: %u x %ux%u\n",
          POOL_SIZE, width, height);
  fflush(stderr);

  return true;
}

bool ExternalMemoryBridge::create_gl_sync_semaphore() {
  if (gl_sync_sem_pool_[0] != VK_NULL_HANDLE) {
    fprintf(stderr, "[ExternalMemoryBridge] gl_sync semaphore pool already created\n");
    return true;
  }

  auto pfn_vkGetSemaphoreWin32HandleKHR =
      reinterpret_cast<PFN_vkGetSemaphoreWin32HandleKHR>(
          vkGetDeviceProcAddr(device_, "vkGetSemaphoreWin32HandleKHR"));
  if (!pfn_vkGetSemaphoreWin32HandleKHR) {
    fprintf(stderr, "[ExternalMemoryBridge] vkGetSemaphoreWin32HandleKHR not available\n");
    fflush(stderr);
    return false;
  }

  VkExportSemaphoreCreateInfo export_info{};
  export_info.sType       = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO;
  export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

  VkSemaphoreCreateInfo sem_info{};
  sem_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  sem_info.pNext = &export_info;

  for (uint32_t i = 0; i < 3; ++i) {
    VkResult result = vkCreateSemaphore(device_, &sem_info, nullptr, &gl_sync_sem_pool_[i]);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkCreateSemaphore (gl_sync[%u]) failed: %d\n", i, result);
      fflush(stderr);
      for (uint32_t j = 0; j < i; ++j) {
        vkDestroySemaphore(device_, gl_sync_sem_pool_[j], nullptr);
        gl_sync_sem_pool_[j] = VK_NULL_HANDLE;
      }
      return false;
    }

    VkSemaphoreGetWin32HandleInfoKHR handle_info{};
    handle_info.sType      = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR;
    handle_info.semaphore  = gl_sync_sem_pool_[i];
    handle_info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;

    result = pfn_vkGetSemaphoreWin32HandleKHR(device_, &handle_info, &gl_sync_sem_handles_[i]);
    if (result != VK_SUCCESS) {
      fprintf(stderr, "[ExternalMemoryBridge] vkGetSemaphoreWin32HandleKHR (slot %u) failed: %d\n", i, result);
      fflush(stderr);
      for (uint32_t j = 0; j <= i; ++j) {
        vkDestroySemaphore(device_, gl_sync_sem_pool_[j], nullptr);
        gl_sync_sem_pool_[j] = VK_NULL_HANDLE;
      }
      return false;
    }

    fprintf(stderr, "[ExternalMemoryBridge] gl_sync_sem[%u] created: sem=%p handle=%p\n",
            i, (void*)gl_sync_sem_pool_[i], gl_sync_sem_handles_[i]);
    fflush(stderr);
  }
  return true;
}

HANDLE ExternalMemoryBridge::get_gl_sync_semaphore_handle(uint32_t slot) const {
  return slot < 3 ? gl_sync_sem_handles_[slot] : nullptr;
}

VkSemaphore ExternalMemoryBridge::get_gl_sync_semaphore(uint32_t slot) const {
  return slot < 3 ? gl_sync_sem_pool_[slot] : VK_NULL_HANDLE;
}

HANDLE ExternalMemoryBridge::get_gl_target_handle(uint32_t idx) const {
  if (idx >= POOL_SIZE) {
    return nullptr;
  }
  return gl_target_handles_[idx];
}

VkImage ExternalMemoryBridge::get_pooled_image(uint32_t frame_idx) {
  if (image_pool_.empty()) {
    fprintf(stderr, "[ExternalMemoryBridge] Image pool not initialized\n");
    fflush(stderr);
    return nullptr;
  }

  size_t pool_idx = frame_idx % POOL_SIZE;
  return image_pool_[pool_idx];
}

bool ExternalMemoryBridge::get_frame_images(
    ID3D11Texture2D* tex,
    VkImage* out_staging,
    VkImage* out_target) {

  if (!tex || !out_staging || !out_target) {
    fprintf(stderr, "[ExternalMemoryBridge] Invalid parameters\n");
    fflush(stderr);
    return false;
  }

  if (image_pool_.empty()) {
    fprintf(stderr, "[ExternalMemoryBridge] Image pool not initialized\n");
    fflush(stderr);
    return false;
  }

  // E5: texture identity check — resolution change / ACCESS_LOST sonrası texture değişirse
  //     stale NT handle ve pool slot'larını temizle, yeniden export et.
  if (tex != cached_texture_ptr_) {
    fprintf(stderr, "[ExternalMemoryBridge] Texture pointer changed — pool invalidated\n");
    fflush(stderr);

    // GPU idle bekle — in-flight command'lar bu image'lari kullanıyor olabilir (F4)
    if (device_ != VK_NULL_HANDLE) {
      vkDeviceWaitIdle(device_);
    }
    for (auto& img : image_pool_) {
      if (img) {
        vkDestroyImage(device_, img, nullptr);
        img = VK_NULL_HANDLE;
      }
    }
    for (auto& mem : pool_memory_) {
      if (mem) {
        vkFreeMemory(device_, mem, nullptr);
        mem = VK_NULL_HANDLE;
      }
    }
    if (cached_d3d11_handle_) {
      CloseHandle(cached_d3d11_handle_);
      cached_d3d11_handle_ = nullptr;
    }
    cached_texture_ptr_ = tex;
  }

  // Hot-path optimization: cached NT handle reuse (no per-frame alloc)
  if (!cached_d3d11_handle_) {
    cached_d3d11_handle_ = export_d3d11_handle(tex);
    if (!cached_d3d11_handle_) {
      fprintf(stderr, "[ExternalMemoryBridge] Failed to export D3D11 handle\n");
      fflush(stderr);
      return false;
    }
  }

  // Round-robin pool selection (no blocking, no heap alloc)
  const uint32_t idx = pool_index_ % POOL_SIZE;
  pool_index_ = (pool_index_ + 1) % POOL_SIZE;

  // Lazy image creation: populate pool slot on first use
  if (!image_pool_[idx]) {
    image_pool_[idx] = create_vulkan_image_from_d3d11(
      cached_d3d11_handle_,
      format_,
      width_,
      height_,
      idx
    );

    if (!image_pool_[idx]) {
      fprintf(stderr, "[ExternalMemoryBridge] Failed to create Vulkan image (pool slot %u)\n", idx);
      fflush(stderr);
      return false;
    }
  }

  // Return cached images (zero-copy, no blocking)
  *out_staging = image_pool_[idx];        // D3D11 import — staging
  *out_target  = gl_target_pool_[idx];    // GL interop target

  return true;
}

VkDeviceMemory ExternalMemoryBridge::get_staging_memory_for_image(VkImage img) const {
  for (int i = 0; i < static_cast<int>(image_pool_.size()); ++i) {
    if (image_pool_[i] == img && i < static_cast<int>(pool_memory_.size())) {
      return pool_memory_[i];
    }
  }
  return VK_NULL_HANDLE;
}

void ExternalMemoryBridge::shutdown() {
  bool expected = false;
  if (!shutdown_called_.compare_exchange_strong(
          expected, true,
          std::memory_order_acq_rel)) {
      return;  // G9: zaten çalıştırıldı
  }
  if (!device_) return;  // B10: guard against double-shutdown or null device

  // D4: Drain all in-flight GPU work before releasing Vulkan resources
  vkDeviceWaitIdle(device_);

  // Staging pool: image önce, memory sonra (Vulkan spec — E14)
  for (auto img : image_pool_) {
    if (img) vkDestroyImage(device_, img, nullptr);
  }
  for (auto mem : pool_memory_) {
    if (mem) vkFreeMemory(device_, mem, nullptr);
  }

  // GL target pool: image önce, memory sonra (Vulkan spec — E14)
  for (auto img : gl_target_pool_) {
    if (img) vkDestroyImage(device_, img, nullptr);
  }
  for (auto mem : gl_target_memory_) {
    if (mem) vkFreeMemory(device_, mem, nullptr);
  }

  // B16: Delete GL memory objects BEFORE closing NT handles —
  // GL side may still hold a reference via glImportMemoryWin32HandleEXT
  if (pfn_delete_memory_objects_ && !gl_memory_objects_.empty()) {
    pfn_delete_memory_objects_(static_cast<int>(gl_memory_objects_.size()),
                               gl_memory_objects_.data());
    gl_memory_objects_.clear();
  }

  // GL target handles (closed AFTER GL memory object deletion)
  for (auto& h : gl_target_handles_) {
    if (h) { CloseHandle(h); h = nullptr; }
  }

  image_pool_.clear();
  pool_memory_.clear();
  gl_target_pool_.clear();
  gl_target_memory_.clear();

  // Task 6: Close cached NT handle
  if (cached_d3d11_handle_) {
    CloseHandle(cached_d3d11_handle_);
    cached_d3d11_handle_ = nullptr;
  }

  // B5/C7: GL sync semaphore pool cleanup
  for (uint32_t i = 0; i < 3; ++i) {
    if (gl_sync_sem_pool_[i] != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, gl_sync_sem_pool_[i], nullptr);
      gl_sync_sem_pool_[i] = VK_NULL_HANDLE;
    }
    if (gl_sync_sem_handles_[i]) {
      CloseHandle(gl_sync_sem_handles_[i]);
      gl_sync_sem_handles_[i] = nullptr;
    }
  }

  device_ = VK_NULL_HANDLE;  // B10: makes destructor call idempotent
  fprintf(stderr, "[ExternalMemoryBridge] Shutdown complete\n");
  fflush(stderr);
}

ExternalMemoryBridge::~ExternalMemoryBridge() {
  shutdown();
}

}
