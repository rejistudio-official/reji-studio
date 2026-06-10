#include "program_widget.h"
#ifdef QT6_AVAILABLE

#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QTimer>
#include <algorithm>

namespace {

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

// Cut shader: renders texA directly — single-texture, no blending.
const char* kCutFragSrc = R"glsl(
    #version 330 core
    uniform sampler2D uTexA;
    in  vec2 vUV;
    out vec4 FragColor;
    void main() { FragColor = texture(uTexA, vUV); }
)glsl";

// Fade shader: GLSL mix() blends outgoing (A) into incoming (B).
// uAlpha = 0.0 → full A (outgoing scene), 1.0 → full B (incoming scene).
const char* kFadeFragSrc = R"glsl(
    #version 330 core
    uniform sampler2D uTexA;    // outgoing
    uniform sampler2D uTexB;    // incoming
    uniform float     uAlpha;
    in  vec2 vUV;
    out vec4 FragColor;
    void main() {
        vec4 a = texture(uTexA, vUV);
        vec4 b = texture(uTexB, vUV);
        FragColor = mix(a, b, uAlpha);
    }
)glsl";

} // namespace

namespace reji {

class ProgramWidget::Impl {
public:
    QOpenGLShaderProgram     cut_shader;
    QOpenGLShaderProgram     fade_shader;
    QOpenGLVertexArrayObject vao;
    QOpenGLBuffer            vbo{QOpenGLBuffer::VertexBuffer};

    // tex_a = currently displayed (outgoing during fade).
    // tex_b = incoming scene (populated by uploadFrame() during fade transition).
    GLuint tex_a{0}, tex_b{0};
    int    tex_w{0}, tex_h{0};

    QMutex frame_mutex;
    QImage pending_frame;
    bool   frame_dirty{false};

    Transition active_transition{Transition::Cut};
    float      fade_alpha{1.f};   // 0.0 → full A, 1.0 → full B (transition done)
    float      fade_step{0.f};    // alpha increment per 16 ms tick

    QTimer* anim_timer{nullptr};
};

ProgramWidget::ProgramWidget(QWidget* parent)
    : QOpenGLWidget(parent), d_(std::make_unique<Impl>())
{
    setMinimumSize(320, 180);
    d_->anim_timer = new QTimer(this);
    d_->anim_timer->setInterval(16);
    connect(d_->anim_timer, &QTimer::timeout, this, &ProgramWidget::advanceTransition);
}

ProgramWidget::~ProgramWidget() {
    makeCurrent();
    if (d_->tex_a) glDeleteTextures(1, &d_->tex_a);
    if (d_->tex_b) glDeleteTextures(1, &d_->tex_b);
    d_->vbo.destroy();
    d_->vao.destroy();
    doneCurrent();
}

void ProgramWidget::uploadFrame(const void* bgra_pixels, int width, int height, int row_pitch) {
    // v0.1 CPU staging-buffer path.
    // v0.2: replace with WGL_NV_DX_INTEROP (wglDXRegisterObjectNV) for zero-copy.
    QMutexLocker lock(&d_->frame_mutex);
    d_->pending_frame = QImage(
        static_cast<const uchar*>(bgra_pixels),
        width, height, row_pitch,
        QImage::Format_ARGB32
    ).copy();
    d_->frame_dirty = true;
    QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
}

void ProgramWidget::beginTransition(Transition type, int duration_ms) {
    d_->active_transition = type;
    if (type == Transition::Cut) {
        // B12: defer swap to paintGL — avoids race with concurrent uploadFrame on tex_a
        transition_requested_.store(true);
        d_->fade_alpha = 1.f;
        update();
    } else {
        // B12: defer swap to paintGL — tex_b gets the outgoing snapshot at paint time
        transition_requested_.store(true);
        d_->fade_alpha = 0.f;
        d_->fade_step  = (duration_ms > 0)
            ? (16.f / static_cast<float>(duration_ms))
            : 1.f;
        d_->anim_timer->start();
    }
}

void ProgramWidget::advanceTransition() {
    d_->fade_alpha = std::min(1.f, d_->fade_alpha + d_->fade_step);
    if (d_->fade_alpha >= 1.f) {
        d_->anim_timer->stop();
        d_->fade_alpha = 1.f;
    }
    update();
}

void ProgramWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.f, 0.f, 0.f, 1.f);

    auto buildShader = [](QOpenGLShaderProgram& prog,
                          const char* vert, const char* frag) {
        prog.addShaderFromSourceCode(QOpenGLShader::Vertex,   vert);
        prog.addShaderFromSourceCode(QOpenGLShader::Fragment, frag);
        prog.link();
    };
    buildShader(d_->cut_shader,  kVertSrc, kCutFragSrc);
    buildShader(d_->fade_shader, kVertSrc, kFadeFragSrc);

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
}

void ProgramWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void ProgramWidget::paintGL() {
    // --- HOT-PATH: no heap allocation after the first frame ---
    glClear(GL_COLOR_BUFFER_BIT);

    // B12: consume deferred swap atomically before upload so uploadFrame writes the correct tex_a
    if (transition_requested_.exchange(false)) {
        std::swap(d_->tex_a, d_->tex_b);
    }

    // Upload any pending frame into tex_a (the "current/incoming" texture).
    {
        QMutexLocker lock(&d_->frame_mutex);
        if (d_->frame_dirty && !d_->pending_frame.isNull()) {
            const QImage img = d_->pending_frame.convertToFormat(QImage::Format_RGBA8888);
            if (!d_->tex_a || img.width() != d_->tex_w || img.height() != d_->tex_h) {
                if (d_->tex_a) glDeleteTextures(1, &d_->tex_a);
                glGenTextures(1, &d_->tex_a);
                glBindTexture(GL_TEXTURE_2D, d_->tex_a);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             img.width(), img.height(), 0,
                             GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
                d_->tex_w = img.width();
                d_->tex_h = img.height();
            } else {
                glBindTexture(GL_TEXTURE_2D, d_->tex_a);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                img.width(), img.height(),
                                GL_RGBA, GL_UNSIGNED_BYTE, img.constBits());
            }
            d_->frame_dirty = false;
        }
    }

    if (!d_->tex_a) return;

    const bool fading = (d_->active_transition == Transition::Fade &&
                         d_->fade_alpha < 1.f && d_->tex_b);

    if (fading) {
        d_->fade_shader.bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, d_->tex_b); // outgoing
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, d_->tex_a); // incoming
        d_->fade_shader.setUniformValue("uTexA",  0);
        d_->fade_shader.setUniformValue("uTexB",  1);
        d_->fade_shader.setUniformValue("uAlpha", d_->fade_alpha);
        d_->vao.bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        d_->vao.release();
        d_->fade_shader.release();
    } else {
        d_->cut_shader.bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, d_->tex_a);
        d_->cut_shader.setUniformValue("uTexA", 0);
        d_->vao.bind();
        glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
        d_->vao.release();
        d_->cut_shader.release();
    }
}

} // namespace reji
#endif // QT6_AVAILABLE
