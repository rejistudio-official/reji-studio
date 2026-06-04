#include "external_memory_bridge.h"
#include <cstdio>

#ifndef REJI_VULKAN_MOCK
#endif

namespace rj::pipeline::gpu {

ExternalMemoryBridge::ExternalMemoryBridge(VkDevice device, VkPhysicalDevice physical_device)
    : device_(device), physical_device_(physical_device), format_(VK_FORMAT_UNDEFINED),
      width_(0), height_(0), pool_index_(0), cached_d3d11_handle_(nullptr) {}

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
    uint32_t height) {

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
  img_info.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  img_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  img_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage vk_img = nullptr;
  VkResult result = vkCreateImage(device_, &img_info, nullptr, &vk_img);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[ExternalMemoryBridge] vkCreateImage failed: %d\n", result);
    fflush(stderr);
    return nullptr;
  }

  VkImportMemoryWin32HandleInfoKHR import_info{};
  import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
  import_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;
  import_info.handle = d3d11_handle;

  VkMemoryRequirements mem_reqs;
  vkGetImageMemoryRequirements(device_, vk_img, &mem_reqs);

  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.pNext = &import_info;
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
      height_
    );

    if (!image_pool_[idx]) {
      fprintf(stderr, "[ExternalMemoryBridge] Failed to create Vulkan image (pool slot %u)\n", idx);
      fflush(stderr);
      return false;
    }
  }

  // Return cached images (zero-copy, no blocking)
  *out_staging = image_pool_[idx];
  *out_target  = image_pool_[idx];  // Same image for now; differentiate in v0.5.2

  return true;
}

void ExternalMemoryBridge::shutdown() {
  for (auto mem : pool_memory_) {
    if (mem) {
      vkFreeMemory(device_, mem, nullptr);
    }
  }

  for (auto img : image_pool_) {
    if (img) {
      vkDestroyImage(device_, img, nullptr);
    }
  }

  image_pool_.clear();
  pool_memory_.clear();

  // Task 6: Close cached NT handle
  if (cached_d3d11_handle_) {
    CloseHandle(cached_d3d11_handle_);
    cached_d3d11_handle_ = nullptr;
  }

  fprintf(stderr, "[ExternalMemoryBridge] Shutdown complete\n");
  fflush(stderr);
}

ExternalMemoryBridge::~ExternalMemoryBridge() {
  shutdown();
}

}
