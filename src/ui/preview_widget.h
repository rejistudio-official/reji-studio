#pragma once
#ifdef QT6_AVAILABLE

#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <memory>

namespace reji {

// ---------------------------------------------------------------------------
// PreviewWidget — Preview (left) monitor.
//
// v0.1 CPU upload path:
//   uploadFrame() accepts BGRA pixels from a D3D11 staging-buffer CPU map.
//   The data is deep-copied into a QImage, then glTexSubImage2D uploads it
//   on the next paintGL tick.
//
// v0.2 (planned):
//   Replace uploadFrame() with WGL_NV_DX_INTEROP (wglDXOpenDeviceNV /
//   wglDXRegisterObjectNV) to share the D3D11 Texture2D handle directly
//   as a GL texture — zero CPU↔GPU copy.
// ---------------------------------------------------------------------------
class PreviewWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PreviewWidget(QWidget* parent = nullptr);
    ~PreviewWidget() override;

    // Upload a BGRA (DXGI_FORMAT_B8G8R8A8_UNORM) CPU-side frame.
    // row_pitch: bytes per row (may include D3D11 alignment padding).
    // Thread-safe: may be called from the pipeline thread.
    void uploadFrame(const void* bgra_pixels, int width, int height, int row_pitch);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
