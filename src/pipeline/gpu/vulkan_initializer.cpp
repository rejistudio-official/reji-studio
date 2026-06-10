#include "vulkan_initializer.h"

#ifndef REJI_VULKAN_MOCK
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
// Mock mode: define extension names as string constants
#define VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME        "VK_KHR_external_memory_win32"
#define VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME           "VK_KHR_external_semaphore"
#define VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME     "VK_KHR_external_semaphore_win32"
#define VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME            "VK_KHR_win32_keyed_mutex"
#define VK_EXT_DEBUG_UTILS_EXTENSION_NAME                  "VK_EXT_debug_utils"
#endif

#include <vector>
#include <cstring>
#include <cstdio>

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
  // Debug: List available instance layers
  uint32_t layer_count = 0;
  vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
  fprintf(stderr, "[Vulkan] Available instance layers: %u\n", layer_count);
  fflush(stderr);
  if (layer_count > 0) {
    std::vector<VkLayerProperties> layers(layer_count);
    vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
    for (const auto& layer : layers) {
      fprintf(stderr, "[Vulkan] Layer: %s (spec ver: %u, impl ver: %u)\n",
              layer.layerName, layer.specVersion, layer.implementationVersion);
      fflush(stderr);
    }
  }

  if (!create_instance()) {
    fprintf(stderr, "[Vulkan] Failed to create instance\n");
    fflush(stderr);
    return false;
  }

  if (!select_device()) {
    fprintf(stderr, "[Vulkan] Failed to select device\n");
    fflush(stderr);
    vkDestroyInstance(instance_, nullptr);
    instance_ = nullptr;
    return false;
  }

  if (!create_device()) {
    fprintf(stderr, "[Vulkan] Failed to create device\n");
    fflush(stderr);
    vkDestroyInstance(instance_, nullptr);
    instance_ = nullptr;
    return false;
  }

  detect_vendor();

  if (!check_required_extensions()) {
    fprintf(stderr, "[Vulkan] Required extensions not supported\n");
    fflush(stderr);
    shutdown();
    return false;
  }

  fprintf(stderr, "[Vulkan] Initialized (vendor: 0x%04x)\n", vendor_id_);
  fflush(stderr);
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

// v0.5.2: Enable validation layers for ALL builds (debug + release)
// VK_ERROR_DEVICE_LOST requires validation layer diagnostics
const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
uint32_t layer_count = 1;
const char* extensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
uint32_t ext_count = 1;

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledLayerCount = layer_count;
  create_info.ppEnabledLayerNames = layers;
  create_info.enabledExtensionCount = ext_count;
  create_info.ppEnabledExtensionNames = extensions;

  VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
  if (result != VK_SUCCESS) {
    fprintf(stderr, "[Vulkan] vkCreateInstance failed: %d\n", result);
    fflush(stderr);
    return false;
  }

#ifndef REJI_VULKAN_MOCK
  // v0.5.2: Enable debug messenger for ALL builds
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
  } else {
    fprintf(stderr, "[Vulkan] Warning: vkCreateDebugUtilsMessengerEXT not available\n");
    fflush(stderr);
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

  for (const auto& dev : devices) {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(dev, &props);

    uint32_t queue_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_count, nullptr);

    std::vector<VkQueueFamilyProperties> queue_props(queue_count);
    vkGetPhysicalDeviceQueueFamilyProperties(dev, &queue_count, queue_props.data());

    for (uint32_t i = 0; i < queue_count; ++i) {
      if (queue_props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        physical_device_ = dev;
        graphics_queue_family_ = i;
        vendor_id_ = props.deviceID;
        fprintf(stderr, "[Vulkan] Selected device: %s\n", props.deviceName);
        fflush(stderr);
        return true;
      }
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

  const char* device_extensions[] = {
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME,
  };

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
  create_info.enabledExtensionCount = 5;
  create_info.ppEnabledExtensionNames = device_extensions;

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

  std::string device_name = props.deviceName;
  if (device_name.find("AMD") != std::string::npos ||
      device_name.find("Radeon") != std::string::npos) {
    vendor_id_ = 0x1002;
  } else if (device_name.find("NVIDIA") != std::string::npos ||
             device_name.find("GeForce") != std::string::npos ||
             device_name.find("RTX") != std::string::npos) {
    vendor_id_ = 0x10DE;
  } else if (device_name.find("Intel") != std::string::npos) {
    vendor_id_ = 0x8086;
  } else {
    vendor_id_ = 0x0000;
  }

  fprintf(stderr, "[Vulkan] Device: %s (vendor: 0x%04x)\n", props.deviceName, vendor_id_);
  fflush(stderr);
}

bool VulkanInitializer::check_required_extensions() {
  uint32_t ext_count = 0;
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, nullptr);

  std::vector<VkExtensionProperties> extensions(ext_count);
  vkEnumerateDeviceExtensionProperties(physical_device_, nullptr, &ext_count, extensions.data());

  // Check for required extensions
  const char* required_exts[] = {
    VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
    VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
    VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME,
    VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME,
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

  return found_count == 5;  // All required extensions found
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
  if (device_) {
    vkDestroyDevice(device_, nullptr);
    device_ = nullptr;
  }
#ifndef REJI_VULKAN_MOCK
  if (debug_messenger_ && instance_) {
    auto destroy_debug_utils = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(
        instance_, "vkDestroyDebugUtilsMessengerEXT");
    if (destroy_debug_utils) {
      destroy_debug_utils(instance_, debug_messenger_, nullptr);
    }
    debug_messenger_ = nullptr;
  }
#endif
  if (instance_) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = nullptr;
  }
}

VulkanInitializer::~VulkanInitializer() {
  shutdown();
}

}
