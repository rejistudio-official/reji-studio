#include "preview_widget.h"
#ifdef QT6_AVAILABLE

#include "render_capability.h"
#include <QImage>
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
// with images that are top-down (D3D / QImage).
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

    QMutex frame_mutex;
    QImage pending_frame;   // written by pipeline thread, consumed in paintGL
    bool   frame_dirty{false};

    // --- Render path ---
    RenderPath render_path{RenderPath::kPbo};

    // --- PBO ping-pong (both kPbo and kNvDxInterop stub) ---
    GLuint pbo[2]{0, 0};
    int    pbo_idx{0};    // current write index; read index = pbo_idx ^ 1
    int    pbo_frame{0};  // frames uploaded; guard: skip read until >= 1
    size_t pbo_size{0};   // last allocated size = w * h * 4
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
    if (d_->pbo[0]) glDeleteBuffers(2, d_->pbo);
    d_->vbo.destroy();
    d_->vao.destroy();
    doneCurrent();
}

void PreviewWidget::selectRenderPath(uint32_t vendor_id) {
    const RenderProfile profile = CapabilityDetector::detect(vendor_id);
    d_->render_path = profile.path;
    fprintf(stderr, "[PreviewWidget] render path: %s (vendor=0x%04X)\n",
            profile.name, vendor_id);
}

RenderPath PreviewWidget::renderPath() const {
    return d_->render_path;
}

void PreviewWidget::uploadFrame(const void* bgra_pixels, int width, int height, int row_pitch) {
    // v0.1 CPU staging-buffer path:
    //   Caller has already MapSubResource'd a D3D11 staging texture to get bgra_pixels.
    //   We deep-copy here so the caller can Unmap immediately after this call returns.
    //
    // v0.2: replace this function with WGL_NV_DX_INTEROP texture sharing to
    //   avoid the CPU copy entirely — call wglDXRegisterObjectNV once, then just
    //   wglDXLockObjectsNV / wglDXUnlockObjectsNV around each paintGL.
    QMutexLocker lock(&d_->frame_mutex);
    d_->pending_frame = QImage(
        static_cast<const uchar*>(bgra_pixels),
        width, height, row_pitch,
        QImage::Format_ARGB32   // BGRA on little-endian == Qt's ARGB32
    ).copy();
    d_->frame_dirty = true;
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
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

    // PBO objects — content allocated lazily on first frame (size unknown here).
    glGenBuffers(2, d_->pbo);
    glFinish();
    DwmFlush();
}

void PreviewWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewWidget::paintGL() {
    // --- HOT-PATH: no heap allocation after the first frame ---
    DwmFlush();
    glClear(GL_COLOR_BUFFER_BIT);

    {
        QMutexLocker lock(&d_->frame_mutex);
        if (d_->frame_dirty && !d_->pending_frame.isNull()) {
            // DXGI sends BGRA data → convert to BGRA8888, not RGBA8888
            const QImage img = d_->pending_frame.convertToFormat(QImage::Format_BGRA8888);

            const size_t needed = static_cast<size_t>(img.width())
                                * static_cast<size_t>(img.height()) * 4u;

            // --- Resize / first-alloc: orphan both PBOs, reset texture and guard ---
            if (needed != d_->pbo_size) {
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, d_->pbo[0]);
                glBufferData(GL_PIXEL_UNPACK_BUFFER,
                             static_cast<GLsizeiptr>(needed),
                             nullptr, GL_STREAM_DRAW);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, d_->pbo[1]);
                glBufferData(GL_PIXEL_UNPACK_BUFFER,
                             static_cast<GLsizeiptr>(needed),
                             nullptr, GL_STREAM_DRAW);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
                d_->pbo_size  = needed;
                d_->pbo_frame = 0;   // guard: read PBO not valid yet

                if (d_->tex_id) glDeleteTextures(1, &d_->tex_id);
                glGenTextures(1, &d_->tex_id);
                glBindTexture(GL_TEXTURE_2D, d_->tex_id);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             img.width(), img.height(), 0,
                             GL_BGRA, GL_UNSIGNED_BYTE, nullptr);
                d_->tex_w = img.width();
                d_->tex_h = img.height();
            }

            const int write_idx = d_->pbo_idx;
            const int read_idx  = d_->pbo_idx ^ 1;

            // CPU → PBO[write] (async DMA start, returns immediately)
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, d_->pbo[write_idx]);
            glBufferData(GL_PIXEL_UNPACK_BUFFER,
                         static_cast<GLsizeiptr>(needed),
                         img.constBits(), GL_STREAM_DRAW);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

            // PBO[read] → texture (GPU DMA from previous frame's PBO)
            // Guard: skip on frame 0 — read PBO is still empty.
            if (d_->pbo_frame >= 1) {
                glBindTexture(GL_TEXTURE_2D, d_->tex_id);
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, d_->pbo[read_idx]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                img.width(), img.height(),
                                GL_BGRA, GL_UNSIGNED_BYTE,
                                nullptr);  // offset into PBO
                glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            }

            d_->pbo_idx ^= 1;
            d_->pbo_frame++;
            d_->frame_dirty = false;
        }
    }

    if (!d_->tex_id) return;

    d_->shader.bind();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, d_->tex_id);
    d_->shader.setUniformValue("uTex", 0);
    d_->vao.bind();
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    d_->vao.release();
    d_->shader.release();
}

} // namespace reji
#endif // QT6_AVAILABLE
