#include "main_window.h"

// ===========================================================================
#ifdef QT6_AVAILABLE
// ===========================================================================

#include "healing_overlay.h"
#include "preview_widget.h"
#include "program_widget.h"
#include "rust_bridge.h"    // also pulls in ffi_bridge.h (RjCommand, rj_command_drain, …)

#include <QApplication>
#include <QCloseEvent>
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
#include <QStyle>
#include <QSystemTrayIcon>
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
            this, [this] { statusBar()->showMessage(tr("Bağlantı normale döndü"), 4000); });

    rust_bridge_->startMonitor();

    buildCentralWidget();
    buildMenuBar();
    buildStatusBar();
    buildSystemTray();
    loadWindowState();
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
    auto* central = new QWidget(this);
    auto* vbox    = new QVBoxLayout(central);
    vbox->setContentsMargins(4, 4, 4, 4);
    vbox->setSpacing(4);

    // ── Preview + Program monitors ─────────────────────────────────────────
    preview_widget_ = new reji::PreviewWidget(this);
    program_widget_ = new reji::ProgramWidget(this);

    auto* monitor_splitter = new QSplitter(Qt::Horizontal, this);
    monitor_splitter->addWidget(preview_widget_);
    monitor_splitter->addWidget(program_widget_);
    monitor_splitter->setSizes({640, 640});
    monitor_splitter->setChildrenCollapsible(false);

    // ── Scene list panel ───────────────────────────────────────────────────
    scene_list_ = new QListWidget;
    for (int i = 1; i <= 6; ++i)
        scene_list_->addItem(QString("Sahne %1").arg(i));
    scene_list_->setCurrentRow(0);

    connect(scene_list_, &QListWidget::itemDoubleClicked,
            this, [this](QListWidgetItem*) {
                emit sceneActivated(scene_list_->currentRow());
            });

    auto* scene_group  = new QGroupBox(tr("Sahneler"), this);
    scene_group->setMaximumWidth(220);
    auto* scene_layout = new QVBoxLayout(scene_group);
    scene_layout->setContentsMargins(4, 4, 4, 4);
    scene_layout->addWidget(scene_list_);

    // ── Top splitter: monitors | scene list ───────────────────────────────
    auto* top_splitter = new QSplitter(Qt::Horizontal, this);
    top_splitter->addWidget(monitor_splitter);
    top_splitter->addWidget(scene_group);
    top_splitter->setStretchFactor(0, 3);
    top_splitter->setStretchFactor(1, 1);
    top_splitter->setChildrenCollapsible(false);

    // ── Transition + stream control bar ───────────────────────────────────
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
    setCentralWidget(central);

    // ── Signals ────────────────────────────────────────────────────────────
    connect(btn_cut_,   &QPushButton::clicked, this, &MainWindow::onCutTransition);
    connect(btn_fade_,  &QPushButton::clicked, this, &MainWindow::onFadeTransition);
    connect(btn_start_, &QPushButton::clicked, this, &MainWindow::startStream);
    connect(btn_stop_,  &QPushButton::clicked, this, &MainWindow::stopStream);

    // ── 100 ms metrics poll ────────────────────────────────────────────────
    metrics_timer_ = new QTimer(this);
    metrics_timer_->setInterval(100);
    connect(metrics_timer_, &QTimer::timeout, this, &MainWindow::pollMetrics);
    metrics_timer_->start();

    // ── Self-healing overlay (floats over central area) ────────────────────
    healing_overlay_ = new reji::HealingOverlay(this);
    healing_overlay_->hide();
    connect(healing_overlay_, &reji::HealingOverlay::undoRequested,
            this, [this] { statusBar()->showMessage(tr("Geri alma isteği gönderildi"), 3000); });
}

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------
void MainWindow::buildMenuBar() {
    QMenu* file_menu = menuBar()->addMenu(tr("&Dosya"));
    file_menu->addAction(tr("&Yeni"),   this, [] {}, QKeySequence::New);
    file_menu->addAction(tr("&Aç"),    this, [] {}, QKeySequence::Open);
    file_menu->addAction(tr("&Kaydet"), this, [] {}, QKeySequence::Save);
    file_menu->addSeparator();
    file_menu->addAction(tr("Çı&kış"), qApp, &QApplication::quit, QKeySequence::Quit);

    QMenu* view_menu = menuBar()->addMenu(tr("&Görünüm"));
    auto* studio_act = view_menu->addAction(tr("Studio Modu"));
    studio_act->setCheckable(true);
    studio_act->setChecked(true);
    connect(studio_act, &QAction::toggled, preview_widget_, &QWidget::setVisible);

    view_menu->addAction(tr("Tam Ekran"), this,
        [this] { isFullScreen() ? showNormal() : showFullScreen(); },
        QKeySequence::FullScreen);

    QMenu* help_menu = menuBar()->addMenu(tr("&Yardım"));
    help_menu->addAction(tr("Hakkında"), this, [this] {
        QMessageBox::about(this, tr("Reji Studio"),
            tr("Reji Studio v0.1\n"
               "Açık kaynak canlı yayın yazılımı.\n\n"
               "Apache 2.0 Lisansı\n"
               "github.com/rejistudio-official/reji-studio"));
    });
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

    statusBar()->addPermanentWidget(lbl_bitrate_);
    statusBar()->addPermanentWidget(make_sep());
    statusBar()->addPermanentWidget(lbl_fps_);
    statusBar()->addPermanentWidget(make_sep());
    statusBar()->addPermanentWidget(lbl_connection_);
    statusBar()->showMessage(tr("Hazır"), 3000);
}

// ---------------------------------------------------------------------------
// SystemTrayIcon
// ---------------------------------------------------------------------------
void MainWindow::buildSystemTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;

    tray_icon_ = new QSystemTrayIcon(this);
    tray_icon_->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    tray_icon_->setToolTip("Reji Studio");

    tray_menu_ = new QMenu(this);
    tray_menu_->addAction(tr("Göster"),        this, &MainWindow::show);
    tray_menu_->addAction(tr("Yayını Başlat"), this, &MainWindow::startStream);
    tray_menu_->addAction(tr("Yayını Durdur"), this, &MainWindow::stopStream);
    tray_menu_->addSeparator();
    tray_menu_->addAction(tr("Çıkış"), qApp, &QApplication::quit);
    tray_icon_->setContextMenu(tray_menu_);

    connect(tray_icon_, &QSystemTrayIcon::activated,
            this, &MainWindow::onTrayActivated);
    tray_icon_->show();
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
        statusBar()->showMessage(tr("Yayın başlatılamadı — pipeline hazır değil"), 4000);
        return;
    }
    stream_active_ = true;
    btn_start_->setEnabled(false);
    btn_stop_ ->setEnabled(true);
    lbl_connection_->setText(tr("● Yayında"));
    lbl_connection_->setStyleSheet("color:#4CAF50;");
    statusBar()->showMessage(tr("Yayın başladı"), 3000);
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
    statusBar()->showMessage(tr("Yayın durduruldu"), 3000);
    rust_bridge_->sendStreamStopEvent();
    emit streamStopped();
}

// ---------------------------------------------------------------------------
// Transition slots
// ---------------------------------------------------------------------------
void MainWindow::onCutTransition() {
    program_widget_->beginTransition(reji::ProgramWidget::Transition::Cut);
    statusBar()->showMessage(tr("CUT"), 800);
    rust_bridge_->sendSceneSwitchEvent(
        static_cast<uint32_t>(scene_list_->currentRow()));
}

void MainWindow::onFadeTransition() {
    program_widget_->beginTransition(reji::ProgramWidget::Transition::Fade, 300);
    statusBar()->showMessage(tr("FADE"), 800);
}

// ---------------------------------------------------------------------------
// Metrics poll — drains the Rust command queue every 100 ms.
// rj_command_drain is lock-free (crossbeam ArrayQueue) and non-blocking.
// ---------------------------------------------------------------------------
void MainWindow::pollMetrics() {
    RjCommand cmds[8];
    const int n = rj_command_drain(cmds, 8);
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
    if (tray_icon_ && QSystemTrayIcon::supportsMessages())
        tray_icon_->showMessage(tr("Reji Studio — Self-Healing"),
                                message, QSystemTrayIcon::Warning, 4000);
    if (healing_overlay_) {
        healing_overlay_->move(
            width() - healing_overlay_->width() - 16,
            menuBar()->height() + 8);
        healing_overlay_->showMessage(message, 10000);
    }
    statusBar()->showMessage(message, 6000);
}

// ---------------------------------------------------------------------------
// Tray activation
// ---------------------------------------------------------------------------
void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) { hide(); }
        else { show(); raise(); activateWindow(); }
    }
}

// ---------------------------------------------------------------------------
// Close event — hide to tray instead of quitting when tray is available
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent* ev) {
    if (tray_icon_ && tray_icon_->isVisible()) {
        hide();
        ev->ignore();
        tray_icon_->showMessage(
            "Reji Studio",
            tr("Arka planda çalışıyor. Çıkmak için tepsi simgesine sağ tıklayın."),
            QSystemTrayIcon::Information, 2500);
    } else {
        saveWindowState();
        ev->accept();
    }
}

// ===========================================================================
#else // !QT6_AVAILABLE — stub implementation
// ===========================================================================

bool MainWindow::initPipeline(const rj::Pipeline::Config& cfg) {
    pipeline_cfg_ = cfg;
    return pipeline_.init(cfg);
}

#endif // QT6_AVAILABLE
