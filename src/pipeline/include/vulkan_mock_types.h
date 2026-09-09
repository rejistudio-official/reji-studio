#pragma once

// REJI_VULKAN_MOCK: vulkan.h'siz derleme için minimal tip takma-adları.
// external_memory_bridge.h'teki `VkDevice = void*` deseninin paylaşılan hali —
// copy_optimizer.h ve gpu_query_timing.h mock'ta bunu include eder (mock
// tasarımından SONRA eklendikleri için koşulsuz vulkan.h çekiyorlardı; CI'da
// bit-rot'un kök nedeni buydu, bkz. docs/TALIMAT_CI_CPP_BUILD_ONARIMI.md
// Parça 3). YALNIZ mock dalından include edilmeli.
//
// NOT: external_memory_bridge.h kendi takma-ad bloğunu korur (yüksek-risk
// dosya, dokunulmadı). Aynı TU'da ikisi birden görünürse `using X = void*;`
// tekrarları aynı tipe çözüldüğünden geçerlidir; bu başlık emb.h'teki
// VkFormat/VkResult/VK_FORMAT_* sabitlerini bilinçli olarak TANIMLAMAZ
// (çift tanım hatası olmasın).

#include <cstdint>

using VkDevice = void*;
using VkPhysicalDevice = void*;
using VkQueue = void*;
using VkImage = void*;
using VkSemaphore = void*;
using VkDeviceMemory = void*;
using VkCommandPool = void*;
using VkCommandBuffer = void*;
using VkQueryPool = void*;
using VkImageLayout = int;

// Üye olarak `= {}` ile tutulan struct'lar — mock'ta içerik gerekmez.
struct VkSubmitInfo {};
struct VkTimelineSemaphoreSubmitInfoKHR {};
struct VkWin32KeyedMutexAcquireReleaseInfoKHR {};

// Fonksiyon işaretçisi üyeler yalnız nullptr ile karşılaştırılır/atanır.
using PFN_vkGetSemaphoreCounterValueKHR = void*;
using PFN_vkWaitSemaphores = void*;

#ifndef VK_NULL_HANDLE
#define VK_NULL_HANDLE nullptr
#endif
