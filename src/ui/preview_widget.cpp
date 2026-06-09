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

void PreviewWidget::setBridge(ExternalMemoryBridge* b) noexcept {
    bridge_ = b;
    fprintf(stderr, "[PreviewWidget] setBridge: bridge=%p\n", b);
    fflush(stderr);
}

void PreviewWidget::initializeGL() {
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

    // v0.5.1: One-shot Vulkan late-binding
    if (pipeline_) {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        if (vk && vk->device()) {
            pipeline_->notify_vulkan_ready(vk->device(), vk->physical_device());
            fprintf(stderr, "[PreviewWidget] notify_vulkan_ready: device=%p\n", (void*)vk->device());
            fflush(stderr);
        }
    }

    // v0.5.2: GL_EXT_memory_object_win32 extension check (GL interop)
    bool has_mem_obj = context()->hasExtension("GL_EXT_memory_object");
    bool has_win32   = context()->hasExtension("GL_EXT_memory_object_win32");
    fprintf(stderr, "[PreviewWidget] GL_EXT_memory_object=%d win32=%d\n",
            has_mem_obj, has_win32);
    fflush(stderr);
}

void PreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewWidget::paintGL() {
    if (profiler_) profiler_->markPaintGLStart();

    // v0.5.1: Debug logging for frame flow
    static int paint_count = 0;
    if (++paint_count % 30 == 0) {
        fprintf(stderr, "[PreviewWidget] paintGL #%d, copy_optimizer_=%p\n", paint_count, copy_optimizer_);
        fflush(stderr);
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
        if (d_->has_pending_copy) {
            was_pending = true;
            // Non-blocking poll: vkGetSemaphoreCounterValueKHR under the hood.
            if (copy_optimizer_ &&
                copy_optimizer_->is_copy_ready(d_->timeline_semaphore,
                                              d_->expected_value)) {
                is_ready = true;
                d_->has_pending_copy = false;
            }
        }
        if (!d_->has_pending_copy && d_->frame_dirty &&
            d_->pending_staging_vk != VK_NULL_HANDLE) {
            staging_vk     = d_->pending_staging_vk;
            target_vk      = d_->pending_target_vk;
            w              = d_->pending_width;
            h              = d_->pending_height;
            have_new_frame = true;
            d_->frame_dirty = false;
        }
    }

    // ---- GPU copy not yet complete: skip frame (non-blocking, no stall) ----
    if (was_pending && !is_ready) {
        if (profiler_) profiler_->markPaintGLEnd();
        return;
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

        // v0.5.2: GL interop — import NT handle → GL texture
        if (bridge_ && gl_target_image_ != VK_NULL_HANDLE) {
            HANDLE nt_handle = bridge_->get_gl_target_handle(static_cast<uint32_t>(w % 3)); // round-robin
            if (nt_handle) {
                // Create GL memory object from Win32 handle (GL_EXT_memory_object_win32)
                if (!gl_memory_object_) {
                    glCreateMemoryObjectsEXT(1, &gl_memory_object_);
                }
                if (gl_memory_object_) {
                    glImportMemoryWin32HandleEXT(gl_memory_object_,
                                                 w * h * 4,  // approximate size (RGBA8)
                                                 GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                                 nt_handle);

                    // Create GL texture from memory object
                    if (!gl_interop_texture_) {
                        glGenTextures(1, &gl_interop_texture_);
                    }
                    if (gl_interop_texture_) {
                        glBindTexture(GL_TEXTURE_2D, gl_interop_texture_);
                        glTexStorageMem2DEXT(GL_TEXTURE_2D, 1, GL_RGBA8,
                                            w, h, gl_memory_object_, 0);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        fprintf(stderr, "[PreviewWidget] GL interop texture created: %ux%u\n", w, h);
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
        // First submission: copy just queued, not ready — skip render, check next tick.
        if (profiler_) profiler_->markPaintGLEnd();
        return;
    }

    // ---- Render path (previous frame's GPU copy completed) ----
    glClear(GL_COLOR_BUFFER_BIT);

    // Use GL interop texture if available, otherwise CPU upload texture
    GLuint tex_to_use = gl_interop_texture_ ? gl_interop_texture_ : d_->tex_id;
    if (!tex_to_use) {
        if (profiler_) profiler_->markPaintGLEnd();
        return;
    }

    d_->shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_to_use);
    d_->shader.setUniformValue("uTex", 0);
    d_->vao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    d_->vao.release();
    d_->shader.release();
    // No glFinish(): swap-chain present handles GPU↔CPU sync; timeline sem
    // already gated the cross-API (Vulkan→OpenGL) data dependency.

    if (profiler_) profiler_->markPaintGLEnd();
}

} // namespace reji
#endif // QT6_AVAILABLE
