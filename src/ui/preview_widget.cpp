#include "preview_widget.h"
#ifdef QT6_AVAILABLE

#include "render_capability.h"
#include "../pipeline/copy_optimizer.h"
#include "../pipeline/include/frame_profiler.h"
#include "../pipeline/gpu/vulkan_initializer.h"
#include "../pipeline/include/pipeline.h"
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QSurfaceFormat>
#include <cstdio>
#include <dwmapi.h>

namespace {

// Fullscreen quad: xy position + uv texcoord.
// Y is flipped (uv.y=1 at bottom) to match GL's bottom-up convention
// with images that are top-down (D3D / Vulkan).
constexpr float kQuadVerts[] = {
    -1.f, -1.f,   0.f, 1.f,
     1.f, -1.f,   1.f, 1.f,
     1.f,  1.f,   1.f, 0.f,
    -1.f,  1.f,   0.f, 0.f,
};

const char* kVertSrc = R"glsl(
    #version 330 core
    layout(location = 0) in vec2 aPos;
    layout(location = 1) in vec2 aUV;
    out vec2 vUV;
    void main() { gl_Position = vec4(aPos, 0.0, 1.0); vUV = aUV; }
)glsl";

const char* kFragSrc = R"glsl(
    #version 330 core
    uniform sampler2D uTex;
    in  vec2 vUV;
    out vec4 FragColor;
    void main() { FragColor = texture(uTex, vUV); }
)glsl";

} // namespace

namespace reji {

class PreviewWidget::Impl {
public:
    QOpenGLShaderProgram     shader;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};

    GLuint tex_id{0};
    int    tex_w{0}, tex_h{0};

    // --- v0.5.1 GPU-only copy state ---
    // Vulkan image handles (set from pipeline thread via submitD3D11Frame).
    QMutex  frame_mutex;
    VkImage  pending_staging_vk = VK_NULL_HANDLE;
    VkImage  pending_target_vk  = VK_NULL_HANDLE;
    uint32_t pending_width      = 0;
    uint32_t pending_height     = 0;
    bool     frame_dirty        = false;

    // Timeline semaphore tracking (per submit, non-blocking poll).
    VkSemaphore timeline_semaphore = VK_NULL_HANDLE;
    uint64_t   expected_value      = 0;
    bool       has_pending_copy    = false;

    // --- Render path ---
    RenderPath render_path{RenderPath::kPbo};
};

PreviewWidget::PreviewWidget(QWidget* parent)
    : QOpenGLWidget(parent), d_(std::make_unique<Impl>())
{
    setMinimumSize(320, 180);
    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(fmt);
}

PreviewWidget::~PreviewWidget() {
    makeCurrent();
    if (d_->tex_id) glDeleteTextures(1, &d_->tex_id);
    // Clean up GL interop textures and memory objects (per-pool-slot)
    for (int i = 0; i < 3; ++i) {
        if (gl_interop_textures_[i]) glDeleteTextures(1, &gl_interop_textures_[i]);
        if (gl_memory_objects_[i] && pfn_DeleteMemoryObjects_) {
            pfn_DeleteMemoryObjects_(1, &gl_memory_objects_[i]);
        }
    }
    d_->vbo.destroy();
    d_->vao.destroy();
    doneCurrent();
}

void PreviewWidget::setCopyOptimizer(GpuCopyOptimizer* optimizer) {
    copy_optimizer_ = optimizer;
    fprintf(stderr, "[PreviewWidget] setCopyOptimizer: optimizer=%p\n", optimizer);
    fflush(stderr);
}

void PreviewWidget::selectRenderPath(uint32_t vendor_id) {
    const RenderProfile profile = CapabilityDetector::detect();
    d_->render_path = profile.path;
    fprintf(stderr, "[PreviewWidget] render path: %s (vendor=0x%04X)\n",
            profile.name, profile.vendor_id);
}

RenderPath PreviewWidget::renderPath() const {
    return d_->render_path;
}

bool PreviewWidget::submitD3D11Frame(VkImage d3d11_staging_vk,
                                     VkImage vulkan_target,
                                     uint32_t width, uint32_t height) {
    if (d3d11_staging_vk == VK_NULL_HANDLE ||
        vulkan_target == VK_NULL_HANDLE ||
        width == 0 || height == 0) {
        return false;
    }
    QMutexLocker lock(&d_->frame_mutex);
    d_->pending_staging_vk = d3d11_staging_vk;
    d_->pending_target_vk  = vulkan_target;
    d_->pending_width      = width;
    d_->pending_height     = height;
    d_->frame_dirty        = true;
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
    return true;
}

void PreviewWidget::setProfiler(rj::FrameProfiler* profiler) {
    profiler_ = profiler;
}

void PreviewWidget::setPipeline(rj::Pipeline* pipeline) noexcept {
    pipeline_ = pipeline;
}

void PreviewWidget::setBridge(rj::pipeline::gpu::ExternalMemoryBridge* b) noexcept {
    bridge_ = b;
    fprintf(stderr, "[PreviewWidget] setBridge: bridge=%p\n", b);
    fflush(stderr);
}

void PreviewWidget::initializeGL() {
    fprintf(stderr, "[PreviewWidget] initializeGL() called\n");
    fflush(stderr);

    initializeOpenGLFunctions();
    glClearColor(0.f, 0.f, 0.f, 1.f);

    bool vs_ok = d_->shader.addShaderFromSourceCode(QOpenGLShader::Vertex,   kVertSrc);
    bool fs_ok = d_->shader.addShaderFromSourceCode(QOpenGLShader::Fragment, kFragSrc);
    bool lk_ok = d_->shader.link();
    fprintf(stderr, "[PreviewWidget] shader compile vs=%d fs=%d link=%d\n", vs_ok, fs_ok, lk_ok);
    if (!lk_ok) fprintf(stderr, "[PreviewWidget] shader log: %s\n",
                        d_->shader.log().toStdString().c_str());

    d_->vao.create();
    d_->vao.bind();
    d_->vbo.create();
    d_->vbo.bind();
    d_->vbo.allocate(kQuadVerts, sizeof(kQuadVerts));
    const int stride = 4 * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<void*>(2 * sizeof(float)));
    d_->vao.release();
    glFinish();  // ensure shader/VBO compiled before first paintGL

    // v0.5.2: GL_EXT_memory_object_win32 extension check (GL interop)
    bool has_mem_obj = context()->hasExtension("GL_EXT_memory_object");
    bool has_win32   = context()->hasExtension("GL_EXT_memory_object_win32");
    fprintf(stderr, "[PreviewWidget] GL_EXT_memory_object=%d win32=%d\n",
            has_mem_obj, has_win32);
    fflush(stderr);

    // Resolve GL extension function pointers
    if (has_mem_obj && has_win32) {
        auto* ctx = QOpenGLContext::currentContext();
        pfn_CreateMemoryObjects_     = (PFNGLCREATEMEMORYOBJECTSEXT)
            ctx->getProcAddress("glCreateMemoryObjectsEXT");
        pfn_DeleteMemoryObjects_     = (PFNGLDELETEMEMORYOBJECTSEXT)
            ctx->getProcAddress("glDeleteMemoryObjectsEXT");
        pfn_ImportMemoryWin32Handle_ = (PFNGLIMPORTMEMORYWIN32HANDLEEXT)
            ctx->getProcAddress("glImportMemoryWin32HandleEXT");
        pfn_TexStorageMem2D_         = (PFNGLTEXSTORAGEMEM2DEXT)
            ctx->getProcAddress("glTexStorageMem2DEXT");

        bool all_ok = pfn_CreateMemoryObjects_ && pfn_ImportMemoryWin32Handle_
                   && pfn_TexStorageMem2D_;
        fprintf(stderr, "[PreviewWidget] GL interop functions resolved: %d\n", all_ok);
        fflush(stderr);
    }
}

void PreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewWidget::paintGL() {
    fprintf(stderr, "[PreviewWidget] paintGL called, bridge_=%p\n", bridge_);
    fflush(stderr);

    if (profiler_) profiler_->markPaintGLStart();

    // v0.5.1: Debug logging and timeout handling
    static int paint_count = 0;
    if (++paint_count % 10 == 0) {
        bool has_pending = false;
        uint32_t frame_dirty = false;
        {
            QMutexLocker lock(&d_->frame_mutex);
            has_pending = d_->has_pending_copy;
            frame_dirty = d_->frame_dirty;
        }
        fprintf(stderr, "[PreviewWidget] paintGL #%d: has_pending=%d frame_dirty=%d copy_opt=%p\n",
                paint_count, has_pending, frame_dirty, copy_optimizer_);
        fflush(stderr);
    }
    // Force-clear pending GPU copy every 60 frames (~1 second at 60fps)
    // Timeline semaphore polling can be delayed; don't block indefinitely
    if (paint_count % 60 == 0 && paint_count > 10) {
        QMutexLocker lock(&d_->frame_mutex);
        if (d_->has_pending_copy) {
            fprintf(stderr, "[PreviewWidget] Force-clearing pending GPU copy (frame #%d)\n", paint_count);
            d_->has_pending_copy = false;
            fflush(stderr);
        }
    }

    // ---- Snapshot state under lock (no heap, no blocking) ----
    bool     was_pending    = false;
    bool     is_ready       = false;
    bool     have_new_frame = false;
    VkImage  staging_vk     = VK_NULL_HANDLE;
    VkImage  target_vk      = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;

    {
        QMutexLocker lock(&d_->frame_mutex);
        // Poll pending GPU copy (async timeline semaphore signal)
        if (d_->has_pending_copy) {
            was_pending = true;
            // Non-blocking poll: check if GPU has signaled the timeline semaphore
            if (copy_optimizer_ &&
                copy_optimizer_->is_copy_ready(d_->timeline_semaphore,
                                              d_->expected_value)) {
                is_ready = true;
                d_->has_pending_copy = false;  // GPU copy complete
            } else {
                // GPU not yet signaled
                // After several frames, assume it's stalled/not critical and move on
                static int poll_frames = 0;
                poll_frames++;
                if (poll_frames > 50) {  // 50 frames = ~830ms at 60fps, plenty of time
                    fprintf(stderr, "[PreviewWidget] Timeout waiting for GPU copy, clearing pending\n");
                    fflush(stderr);
                    d_->has_pending_copy = false;  // Force clear
                    poll_frames = 0;
                }
                is_ready = false;
            }
        }

        // Load new frame (any pending GPU copy from previous frames)
        // ALWAYS allow new frame if available (don't wait for old GPU copy to complete)
        // Worst case: texture shows previous frame, but keeps rendering
        if (d_->frame_dirty && d_->pending_staging_vk != VK_NULL_HANDLE) {
            staging_vk     = d_->pending_staging_vk;
            target_vk      = d_->pending_target_vk;
            w              = d_->pending_width;
            h              = d_->pending_height;
            have_new_frame = true;
            d_->frame_dirty = false;
            d_->has_pending_copy = false;  // Clear any old pending, submit new
        }
    }

    // ---- Handle pending GPU copy ----
    // Note: is_copy_ready() polls async signal value, which may be delayed
    // If ready, clear pending flag. If not ready yet, we'll still render with
    // the latest available texture (might be previous frame, but that's ok).
    if (was_pending && is_ready) {
        // GPU copy complete, clear pending flag
        // (next iteration will load new frame if available)
    }

    // ---- Submit GPU-only copy (non-blocking submit, returns timeline sem) ----
    if (have_new_frame) {
        if (!copy_optimizer_) {
            // No optimizer wired: skip frame.
            if (profiler_) profiler_->markPaintGLEnd();
            return;
        }

        // Lazy texture alloc on first frame / size change.
        // (Data is written by Vulkan compute via OpenGL interop — no CPU upload.)
        if (w != d_->tex_w || h != d_->tex_h) {
            if (d_->tex_id) glDeleteTextures(1, &d_->tex_id);
            glGenTextures(1, &d_->tex_id);
            glBindTexture(GL_TEXTURE_2D, d_->tex_id);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                         GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
            d_->tex_w = w;
            d_->tex_h = h;
        }

        VkSemaphore sem   = VK_NULL_HANDLE;
        uint64_t    value = 0;
        VkImage result_target_image = VK_NULL_HANDLE;
        if (!copy_optimizer_->execute_copy(staging_vk, target_vk, w, h,
                                           &sem, &value, &result_target_image)) {
            fprintf(stderr, "[PreviewWidget] execute_copy failed, skipping frame\n");
            if (profiler_) profiler_->markPaintGLEnd();
            return;
        }
        // Store result for GL interop (will be consumed in future GL interop setup)
        gl_target_image_ = result_target_image;

        // v0.5.2: GL interop — import NT handle → GL texture (per-pool-slot)
        if (bridge_ && gl_target_image_ != VK_NULL_HANDLE && pfn_ImportMemoryWin32Handle_) {
            uint32_t pool_idx = frame_counter_ % 3;  // round-robin: 0,1,2
            current_pool_idx_ = pool_idx;
            frame_counter_++;  // increment for next submit
            HANDLE nt_handle = bridge_->get_gl_target_handle(pool_idx);
            if (nt_handle) {
                // Create GL memory object from Win32 handle if not yet created for this slot
                if (!gl_memory_objects_[pool_idx] && pfn_CreateMemoryObjects_) {
                    pfn_CreateMemoryObjects_(1, &gl_memory_objects_[pool_idx]);
                }
                if (gl_memory_objects_[pool_idx]) {
                    pfn_ImportMemoryWin32Handle_(gl_memory_objects_[pool_idx],
                                                 w * h * 4,  // approximate size (RGBA8)
                                                 GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                                 nt_handle);

                    // Delete old texture if size changed
                    if ((w != gl_target_w_ || h != gl_target_h_) && gl_interop_textures_[pool_idx]) {
                        glDeleteTextures(1, &gl_interop_textures_[pool_idx]);
                        gl_interop_textures_[pool_idx] = 0;
                    }

                    // Create GL texture from memory object
                    if (!gl_interop_textures_[pool_idx]) {
                        glGenTextures(1, &gl_interop_textures_[pool_idx]);
                    }
                    if (gl_interop_textures_[pool_idx] && pfn_TexStorageMem2D_) {
                        glBindTexture(GL_TEXTURE_2D, gl_interop_textures_[pool_idx]);
                        pfn_TexStorageMem2D_(GL_TEXTURE_2D, 1, GL_RGBA8,
                                            w, h, gl_memory_objects_[pool_idx], 0);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        gl_target_w_ = w;
                        gl_target_h_ = h;
                        fprintf(stderr, "[PreviewWidget] GL interop texture [%u] created: %ux%u\n", pool_idx, w, h);
                        fflush(stderr);
                    }
                }
            }
        }

        {
            QMutexLocker lock(&d_->frame_mutex);
            d_->timeline_semaphore = sem;
            d_->expected_value    = value;
            d_->has_pending_copy  = true;
        }
        // Submission queued async; continue to render the previous frame (if available)
        // (don't skip render just because we submitted new work)
    }

    // ---- Render path (previous frame's GPU copy completed) ----
    glClear(GL_COLOR_BUFFER_BIT);

    // Use GL interop texture if available (from current pool slot), otherwise CPU fallback
    GLuint tex_to_use = gl_interop_textures_[current_pool_idx_] ? gl_interop_textures_[current_pool_idx_] : d_->tex_id;
    if (!tex_to_use) {
        fprintf(stderr, "[PreviewWidget] render skipped: no texture (pool_idx=%u, interop=%u, fallback=%u)\n",
                current_pool_idx_, gl_interop_textures_[current_pool_idx_], d_->tex_id);
        if (profiler_) profiler_->markPaintGLEnd();
        return;
    }

    bool use_gl_interop = (tex_to_use == gl_interop_textures_[current_pool_idx_]);
    if (use_gl_interop) {
        fprintf(stderr, "[PreviewWidget] render: GL interop texture[%u] (%u)\n", current_pool_idx_, tex_to_use);
    } else {
        fprintf(stderr, "[PreviewWidget] render: CPU fallback texture (%u)\n", tex_to_use);
    }

    d_->shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_to_use);
    d_->shader.setUniformValue("uTex", 0);
    d_->vao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    d_->vao.release();
    d_->shader.release();
    // Note: Timeline semaphore gates Vulkan→OpenGL data dependency
    // (vkQueueSubmit is async; GPU→GL sync happens at next frame's is_copy_ready() poll)

    if (profiler_) profiler_->markPaintGLEnd();
}

} // namespace reji
#endif // QT6_AVAILABLE
