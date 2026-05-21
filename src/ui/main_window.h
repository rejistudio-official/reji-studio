#pragma once
#include <cstdint>
#include "../pipeline/include/pipeline.h"  // rj::Pipeline, rj::Pipeline::Config

// ---------------------------------------------------------------------------
// QT6_AVAILABLE is defined by CMakeLists.txt when find_package(Qt6) succeeds.
// Without it this header still compiles — MainWindow becomes a no-op stub.
// ---------------------------------------------------------------------------
#ifdef QT6_AVAILABLE

#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPushButton>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>

class QCloseEvent;

// ---------------------------------------------------------------------------
// PreviewGLWidget — shared by both the Preview and Program monitors.
// v0.1: renders solid black.  Pipeline texture blit added in v0.2.
// ---------------------------------------------------------------------------
class PreviewGLWidget : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    explicit PreviewGLWidget(QWidget* parent = nullptr);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
};

// ---------------------------------------------------------------------------
// MainWindow — top-level application shell.
//
//  ┌─────────────────────────────────────────────────┐
//  │  MenuBar (File · View · Help)                   │
//  ├──────────────────────────────┬──────────────────┤
//  │  Preview (GL) │ Program (GL) │  Sahneler        │
//  │               │              │  ┌────────────┐  │
//  │               │              │  │ Sahne 1    │  │
//  │               │              │  │ Sahne 2    │  │
//  │               │              │  └────────────┘  │
//  ├──────────────────────────────┴──────────────────┤
//  │  [CUT]  [FADE]                  [START]  [STOP] │
//  ├─────────────────────────────────────────────────┤
//  │  StatusBar: bitrate · fps · connection          │
//  └─────────────────────────────────────────────────┘
//
// Thread safety: all methods must be called from the Qt GUI thread.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Stores config and calls Pipeline::init(cfg). Returns false on failure.
    bool initPipeline(const rj::Pipeline::Config& cfg);

public slots:
    /// Updates status bar labels. Called by metrics_timer_ (100 ms cadence).
    void onMetricsUpdate(uint32_t bitrate_kbps, float fps_actual, bool connected);

    /// Shows a system-tray balloon warning. Wire to Rust HealingEvent in v0.2.
    void onHealingNotification(const QString& message);

signals:
    /// Emitted after Pipeline::start_stream() succeeds.
    void streamStarted();
    /// Emitted after Pipeline::stop_stream().
    void streamStopped();
    /// Emitted on scene-list double-click; index matches QListWidget row.
    void sceneActivated(int scene_index);

protected:
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void startStream();
    void stopStream();
    void onCutTransition();
    void onFadeTransition();
    /// 100 ms poll hook — v0.2: drain rj_metrics ring buffer (ffi_bridge.h).
    void pollMetrics();
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);

private:
    void buildMenuBar();
    void buildCentralWidget();
    void buildStatusBar();
    void buildSystemTray();
    void saveWindowState();
    void loadWindowState();

    // ── Monitor widgets ────────────────────────────────────────────────────
    PreviewGLWidget* preview_widget_{nullptr};
    PreviewGLWidget* program_widget_{nullptr};

    // ── Scene panel ────────────────────────────────────────────────────────
    QListWidget* scene_list_{nullptr};

    // ── Transition / stream control ────────────────────────────────────────
    QPushButton* btn_cut_{nullptr};
    QPushButton* btn_fade_{nullptr};
    QPushButton* btn_start_{nullptr};
    QPushButton* btn_stop_{nullptr};

    // ── Status bar ─────────────────────────────────────────────────────────
    QLabel* lbl_bitrate_{nullptr};
    QLabel* lbl_fps_{nullptr};
    QLabel* lbl_connection_{nullptr};

    // ── System tray ────────────────────────────────────────────────────────
    QSystemTrayIcon* tray_icon_{nullptr};
    QMenu*           tray_menu_{nullptr};

    // ── Rust metrics poll (wired to rj_metrics ring buffer in v0.2) ────────
    QTimer* metrics_timer_{nullptr};

    // ── Pipeline ───────────────────────────────────────────────────────────
    rj::Pipeline         pipeline_;
    rj::Pipeline::Config pipeline_cfg_{};
    bool                 stream_active_{false};

    // ── Persistent window state ────────────────────────────────────────────
    QSettings settings_{"RejiStudio", "RejiStudio"};
};

// ===========================================================================
#else // !QT6_AVAILABLE — minimal stub, compiles without Qt6 headers
// ===========================================================================
// Stub MainWindow: preserves the public interface so call-sites compile
// unchanged even when Qt6 is absent.  No GUI functionality is provided.
// ---------------------------------------------------------------------------
class MainWindow {
public:
    MainWindow()  = default;
    ~MainWindow() = default;

    bool initPipeline(const rj::Pipeline::Config& cfg);

    void show()    noexcept {}
    void hide()    noexcept {}
    bool isVisible() const noexcept { return false; }

    void onMetricsUpdate(uint32_t /*bitrate_kbps*/,
                         float    /*fps_actual*/,
                         bool     /*connected*/) noexcept {}
    void onHealingNotification(const char* /*message*/) noexcept {}

private:
    rj::Pipeline         pipeline_;
    rj::Pipeline::Config pipeline_cfg_{};
};

#endif // QT6_AVAILABLE
