#pragma once
#include <cstdint>
#include "../pipeline/include/pipeline.h"  // rj::Pipeline, rj::Pipeline::Config
#include "../pipeline/copy_optimizer.h"     // GpuCopyOptimizer

// ---------------------------------------------------------------------------
// QT6_AVAILABLE is defined by CMakeLists.txt when find_package(Qt6) succeeds.
// Without it, MainWindow compiles as a no-op stub that preserves the public API.
// ---------------------------------------------------------------------------
#ifdef QT6_AVAILABLE

#include <QMainWindow>
#include <QSettings>

class QCloseEvent;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QMenu;
class QPushButton;
class QThread;
class QTimer;

namespace reji {
    class PreviewWidget;
    class ProgramWidget;
    class HealingOverlay;
    class RustBridge;
    class SettingsDialog;
}

// ---------------------------------------------------------------------------
// MainWindow — top-level application shell.
//
//  ┌─────────────────────────────────────────────────┐
//  │  MenuBar (Dosya · Görünüm · Yardım)             │
//  ├──────────────────────────────┬──────────────────┤
//  │  Preview (GL) │ Program (GL) │  Sahneler        │
//  │               │              │  ┌────────────┐  │
//  │               │              │  │ Sahne 1    │  │
//  │               │              │  └────────────┘  │
//  ├──────────────────────────────┴──────────────────┤
//  │  [CUT]  [FADE]                  [START]  [STOP] │
//  ├─────────────────────────────────────────────────┤
//  │  StatusBar: bitrate · fps · connection          │
//  └─────────────────────────────────────────────────┘
//
// Thread safety: all methods must be called from the Qt GUI thread.
// RustBridge serialises every FFI call through a QMutex internally.
// ---------------------------------------------------------------------------
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    /// Stores config and calls Pipeline::init(cfg). Returns false on failure.
    bool initPipeline(const rj::Pipeline::Config& cfg);

public slots:
    /// Updates status-bar labels. Called by metrics_timer_ (100 ms cadence).
    void onMetricsUpdate(uint32_t bitrate_kbps, float fps_actual, bool connected);

    /// Shows tray balloon + HealingOverlay. Wire to Rust HealingEvent in v0.2.
    void onHealingNotification(const QString& message);

signals:
    void streamStarted();
    void streamStopped();
    void sceneActivated(int scene_index);

protected:
    void closeEvent(QCloseEvent* ev) override;

private slots:
    void startStream();
    void stopStream();
    void onCutTransition();
    void onFadeTransition();
    /// 100 ms poll: drains rj_command_drain and updates status-bar metrics.
    void pollMetrics();
    /// Shown when RustBridge emits reduceBitrate().
    void onReduceBitrate(uint32_t target_kbps, const QString& reason);
    /// Scene panel: add a new named scene.
    void addScene();
    /// Scene panel: remove the selected scene (minimum 1 scene enforced).
    void removeScene();
    /// Opens Settings dialog
    void onSettingsClicked();

private:
    void buildMenuBar();
    void buildCentralWidget();
    void buildStatusBar();
    void saveWindowState();
    void loadWindowState();
    void stopFrameThread();

    // ── Video monitors ─────────────────────────────────────────────────────
    reji::PreviewWidget* preview_widget_{nullptr};
    reji::ProgramWidget* program_widget_{nullptr};

    // ── Scene panel ────────────────────────────────────────────────────────
    QListWidget* scene_list_{nullptr};
    QPushButton* btn_scene_add_{nullptr};
    QPushButton* btn_scene_remove_{nullptr};

    // ── Transition / stream control ────────────────────────────────────────
    QPushButton* btn_cut_{nullptr};
    QPushButton* btn_fade_{nullptr};
    QPushButton* btn_start_{nullptr};
    QPushButton* btn_stop_{nullptr};

    // ── Status bar ─────────────────────────────────────────────────────────
    QLabel* lbl_status_{nullptr};
    QLabel* lbl_bitrate_{nullptr};
    QLabel* lbl_fps_{nullptr};
    QLabel* lbl_connection_{nullptr};

    // ── Rust bridge + self-healing overlay ─────────────────────────────────
    reji::RustBridge*     rust_bridge_{nullptr};
    reji::HealingOverlay* healing_overlay_{nullptr};
    reji::SettingsDialog* settings_dialog_{nullptr};
    QTimer*               metrics_timer_{nullptr};

    // ── Pipeline ───────────────────────────────────────────────────────────
    rj::Pipeline         pipeline_;
    rj::Pipeline::Config pipeline_cfg_{};
    bool                 stream_active_{false};
    GpuCopyOptimizer     copy_optimizer_;              // v0.5.1: GPU-only blit + timeline sem
    bool                 copy_optimizer_initialized_{false};

    // ── Frame thread — DXGI single-thread requirement ──────────────────────
    QThread* frame_thread_{nullptr};

    // ── Persistent window state ────────────────────────────────────────────
    QSettings settings_{"RejiStudio", "RejiStudio"};
};

// ===========================================================================
#else // !QT6_AVAILABLE — minimal stub, compiles without Qt6 headers
// ===========================================================================
class MainWindow {
public:
    MainWindow()  = default;
    ~MainWindow() = default;

    bool initPipeline(const rj::Pipeline::Config& cfg);

    void show()    noexcept {}
    void hide()    noexcept {}
    bool isVisible() const noexcept { return false; }

    void onMetricsUpdate(uint32_t, float, bool) noexcept {}
    void onHealingNotification(const char*) noexcept {}

private:
    rj::Pipeline         pipeline_;
    rj::Pipeline::Config pipeline_cfg_{};
};

#endif // QT6_AVAILABLE
