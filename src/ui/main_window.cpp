#include "main_window.h"

// ===========================================================================
#ifdef QT6_AVAILABLE
// ===========================================================================

#include "healing_overlay.h"
#include "preview_widget.h"
#include "program_widget.h"
#include "rust_bridge.h"    // also pulls in ffi_bridge.h (RjCommand, rj_command_drain, …)
#include "settings_dialog.h"
#include "../pipeline/gpu/vulkan_initializer.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStatusBar>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Reji Studio");
    setMinimumSize(1280, 720);

    rust_bridge_ = new reji::RustBridge(this);
    connect(rust_bridge_, &reji::RustBridge::reduceBitrate,
            this, &MainWindow::onReduceBitrate, Qt::QueuedConnection);
    connect(rust_bridge_, &reji::RustBridge::restoreNormal,
            this, [this] { lbl_status_->setText(tr("Bağlantı normale döndü")); });

    rust_bridge_->startMonitor();

    buildCentralWidget();
    buildMenuBar();
    buildStatusBar();
    loadWindowState();

    // v0.2: Pipeline init + preview callback
    rj::Pipeline::Config pcfg;  // varsayılan: 1920x1080, 60fps, 6000kbps
    if (!pipeline_.init(pcfg)) {
        qDebug() << "Pipeline init failed";
        lbl_status_->setText(tr("Pipeline init başarısız — NVENC SDK eksik olabilir"));
    } else {
        preview_widget_->setPipeline(&pipeline_);
        preview_widget_->selectRenderPath(pipeline_.display_vendor_id());
        // v0.5.1: Vulkan device handle late-binding
        {
            auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
            if (vk && vk->device()) {
                pipeline_.notify_vulkan_ready(vk->device(), vk->physical_device());
                fprintf(stderr, "[MainWindow] notify_vulkan_ready\n");
                fflush(stderr);

                // v0.5.1: Initialize GpuCopyOptimizer with Vulkan device + queue
                if (vk->graphics_queue() != VK_NULL_HANDLE) {
                    GpuCopyOptimizer::Config copy_cfg;
                    if (copy_optimizer_.init(vk->device(), vk->graphics_queue(), vk->physical_device(),
                                             vk->graphics_queue_family(), copy_cfg)) {
                        preview_widget_->setCopyOptimizer(&copy_optimizer_);
                        // v0.5.2: Wire GL interop bridge for NT handle import
                        preview_widget_->setBridge(pipeline_.get_external_memory_bridge());
                        fprintf(stderr, "[MainWindow] GpuCopyOptimizer initialized\n");
                        fflush(stderr);
                    } else {
                        fprintf(stderr, "[MainWindow] GpuCopyOptimizer init failed\n");
                    }
                }
            }
        }
        // Wire profiler to preview widget
        if (pipeline_.profiler()) {
            preview_widget_->setProfiler(pipeline_.profiler());
        }

        // v0.5.1: Zero-copy D3D11 frame callback → PreviewWidget::submitD3D11Frame
        // Pipeline calls this for each captured frame (D3D11→VkImage)
        pipeline_.set_d3d11_frame_callback(
            [this](void* staging_texture, uint32_t width, uint32_t height) {
                // v0.5.1: Get cached frame images from ExternalMemoryBridge
                VkImage staging_vk = VK_NULL_HANDLE;
                VkImage target_vk = VK_NULL_HANDLE;
                if (pipeline_.get_last_frame_images(&staging_vk, &target_vk)) {
                    // Queue frame for GPU copy in preview widget (non-blocking)
                    preview_widget_->submitD3D11Frame(staging_vk, target_vk, width, height);
                }
                (void)staging_texture;  // D3D11 handle not needed — we have VkImages
            }
        );
    }

    // run_frame() ayrı thread'de çalışsın
    frame_thread_ = new QThread(this);
    auto* worker = new QObject();
    worker->moveToThread(frame_thread_);
    connect(frame_thread_, &QThread::started, worker, [this, worker] {
        while (!frame_thread_->isInterruptionRequested()) {
            pipeline_.run_frame();
            QThread::msleep(16);
        }
        worker->deleteLater();
    });
    frame_thread_->start();
}

MainWindow::~MainWindow() {
    saveWindowState();
}

bool MainWindow::initPipeline(const rj::Pipeline::Config& cfg) {
    pipeline_cfg_ = cfg;
    return pipeline_.init(cfg);
}

// ---------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------
void MainWindow::buildCentralWidget() {
    // lbl_status_ burada erken initialize ediliyor — buildStatusBar()'dan önce kullanılıyor
    lbl_status_ = new QLabel(tr("Hazır"), this);

    auto* central = new QWidget(this);
    auto* vbox    = new QVBoxLayout(central);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);
    setCentralWidget(central);

    preview_widget_ = new reji::PreviewWidget(this);
    program_widget_ = new reji::ProgramWidget(this);

    auto* monitor_splitter = new QSplitter(Qt::Horizontal, this);
    monitor_splitter->addWidget(preview_widget_);
    monitor_splitter->addWidget(program_widget_);
    monitor_splitter->setStretchFactor(0, 1);
    monitor_splitter->setStretchFactor(1, 1);

    scene_list_ = new QListWidget(this);
    scene_list_->setDragDropMode(QAbstractItemView::InternalMove);
    for (int i = 1; i <= 3; ++i) {
        auto* item = new QListWidgetItem(tr("Sahne %1").arg(i));
        item->setFlags(item->flags() | Qt::ItemIsEditable);
        scene_list_->addItem(item);
    }
    scene_list_->setCurrentRow(0);
    connect(scene_list_, &QListWidget::itemActivated,
            this, [this](QListWidgetItem* item) {
                const int idx = scene_list_->currentRow();
                const QString name = item ? item->text() : tr("Sahne %1").arg(idx + 1);
                if (lbl_status_)
                    lbl_status_->setText(tr("%1 → Program (CUT)").arg(name));
                program_widget_->beginTransition(reji::ProgramWidget::Transition::Cut);
                rust_bridge_->sendSceneSwitchEvent(static_cast<uint32_t>(idx));
                emit sceneActivated(idx);
            });

    btn_scene_add_    = new QPushButton("+", this);
    btn_scene_remove_ = new QPushButton("−", this);
    btn_scene_add_->setFixedSize(28, 24);
    btn_scene_remove_->setFixedSize(28, 24);
    btn_scene_add_->setToolTip(tr("Sahne ekle"));
    btn_scene_remove_->setToolTip(tr("Sahne sil (min. 1)"));
    auto* scene_btn_layout = new QHBoxLayout;
    scene_btn_layout->setContentsMargins(0, 2, 0, 0);
    scene_btn_layout->addWidget(btn_scene_add_);
    scene_btn_layout->addWidget(btn_scene_remove_);
    scene_btn_layout->addStretch();

    auto* scene_group  = new QGroupBox(tr("Sahneler"), this);
    scene_group->setMaximumWidth(220);
    auto* scene_layout = new QVBoxLayout(scene_group);
    scene_layout->setContentsMargins(4, 4, 4, 4);
    scene_layout->addWidget(scene_list_);
    scene_layout->addLayout(scene_btn_layout);

    auto* top_splitter = new QSplitter(Qt::Horizontal, this);
    top_splitter->addWidget(monitor_splitter);
    top_splitter->addWidget(scene_group);
    top_splitter->setStretchFactor(0, 3);
    top_splitter->setStretchFactor(1, 1);

    btn_cut_   = new QPushButton(tr("CUT"),   this);
    btn_fade_  = new QPushButton(tr("FADE"),  this);
    btn_start_ = new QPushButton(tr("START"), this);
    btn_stop_  = new QPushButton(tr("STOP"),  this);

    for (auto* b : {btn_cut_, btn_fade_, btn_start_, btn_stop_})
        b->setMinimumHeight(36);
    btn_stop_->setEnabled(false);

    btn_cut_->setStyleSheet(
        "QPushButton{background:#CC3333;color:white;font-weight:bold;"
        "border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#E04444;}");
    btn_fade_->setStyleSheet(
        "QPushButton{background:#3355CC;color:white;font-weight:bold;"
        "border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#4466DD;}");
    btn_start_->setStyleSheet(
        "QPushButton{background:#227722;color:white;font-weight:bold;"
        "border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#2E9B2E;}");
    btn_stop_->setStyleSheet(
        "QPushButton:enabled{background:#CC3333;color:white;font-weight:bold;"
        "border-radius:4px;padding:0 12px;}"
        "QPushButton:enabled:hover{background:#E04444;}"
        "QPushButton:disabled{background:#555;color:#999;"
        "border-radius:4px;padding:0 12px;}");

    auto* tbar_layout = new QHBoxLayout;
    tbar_layout->setContentsMargins(2, 4, 2, 4);
    tbar_layout->addWidget(btn_cut_);
    tbar_layout->addWidget(btn_fade_);
    tbar_layout->addStretch();
    tbar_layout->addWidget(btn_start_);
    tbar_layout->addWidget(btn_stop_);

    auto* tbar_widget = new QWidget(this);
    tbar_widget->setLayout(tbar_layout);
    tbar_widget->setFixedHeight(52);

    vbox->addWidget(top_splitter, 1);
    vbox->addWidget(tbar_widget, 0);

    connect(btn_cut_,          &QPushButton::clicked, this, &MainWindow::onCutTransition);
    connect(btn_fade_,         &QPushButton::clicked, this, &MainWindow::onFadeTransition);
    connect(btn_start_,        &QPushButton::clicked, this, &MainWindow::startStream);
    connect(btn_stop_,         &QPushButton::clicked, this, &MainWindow::stopStream);
    connect(btn_scene_add_,    &QPushButton::clicked, this, &MainWindow::addScene);
    connect(btn_scene_remove_, &QPushButton::clicked, this, &MainWindow::removeScene);

    healing_overlay_ = new reji::HealingOverlay(this);
    healing_overlay_->hide();
    connect(healing_overlay_, &reji::HealingOverlay::undoRequested,
            this, [this] { lbl_status_->setText(tr("Geri alma isteği gönderildi")); });
}

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------
void MainWindow::buildMenuBar() {
    QMenu* file_menu = menuBar()->addMenu(tr("&Dosya"));

    auto *new_act = file_menu->addAction(tr("&Yeni"));
    new_act->setShortcut(QKeySequence::New);

    auto *open_act = file_menu->addAction(tr("&Aç"));
    open_act->setShortcut(QKeySequence::Open);

    auto *save_act = file_menu->addAction(tr("&Kaydet"));
    save_act->setShortcut(QKeySequence::Save);

    file_menu->addSeparator();

    auto *quit_act = file_menu->addAction(tr("Çı&kış"));
    quit_act->setShortcut(QKeySequence::Quit);
    connect(quit_act, &QAction::triggered, qApp, &QApplication::quit);

    QMenu* view_menu = menuBar()->addMenu(tr("&Görünüm"));
    auto* studio_act = view_menu->addAction(tr("Studio Modu"));
    studio_act->setCheckable(true);
    studio_act->setChecked(true);
    connect(studio_act, &QAction::toggled, preview_widget_, &QWidget::setVisible);

    auto *fullscreen_act = view_menu->addAction(tr("Tam Ekran"));
    fullscreen_act->setShortcut(QKeySequence::FullScreen);
    connect(fullscreen_act, &QAction::triggered, this,
        [this] { isFullScreen() ? showNormal() : showFullScreen(); });

    QMenu* help_menu = menuBar()->addMenu(tr("&Yardım"));
    auto *about_act = help_menu->addAction(tr("Hakkında"));
    connect(about_act, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("Reji Studio"),
            tr("Reji Studio v0.1\n"
               "Açık kaynak canlı yayın yazılımı.\n\n"
               "Apache 2.0 Lisansı\n"
               "github.com/rejistudio-official/reji-studio"));
    });

    // Tools menu: Settings
    QMenu* tools_menu = menuBar()->addMenu(tr("&Araçlar"));
    auto* action_settings = tools_menu->addAction(tr("&Ayarlar"));
    connect(action_settings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
}

// ---------------------------------------------------------------------------
// StatusBar
// ---------------------------------------------------------------------------
void MainWindow::buildStatusBar() {
    lbl_bitrate_    = new QLabel("-- kbps", this);
    lbl_fps_        = new QLabel("-- fps",  this);
    lbl_connection_ = new QLabel(tr("● Bağlantı yok"), this);
    lbl_connection_->setStyleSheet("color:#888888;");

    auto make_sep = [this]() -> QFrame* {
        auto* f = new QFrame(this);
        f->setFrameShape(QFrame::VLine);
        f->setFrameShadow(QFrame::Sunken);
        return f;
    };

    statusBar()->addWidget(lbl_status_);
    statusBar()->addPermanentWidget(lbl_bitrate_);
    statusBar()->addPermanentWidget(make_sep());
    statusBar()->addPermanentWidget(lbl_fps_);
    statusBar()->addPermanentWidget(make_sep());
    statusBar()->addPermanentWidget(lbl_connection_);
    lbl_status_->setText(tr("Hazır"));
}

// ---------------------------------------------------------------------------
// WindowState persistence
// ---------------------------------------------------------------------------
void MainWindow::saveWindowState() {
    settings_.setValue("geometry",    saveGeometry());
    settings_.setValue("windowState", saveState());
}

void MainWindow::loadWindowState() {
    if (settings_.contains("geometry"))
        restoreGeometry(settings_.value("geometry").toByteArray());
    if (settings_.contains("windowState"))
        restoreState(settings_.value("windowState").toByteArray());
}

// ---------------------------------------------------------------------------
// Stream control
// ---------------------------------------------------------------------------
void MainWindow::startStream() {
    if (stream_active_) return;
    if (!pipeline_.start_stream()) {
        lbl_status_->setText(tr("Yayın başlatılamadı — pipeline hazır değil"));
        return;
    }
    stream_active_ = true;
    btn_start_->setEnabled(false);
    btn_stop_ ->setEnabled(true);
    lbl_connection_->setText(tr("● Yayında"));
    lbl_connection_->setStyleSheet("color:#4CAF50;");
    lbl_status_->setText(tr("Yayın başladı"));
    rust_bridge_->sendStreamStartEvent();
    emit streamStarted();
}

void MainWindow::stopStream() {
    if (!stream_active_) return;
    (void)pipeline_.stop_stream();
    stream_active_ = false;
    btn_start_->setEnabled(true);
    btn_stop_ ->setEnabled(false);
    lbl_connection_->setText(tr("● Bağlantı yok"));
    lbl_connection_->setStyleSheet("color:#888888;");
    lbl_status_->setText(tr("Yayın durduruldu"));
    rust_bridge_->sendStreamStopEvent();
    emit streamStopped();
}

// ---------------------------------------------------------------------------
// Transition slots
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Scene management
// ---------------------------------------------------------------------------
void MainWindow::addScene() {
    const int n    = scene_list_->count();
    auto* item     = new QListWidgetItem(tr("Sahne %1").arg(n + 1));
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    scene_list_->addItem(item);
    scene_list_->setCurrentItem(item);
    scene_list_->editItem(item);
}

void MainWindow::removeScene() {
    if (scene_list_->count() <= 1) return;
    const int row = scene_list_->currentRow();
    delete scene_list_->takeItem(row);
    if (scene_list_->currentRow() < 0)
        scene_list_->setCurrentRow(0);
}

void MainWindow::onCutTransition() {
    program_widget_->beginTransition(reji::ProgramWidget::Transition::Cut);
    lbl_status_->setText(tr("CUT"));
    rust_bridge_->sendSceneSwitchEvent(
        static_cast<uint32_t>(scene_list_->currentRow()));
}

void MainWindow::onFadeTransition() {
    program_widget_->beginTransition(reji::ProgramWidget::Transition::Fade, 300);
    lbl_status_->setText(tr("FADE"));
}

void MainWindow::onSettingsClicked() {
    if (!settings_dialog_) {
        settings_dialog_ = new reji::SettingsDialog(this);

        // Connect mode change signal to HealingOverlay
        connect(settings_dialog_, &reji::SettingsDialog::healingModeChanged,
                this, [this](reji::HealingMode mode) {
            if (healing_overlay_) {
                healing_overlay_->setHealingMode(mode);
            }
        });

        // Link SettingsDialog to HealingOverlay for Yaklaşım C
        if (healing_overlay_) {
            healing_overlay_->setSettingsDialog(settings_dialog_);
        }
    }

    settings_dialog_->exec();
}

// ---------------------------------------------------------------------------
// Metrics poll — drains the Rust command queue every 100 ms.
// rj_command_drain is lock-free (crossbeam ArrayQueue) and non-blocking.
// ---------------------------------------------------------------------------
void MainWindow::pollMetrics() {
    RjCommand cmds[8];
    int n = rj_command_drain(cmds, 8);
    if (n < 0) n = 0;
    else if (n > 8) n = 8;
    for (int i = 0; i < n; ++i) {
        switch (cmds[i].cmd_type) {
            case RJ_CMD_BITRATE_SET:
                lbl_bitrate_->setText(
                    QString("%1 kbps").arg(cmds[i].param_u32));
                break;
            case RJ_CMD_PREVIEW_FPS:
                // v0.2: throttle preview_widget_ refresh rate
                break;
            default:
                break;
        }
    }
    (void)pipeline_.is_running();
}

void MainWindow::onMetricsUpdate(uint32_t bitrate_kbps, float fps_actual, bool connected) {
    lbl_bitrate_->setText(QString("%1 kbps").arg(bitrate_kbps));
    lbl_fps_->setText(QString("%1 fps").arg(
        static_cast<double>(fps_actual), 0, 'f', 1));
    if (connected) {
        lbl_connection_->setText(tr("● Bağlı"));
        lbl_connection_->setStyleSheet("color:#4CAF50;");
    } else {
        lbl_connection_->setText(tr("● Bağlantı kesildi"));
        lbl_connection_->setStyleSheet("color:#F44336;");
    }
}

// ---------------------------------------------------------------------------
// Self-healing notification
// ---------------------------------------------------------------------------
void MainWindow::onReduceBitrate(uint32_t target_kbps, const QString& reason) {
    const QString msg = tr("Self-healing: bitrate → %1 kbps\n%2")
        .arg(target_kbps).arg(reason);
    onHealingNotification(msg);
}

void MainWindow::onHealingNotification(const QString& message) {
    if (healing_overlay_) {
        healing_overlay_->move(
            width() - healing_overlay_->width() - 16,
            menuBar()->height() + 8);
        healing_overlay_->showMessage(message, 10000);
    }
    lbl_status_->setText(message);
}

// ---------------------------------------------------------------------------
// Close event
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent* ev) {
    saveWindowState();
    if (frame_thread_) {
        frame_thread_->requestInterruption();
        frame_thread_->wait(1000);
    }
    ev->accept();
}

// ===========================================================================
#else // !QT6_AVAILABLE — stub implementation
// ===========================================================================

bool MainWindow::initPipeline(const rj::Pipeline::Config& cfg) {
    pipeline_cfg_ = cfg;
    return pipeline_.init(cfg);
}

#endif // QT6_AVAILABLE
