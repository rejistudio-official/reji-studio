# GPU Copy Optimizer — Adım 2: vkCmdBlitImage + Timeline Signal

**Date:** 2026-06-09  
**Status:** Design Approved ✅  
**Scope:** execute_copy() implementation — D3D11 staging image → Vulkan target image blit + timeline sync

---

## 1. Context & Objectives

**Previous Step (Adım 1):**
- ExternalMemoryBridge: GL target pool + NT handle export ✅
- GpuCopyOptimizer: init() + timeline semaphore setup ✅

**This Step:**
- Implement execute_copy() with real vkCmdBlitImage recording + submission
- Timeline semaphore signal for GPU-CPU non-blocking sync
- Prepare target image for GL interop consumption (SHADER_READ_ONLY layout)

**Success Criteria:**
```
[GpuCopyOptimizer] execute_copy: blit submitted, timeline=1
```

---

## 2. Architecture: Command Buffer Flow

```
execute_copy(staging, target, width, height, out_sem, out_value, out_target_image)
  ↓
Input validation (images, semaphore ptrs, size)
  ↓
vkBeginCommandBuffer(command_buffer_, RESET)
  ↓
[Layout Transition 1] Staging: GENERAL → TRANSFER_SRC_OPTIMAL
  (External memory D3D11 import stays in GENERAL; explicit transition safe)
  ↓
[Layout Transition 2] Target: UNDEFINED → TRANSFER_DST_OPTIMAL
  (Fresh allocation, UNDEFINED OK per Vulkan spec)
  ↓
vkCmdBlitImage(src=staging, dst=target, filter=LINEAR, full region)
  ↓
[Pipeline Barrier] Target: TRANSFER_DST → SHADER_READ_ONLY_OPTIMAL
  (Prepares for GL interop texture read)
  ↓
vkEndCommandBuffer()
  ↓
vkQueueSubmit(queue_, submit_info, timeline_signal={timeline_counter_})
  (Non-blocking: no fence, no vkWaitForFences)
  ↓
Increment timeline_counter_ += FRAME_INCREMENT
  ↓
Return:
  - out_timeline_semaphore = timeline_semaphore_
  - out_timeline_value = timeline_counter_ (pre-increment value)
  - out_target_image = vulkan_target
  ↓
PreviewWidget: is_copy_ready() polls counter, then GL interop consumes target
```

---

## 3. Function Signature Changes

### Before
```cpp
bool execute_copy(VkImage d3d11_staging_vk,
                  VkImage vulkan_target,
                  uint32_t width,
                  uint32_t height,
                  VkSemaphore* out_timeline_semaphore,
                  uint64_t* out_timeline_value);
```

### After (Adım 2)
```cpp
bool execute_copy(VkImage d3d11_staging_vk,
                  VkImage vulkan_target,
                  uint32_t width,
                  uint32_t height,
                  VkSemaphore* out_timeline_semaphore,
                  uint64_t* out_timeline_value,
                  VkImage* out_target_image);  // ← NEW
```

**File Updates Required:**
- `copy_optimizer.h` — function declaration
- `copy_optimizer.cpp` — function body
- `preview_widget.cpp` — call site (receive out_target_image)

---

## 4. Vulkan Command Recording Details

### 4.1 Input Validation
```cpp
if (!device_ || !command_buffer_ || !d3d11_staging_vk || !vulkan_target) {
    return false;
}
if (!out_timeline_semaphore || !out_timeline_value || !out_target_image) {
    return false;
}
if (width == 0 || height == 0) {
    return false;
}
```

### 4.2 Begin Command Buffer
```cpp
VkCommandBufferBeginInfo begin_info = {};
begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
CHECK_VK(vkBeginCommandBuffer(command_buffer_, &begin_info));
```

### 4.3 Layout Transitions

**Staging Image: GENERAL → TRANSFER_SRC_OPTIMAL**
```cpp
VkImageMemoryBarrier barrier_staging = {};
barrier_staging.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier_staging.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
barrier_staging.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
barrier_staging.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
barrier_staging.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
barrier_staging.image = d3d11_staging_vk;
barrier_staging.subresourceRange = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
};

vkCmdPipelineBarrier(command_buffer_,
                      VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      0, 0, nullptr, 0, nullptr, 1, &barrier_staging);
```

**Target Image: UNDEFINED → TRANSFER_DST_OPTIMAL**
```cpp
VkImageMemoryBarrier barrier_target = {};
barrier_target.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier_target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
barrier_target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
barrier_target.srcAccessMask = 0;
barrier_target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
barrier_target.image = vulkan_target;
barrier_target.subresourceRange = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
};

vkCmdPipelineBarrier(command_buffer_,
                      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      0, 0, nullptr, 0, nullptr, 1, &barrier_target);
```

### 4.4 vkCmdBlitImage
```cpp
VkImageBlit blit_region = {};
blit_region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
blit_region.srcOffsets[0] = {0, 0, 0};
blit_region.srcOffsets[1] = {(int32_t)width, (int32_t)height, 1};
blit_region.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
blit_region.dstOffsets[0] = {0, 0, 0};
blit_region.dstOffsets[1] = {(int32_t)width, (int32_t)height, 1};

vkCmdBlitImage(command_buffer_,
               d3d11_staging_vk, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               vulkan_target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               1, &blit_region,
               VK_FILTER_LINEAR);  // Safe interpolation filter
```

### 4.5 Post-Blit Barrier: TRANSFER_DST → SHADER_READ_ONLY_OPTIMAL
```cpp
VkImageMemoryBarrier barrier_final = {};
barrier_final.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
barrier_final.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
barrier_final.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
barrier_final.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
barrier_final.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
barrier_final.image = vulkan_target;
barrier_final.subresourceRange = {
    VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
};

vkCmdPipelineBarrier(command_buffer_,
                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                      VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                      0, 0, nullptr, 0, nullptr, 1, &barrier_final);
```

### 4.6 End Command Buffer
```cpp
CHECK_VK(vkEndCommandBuffer(command_buffer_));
```

---

## 5. Submission & Timeline Signal

```cpp
// Timeline semaphore signal value (pre-increment)
uint64_t signal_value = timeline_counter_;

VkTimelineSemaphoreSubmitInfo timeline_submit_info = {};
timeline_submit_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR;
timeline_submit_info.signalSemaphoreValueCount = 1;
timeline_submit_info.pSignalSemaphoreValues = &signal_value;

VkSubmitInfo submit_info = {};
submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
submit_info.pNext = &timeline_submit_info;
submit_info.commandBufferCount = 1;
submit_info.pCommandBuffers = &command_buffer_;
submit_info.signalSemaphoreCount = 1;
submit_info.pSignalSemaphores = &timeline_semaphore_;

CHECK_VK(vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE));
// ↑ No fence: non-blocking, timeline handles sync
```

---

## 6. Return Values & State Update

```cpp
// Return outputs
*out_timeline_semaphore = timeline_semaphore_;
*out_timeline_value = signal_value;  // Pre-increment value
*out_target_image = vulkan_target;    // ← NEW: target image for GL interop

// Increment for next frame
timeline_counter_ += FRAME_INCREMENT;

fprintf(stderr, "[GpuCopyOptimizer] execute_copy: blit submitted, timeline=%llu\n",
        signal_value);

return true;
```

---

## 7. Error Handling Strategy

| Error Scenario | Handling |
|---|---|
| Invalid images/device | Early return (pre-vkBeginCommandBuffer) |
| vkBeginCommandBuffer fails | CHECK_VK → log + return false |
| vkCmdBlitImage fails | Not possible (not recorded, only returned via next submit) |
| vkQueueSubmit fails | CHECK_VK → log + return false |
| Exception during flow | Try-catch wrapping existing catch block |

**No SEH needed:** vkQueueSubmit non-blocking, no __try/__except required.

---

## 8. Hot-Path Compliance ✅

| Rule | Status |
|---|---|
| No malloc/new | ✅ Pre-allocated command_buffer_, timeline_semaphore_ |
| No std::vector/std::string | ✅ Stack-allocated barriers, no heap ops |
| No blocking calls | ✅ vkQueueSubmit(..., VK_NULL_HANDLE) non-blocking |
| No JSON/WMI/string format (exec path) | ✅ Only fprintf (exit code path) |

---

## 9. Testing Verification

**Expected Log Output (v0.5.2 completion):**
```
[GpuCopyOptimizer] execute_copy: blit submitted, timeline=1
[PreviewWidget] is_copy_ready: timeline counter=1 ✓ ready
[PreviewWidget] GL interop texture created from target image
```

**Test Flow:**
1. Call execute_copy(staging, target, w, h, &sem, &val, &img)
2. Verify return values: sem ≠ null, val ≠ 0, img ≠ null
3. Call is_copy_ready(sem, val) → should return true within timeout
4. GL interop consumes img (bind, sample) → should not crash

---

## 10. Call Site Updates

**preview_widget.cpp** (future Adım 4):
```cpp
VkImage target_image = nullptr;
if (!optimizer_->execute_copy(staging, target, w, h, &sem, &val, &target_image)) {
    return false;
}
// Use target_image in GL interop texture binding
```

---

## Next Step: Writing-Plans (Implementation)

This spec is ready for implementation. No further clarifications needed.

---

**Author:** Claude (Brainstorming + Design)  
**Approved by:** User ✅
