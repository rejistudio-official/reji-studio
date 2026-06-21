#include "vulkan_initializer.h"

#ifndef REJI_VULKAN_MOCK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#include <dxgi1_2.h>
#include <wrl/client.h>
#else
// Mock mode: define extension names as string constants
#define VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME        "VK_KHR_external_memory_win32"
#define VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME           "VK_KHR_external_semaphore"
#define VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME     "VK_KHR_external_semaphore_win32"
#define VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME            "VK_KHR_win32_keyed_mutex"
#define VK_EXT_DEBUG_UTILS_EXTENSION_NAME                  "VK_EXT_debug_utils"
#endif

#include <vector>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>

namespace rj::pipeline::gpu {

VulkanInitializer* VulkanInitializer::get() {
  static VulkanInitializer s_instance;
  return &s_instance;
}

#ifndef REJI_VULKAN_MOCK
static VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT type,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {
  const char* severity_str = "UNKNOWN";
  switch (severity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: severity_str = "VERBOSE"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT: severity_str = "INFO"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: severity_str = "WARNING"; break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT: severity_str = "ERROR"; break;
    default: break;
  }
  fprintf(stderr, "[Vulkan Debug] [%s] %s\n", severity_str, pCallbackData->pMessage);
  fflush(stderr);
  return VK_FALSE;
}
#endif

bool VulkanInitializer::initialize() {
  if (initialized_) return true;

  if (!vulkan_init_initialize()) {
    fprintf(stderr, "[Vulkan] Zig init failed\n");
    return false;
  }

  instance_              = vulkan_init_instance();
  physical_device_       = vulkan_init_physical_device();
  device_                = vulkan_init_device();
  graphics_queue_        = vulkan_init_graphics_queue();
  graphics_queue_family_ = vulkan_init_graphics_queue_family();
  vendor_id_             = vulkan_init_vendor_id();
  use_keyed_mutex_       = vulkan_init_use_keyed_mutex();
  initialized_           = true;

  fprintf(stderr, "[Vulkan] Zig init OK, vendor=0x%04X\n", vendor_id_);
  return true;
}

bool VulkanInitializer::create_instance() {
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "Reji Studio";
  app_info.applicationVersion = VK_MAKE_VERSION(0, 5, 0);
  app_info.pEngineName = "Reji Pipeline";
  app_info.engineVersion = VK_MAKE_VERSION(0, 5, 0);
  app_info.apiVersion = VK_API_VERSION_1_3;

  // Validation layer priority:
  //   1. Debug build          → always active
  //   2. cmake -DRJ_VALIDATION=ON → always active (Release CI path)
  //   3. RJ_ENABLE_VULKAN_VALIDATION env var → runtime opt-in (Release)
  std::vector<const char*> req_layers;
#ifndef NDEBUG
  req_layers.push_back("VK_LAYER_KHRONOS_validation");
#elif defined(RJ_VALIDATION)
  req_layers.push_back("VK_LAYER_KHRONOS_validation");
#else
  if (getenv("RJ_ENABLE_VULKAN_VALIDATION")) {
    req_layers.push_back("VK_LAYER_KHRONOS_validation");
  }
#endif

  // Remove layers not present on this system (prevents VK_ERROR_LAYER_NOT_PRESENT)
  {
    uint32_t avail_count = 0;
    vkEnumerateInstanceLayerProperties(&avail_count, nullptr);
    std::vector<VkLayerProperties> avail(avail_count);
    vkEnumerateInstanceLayerProperties(&avail_count, avail.data());
    req_layers.erase(
      std::remove_if(req_layers.begin(), req_layers.end(),
        [&](const char* name) {
          bool found = std::any_of(avail.begin(), avail.end(),
            [&](const VkLayerProperties& p) {
              return strcmp(p.layerName, name) == 0;
            });
          if (!found)
            fprintf(stderr, "[Vulkan] Layer %s not found, skipping\n", name);
          return !found;
        }),
      req_layers.end());
  }

  // Debug utils extension only needed when validation layer is active
  std::vector<const char*> req_extensions;
  if (!req_layers.empty()) {
    req_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledLayerCount     = static_cast<uint32_t>(req_layers.size());
  create_info.ppEnabledLayerNames   = req_layers.empty() ? nullptr : req_layers.data();
  create_info.enabledExtensionCount = static_cast<uint32_t>(req_extensions.size());
  create_info.ppEnabledExtensionNames = req_extensions.empty() ? nullptr : req_extensions.data();

  VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[Vulkan] vkCreateInstance failed: %d\n", result);
    fflush(stderr);
    return false;
  }

#ifndef REJI_VULKAN_MOCK
  // Debug messenger yalnızca VK_EXT_debug_utils extension aktifse oluşturulur.
  bool has_debug_utils = std::any_of(req_extensions.begin(), req_extensions.end(),
      [](const char* ext) {
          return std::strcmp(ext, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
      });

  if (has_debug_utils) {
    auto create_debug_utils = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkCreateDebugUtilsMessengerEXT");
    if (create_debug_utils) {
      VkDebugUtilsMessengerCreateInfoEXT debug_info{};
      debug_info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
      debug_info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT |
                                   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT;
      debug_info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debug_info.pfnUserCallback = debug_callback;
      create_debug_utils(instance_, &debug_info, nullptr, &debug_messenger_);
      fprintf(stderr, "[Vulkan] Debug messenger created\n");
      fflush(stderr);
    }
  }
#endif

  return true;
}

bool VulkanInitializer::select_device() {
  uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
  if (device_count == 0) {
    fprintf(stderr, "[Vulkan] No physical devices found\n");
    fflush(stderr);
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());

#ifndef REJI_VULKAN_MOCK
  // C8: Discover display adapter LUID via DXGI (AMD iGPU — DXGI Duplication runs there).
  // Matching prevents selecting NVIDIA when it has a graphics queue and enumerates first.
  LUID display_luid{};
  bool have_luid = false;
  {
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
      Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
      for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        if (desc.VendorId == 0x1002) {
          display_luid = desc.AdapterLuid;
          have_luid    = true;
          fprintf(stderr, "[Vulkan] DXGI display adapter LUID: %08lX:%08lX (AMD)\n",
                  display_luid.HighPart, display_luid.LowPart);
          fflush(stderr);
          break;
        }
        adapter.Reset();
      }
    }
  }
#endif

  // Queue selection helper — sets physical_device_ + graphics_queue_family_.
  auto try_select_queue = [&](VkPhysicalDevice dev) -> bool {
    uint32_t qc = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qc, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qc);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &qc, qp.data());
    for (uint32_t i = 0; i < qc; ++i) {
      if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        physical_device_       = dev;
        graphics_queue_family_ = i;
        return true;
      }
    }
    return false;
  };

#ifndef REJI_VULKAN_MOCK
  // C8: First pass — prefer the device whose LUID matches the DXGI display adapter.
  if (have_luid) {
    for (const auto& dev : devices) {
      VkPhysicalDeviceIDProperties id_props{};
      id_props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
      VkPhysicalDeviceProperties2 props2{};
      props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
      props2.pNext = &id_props;
      vkGetPhysicalDeviceProperties2(dev, &props2);

      if (id_props.deviceLUIDValid &&
          memcmp(id_props.deviceLUID, &display_luid, VK_LUID_SIZE) == 0 &&
          try_select_queue(dev)) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(dev, &props);
        vendor_id_ = props.vendorID;
        fprintf(stderr, "[Vulkan] Selected device (LUID match): %s (vendor: 0x%04x)\n",
                props.deviceName, vendor_id_);
        fflush(stderr);
        return true;
      }
    }
    fprintf(stderr, "[Vulkan] LUID match failed — falling back to first graphics device\n");
    fflush(stderr);
  }
#endif

  // Fallback: first device with a graphics queue.
  for (const auto& dev : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);
    if (try_select_queue(dev)) {
      vendor_id_ = props.vendorID;
      fprintf(stderr, "[Vulkan] Selected device (fallback): %s (vendor: 0x%04x)\n",
              props.deviceName, vendor_id_);
      fflush(stderr);
      return true;
    }
  }

  return false;
}

bool VulkanInitializer::create_device() {
  VkDeviceQueueCreateInfo queue_create_info{};
  queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_create_info.queueFamilyIndex = graphics_queue_family_;
  queue_create_info.queueCount = 1;
  float queue_priority = 1.0f;
  queue_create_info.pQueuePriorities = &queue_priority;

  std::vector<const char*> device_extensions = {
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
  };

  // D11: VK_KHR_win32_keyed_mutex opsiyonel — AMD iGPU'da desteklenmeyebilir
  bool keyed_mutex_supported = has_extension(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME);
  if (keyed_mutex_supported) {
      device_extensions.push_back(VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME);
      use_keyed_mutex_ = true;
      fprintf(stderr, "[Vulkan] Keyed mutex destekleniyor — etkinleştiriliyor\n");
      fflush(stderr);
  } else {
      fprintf(stderr, "[Vulkan] Keyed mutex desteklenmiyor"
                      " — timeline semaphore sync kullanılacak\n");
      fflush(stderr);
  }

  VkPhysicalDeviceTimelineSemaphoreFeatures timeline_sem_features{};
  timeline_sem_features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES;
  timeline_sem_features.timelineSemaphore = VK_TRUE;

  VkPhysicalDeviceFeatures device_features{};

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.pNext = &timeline_sem_features;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_create_info;
  create_info.pEnabledFeatures = &device_features;
  create_info.enabledExtensionCount = static_cast<uint32_t>(device_extensions.size());
  create_info.ppEnabledExtensionNames = device_extensions.data();

  VkResult result = vkCreateDevice(physical_device_, &create_info, nullptr, &device_);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[Vulkan] vkCreateDevice failed: %d\n", result);
    fflush(stderr);
    return false;
  }

  vkGetDeviceQueue(device_, graphics_queue_family_, 0, &graphics_queue_);
  return true;
}

void VulkanInitializer::detect_vendor() {
  VkPhysicalDeviceProperties props;
  vkGetPhysicalDeviceProperties(physical_device_, &props);
  // C8: Use PCI vendorID directly — string heuristic was unreliable and
  // could override the correct value set in select_device().
  vendor_id_ = props.vendorID;
  fprintf(stderr, "[Vulkan] Device: %s (vendorID: 0x%04x deviceID: 0x%04x)\n",
          props.deviceName, props.vendorID, props.deviceID);
  fflush(stderr);
}

bool VulkanInitializer::check_required_extensions() {
  uint32_t ext_count = 0;
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);

  std::vector<VkExtensionProperties> extensions(ext_count);
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, extensions.data());

  // D11: VK_KHR_win32_keyed_mutex opsiyonel — sadece 4 zorunlu extension kontrol edilir
  const char* required_exts[] = {
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
  };
  int found_count = 0;

  for (const auto& req_ext : required_exts) {
    for (const auto& ext : extensions) {
      if (std::strcmp(ext.extensionName, req_ext) == 0) {
        found_count++;
        break;
      }
    }
  }

  return found_count == 4;  // Keyed mutex opsiyonel — 4 zorunlu extension yeterli
}

bool VulkanInitializer::has_extension(const std::string& ext_name) const {
  uint32_t ext_count = 0;
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);

  std::vector<VkExtensionProperties> extensions(ext_count);
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, extensions.data());

  for (const auto& ext : extensions) {
    if (ext_name == ext.extensionName) {
      return true;
    }
  }

  return false;
}

VulkanVendor VulkanInitializer::vendor() const {
  switch (vendor_id_) {
    case 0x1002: return VulkanVendor::kAMD;
    case 0x10DE: return VulkanVendor::kNVIDIA;
    case 0x8086: return VulkanVendor::kIntel;
    default: return VulkanVendor::kUnknown;
  }
}

void VulkanInitializer::shutdown() {
  if (!initialized_) return;
  vulkan_init_shutdown();
  initialized_ = false;
}

VulkanInitializer::~VulkanInitializer() {
  shutdown();
}

}
