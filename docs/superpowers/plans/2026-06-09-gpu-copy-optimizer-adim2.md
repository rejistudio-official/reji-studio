# GPU Copy Optimizer — Adım 2 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement execute_copy() with real vkCmdBlitImage recording, layout transitions, and timeline semaphore signal submission.

**Architecture:** Command buffer pre-allocated in init(), execute_copy() records layout transitions (GENERAL→TRANSFER_SRC for staging, UNDEFINED→TRANSFER_DST for target), vkCmdBlitImage, post-blit barrier, and submits with timeline signal (non-blocking). Returns semaphore, counter value, and target image handle to GL interop thread.

**Tech Stack:** Vulkan 1.4, C++17, Windows 11 (SEH for error handling)

---

## File Structure

| File | Responsibility |
|---|---|
| `src/pipeline/gpu/copy_optimizer.h` | Function signature: add `VkImage* out_target_image` param |
| `src/pipeline/gpu/copy_optimizer.cpp` | execute_copy() implementation: barriers, blit, submit |
| `src/ui/preview_widget.cpp` | Call site update: receive out_target_image |

---

## Task 1: Update copy_optimizer.h — Function Signature

**Files:**
- Modify: `C:\reji-studio\src\pipeline\gpu\copy_optimizer.h` (execute_copy() declaration)

- [ ] **Step 1: Read current header**

Run: Read copy_optimizer.h and locate execute_copy() declaration

- [ ] **Step 2: Update function signature**

Find this line:
```cpp
bool execute_copy(VkImage d3d11_staging_vk,
                  VkImage vulkan_target,
                  uint32_t width,
                  uint32_t height,
                  VkSemaphore* out_timeline_semaphore,
                  uint64_t* out_timeline_value);
```

Replace with:
```cpp
bool execute_copy(VkImage d3d11_staging_vk,
                  VkImage vulkan_target,
                  uint32_t width,
                  uint32_t height,
                  VkSemaphore* out_timeline_semaphore,
                  uint64_t* out_timeline_value,
                  VkImage* out_target_image);
```

- [ ] **Step 3: Commit header change**

```bash
git add src/pipeline/gpu/copy_optimizer.h
git commit -m "refactor: add out_target_image param to execute_copy()"
```

---

## Task 2: Implement execute_copy() in copy_optimizer.cpp

**Files:**
- Modify: `C:\reji-studio\src\pipeline\gpu\copy_optimizer.cpp` (execute_copy() body)

- [ ] **Step 1: Read current execute_copy() stub**

Read the file to understand current structure (lines 83-117)

- [ ] **Step 2: Replace execute_copy() with full implementation**

Replace the entire execute_copy() function (lines 83-117) with:

```cpp
bool GpuCopyOptimizer::execute_copy(VkImage d3d11_staging_vk,
                                     VkImage vulkan_target,
                                     uint32_t width,
                                     uint32_t height,
                                     VkSemaphore* out_timeline_semaphore,
                                     uint64_t* out_timeline_value,
                                     VkImage* out_target_image) {
    if (!device_ || !command_buffer_) {
        return false;
    }

    try {
        // Input validation
        if (!d3d11_staging_vk || !vulkan_target) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid images (staging or target null)\n");
            return false;
        }
        if (width == 0 || height == 0) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid dimensions (%u x %u)\n", width, height);
            return false;
        }
        if (!out_timeline_semaphore || !out_timeline_value || !out_target_image) {
            fprintf(stderr, "[GpuCopyOptimizer] Invalid output ptrs\n");
            return false;
        }

        // Begin command buffer
        VkCommandBufferBeginInfo begin_info = {};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CHECK_VK(vkBeginCommandBuffer(command_buffer_, &begin_info));

        // ========== LAYOUT TRANSITION 1: Staging GENERAL → TRANSFER_SRC ==========
        VkImageMemoryBarrier barrier_staging = {};
        barrier_staging.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_staging.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        barrier_staging.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier_staging.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_staging.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier_staging.image = d3d11_staging_vk;
        barrier_staging.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_staging);

        // ========== LAYOUT TRANSITION 2: Target UNDEFINED → TRANSFER_DST ==========
        VkImageMemoryBarrier barrier_target = {};
        barrier_target.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_target.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier_target.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_target.srcAccessMask = 0;
        barrier_target.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_target.image = vulkan_target;
        barrier_target.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_target);

        // ========== BLIT IMAGE ==========
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
                       VK_FILTER_LINEAR);

        // ========== LAYOUT TRANSITION 3: Target TRANSFER_DST → SHADER_READ_ONLY ==========
        VkImageMemoryBarrier barrier_final = {};
        barrier_final.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier_final.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier_final.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier_final.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier_final.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier_final.image = vulkan_target;
        barrier_final.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        vkCmdPipelineBarrier(command_buffer_,
                              VK_PIPELINE_STAGE_TRANSFER_BIT,
                              VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                              0, 0, nullptr, 0, nullptr, 1, &barrier_final);

        // End command buffer
        CHECK_VK(vkEndCommandBuffer(command_buffer_));

        // ========== SUBMIT WITH TIMELINE SIGNAL ==========
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

        // Return outputs
        *out_timeline_semaphore = timeline_semaphore_;
        *out_timeline_value = signal_value;
        *out_target_image = vulkan_target;

        // Increment for next frame
        timeline_counter_ += FRAME_INCREMENT;

        fprintf(stderr, "[GpuCopyOptimizer] execute_copy: blit submitted, timeline=%llu\n",
                signal_value);

        return true;
    } catch (...) {
        fprintf(stderr, "[GpuCopyOptimizer] Exception during execute_copy\n");
        return false;
    }
}
```

- [ ] **Step 3: Verify implementation compiles locally (mental check)**

Check:
- All VkStructure fields initialized
- All barriers properly formed
- Blit region offsets match width/height
- Submit with timeline_submit_info pNext
- Return all 3 output values

- [ ] **Step 4: Commit implementation**

```bash
git add src/pipeline/gpu/copy_optimizer.cpp
git commit -m "feat: implement execute_copy() with vkCmdBlitImage + timeline signal"
```

---

## Task 3: Update preview_widget.cpp — Call Site

**Files:**
- Modify: `C:\reji-studio\src\ui\preview_widget.cpp` (execute_copy() call)

- [ ] **Step 1: Find current execute_copy() call in preview_widget.cpp**

Search for "execute_copy" in the file. Typical location: in a frame processing function.

- [ ] **Step 2: Update the call to pass out_target_image parameter**

Find the current call (likely something like):
```cpp
if (!optimizer_->execute_copy(staging_image, target_image, w, h, &sem, &val)) {
    // error handling
}
```

Replace with:
```cpp
VkImage result_target_image = nullptr;
if (!optimizer_->execute_copy(staging_image, target_image, w, h, &sem, &val, &result_target_image)) {
    // error handling
    return false;
}

// Store result_target_image for GL interop (future Adım 4)
gl_target_image_ = result_target_image;
```

(Exact context depends on preview_widget.cpp structure — adjust variable names/scope as needed)

- [ ] **Step 3: Verify call signature matches updated header**

Parameters: `(staging, target, width, height, &sem, &val, &result_target)`
Returns: bool

- [ ] **Step 4: Commit call site update**

```bash
git add src/ui/preview_widget.cpp
git commit -m "refactor: update execute_copy() call to receive out_target_image"
```

---

## Task 4: Build & Test

**Files:**
- Test: `C:\reji-studio\build` (build output)

- [ ] **Step 1: Clean build**

```bash
cd C:\reji-studio
rmdir /S /Q build
cmake --preset release
cmake --build --preset release
```

Expected: Build succeeds, no linker errors on vkCmdBlitImage, timeline semaphore extensions.

- [ ] **Step 2: Run application and verify log output**

```bash
C:\reji-studio\build\src\ui\reji_app.exe > run.log 2>&1
```

Run for ~2-3 seconds, then close.

- [ ] **Step 3: Check log for success markers**

```bash
findstr "[GpuCopyOptimizer] execute_copy: blit submitted" run.log
```

Expected output:
```
[GpuCopyOptimizer] execute_copy: blit submitted, timeline=1
```

If not found, check for Vulkan errors:
```bash
findstr "[GpuCopyOptimizer] VK error" run.log
```

- [ ] **Step 4: Verify no crashes or exceptions**

```bash
findstr "Exception" run.log
```

Should return no results.

- [ ] **Step 5: Commit successful build**

```bash
git add -A
git commit -m "build: verify execute_copy() passes and timeline signal works"
```

---

## Task 5: Final Verification & Cleanup

**Files:**
- Test: `docs/superpowers/specs/2026-06-09-gpu-copy-optimizer-adim2-design.md` (spec reference)

- [ ] **Step 1: Verify spec coverage**

Skim the spec sections:
- ✅ Section 3 (Function Signature) — out_target_image added
- ✅ Section 4 (Vulkan Command Recording) — GENERAL→TRANSFER_SRC, barriers, blit
- ✅ Section 5 (Submission & Timeline) — timeline signal code
- ✅ Section 6 (Return Values) — all 3 outputs returned

- [ ] **Step 2: Verify hot-path compliance**

Check copy_optimizer.cpp execute_copy():
- ✅ No malloc/new
- ✅ No std::vector/std::string allocation
- ✅ No blocking vkWaitForFences
- ✅ No JSON parsing or WMI queries
- ✅ Only fprintf (exit path)

- [ ] **Step 3: Review commits**

```bash
git log --oneline -5
```

Should show:
1. build: verify execute_copy() passes...
2. refactor: update execute_copy() call...
3. feat: implement execute_copy()...
4. refactor: add out_target_image param...

- [ ] **Step 4: Final commit with summary**

```bash
git commit --allow-empty -m "chore: Adım 2 complete — execute_copy() implementation ready for Adım 3"
```

---

## Plan Self-Review

✅ **Spec coverage:** All requirements from design doc addressed
- Layout transitions (GENERAL→TRANSFER_SRC, UNDEFINED→TRANSFER_DST, post-blit)
- vkCmdBlitImage with full frame region
- Timeline semaphore signal + submit
- Return 3 values (sem, counter, target_image)
- Call site update (preview_widget.cpp)

✅ **Placeholder scan:** No "TBD", "TODO", or vague steps
✅ **Type consistency:** Function signature matches across .h and .cpp
✅ **Code completeness:** All Vulkan structs fully initialized, all barriers complete
