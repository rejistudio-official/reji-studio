#include "vulkan_initializer.h"

#ifndef REJI_VULKAN_MOCK
#include <vulkan/vulkan_win32.h>
#else
// Mock mode: define extension name as string constant
#define VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME "VK_KHR_external_memory_win32"
#endif

#include <vector>
#include <cstring>
#include <cstdio>

namespace rj::pipeline::gpu {

bool VulkanInitializer::initialize() {
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

#ifdef _DEBUG
  const char* layers[] = {"VK_LAYER_KHRONOS_validation"};
  uint32_t layer_count = 1;
#else
  uint32_t layer_count = 0;
  const char** layers = nullptr;
#endif

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &app_info;
  create_info.enabledLayerCount = layer_count;
  create_info.ppEnabledLayerNames = layers;
  create_info.enabledExtensionCount = 0;
  create_info.ppEnabledExtensionNames = nullptr;

  VkResult result = vkCreateInstance(&create_info, nullptr, &instance_);
  return result == VK_SUCCESS;
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
  };

  VkPhysicalDeviceFeatures device_features{};

  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_create_info;
  create_info.pEnabledFeatures = &device_features;
  create_info.enabledExtensionCount = 1;
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

  const char* required_ext = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
  for (const auto& ext : extensions) {
    if (std::strcmp(ext.extensionName, required_ext) == 0) {
      return true;
    }
  }

  return false;
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
  if (instance_) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = nullptr;
  }
}

VulkanInitializer::~VulkanInitializer() {
  shutdown();
}

}
