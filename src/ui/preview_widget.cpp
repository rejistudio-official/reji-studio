#include "preview_widget.h"
#ifdef QT6_AVAILABLE

#include "render_capability.h"
#include "../pipeline/copy_optimizer.h"
#include "../pipeline/include/frame_profiler.h"
#include "../pipeline/gpu/vulkan_initializer.h"
#include "../pipeline/include/pipeline.h"
#include <QByteArray>
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
    void main() { FragColor = texture(uTex, vUV).bgra; }
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
    uint32_t pending_slot       = 0;  // I23: bridge pool slot'u (tek doğruluk kaynağı)
    bool     frame_dirty        = false;

    // CPU fallback path (WGC staging)
    QByteArray cpu_bgra;
    int        cpu_w           = 0;
    int        cpu_h           = 0;
    int        cpu_pitch       = 0;
    bool       cpu_frame_dirty = false;

    QByteArray cpu_bgra_buf_;      // pre-allocated, reuse across frames
    int        cpu_buf_capacity_ = 0;

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
    // D5: GL draw fence cleanup
    for (int i = 0; i < 3; ++i) {
        if (gl_draw_fences_[i] && pfn_DeleteSync_) {
            pfn_DeleteSync_(gl_draw_fences_[i]);
            gl_draw_fences_[i] = nullptr;
        }
    }
    // B5/C7: GL sync semaphore pool cleanup
    {
        auto pfn_DeleteSemaphores = (void(*)(GLsizei, const GLuint*))
            QOpenGLContext::currentContext()->getProcAddress("glDeleteSemaphoresEXT");
        if (pfn_DeleteSemaphores) {
            for (int i = 0; i < 3; ++i) {
                if (gl_sync_semaphores_[i]) {
                    pfn_DeleteSemaphores(1, &gl_sync_semaphores_[i]);
                    gl_sync_semaphores_[i] = 0;
                }
            }
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
                                     uint32_t width, uint32_t height,
                                     uint32_t pool_slot) {
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
    d_->pending_slot       = pool_slot;  // I23: bridge slot'unu paintGL'e taşı
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

void PreviewWidget::uploadCpuFrame(const void* bgra, int w, int h, int pitch) {
    const int needed = pitch * h;
    QMutexLocker lk(&d_->frame_mutex);

    if (needed > d_->cpu_buf_capacity_) {
        d_->cpu_bgra_buf_.resize(needed);
        d_->cpu_buf_capacity_ = needed;
    }
    memcpy(d_->cpu_bgra_buf_.data(), bgra, needed);
    d_->cpu_bgra        = d_->cpu_bgra_buf_;  // shallow copy (QByteArray COW)
    d_->cpu_w           = w;
    d_->cpu_h           = h;
    d_->cpu_pitch       = pitch;
    d_->cpu_frame_dirty = true;
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
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

    // GL capability detection — timeline semaphore ve interop extension'ları
    struct GlCaps {
        bool timeline_semaphore;
        bool binary_semaphore;
        bool win32_semaphore;
        bool memory_object;
        bool memory_object_win32;
    };

    auto detect_gl_caps = [this](QOpenGLContext* ctx) -> GlCaps {
        GlCaps caps{};
        caps.timeline_semaphore  = ctx->hasExtension("GL_EXT_timeline_semaphore");
        caps.binary_semaphore    = ctx->hasExtension("GL_EXT_semaphore");
        caps.win32_semaphore     = ctx->hasExtension("GL_EXT_semaphore_win32");
        caps.memory_object       = ctx->hasExtension("GL_EXT_memory_object");
        caps.memory_object_win32 = ctx->hasExtension("GL_EXT_memory_object_win32");

        fprintf(stderr, "[GL Caps] Renderer: %s\n",
                (const char*)glGetString(GL_RENDERER));
        fprintf(stderr, "[GL Caps] GL_EXT_timeline_semaphore : %s\n",
                caps.timeline_semaphore  ? "YES" : "NO");
        fprintf(stderr, "[GL Caps] GL_EXT_semaphore          : %s\n",
                caps.binary_semaphore    ? "YES" : "NO");
        fprintf(stderr, "[GL Caps] GL_EXT_semaphore_win32    : %s\n",
                caps.win32_semaphore     ? "YES" : "NO");
        fprintf(stderr, "[GL Caps] GL_EXT_memory_object      : %s\n",
                caps.memory_object       ? "YES" : "NO");
        fprintf(stderr, "[GL Caps] GL_EXT_memory_object_win32: %s\n",
                caps.memory_object_win32 ? "YES" : "NO");
        return caps;
    };

    auto gl_caps = detect_gl_caps(QOpenGLContext::currentContext());

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

    // D5: GL 3.2 core sync object function pointers
    {
        auto* ctx = QOpenGLContext::currentContext();
        pfn_FenceSync_      = (PFNGLFENCESYNCPROC)      ctx->getProcAddress("glFenceSync");
        pfn_DeleteSync_     = (PFNGLDELETESYNCPROC)     ctx->getProcAddress("glDeleteSync");
        pfn_ClientWaitSync_ = (PFNGLCLIENTWAITSYNCPROC) ctx->getProcAddress("glClientWaitSync");
        fprintf(stderr, "[PreviewWidget] GL sync procs: fence=%p delete=%p wait=%p\n",
                (void*)pfn_FenceSync_, (void*)pfn_DeleteSync_, (void*)pfn_ClientWaitSync_);
        fflush(stderr);
    }

    // B5: GL_EXT_semaphore_win32 — Vulkan/GL sync semaphore
    bool has_sem     = context()->hasExtension("GL_EXT_semaphore");
    bool has_sem_w32 = context()->hasExtension("GL_EXT_semaphore_win32");
    fprintf(stderr, "[PreviewWidget] GL_EXT_semaphore=%d win32=%d\n", has_sem, has_sem_w32);
    fflush(stderr);

    if (has_sem && has_sem_w32) {
        auto* ctx = QOpenGLContext::currentContext();
        pfn_WaitSemaphore_ = (PFNGLWAITSEMAPHOREEXT)
            ctx->getProcAddress("glWaitSemaphoreEXT");
        pfn_ImportSemaphoreWin32Handle_ = (PFNGLIMPORTSEMAPHOREWIN32HANDLEEXT)
            ctx->getProcAddress("glImportSemaphoreWin32HandleEXT");

        bool fns_ok = pfn_WaitSemaphore_ && pfn_ImportSemaphoreWin32Handle_;
        fprintf(stderr, "[PreviewWidget] GL semaphore functions resolved: %d\n", fns_ok);
        fflush(stderr);

        if (fns_ok && bridge_) {
            auto pfn_GenSemaphores = (void(*)(GLsizei, GLuint*))
                ctx->getProcAddress("glGenSemaphoresEXT");
            if (pfn_GenSemaphores) {
                pfn_GenSemaphores(3, gl_sync_semaphores_);
                for (uint32_t slot = 0; slot < 3; ++slot) {
                    HANDLE h = bridge_->get_gl_sync_semaphore_handle(slot);
                    if (h && gl_sync_semaphores_[slot]) {
                        pfn_ImportSemaphoreWin32Handle_(gl_sync_semaphores_[slot],
                            GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, h);
                        fprintf(stderr, "[PreviewWidget] GL sync semaphore[%u] imported (sem=%u)\n",
                                slot, gl_sync_semaphores_[slot]);
                        fflush(stderr);
                    }
                }
            }
        }
    }
}

void PreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewWidget::paintGL() {
    static int paint_count = 0;
#ifdef RJ_DEBUG_VERBOSE
    fprintf(stderr, "[PreviewWidget] paintGL called, bridge_=%p\n", bridge_);
    fflush(stderr);
#endif

    if (profiler_) profiler_->markPaintGLStart();
    if (++paint_count % 10 == 0) {
        bool has_pending = false;
        uint32_t frame_dirty = false;
        {
            QMutexLocker lock(&d_->frame_mutex);
            has_pending = d_->has_pending_copy;
            frame_dirty = d_->frame_dirty;
        }
#ifdef RJ_DEBUG_VERBOSE
        fprintf(stderr, "[PreviewWidget] paintGL #%d: has_pending=%d frame_dirty=%d copy_opt=%p\n",
                paint_count, has_pending, frame_dirty, copy_optimizer_);
        fflush(stderr);
#endif
    }
    // ---- Snapshot state under lock (no heap, no blocking) ----
    bool     was_pending    = false;
    bool     is_ready       = false;
    bool     have_new_frame = false;
    VkImage  staging_vk     = VK_NULL_HANDLE;
    VkImage  target_vk      = VK_NULL_HANDLE;
    uint32_t w = 0, h = 0;
    uint32_t bridge_slot    = 0;  // I23: bu image çiftini üreten bridge pool slot'u
    bool       have_cpu_frame = false;
    int        cpu_fw = 0, cpu_fh = 0;
    QByteArray cpu_fdata;

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
                poll_frames_ = 0;
            } else {
                // GPU not yet signaled
                // After several frames, assume it's stalled/not critical and move on
                poll_frames_++;
                if (poll_frames_ > 50) {  // 50 frames = ~830ms at 60fps, plenty of time
#ifdef RJ_DEBUG_VERBOSE
                    fprintf(stderr, "[PreviewWidget] Timeout waiting for GPU copy, clearing pending\n");
                    fflush(stderr);
#endif
                    d_->has_pending_copy = false;  // Force clear
                    poll_frames_ = 0;
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
            bridge_slot    = d_->pending_slot;  // I23: bridge slot'unu kilit altında al
            have_new_frame = true;
            d_->frame_dirty = false;
            d_->has_pending_copy = false;  // Clear any old pending, submit new
        }

        if (d_->cpu_frame_dirty) {
            cpu_fdata       = d_->cpu_bgra;
            cpu_fw          = d_->cpu_w;
            cpu_fh          = d_->cpu_h;
            have_cpu_frame  = true;
            d_->cpu_frame_dirty = false;
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

    // ---- CPU fallback upload (WGC staging path) ----
    // Upload BGRA bytes with GL_RGBA format so the shader's .bgra swizzle corrects
    // channel order to match the Vulkan interop path (both end up R,G,B,A in FragColor).
    if (have_cpu_frame && cpu_fw > 0 && cpu_fh > 0) {
        if (!d_->tex_id) glGenTextures(1, &d_->tex_id);
        glBindTexture(GL_TEXTURE_2D, d_->tex_id);
        if (cpu_fw != d_->tex_w || cpu_fh != d_->tex_h) {
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, cpu_fw, cpu_fh, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, cpu_fdata.constData());
            d_->tex_w = cpu_fw;
            d_->tex_h = cpu_fh;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, cpu_fw, cpu_fh,
                            GL_RGBA, GL_UNSIGNED_BYTE, cpu_fdata.constData());
        }
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    // ---- Submit GPU-only copy (non-blocking submit, returns timeline sem) ----
    if (have_new_frame) {
        if (!copy_optimizer_) {
            // No optimizer wired: skip frame.
            if (profiler_) profiler_->markPaintGLEnd();
            return;
        }

        // K1: capture çözünürlüğü değişti mi? (DXGI-recovery / display-res değişimi —
        // encoder DRC/healing DEĞİL, o capture dims'e dokunmaz.) GL target pool startup'ta
        // bir kez init edilip resize'da yeniden kurulmadığından init boyutunda donuyordu:
        // execute_copy blit dst-extent'i (Vulkan) + aşağıdaki glTexStorageMem (GL) init-boyut
        // memory'yi aşardı (UB, özellikle resize-UP). Pool'u BÜTÜN olarak yeni boyutta yeniden
        // kur — execute_copy ÖNCESİ, aynı paintGL/GL-thread akışında sıralı, yani yarı-kurulmuş
        // pool'u hiçbir kopya görmez (cross-thread pending yarışı yok):
        //   1) Vulkan tarafı (bridge): GPU-idle + image/memory/handle rebuild (idempotent)
        //   2) GL tarafı: tüm memory object + interop texture geçersiz → sonraki per-slot blok
        //      yeni NT handle'ı re-import eder (satır ~516'daki once-guard böylece resetlenir).
        if (bridge_ && (w != gl_pool_w_ || h != gl_pool_h_)) {
            bridge_->resize_gl_target_pool(w, h);
            // target_vk pending'den geldi ve resize eski image'i YOK ETTİ → dangling.
            // Yeni pool'dan bu slot'un target'ını yeniden çek (ofset bridge içinde).
            target_vk = bridge_->get_execute_target(bridge_slot);
            for (int i = 0; i < 3; ++i) {
                if (gl_interop_textures_[i]) {
                    glDeleteTextures(1, &gl_interop_textures_[i]);
                    gl_interop_textures_[i] = 0;
                }
                if (gl_memory_objects_[i] && pfn_DeleteMemoryObjects_) {
                    pfn_DeleteMemoryObjects_(1, &gl_memory_objects_[i]);
                    gl_memory_objects_[i] = 0;
                }
            }
            gl_pool_w_ = w;
            gl_pool_h_ = h;
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
        // I23: bridge slot'u TEK doğruluk kaynağı — bu image çiftini üreten slot.
        //      GL-interop indexlemesi, execute_copy ve fence-wait aynı slot'u kullanır
        //      (eski last_used_slot()/next_slot() paralel-sayaç drift'i elendi).
        uint32_t pool_idx = bridge_slot;
        current_pool_idx_ = pool_idx;
        // K3: execute_copy YAZDIĞI image = image[(pool_idx+1)%3] (ping-pong ofseti — producer
        //     bir kare ileriye üretir, consumer image[pool_idx]'i okur). GL sync semaphore,
        //     slot_gl_signaled_ ve pre-write fence bu YAZILAN image ile indekslenmeli (sync
        //     index = image index) — aksi halde semaphore/fence yanlış image'ı korur (off-by-one).
        //     Consumer (render) tarafı doğal image-index'i (current_pool_idx_) kullanır, değişmez.
        uint32_t gl_signal_slot = (pool_idx + 1) % 3;
        VkSemaphore gl_sync_sem = bridge_
            ? bridge_->get_gl_sync_semaphore(gl_signal_slot)
            : VK_NULL_HANDLE;
        // H2: keyed mutex must protect shared texture memory, not the per-frame staging image
        VkDeviceMemory staging_mem = bridge_
            ? bridge_->get_shared_texture_memory()
            : VK_NULL_HANDLE;
        // K6 (savunma-derinliği, I13/J13 tripwire): keyed-mutex staging_mem'i korur; tüm pool
        // slotları AYNI D3D11 shared-texture memory'sini alias eder (Zig I32 tasarımı — Faz 0'da
        // doğrulandı). Bu değişmez bozulursa keyed-mutex YANLIŞ VkDeviceMemory'yi korur, senkronu
        // sessizce geçersiz kılar. Ucuz pointer-karşılaştırması; ASLA tetiklenmemeli — tetiklenirse
        // Zig-tarafı aliasing regresyonudur (loud log, kare düşürülmez: yalnız teşhis tripwire).
        if (bridge_ && staging_mem != VK_NULL_HANDLE && staging_vk != VK_NULL_HANDLE) {
            VkDeviceMemory actual_mem = bridge_->get_staging_memory_for_image(staging_vk);
            if (actual_mem != staging_mem) {
                fprintf(stderr, "[PreviewWidget] K6 IHLALI: keyed-mutex mem=%p != staging image mem=%p "
                                "— pool aliasing bozuldu, sync gecersiz olabilir\n",
                        (void*)staging_mem, (void*)actual_mem);
                fflush(stderr);
            }
        }
        // D5+K3: execute_copy'nin YAZACAĞI image'in (gl_signal_slot) GL-okuma fence'ini bekle —
        //        GL o image'i okumayı bitirmeden Vulkan üzerine yazmasın (doğru image, off-by-one
        //        düzeltildi). İlk kullanımda fence null → atlanır: o image hiç okunmamış, WAR yok (K5).
        if (gl_draw_fences_[gl_signal_slot] && pfn_ClientWaitSync_) {
            GLenum ws = pfn_ClientWaitSync_(gl_draw_fences_[gl_signal_slot],
                                            GL_SYNC_FLUSH_COMMANDS_BIT, 1'000'000);
            // K4: dönüşü kontrol et (eskiden atılıyordu). ALREADY_SIGNALED/CONDITION_SATISFIED
            // dışında (TIMEOUT_EXPIRED/WAIT_FAILED) GL hâlâ image[gl_signal_slot]'i okuyor
            // olabilir → üzerine YAZMA (WAR), bu preview karesini düşür. Mevcut execute_copy-fail
            // deseniyle simetrik (markPaintGLEnd + return). 1ms bounded kalır — uzatmak GL/UI
            // thread'ini bloklardı (K2 felsefesi: boz-mak yerine kareyi at).
            if (ws != GL_ALREADY_SIGNALED && ws != GL_CONDITION_SATISFIED) {
                if (profiler_) profiler_->markPaintGLEnd();
                return;
            }
        }
        bool ok = copy_optimizer_->execute_copy(staging_vk, target_vk, w, h, pool_idx,
                                           gl_signal_slot,
                                           &sem, &value, &result_target_image,
                                           gl_sync_sem, staging_mem);
        if (!ok) {
#ifdef RJ_DEBUG_VERBOSE
            fprintf(stderr, "[PreviewWidget] execute_copy failed, skipping frame\n");
#endif
            if (profiler_) profiler_->markPaintGLEnd();
            return;
        }
        // Store result for GL interop (will be consumed in future GL interop setup)
        gl_target_image_ = result_target_image;

        // v0.5.2: GL interop — import NT handle → GL texture (per-pool-slot)
        if (bridge_ && gl_target_image_ != VK_NULL_HANDLE && pfn_ImportMemoryWin32Handle_) {
            HANDLE nt_handle = bridge_->get_gl_target_handle(pool_idx);
            if (nt_handle) {
                // Create GL memory object and import NT handle — only once per pool slot
                if (!gl_memory_objects_[pool_idx] && pfn_CreateMemoryObjects_) {
                    pfn_CreateMemoryObjects_(1, &gl_memory_objects_[pool_idx]);
                    if (gl_memory_objects_[pool_idx]) {
                        VkDeviceSize exact_size = bridge_->gl_target_size(pool_idx);
                        pfn_ImportMemoryWin32Handle_(gl_memory_objects_[pool_idx],
                                                     exact_size,  // G6: exact VkMemoryRequirements::size
                                                     GL_HANDLE_TYPE_OPAQUE_WIN32_EXT,
                                                     nt_handle);
                    }
                }
                if (gl_memory_objects_[pool_idx]) {
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
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                        glBindTexture(GL_TEXTURE_2D, 0);
                        gl_target_w_ = w;
                        gl_target_h_ = h;
#ifdef RJ_DEBUG_VERBOSE
                        fprintf(stderr, "[PreviewWidget] GL interop texture [%u] created: %ux%u\n", pool_idx, w, h);
                        fflush(stderr);
#endif
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
#ifdef RJ_DEBUG_VERBOSE
        fprintf(stderr, "[PreviewWidget] render skipped: no texture (pool_idx=%u, interop=%u, fallback=%u)\n",
                current_pool_idx_, gl_interop_textures_[current_pool_idx_], d_->tex_id);
#endif
        if (profiler_) profiler_->markPaintGLEnd();
        return;
    }

    bool use_gl_interop = (tex_to_use == gl_interop_textures_[current_pool_idx_]);
#ifdef RJ_DEBUG_VERBOSE
    if (use_gl_interop) {
        fprintf(stderr, "[PreviewWidget] render: GL interop texture[%u] (%u)\n", current_pool_idx_, tex_to_use);
    } else {
        fprintf(stderr, "[PreviewWidget] render: CPU fallback texture (%u)\n", tex_to_use);
    }
#endif

    // B5/E3: GPU-side sync — sadece Vulkan bu slotu sinyallediyse bekle (double-wait önlemi)
    if (use_gl_interop && pfn_WaitSemaphore_ && gl_sync_semaphores_[current_pool_idx_]
        && copy_optimizer_ && copy_optimizer_->is_slot_signaled(current_pool_idx_)) {
        GLenum layout = GL_LAYOUT_SHADER_READ_ONLY_EXT;
        pfn_WaitSemaphore_(gl_sync_semaphores_[current_pool_idx_], 0, nullptr,
                           1, &tex_to_use, &layout);
        // D12: semaphore tüketildi — execute_copy bu slotu yeniden signal edebilir
        copy_optimizer_->clear_gl_signal(current_pool_idx_);
    }

    d_->shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_to_use);
    d_->shader.setUniformValue("uTex", 0);
    d_->vao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    d_->vao.release();
    d_->shader.release();

    // D5: record fence for this slot so the next Vulkan write waits for GL to finish
    if (pfn_DeleteSync_ && gl_draw_fences_[current_pool_idx_])
        pfn_DeleteSync_(gl_draw_fences_[current_pool_idx_]);
    gl_draw_fences_[current_pool_idx_] = pfn_FenceSync_
        ? pfn_FenceSync_(GL_SYNC_GPU_COMMANDS_COMPLETE, 0) : nullptr;

    if (profiler_) profiler_->markPaintGLEnd();
}

} // namespace reji
#endif // QT6_AVAILABLE
