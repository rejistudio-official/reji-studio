#pragma once
#ifdef QT6_AVAILABLE

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>

namespace reji {

// ---------------------------------------------------------------------------
// ProgramWidget — Program (right) monitor with GPU-accelerated transitions.
//
// Cut:  swaps the active texture instantly (one draw call, cut_shader).
// Fade: blends tex_a (outgoing) → tex_b (incoming) over duration_ms using
//       a mix() GLSL shader; a 16 ms QTimer drives the alpha increment.
//
// Same v0.1 CPU upload path as PreviewWidget; see uploadFrame() for details.
// ---------------------------------------------------------------------------
class ProgramWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum class Transition { Cut, Fade };

    explicit ProgramWidget(QWidget* parent = nullptr);
    ~ProgramWidget() override;

    // Feed a BGRA CPU frame (D3D11 staging-buffer map, row_pitch in bytes).
    void uploadFrame(const void* bgra_pixels, int width, int height, int row_pitch);

public slots:
    // Begin a Cut or Fade transition.
    // For Fade: the outgoing frame is the currently displayed texture;
    //           the next uploadFrame() calls feed the incoming scene.
    void beginTransition(reji::ProgramWidget::Transition type, int duration_ms = 300);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private slots:
    void advanceTransition();

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
