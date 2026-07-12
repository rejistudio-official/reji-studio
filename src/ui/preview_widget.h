#pragma once
#ifdef QT6_AVAILABLE

namespace rj {
    class Pipeline;
    class FrameProfiler;
}
class GpuCopyOptimizer;
namespace rj::pipeline::gpu {
    class ExternalMemoryBridge;
}

#include "render_capability.h"
#include "../pipeline/gpu/external_memory_bridge.h"
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <cstdint>
#include <memory>

namespace reji {

// GL_EXT_memory_object_win32 function pointers
using PFNGLCREATEMEMORYOBJECTSEXT    = void(*)(GLsizei, GLuint*);
using PFNGLDELETEMEMORYOBJECTSEXT    = void(*)(GLsizei, const GLuint*);
using PFNGLIMPORTMEMORYWIN32HANDLEEXT = void(*)(GLuint, GLuint64, GLenum, void*);
using PFNGLTEXSTORAGEMEM2DEXT        = void(*)(GLenum, GLsizei, GLenum, GLsizei, GLsizei, GLuint, GLuint64);

// GL_EXT_semaphore_win32 function pointers (B5: Vulkan/GL sync)
using PFNGLWAITSEMAPHOREEXT              = void(*)(GLuint, GLuint, const GLuint*, GLuint, const GLuint*, const GLenum*);
using PFNGLIMPORTSEMAPHOREWIN32HANDLEEXT = void(*)(GLuint, GLenum, void*);

// ---------------------------------------------------------------------------
// PreviewWidget — Preview (left) monitor.
//
// v0.5.1 zero-copy path:
//   submitD3D11Frame() receives a D3D11-imported VkImage + a Vulkan target
//   VkImage from the pipeline thread. paintGL() submits a GPU-only compute
//   copy via GpuCopyOptimizer::execute_copy() and polls is_copy_ready() —
//   if the copy is not yet complete, the frame is skipped (no CPU stall).
//
// Hot-path guarantees:
//   * No blocking calls (vkWaitForFences / vkDeviceWaitIdle forbidden)
//   * No heap allocation in paintGL()
//   * Timeline semaphore polled via vkGetSemaphoreCounterValueKHR (async)
//
// Fallback:
//   The legacy CPU uploadFrame() path has been removed. Hosts that need a
//   non-Vulkan fallback (e.g. AMD iGPU without external memory) should add
//   a dedicated CPU upload path (out of scope here).
// ---------------------------------------------------------------------------
class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Submit a D3D11 frame for GPU-only copy (v0.5.1).
    //   d3d11_staging_vk : D3D11 Texture2D imported as VkImage (external memory)
    //   vulkan_target    : target Vulkan image (OpenGL interop target)
    // Stores the handles under a mutex and schedules a paintGL tick.
    // Thread-safe: may be called from the pipeline thread.
    // Returns true if the frame was queued, false on invalid args.
    // I23: pool_slot — bridge'in bu image çiftini ürettiği round-robin slot.
    // paintGL bu slot'u execute_copy'ye + GL-interop indexlemesine tek kaynak
    // olarak taşır (optimizer'ın ayrı sayacı yerine).
    bool submitD3D11Frame(VkImage d3d11_staging_vk,
                          VkImage vulkan_target,
                          uint32_t width, uint32_t height,
                          uint32_t pool_slot);

    // Set the GpuCopyOptimizer (borrowed, lifecycle managed externally).
    // Must be called before submitD3D11Frame().
    void setCopyOptimizer(GpuCopyOptimizer* optimizer);

    // Select GL upload path based on display adapter vendor_id.
    // Must be called from the GL thread (or before the widget is shown) after
    // pipeline init. Safe to call multiple times.
    void selectRenderPath(uint32_t vendor_id);

    // Get current render path.
    RenderPath renderPath() const;

    // Wire profiler for paintGL timing.
    // Profiler is borrowed; lifecycle managed by Pipeline.
    void setProfiler(rj::FrameProfiler* profiler);

    // Wire pipeline for Vulkan notification on GL initialization.
    // Pipeline is borrowed; lifecycle managed externally.
    void setPipeline(rj::Pipeline* pipeline) noexcept;

    // Wire bridge for GL interop (external memory NT handle).
    // Bridge is borrowed; lifecycle managed externally.
    void setBridge(rj::pipeline::gpu::ExternalMemoryBridge* b) noexcept;

public slots:
    // Thread-safe CPU frame upload for WGC staging path.
    void uploadCpuFrame(const void* bgra, int w, int h, int pitch);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    class Impl;
    std::unique_ptr<Impl> d_;
    GpuCopyOptimizer* copy_optimizer_ = nullptr;  // borrowed, not owned
    rj::pipeline::gpu::ExternalMemoryBridge* bridge_ = nullptr;      // borrowed, not owned
    rj::FrameProfiler* profiler_ = nullptr;
    rj::Pipeline* pipeline_ = nullptr;            // borrowed, not owned
    VkImage gl_target_image_ = VK_NULL_HANDLE;    // Target image from execute_copy (GL interop)
    GLuint gl_interop_textures_[3] = {0, 0, 0};   // GL textures per pool slot (POOL_SIZE=3)
    GLuint gl_memory_objects_[3] = {0, 0, 0};     // GL memory objects per pool slot (one per NT handle)
    uint32_t gl_target_w_ = 0, gl_target_h_ = 0;  // Current GL interop texture dimensions
    uint32_t current_pool_idx_ = 0;                // Current active pool index (for texture selection)
    uint32_t frame_counter_ = 0;                   // Frame counter for round-robin pool cycling
    int poll_frames_ = 0;

    // GL_EXT_memory_object_win32 function pointers
    PFNGLCREATEMEMORYOBJECTSEXT     pfn_CreateMemoryObjects_     = nullptr;
    PFNGLDELETEMEMORYOBJECTSEXT     pfn_DeleteMemoryObjects_     = nullptr;
    PFNGLIMPORTMEMORYWIN32HANDLEEXT pfn_ImportMemoryWin32Handle_ = nullptr;
    PFNGLTEXSTORAGEMEM2DEXT         pfn_TexStorageMem2D_         = nullptr;

    // GL_EXT_semaphore_win32 function pointers (B5)
    PFNGLWAITSEMAPHOREEXT              pfn_WaitSemaphore_              = nullptr;
    PFNGLIMPORTSEMAPHOREWIN32HANDLEEXT pfn_ImportSemaphoreWin32Handle_ = nullptr;

    // GL 3.2 core sync object function pointers (D5)
    using PFNGLFENCESYNCPROC      = GLsync(*)(GLenum, GLbitfield);
    using PFNGLDELETESYNCPROC     = void(*)(GLsync);
    using PFNGLCLIENTWAITSYNCPROC = GLenum(*)(GLsync, GLbitfield, GLuint64);
    PFNGLFENCESYNCPROC      pfn_FenceSync_      = nullptr;
    PFNGLDELETESYNCPROC     pfn_DeleteSync_     = nullptr;
    PFNGLCLIENTWAITSYNCPROC pfn_ClientWaitSync_ = nullptr;
    GLuint                             gl_sync_semaphores_[3]          = {0, 0, 0};

    // D5: Per-slot GL fence — ensures GL finishes reading slot before Vulkan overwrites it
    GLsync gl_draw_fences_[3] = {nullptr, nullptr, nullptr};
};

} // namespace reji
#endif // QT6_AVAILABLE
