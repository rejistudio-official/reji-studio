#include "main_window.h"

// ===========================================================================
#ifdef QT6_AVAILABLE
// ===========================================================================

#include <QApplication>
#include <QCloseEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidgetItem>
#include <QMenuBar>
#include <QMessageBox>
#include <QSplitter>
#include <QStatusBar>
#include <QStyle>
#include <QVBoxLayout>

// ---------------------------------------------------------------------------
// PreviewGLWidget
// ---------------------------------------------------------------------------
PreviewGLWidget::PreviewGLWidget(QWidget* parent) : QOpenGLWidget(parent) {
    setMinimumSize(320, 180);
}

void PreviewGLWidget::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
}

void PreviewGLWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PreviewGLWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

// ---------------------------------------------------------------------------
// MainWindow — construction
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("Reji Studio");
    setMinimumSize(1280, 720);

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
    preview_widget_ = new PreviewGLWidget(this);
    program_widget_ = new PreviewGLWidget(this);

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

    btn_cut_ ->setMinimumHeight(36);
    btn_fade_->setMinimumHeight(36);
    btn_start_->setMinimumHeight(36);
    btn_stop_ ->setMinimumHeight(36);
    btn_stop_ ->setEnabled(false);

    btn_cut_ ->setStyleSheet(
        "QPushButton{background:#CC3333;color:white;font-weight:bold;border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#E04444;}");
    btn_fade_->setStyleSheet(
        "QPushButton{background:#3355CC;color:white;font-weight:bold;border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#4466DD;}");
    btn_start_->setStyleSheet(
        "QPushButton{background:#227722;color:white;font-weight:bold;border-radius:4px;padding:0 12px;}"
        "QPushButton:hover{background:#2E9B2E;}");
    btn_stop_->setStyleSheet(
        "QPushButton:enabled{background:#CC3333;color:white;font-weight:bold;border-radius:4px;padding:0 12px;}"
        "QPushButton:enabled:hover{background:#E04444;}"
        "QPushButton:disabled{background:#555;color:#999;border-radius:4px;padding:0 12px;}");

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

    // ── 100 ms metrics poll — v0.2: drain rj_metrics ring buffer ──────────
    metrics_timer_ = new QTimer(this);
    metrics_timer_->setInterval(100);
    connect(metrics_timer_, &QTimer::timeout, this, &MainWindow::pollMetrics);
    metrics_timer_->start();
}

// ---------------------------------------------------------------------------
// MenuBar
// ---------------------------------------------------------------------------
void MainWindow::buildMenuBar() {
    // File
    QMenu* file_menu = menuBar()->addMenu(tr("&Dosya"));
    file_menu->addAction(tr("&Yeni"),  this, [] { /* TODO v0.2 */ }, QKeySequence::New);
    file_menu->addAction(tr("&Aç"),   this, [] { /* TODO v0.2 */ }, QKeySequence::Open);
    file_menu->addAction(tr("&Kaydet"), this, [] { /* TODO v0.2 */ }, QKeySequence::Save);
    file_menu->addSeparator();
    file_menu->addAction(tr("Çı&kış"), qApp, &QApplication::quit, QKeySequence::Quit);

    // View
    QMenu* view_menu = menuBar()->addMenu(tr("&Görünüm"));

    auto* studio_act = view_menu->addAction(tr("Studio Modu"));
    studio_act->setCheckable(true);
    studio_act->setChecked(true);
    // Studio mode: toggle Preview monitor visibility (Program stays always visible).
    connect(studio_act, &QAction::toggled, preview_widget_, &QWidget::setVisible);

    view_menu->addAction(tr("Tam Ekran"), this,
        [this] { isFullScreen() ? showNormal() : showFullScreen(); },
        QKeySequence::FullScreen);

    // Help
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
    lbl_connection_->setStyleSheet("color: #888888;");

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
    statusBar()->showMessage(tr("Yayın başladı"), 3000);
    emit streamStarted();
}

void MainWindow::stopStream() {
    if (!stream_active_) return;
    (void)pipeline_.stop_stream();
    stream_active_ = false;
    btn_start_->setEnabled(true);
    btn_stop_ ->setEnabled(false);
    lbl_connection_->setText(tr("● Bağlantı yok"));
    lbl_connection_->setStyleSheet("color: #888888;");
    statusBar()->showMessage(tr("Yayın durduruldu"), 3000);
    emit streamStopped();
}

// ---------------------------------------------------------------------------
// Transition slots
// ---------------------------------------------------------------------------
void MainWindow::onCutTransition() {
    // TODO v0.2: atomic scene swap — send RJ_CMD_SCENE_SWITCH via rj_command_drain
    statusBar()->showMessage(tr("CUT"), 800);
}

void MainWindow::onFadeTransition() {
    // TODO v0.2: timed fade transition via pipeline command
    statusBar()->showMessage(tr("FADE"), 800);
}

// ---------------------------------------------------------------------------
// Metrics poll — v0.2: wire to rj_metrics ring buffer (ffi_bridge.h)
// ---------------------------------------------------------------------------
void MainWindow::pollMetrics() {
    // Placeholder.  In v0.2 this will call rj_command_drain and read from the
    // crossbeam ring buffer written by Pipeline::run_frame() → rj_metrics_push:
    //
    //   RjMetricSample sample{};
    //   if (rj_pipeline_status(&sample) == 0 &&
    //       sample.magic_head == RJ_METRIC_MAGIC) {
    //       onMetricsUpdate(sample.bitrate_kbps, sample.fps_actual,
    //                       pipeline_.is_running());
    //   }
    (void)pipeline_.is_running();  // keep the pipeline pointer live in debug builds
}

void MainWindow::onMetricsUpdate(uint32_t bitrate_kbps, float fps_actual, bool connected) {
    lbl_bitrate_->setText(QString("%1 kbps").arg(bitrate_kbps));
    lbl_fps_->setText(QString("%1 fps").arg(static_cast<double>(fps_actual), 0, 'f', 1));
    if (connected) {
        lbl_connection_->setText(tr("● Bağlı"));
        lbl_connection_->setStyleSheet("color: #4CAF50;");
    } else {
        lbl_connection_->setText(tr("● Bağlantı kesildi"));
        lbl_connection_->setStyleSheet("color: #F44336;");
    }
}

// ---------------------------------------------------------------------------
// Self-healing notification — wired to Rust HealingEvent in v0.2
// ---------------------------------------------------------------------------
void MainWindow::onHealingNotification(const QString& message) {
    if (tray_icon_ && QSystemTrayIcon::supportsMessages())
        tray_icon_->showMessage(tr("Reji Studio — Self-Healing"),
                                message,
                                QSystemTrayIcon::Warning,
                                4000);
    statusBar()->showMessage(message, 6000);
}

// ---------------------------------------------------------------------------
// Tray activation
// ---------------------------------------------------------------------------
void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (reason == QSystemTrayIcon::DoubleClick) {
        if (isVisible()) {
            hide();
        } else {
            show();
            raise();
            activateWindow();
        }
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
            QSystemTrayIcon::Information,
            2500);
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
