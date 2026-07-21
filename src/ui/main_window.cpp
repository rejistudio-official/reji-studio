#include "main_window.h"

// ===========================================================================
#ifdef QT6_AVAILABLE
// ===========================================================================

#include "healing_overlay.h"
#include "reji_constants.h"
#include "preview_widget.h"
#include "program_widget.h"
#include "rust_bridge.h"    // also pulls in ffi_bridge.h (RjCommand, rj_command_drain, …)
#include "settings_dialog.h"
#include "profile_advisor.h"   // reji::ProfileId, preset_for, profile_resource_name
#include "rules_watch.h"        // reji::ui::armRulesWatchOn (kuruluş-sırası seam'i)
#include "resource_init.h"      // reji::ui::ensureResourcesRegistered (qrc kaydı)
#include "../pipeline/gpu/vulkan_initializer.h"

#include <vector>
#include <QApplication>
#include <QCloseEvent>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
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
#include <QDateTime>
#include <QTimer>
#include <QFileDialog>
#include <QInputDialog>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// Profilin kullanıcıya görünen (Türkçe) adı — öneri diyaloğu ve durum satırı için.
// Kaynak kökü (profile_resource_name, "performance"...) İngilizce/dosya-adı; bu ayrı.
QString profileDisplayName(reji::ProfileId id) {
    switch (id) {
        case reji::ProfileId::Performance: return QStringLiteral("Performans");
        case reji::ProfileId::Stability:   return QStringLiteral("Stabilite");
        case reji::ProfileId::Efficiency:  return QStringLiteral("Verimlilik");
    }
    return QStringLiteral("Performans");  // ulaşılmaz — enum tam kapsanır
}

// GPU vendor id → kısa ad (öneri diyaloğunda görünür sinyal). Bilinmeyen → "GPU".
QString vendorName(uint32_t vendor_id) {
    switch (vendor_id) {
        case 0x10DE: return QStringLiteral("NVIDIA");
        case 0x1002: return QStringLiteral("AMD");
        case 0x8086: return QStringLiteral("Intel");
        default:     return QStringLiteral("GPU");
    }
}
}  // namespace

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // V10/L1-ek: qrc kaydını her şeyden önce zorla — seedRulesFromTemplate,
    // applyProfile ve validateRulesFile ":/config/..." kaynaklarını okur.
    reji::ui::ensureResourcesRegistered();
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

    metrics_timer_ = new QTimer(this);
    connect(metrics_timer_, &QTimer::timeout,
            this, &MainWindow::pollMetrics);
    metrics_timer_->start(1000);

    auto* action_timer = new QTimer(this);
    connect(action_timer, &QTimer::timeout, this, &MainWindow::pollHealingActions);
    action_timer->start(rj::constants::kActionPollIntervalMs);

    // Vulkan init — pipeline ve VK Caps logu için önce yapılmalı
    {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        if (!vk->initialize()) {
            fprintf(stderr, "[MainWindow] Vulkan init failed — GPU interop devre dışı\n");
            fflush(stderr);
        }
    }

    // SettingsDialog'ı erken oluştur — pipeline init öncesi SRT değerlerini okumak için
    settings_dialog_ = new reji::SettingsDialog(this);
    connect(settings_dialog_, &reji::SettingsDialog::healingModeChanged,
            this, [this](reji::HealingMode mode) {
        if (healing_overlay_) healing_overlay_->setHealingMode(mode);
        // V8/I19: UI mod seçimini Rust orchestrator'a ilet — yoksa HEALING_MODE
        // kalıcı 0 (AutoPilot) kalır ve Assist/Manual seçimi motora hiç ulaşmaz.
        // reji::HealingMode 0..=3 (AutoPilot/CoPilot/Assist/Manual), Rust from_raw
        // ile birebir sıralı.
        rj_set_healing_mode(static_cast<uint32_t>(mode));
        syncAutoApproveToRust();  // V8/I33c: auto-onay ayarlarını da senkronla
        syncWsPasswordToRust();   // V8/I8: WS parolasını da senkronla
        notifyHealingSettingsSaved(mode);  // Madde 6/A: kaydedildi bildirimi
    });
    // "Kuralları Düzenle" — rules.json'u harici editörde aç (Commit 1).
    connect(settings_dialog_, &reji::SettingsDialog::editRulesRequested,
            this, &MainWindow::openRulesInEditor);
    // "Otomatik yeniden yükle" — dosya izleme + hot-reload (Commit 2).
    connect(settings_dialog_, &reji::SettingsDialog::autoReloadToggled,
            this, &MainWindow::onAutoReloadToggled);
    // "Dışa Aktar / İçe Aktar" — kural seti paylaşımı.
    connect(settings_dialog_, &reji::SettingsDialog::exportRulesRequested,
            this, &MainWindow::exportRules);
    connect(settings_dialog_, &reji::SettingsDialog::importRulesRequested,
            this, &MainWindow::importRules);
    // Kalıcı durum: açılışta önceki oturumdaki tercihi geri yükle. setChecked
    // stateChanged → autoReloadToggled zincirini tetikler, izleme kurulur.
    if (settings_.value(QStringLiteral("rules/auto_reload"), false).toBool()) {
        settings_dialog_->setAutoReloadEnabled(true);
    }
    if (healing_overlay_) {
        healing_overlay_->setSettingsDialog(settings_dialog_);
    }

    // v0.2: Pipeline init + preview callback
    pipeline_ = std::make_shared<rj::Pipeline>();
    rj::Pipeline::Config pcfg;  // varsayılan: 1920x1080, 60fps, 6000kbps
    // SRT ayarları — SettingsDialog'dan al, yoksa varsayılan
    if (settings_dialog_) {
        // Video ayarlari — cfg_in.bitrate_kbps healing referans noktalarini
        // (original/max/atomic) init'te tek kaynaktan besler; fps pacer + encoder.
        pcfg.bitrate_kbps = settings_dialog_->videoBitrateKbps();
        pcfg.fps          = settings_dialog_->videoFps();
        strncpy_s(pcfg.srt_host, sizeof(pcfg.srt_host),
                  settings_dialog_->srtHost().toStdString().c_str(), _TRUNCATE);
        pcfg.srt_port = settings_dialog_->srtPort();
        // Faz2/Aşama2.2: protokol seçimi + RTMP sunucu URL'i + stream key (ayrı)
        pcfg.transport_protocol = settings_dialog_->transportProtocol();
        strncpy_s(pcfg.rtmp_url, sizeof(pcfg.rtmp_url),
                  settings_dialog_->rtmpUrl().toStdString().c_str(), _TRUNCATE);
        strncpy_s(pcfg.rtmp_key, sizeof(pcfg.rtmp_key),
                  settings_dialog_->rtmpStreamKey().toStdString().c_str(), _TRUNCATE);
        // Ses Ayarları (RTMP/FLV AAC MVP): etkin mi + seçili WASAPI cihaz id'si.
        // Boş id → sistem varsayılan endpoint'i. Ses yalnız RTMP çıkışında gönderilir
        // (pipeline init içinde transport==RTMP koşulu).
        pcfg.audio_enabled = settings_dialog_->isAudioEnabled();
        const std::wstring adev = settings_dialog_->audioDeviceId().toStdWString();
        wcsncpy_s(pcfg.audio_device_id,
                  sizeof(pcfg.audio_device_id) / sizeof(wchar_t),
                  adev.c_str(), _TRUNCATE);
    } else {
        strncpy_s(pcfg.srt_host, sizeof(pcfg.srt_host), "127.0.0.1", _TRUNCATE);
        pcfg.srt_port = 9000;
    }
    // V10/L5: TEK init yolu — wiring + frame thread + ilk-kurulum önerisi
    // initPipeline içinde. Eskiden ctor kendi pipeline_->init() yolunu
    // kullandığından initPipeline'daki öneri singleShot'ı HİÇBİR akışta
    // tetiklenmiyordu (initPipeline'ın çağıranı yoktu — ölü koddu).
    if (!initPipeline(pcfg)) {
        qDebug() << "Pipeline init failed";
        lbl_status_->setText(tr("Pipeline init başarısız — NVENC SDK eksik olabilir"));
    }
}

// V10/L5: init-sonrası GUI wiring — initPipeline'ın parçası (eski ctor
// else-bloğu, birebir taşındı).
void MainWindow::wireUpPipeline() {
    preview_widget_->setPipeline(pipeline_.get());
    preview_widget_->selectRenderPath(pipeline_->display_vendor_id());
    // v0.5.1: Vulkan device handle late-binding
    {
        auto* vk = rj::pipeline::gpu::VulkanInitializer::get();
        if (vk && vk->device()) {
            pipeline_->notify_vulkan_ready(vk->device(), vk->physical_device());
            fprintf(stderr, "[MainWindow] notify_vulkan_ready\n");
            fflush(stderr);

            // v0.5.1: Initialize GpuCopyOptimizer with Vulkan device + queue
            if (vk->graphics_queue() != VK_NULL_HANDLE) {
                GpuCopyOptimizer::Config copy_cfg;
                if (copy_optimizer_.init(vk->device(), vk->graphics_queue(), vk->physical_device(),
                                         vk->graphics_queue_family(), copy_cfg)) {
                    copy_optimizer_initialized_ = true;
                    preview_widget_->setCopyOptimizer(&copy_optimizer_);
                    // v0.5.2: Wire GL interop bridge for NT handle import
                    auto* bridge = pipeline_->get_external_memory_bridge();
                    preview_widget_->setBridge(bridge);
                    fprintf(stderr, "[MainWindow] GpuCopyOptimizer initialized, bridge=%p\n", (void*)bridge);
                    fflush(stderr);
                } else {
                    fprintf(stderr, "[MainWindow] GpuCopyOptimizer init failed\n");
                }
            }
        }
    }
    // Wire profiler to preview widget
    if (pipeline_->profiler()) {
        preview_widget_->setProfiler(pipeline_->profiler());
    }

    // v0.5.1: Zero-copy D3D11 frame callback → PreviewWidget::submitD3D11Frame
    // Pipeline calls this for each captured frame (D3D11→VkImage)
    pipeline_->set_d3d11_frame_callback(
        [this](void* staging_texture, uint32_t width, uint32_t height) {
            // v0.5.1: Get cached frame images from ExternalMemoryBridge
            VkImage staging_vk = VK_NULL_HANDLE;
            VkImage target_vk = VK_NULL_HANDLE;
            uint32_t pool_slot = 0;  // I23: bridge pool slot'u (tek doğruluk kaynağı)
            bool got = pipeline_->get_last_frame_images(&staging_vk, &target_vk, &pool_slot);
            if (got) {
                preview_widget_->submitD3D11Frame(staging_vk, target_vk, width, height, pool_slot);
            }
            (void)staging_texture;
        }
    );

    // CPU staging path: WGC → PreviewWidget (left panel) + ProgramWidget (right panel)
    pipeline_->set_preview_callback(
        [this](const void* bgra, int width, int height, int pitch) {
            preview_widget_->uploadCpuFrame(bgra, width, height, pitch);
            program_widget_->uploadFrame(bgra, width, height, pitch);
        }
    );

    // WebSocket scene commands → GUI thread via QueuedConnection
    pipeline_->set_scene_command_callback([this](int cmd, uint32_t param) {
        QMetaObject::invokeMethod(this, [this, cmd, param]() {
            if (cmd == 3) onCutTransition();
            if (cmd == 4) onFadeTransition();
            if (cmd == 5) {  // SetCurrentProgramScene → satırı seç + cut
                if (param < static_cast<uint32_t>(scene_list_->count())) {
                    scene_list_->setCurrentRow(static_cast<int>(param));
                    onCutTransition();  // sendSceneSwitchEvent zaten içinde (tek gerçek kaynak)
                } else {
                    qWarning("[MainWindow] SetScene: idx %u sınır dışı (count=%d), yok sayıldı",
                             param, scene_list_->count());
                }
            }
        }, Qt::QueuedConnection);
    });
}

// V10/L5: run_frame() ayrı thread'de — yalnız init() başarılıysa ve bir kez
// (idempotent; wiring TAMAMLANDIKTAN sonra çağrılmalı, callback'ler thread
// başlamadan set edilmiş olur).
void MainWindow::startFrameThread() {
    if (frame_thread_) return;
    frame_thread_ = new QThread(this);
    auto* worker = new QObject();
    worker->moveToThread(frame_thread_);
    connect(frame_thread_, &QThread::started, worker, [this, worker] {
        while (!frame_thread_->isInterruptionRequested()) {
            pipeline_->run_frame();
            // D10b: AcquireNextFrame timeout_ms=17>0 — DXGI zaten pacing yapar, msleep gereksiz
        }
        delete worker;
    });
    frame_thread_->start();
}

MainWindow::~MainWindow() {
    saveWindowState();
    stopFrameThread();
    // V8/I12: sever PreviewWidget's borrowed references to copy_optimizer_/bridge_
    // BEFORE shutting the optimizer down. paintGL() runs on this (GUI) thread, so
    // once we clear the pointers here no later paint can call into a torn-down
    // optimizer. Pairs with V8/I6's alive_ guard as defense-in-depth.
    if (preview_widget_) {
        preview_widget_->setCopyOptimizer(nullptr);
        preview_widget_->setBridge(nullptr);
    }
    if (copy_optimizer_initialized_) {
        copy_optimizer_.shutdown();
    }
}

void MainWindow::stopFrameThread() {
    if (frame_thread_ && frame_thread_->isRunning()) {
        frame_thread_->requestInterruption();
        frame_thread_->quit();  // event loop'u durdur
        if (!frame_thread_->wait(5000)) {
            fprintf(stderr, "[MainWindow] Frame thread 5s timeout\n");
            fflush(stderr);
        }
    }
}

bool MainWindow::initPipeline(const rj::Pipeline::Config& cfg) {
    // V10/L5: yeniden init desteklenmez — frame thread çalışırken pipeline'ı
    // yeniden kurmak callback/thread yaşam döngüsünü bozar.
    if (frame_thread_) {
        qWarning("[MainWindow] initPipeline: yeniden init desteklenmez — yok sayıldı");
        return false;
    }
    pipeline_cfg_ = cfg;
    if (!pipeline_) pipeline_ = std::make_shared<rj::Pipeline>();
    const bool ok = pipeline_->init(cfg);
    if (ok) {
        // Wiring frame thread'den ÖNCE: callback'ler thread başlamadan set olur.
        wireUpPipeline();
        startFrameThread();
        // İlk kurulumda bir kez donanım profili öner — event-loop'a ertele ki
        // pencere görünür olsun ve GpuScan init sonrası hazır olsun.
        QTimer::singleShot(0, this, &MainWindow::maybeSuggestProfileOnFirstRun);
    }
    return ok;
}

void MainWindow::maybeSuggestProfileOnFirstRun() {
    // Yalnız ilk kurulumda bir kez (Bölüm D kararı: canlı izleme/tekrar sorma yok).
    if (settings_.value(QStringLiteral("profile/asked"), false).toBool()) return;

    // Donanım sinyalleri: vendor/VRAM pipeline GpuScan'den (donanım izolasyonu),
    // RAM/batarya profile_advisor'dan.
    const uint32_t vendor = pipeline_ ? pipeline_->display_vendor_id() : 0;
    const uint64_t vram   = pipeline_ ? pipeline_->max_gpu_vram_mb()   : 0;
    const reji::HwSignals sig = reji::collect_hw_signals(vendor, vram);
    const reji::ProfileId suggested = reji::suggest_profile(sig);

    // Seçim ne olursa olsun bir daha otomatik sorma (ilk-kurulum sınırı).
    settings_.setValue(QStringLiteral("profile/asked"), true);

    // Tetikleyen sinyaller GÖRÜNÜR (Özellik#1 şeffaflığı — "sistem böyle karar
    // verdi" değil, "şu sinyaller şu profili öneriyor").
    const QString detail = tr("Donanım: %1 · VRAM %2 MB · RAM %3 MB · Güç: %4")
        .arg(vendorName(vendor))
        .arg(vram)
        .arg(sig.total_ram_mb)
        .arg(sig.on_battery ? tr("Batarya") : tr("AC"));

    QMessageBox box(this);
    box.setWindowTitle(tr("Donanım Profili"));
    box.setText(tr("Donanımınız için <b>%1</b> profili öneriliyor.")
                    .arg(profileDisplayName(suggested)));
    box.setInformativeText(
        detail + tr("\n\nUygulansın mı? Kural seti + bitrate/FPS ön-ayarı uygulanır; "
                    "sonra Ayarlar'dan elle değiştirebilirsiniz."));
    QPushButton* applyBtn  = box.addButton(tr("Uygula"), QMessageBox::AcceptRole);
    QPushButton* chooseBtn = box.addButton(tr("Başka profil seç..."), QMessageBox::ActionRole);
    QPushButton* skipBtn   = box.addButton(tr("Şimdilik atla"), QMessageBox::RejectRole);
    box.setDefaultButton(applyBtn);
    box.exec();

    if (box.clickedButton() == skipBtn) {
        return;  // Manuel — hiçbir profil uygulanmaz (kullanıcı elle yapılandırır).
    }

    reji::ProfileId chosen = suggested;
    if (box.clickedButton() == chooseBtn) {
        const QStringList items{ profileDisplayName(reji::ProfileId::Performance),
                                 profileDisplayName(reji::ProfileId::Stability),
                                 profileDisplayName(reji::ProfileId::Efficiency) };
        bool ok = false;
        const QString pick = QInputDialog::getItem(
            this, tr("Profil Seç"), tr("Profil:"),
            items, items.indexOf(profileDisplayName(suggested)), false, &ok);
        if (!ok) return;  // iptal — hiçbir şey uygulama
        if (pick == profileDisplayName(reji::ProfileId::Stability)) {
            chosen = reji::ProfileId::Stability;
        } else if (pick == profileDisplayName(reji::ProfileId::Efficiency)) {
            chosen = reji::ProfileId::Efficiency;
        } else {
            chosen = reji::ProfileId::Performance;
        }
    }

    applyProfile(chosen);
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
    pushSceneNamesToRust();  // ilk sahne isimlerini Rust'a bildir (GetSceneList için)
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

    // Madde 6/B: CUT/FADE geçiş shader'ı gerçekten çalışıyor (program_widget.cpp),
    // ancak şu an tek kaynak hem preview hem program'a beslendiğinden geçiş görünür
    // fark üretmiyor — çoklu-kaynak/sahne kompozisyonu Faz 3'te (ISource) gelecek.
    // GUI Gözlem Turu'nda butonları DEVRE DIŞI BIRAKMA reddedilmişti (çalışan bir
    // mekanizmayı "kırık" göstermemek için). Burada yalnız bilgilendirici bir tooltip
    // ekleniyor: setToolTip, setEnabled'dan bağımsızdır — butonlar tam tıklanabilir
    // ve işlevsel kalır (o karara çelişki YOK).
    const QString transition_hint = tr(
        "Geçiş çalışıyor, ancak şu an tek kaynak var; görünür fark için ikinci bir "
        "kaynak gerekir. Sahne kompozisyonu (çoklu kaynak) Faz 3'te gelecek.");
    btn_cut_->setToolTip(transition_hint);
    btn_fade_->setToolTip(transition_hint);
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
    connect(healing_overlay_, &reji::HealingOverlay::actionApproved,
            this, [](uint32_t action_id) { rj_action_approve(action_id); });
    // V8/I33: explicit reddetme → Rust (pending'den siler + kural cooldown).
    connect(healing_overlay_, &reji::HealingOverlay::actionRejected,
            this, [](uint32_t action_id) { rj_action_reject(action_id); });

    // V8/I19: başlangıç modunu Rust'a bir kez senkronla. healingModeChanged yalnız
    // OK'e basınca emit ediliyor; bu olmadan HEALING_MODE startup'ta 0 (AutoPilot)
    // kalırken UI combo default'u CoPilot gösterir → ayrışık. DAVRANIŞ DEĞİŞİKLİĞİ:
    // varsayılan başlangıç modu artık AutoPilot yerine CoPilot (UI ile tutarlı).
    if (settings_dialog_) {
        rj_set_healing_mode(static_cast<uint32_t>(settings_dialog_->healingMode()));
        // V8/I33c: per-kategori auto-onay ayarlarını da startup'ta Rust'a senkronla
        // (kapı motorda — UI checkbox default'ları motora ulaşmalı).
        syncAutoApproveToRust();
        syncWsPasswordToRust();  // V8/I8: WS parolasını startup'ta Rust'a senkronla
    }
}

// V8/I33c: SettingsDialog'daki per-kategori auto-onay checkbox'larını Rust
// motoruna iter (kapı motorda; UI yalnız değeri iletir). Startup'ta ve
// healingModeChanged (OK) her tetiklendiğinde çağrılır. (V8/I34: eski inert
// "source auto" kutusu kaldırıldı — source-switch aksiyonu yok.)
void MainWindow::syncAutoApproveToRust() {
    if (!settings_dialog_) return;
    rj_set_action_auto_approve(RJ_ACTION_CAT_BITRATE,    settings_dialog_->isBitrateAuto());
    rj_set_action_auto_approve(RJ_ACTION_CAT_RESOLUTION, settings_dialog_->isResolutionAuto());
    rj_set_action_auto_approve(RJ_ACTION_CAT_FPS,        settings_dialog_->isFpsAuto());
}

// V8/I8: SettingsDialog WS parolasını Rust motoruna iter (boş = auth kapalı).
// Startup'ta ve healingModeChanged (OK) her tetiklendiğinde çağrılır — parola
// yalnız yeni WS bağlantılarına uygulanır (mevcut oturumlar sürer). QByteArray
// çağrı boyunca canlı; Rust değeri kopyalar. Parola burada LOGLANMAZ.
void MainWindow::syncWsPasswordToRust() {
    if (!settings_dialog_) return;
    const QByteArray pw = settings_dialog_->wsPassword().toUtf8();
    rj_set_ws_password(pw.constData());
}

// Madde 6/A: Ayarlar OK'ine basıldığında (mod + per-kategori auto-onay Rust'a
// senkronlandıktan sonra) kullanıcıya "kaydedildi" geri bildirimi ver. GUI Gözlem
// Turu'nda doğrulandığı gibi, healing ayarlarının etkisi yalnız BİR SONRAKİ healing
// aksiyonunda görünür — bu bekleyiş daha önce kullanıcıya hiç belirtilmiyordu
// ("ayar kaydedildi mi?" belirsizliği). Mevcut lbl_status_ desenini kullanır; yeni
// mekanizma icat edilmez. kHealingSettingsNotifyMs sonra "Hazır"a döner.
void MainWindow::notifyHealingSettingsSaved(reji::HealingMode mode) {
    if (!lbl_status_) return;

    QString mode_name;
    switch (mode) {
        case reji::HealingMode::AutoPilot: mode_name = tr("Auto-Pilot"); break;
        case reji::HealingMode::CoPilot:   mode_name = tr("Co-Pilot");   break;
        case reji::HealingMode::Assist:    mode_name = tr("Assist");     break;
        case reji::HealingMode::Manual:    mode_name = tr("Manual");     break;
    }

    lbl_status_->setText(
        tr("Healing ayarları kaydedildi (Mod: %1) — bir sonraki healing aksiyonunda etkili olur")
            .arg(mode_name));

    // Kısa süreli bildirim: süre sonunda "Hazır"a dön. QPointer yerine 'this'
    // yaşam süresi pencereninkiyle aynı (lbl_status_ child widget); tek-atışlık.
    QTimer::singleShot(rj::constants::kHealingSettingsNotifyMs, this, [this] {
        if (lbl_status_) lbl_status_->setText(tr("Hazır"));
    });
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
    lbl_rules_ = new QLabel(this);   // auto-reload açılana dek gizli
    lbl_rules_->hide();

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
    statusBar()->addPermanentWidget(make_sep());
    statusBar()->addPermanentWidget(lbl_rules_);
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
    if (!pipeline_->start_stream()) {
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
    (void)pipeline_->stop_stream();
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
    pushSceneNamesToRust();  // yeni sahne listesini Rust'a bildir
}

void MainWindow::removeScene() {
    if (scene_list_->count() <= 1) return;
    const int row = scene_list_->currentRow();
    delete scene_list_->takeItem(row);
    if (scene_list_->currentRow() < 0)
        scene_list_->setCurrentRow(0);
    pushSceneNamesToRust();  // güncel sahne listesini Rust'a bildir
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

void MainWindow::pushSceneNamesToRust() {
    if (!scene_list_) return;
    // DİKKAT: utf8_names (QByteArray'ler) rj_push_scene_names çağrısı bitene kadar
    // scope'ta kalmalı — ptrs constData()'ları bunlara işaret ediyor. Rust HEMEN
    // kopyaladığı için çağrı SONRASI serbest, ama çağrı SIRASINDA canlı olmaları şart.
    std::vector<QByteArray>  utf8_names;
    std::vector<const char*> ptrs;
    utf8_names.reserve(scene_list_->count());
    ptrs.reserve(scene_list_->count());
    for (int i = 0; i < scene_list_->count(); ++i) {
        utf8_names.push_back(scene_list_->item(i)->text().toUtf8());
        ptrs.push_back(utf8_names.back().constData());
    }
    rj_push_scene_names(ptrs.data(), static_cast<uint32_t>(ptrs.size()));
}

void MainWindow::onSettingsClicked() {
    // settings_dialog_ constructor'da oluşturulur; bu guard sıra dışı durumlar içindir
    if (!settings_dialog_) {
        settings_dialog_ = new reji::SettingsDialog(this);
        connect(settings_dialog_, &reji::SettingsDialog::healingModeChanged,
                this, [this](reji::HealingMode mode) {
            if (healing_overlay_) healing_overlay_->setHealingMode(mode);
            rj_set_healing_mode(static_cast<uint32_t>(mode));  // V8/I19: modu Rust'a ilet (bkz. ctor'daki handler)
            syncAutoApproveToRust();  // V8/I33c: auto-onay ayarlarını da senkronla
            syncWsPasswordToRust();   // V8/I8: WS parolasını da senkronla
            notifyHealingSettingsSaved(mode);  // Madde 6/A: kaydedildi bildirimi
        });
        connect(settings_dialog_, &reji::SettingsDialog::editRulesRequested,
                this, &MainWindow::openRulesInEditor);
        connect(settings_dialog_, &reji::SettingsDialog::autoReloadToggled,
                this, &MainWindow::onAutoReloadToggled);
        if (healing_overlay_) healing_overlay_->setSettingsDialog(settings_dialog_);
    }
    // WS görünürlüğü: dialog her açılışında port + anlık aktif bağlantı sayısını
    // FFI'dan taze oku ve dialog'a ilet (bir kerelik sorgu; canlı poll yok). Tüm
    // rj_ çağrıları MainWindow'da toplanır — dialog FFI'dan bağımsız kalır.
    settings_dialog_->setWsStatus(rj_get_ws_port(), rj_get_ws_connection_count());
    // Kural görünürlüğü: motorun aktif kural listesini FFI'dan taze oku ve dialog'a
    // ilet (bir kerelik snapshot; canlı poll yok — WS görünürlük deseniyle aynı).
    // Rust JSON'u NUL-terminated tampona yazar; kural seti küçük (birkaç kural <1KB),
    // 64KB fazlasıyla yeter. Negatif dönüş (init değil / cap yetersiz) → boş string,
    // dialog "Kural okunamadı" placeholder'ı gösterir (sessizce yutma yok).
    {
        char rules_buf[65536];
        const int written = rj_rules_snapshot_json(rules_buf, static_cast<int>(sizeof(rules_buf)));
        settings_dialog_->setRules(written > 0 ? QString::fromUtf8(rules_buf, written)
                                               : QString());
    }
    settings_dialog_->exec();
}

// ---------------------------------------------------------------------------
// Kural dosyası — kanonik yol + gömülü şablondan tohumlama + editörde açma.
// ---------------------------------------------------------------------------
QString MainWindow::rulesFilePath() const {
    // QDir::homePath() Windows'ta USERPROFILE'ı döner; Rust tarafı da
    // (ffi.rs) %USERPROFILE%\.reji\rules.json kullanır — birebir eşleşir.
    return QDir::homePath() + QStringLiteral("/.reji/rules.json");
}

bool MainWindow::seedRulesFromTemplate(const QString& targetPath) {
    QFile tpl(QStringLiteral(":/config/rules.json.template"));
    if (!tpl.open(QIODevice::ReadOnly)) return false;
    const QByteArray content = tpl.readAll();
    tpl.close();

    // Üst dizin (~/.reji) yoksa oluştur.
    const QDir parent = QFileInfo(targetPath).dir();
    if (!parent.exists() && !parent.mkpath(QStringLiteral("."))) return false;

    QFile out(targetPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const qint64 written = out.write(content);
    out.close();
    return written == content.size();
}

void MainWindow::openRulesInEditor() {
    const QString path = rulesFilePath();

    // Dosya yoksa gömülü şablondan tohumla (kullanıcı çalışır bir başlangıç
    // dosyası bulur — Faz 1 kararı).
    if (!QFileInfo::exists(path)) {
        if (!seedRulesFromTemplate(path)) {
            QMessageBox::warning(this, tr("Kuralları Düzenle"),
                tr("Kural dosyası oluşturulamadı:\n%1").arg(path));
            return;
        }
    }

    // Kuruluş-sırası düzeltmesi: dosya artık kesin var (yukarıda tohumlandıysa
    // da). Auto-reload, dosya/dizin daha yokken işaretlenmiş olabilir — o anda
    // armRulesWatch() QFileInfo::exists() koruması yüzünden boş kalmıştı. Yol
    // artık var olduğuna göre watcher'ı yeniden silahlandır; aksi halde ilk
    // düzenlemeye kadar hiçbir fileChanged/directoryChanged tetiklenmez (sessiz
    // regresyon). armRulesWatch() idempotent (!contains guard'lı) — zaten
    // izleniyorsa zararsız.
    if (settings_dialog_ && settings_dialog_->isAutoReloadEnabled()) {
        ensureRulesWatcher();
        armRulesWatch();
    }

    // QDesktopServices başarısızlığı sessizce yutulmaz (I10 dersi).
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(path))) {
        QMessageBox::warning(this, tr("Kuralları Düzenle"),
            tr("Dosya varsayılan editörde açılamadı:\n%1").arg(path));
    }
}

// ---------------------------------------------------------------------------
// Otomatik yeniden yükleme (hot-reload) — QFileSystemWatcher + debounce.
// ---------------------------------------------------------------------------
namespace {
// Editörler tek kayıtta birden çok dosya-sistemi olayı üretir; bu pencere
// içindeki olaylar tek bir reload'a birleştirilir.
constexpr int kRulesReloadDebounceMs = 300;
}

void MainWindow::ensureRulesWatcher() {
    if (!rules_watcher_) {
        rules_watcher_ = new QFileSystemWatcher(this);
        connect(rules_watcher_, &QFileSystemWatcher::fileChanged,
                this, &MainWindow::onRulesPathChanged);
        connect(rules_watcher_, &QFileSystemWatcher::directoryChanged,
                this, &MainWindow::onRulesPathChanged);
    }
    if (!rules_reload_debounce_) {
        rules_reload_debounce_ = new QTimer(this);
        rules_reload_debounce_->setSingleShot(true);
        rules_reload_debounce_->setInterval(kRulesReloadDebounceMs);
        connect(rules_reload_debounce_, &QTimer::timeout,
                this, &MainWindow::reloadRulesNow);
    }
}

void MainWindow::armRulesWatch() {
    if (!rules_watcher_) return;
    // Saf çekirdek rules_watch.h'de — birim testiyle kilitlenen kuruluş-sırası
    // değişmezi (dosya yokken hiçbir yol eklenmez → re-arm şart) orada belgeli.
    // V10/L4: checkbox durumu çekirdeğe geçirilir — auto-reload kapalıyken
    // (writeValidatedRules/reloadRulesNow re-arm çağrıları dahil) yol eklenmez.
    const bool enabled = settings_dialog_ && settings_dialog_->isAutoReloadEnabled();
    reji::ui::armRulesWatchOn(*rules_watcher_, rulesFilePath(), enabled);
}

void MainWindow::onAutoReloadToggled(bool enabled) {
    settings_.setValue(QStringLiteral("rules/auto_reload"), enabled);  // kalıcılık

    if (!enabled) {
        if (rules_watcher_) {
            const QStringList files = rules_watcher_->files();
            const QStringList dirs  = rules_watcher_->directories();
            if (!files.isEmpty()) rules_watcher_->removePaths(files);
            if (!dirs.isEmpty())  rules_watcher_->removePaths(dirs);
        }
        if (rules_reload_debounce_) rules_reload_debounce_->stop();
        if (lbl_rules_) { lbl_rules_->clear(); lbl_rules_->hide(); }
        return;
    }

    ensureRulesWatcher();
    armRulesWatch();
    if (lbl_rules_) {
        lbl_rules_->setStyleSheet(QString());   // nötr
        lbl_rules_->setText(tr("Kurallar: izleniyor"));
        lbl_rules_->show();
    }
}

void MainWindow::onRulesPathChanged(const QString& /*path*/) {
    // Yalnızca debounce'u (yeniden) başlat — gerçek reload reloadRulesNow'da.
    if (rules_reload_debounce_) rules_reload_debounce_->start();
}

void MainWindow::reloadRulesNow() {
    // Atomic-save tuzağı: editör dosyayı sil+yeniden-yaz ile kaydettiyse yol
    // watcher'dan düşmüştür — yeniden ekle, yoksa ilk kayıttan sonra susar.
    armRulesWatch();

    const QString    path = rulesFilePath();
    const QByteArray path_utf8 = path.toUtf8();
    const int32_t    rv = rj_reload_rules(path_utf8.constData());

    if (!lbl_rules_) return;
    if (rv == 1) {
        // Başarı önceki (yapışkan) hatayı da temizler.
        lbl_rules_->setStyleSheet(QString());
        lbl_rules_->setText(tr("Kurallar: yeniden yüklendi %1")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"))));
    } else {
        // Hata sessizce yutulmaz (I10). Yapışkan: yalnızca sonraki BAŞARILI
        // reload temizler — zaman aşımıyla kaybolmaz.
        lbl_rules_->setStyleSheet(QStringLiteral("color:#E53935;"));
        lbl_rules_->setText(tr("Kurallar: YÜKLEME HATASI — eski kurallar korunuyor (JSON?)"));
    }
}

// ---------------------------------------------------------------------------
// Kural seti paylaşımı — dışa/içe aktar.
// ---------------------------------------------------------------------------
void MainWindow::exportRules() {
    const QString src = rulesFilePath();
    if (!QFileInfo::exists(src)) {
        QMessageBox::warning(this, tr("Dışa Aktar"),
            tr("Dışa aktarılacak kural dosyası bulunamadı:\n%1").arg(src));
        return;
    }

    // Kör-kopya koruması: motor rollback ile eski kurallarda çalışırken diskteki
    // rules.json bozulmuş olabilir — bozuk dosyayı "paylaşılabilir kural seti"
    // olarak dışarı taşıma, kaynağında yakala.
    QString verr;
    if (!validateRulesFile(src, verr)) {
        QMessageBox::warning(this, tr("Dışa Aktar"),
            tr("Mevcut kural dosyası doğrulamadan geçemedi — dışa aktarım iptal edildi.\n"
               "Motor bellekteki eski kurallarla çalışıyor olabilir; önce dosyayı düzeltin:\n%1\n\n%2")
            .arg(src, verr));
        return;
    }

    const QString defaultName = QStringLiteral("reji-rules-%1.json")
        .arg(QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    const QString dest = QFileDialog::getSaveFileName(
        this,
        tr("Kural Setini Dışa Aktar"),
        QDir::homePath() + QLatin1Char('/') + defaultName,
        tr("JSON Kural Dosyası (*.json);;Tüm dosyalar (*)"));

    if (dest.isEmpty()) return;

    // V10/L1(d): eski remove-sonra-copy deseni, copy başarısızsa kullanıcının
    // hedefteki mevcut dosyasını geri dönüşsüz siliyordu (TOCTOU). QSaveFile
    // temp+rename ile atomik — başarısızlıkta hedefteki eski dosya aynen kalır.
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Dışa Aktar"),
            tr("Kaynak dosya okunamadı:\n%1\n(%2)").arg(src, in.errorString()));
        return;
    }
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Dışa Aktar"),
            tr("Dosya yazılamadı:\n%1\n(%2)").arg(dest, out.errorString()));
        return;
    }
    if (out.write(in.readAll()) < 0 || !out.commit()) {
        QMessageBox::warning(this, tr("Dışa Aktar"),
            tr("Dosya yazılamadı:\n%1\n(%2)").arg(dest, out.errorString()));
        return;
    }

    if (lbl_rules_) {
        lbl_rules_->setStyleSheet(QString());
        lbl_rules_->setText(tr("Kurallar: dışa aktarıldı → %1")
            .arg(QFileInfo(dest).fileName()));
        lbl_rules_->show();
    }
}

bool MainWindow::validateRulesFile(const QString& src, QString& errMsg) {
    // Geçici konuma kopyala ve doğrula (asıl rules.json'a VE motora DOKUNMA).
    // `src` bir kullanıcı dosyası VEYA gömülü qrc kaynağı (":/config/...") olabilir;
    // QFile her ikisini de okur, rj_validate_rules yalnız gerçek dosyada çalışır.
    // QFile::copy hedef dosya mevcutken false döner, QTemporaryFile::open() ise
    // dosyayı diskte oluşturur — bu yüzden kopya değil, içerik doğrudan yazılır.
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        errMsg = tr("Doğrulama için kopyalanamadı:\n%1\n(%2)")
            .arg(src, in.errorString());
        return false;
    }

    QTemporaryFile tmp;
    tmp.setAutoRemove(true);  // Dosya nesne ömrü boyunca yaşar, destructor siler.
    if (!tmp.open()) {
        errMsg = tr("Geçici doğrulama dosyası oluşturulamadı:\n%1")
            .arg(tmp.errorString());
        return false;
    }
    if (tmp.write(in.readAll()) < 0 || !tmp.flush()) {
        errMsg = tr("Geçici doğrulama dosyasına yazılamadı:\n%1")
            .arg(tmp.errorString());
        return false;
    }
    const QString tmpPath = tmp.fileName();
    tmp.close();  // Windows'ta rj_validate_rules açarken paylaşım çakışması olmasın.

    // V10/L1(c): yan-etkisiz doğrulama — eski rj_reload_rules yolu geçerli
    // dosyada CANLI motoru geçici-dosya kurallarına çeviriyordu; hata yolunda
    // motor orada kalıyor, "eski kurallar korunuyor" mesajı yalan oluyordu.
    const QByteArray tmpPathUtf8 = tmpPath.toUtf8();
    if (rj_validate_rules(tmpPathUtf8.constData()) != 1) {
        errMsg = tr("Dosya geçerli bir kural seti değil.\n\n"
                    "Beklenen format: geçerli JSON, her kuralda 'id', 'condition', 'action' alanları.");
        return false;
    }
    return true;
}

bool MainWindow::writeValidatedRules(const QString& src, QString& errMsg) {
    // Adım 1: Geçici kopya üzerinde doğrula (asıl rules.json'a dokunulmaz).
    if (!validateRulesFile(src, errMsg)) return false;

    // Adım 2: Mevcut rules.json'ı yedekle (geri dönüşsüz kayıp yok — I10).
    // V10/L1(a): copy dönüşü kontrol edilir — yedek alınamazsa asıl dosyaya
    // hiç dokunmadan iptal (eskiden sessizce yedeksiz devam ediliyordu).
    const QString dest    = rulesFilePath();
    const QString backup  = dest + QStringLiteral(".backup");
    if (QFileInfo::exists(dest)) {
        QFile::remove(backup);
        if (!QFile::copy(dest, backup)) {
            errMsg = tr("Yedek alınamadı:\n%1\nMevcut kurallara dokunulmadı.").arg(backup);
            return false;
        }
    }

    // Adım 3: Üst dizini oluştur (henüz yoksa) ve yeni dosyayı ATOMİK yaz.
    // V10/L1(b): eski remove+copy deseni copy-başarısızlığında rules.json'u
    // diskte bırakmıyordu (backup'tan restore da yoktu). QSaveFile geçici ada
    // yazıp commit'te rename eder — başarısızlıkta eski dosya aynen kalır.
    const QDir parentDir = QFileInfo(dest).dir();
    if (!parentDir.exists()) parentDir.mkpath(QStringLiteral("."));

    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        errMsg = tr("Kaynak dosya okunamadı:\n%1\n(%2)").arg(src, in.errorString());
        return false;
    }
    QSaveFile outFile(dest);
    if (!outFile.open(QIODevice::WriteOnly)) {
        errMsg = tr("Dosya yazılamadı:\n%1\n(%2)").arg(dest, outFile.errorString());
        return false;
    }
    if (outFile.write(in.readAll()) < 0 || !outFile.commit()) {
        errMsg = tr("Dosya yazılamadı:\n%1\n(%2)").arg(dest, outFile.errorString());
        return false;
    }

    // Adım 4: Reload — watcher aktifse dosya değişimini debounce ile o tetikler;
    // değilse yolu (yeni oluştu) ekleyip elle yükle.
    const bool watcherActive = settings_dialog_ && settings_dialog_->isAutoReloadEnabled();
    if (!watcherActive) {
        armRulesWatch();
        reloadRulesNow();
    }
    return true;
}

void MainWindow::importRules() {
    const QString src = QFileDialog::getOpenFileName(
        this,
        tr("Kural Setini İçe Aktar"),
        QDir::homePath(),
        tr("JSON Kural Dosyası (*.json);;Tüm dosyalar (*)"));

    if (src.isEmpty()) return;

    QString err;
    if (!writeValidatedRules(src, err)) {
        if (lbl_rules_) {
            lbl_rules_->setStyleSheet(QStringLiteral("color:#E53935;"));
            // errMsg gerçek sebebi taşır (kopyalama mı, doğrulama mı) — etiket
            // hata sınıfı iddia etmez, ayrıntı uyarı diyaloğunda.
            lbl_rules_->setText(tr("Kurallar: İÇE AKTARIM BAŞARISIZ — dosya uygulanmadı"));
            lbl_rules_->show();
        }
        QMessageBox::warning(this, tr("İçe Aktar"), err);
        return;
    }

    // Başarı: watcher aktifse debounce ile yüklenecek (durum satırını burada göster);
    // değilse writeValidatedRules zaten reloadRulesNow() ile lbl_rules_'ı güncelledi.
    const bool watcherActive = settings_dialog_ && settings_dialog_->isAutoReloadEnabled();
    if (watcherActive && lbl_rules_) {
        lbl_rules_->setStyleSheet(QString());
        lbl_rules_->setText(tr("Kurallar: içe aktarıldı — yeniden yükleniyor..."));
        lbl_rules_->show();
    }
}

bool MainWindow::applyProfile(reji::ProfileId id) {
    // Gömülü profil kural setini (:/config/profiles/<kök>.json) aynı Sütun 3
    // güvenlik akışıyla yaz. Kaynak qrc olduğundan QFile::copy doğrudan çalışır.
    const QString res = QStringLiteral(":/config/profiles/%1.json")
        .arg(QString::fromLatin1(reji::profile_resource_name(id)));

    QString err;
    if (!writeValidatedRules(res, err)) {
        // Beklenmez — gömülü profiller Commit 1 testinde + RCC'de doğrulandı; yine
        // de sessiz yutma yok (I10).
        if (lbl_rules_) {
            lbl_rules_->setStyleSheet(QStringLiteral("color:#E53935;"));
            lbl_rules_->setText(tr("Profil uygulanamadı — mevcut kurallar korunuyor"));
            lbl_rules_->show();
        }
        QMessageBox::warning(this, tr("Donanım Profili"), err);
        return false;
    }

    // Preset: bitrate/FPS'i SettingsDialog'a uygula — encoder init bu değerleri okur
    // (ilk kurulumda yayın başlamadan uygulanır, canlı reconfigure gerekmez).
    const reji::ProfilePreset preset = reji::preset_for(id);
    if (settings_dialog_) {
        settings_dialog_->setVideoBitrateKbps(preset.bitrate_kbps);
        settings_dialog_->setVideoFps(preset.fps);
    }

    if (lbl_rules_) {
        lbl_rules_->setStyleSheet(QString());
        lbl_rules_->setText(tr("Profil uygulandı: %1 (%2 kbps · %3 fps)")
            .arg(profileDisplayName(id))
            .arg(preset.bitrate_kbps)
            .arg(preset.fps));
        lbl_rules_->show();
    }
    return true;
}

// ---------------------------------------------------------------------------
// Metrics poll — drains the Rust command queue every 100 ms.
// rj_command_drain is lock-free (crossbeam ArrayQueue) and non-blocking.
// ---------------------------------------------------------------------------
void MainWindow::pollMetrics() {
    metrics_collector_.poll();

    RjMetricSample sample{};
    if (rj_metrics_poll(&sample) == 0) return;

    lbl_fps_->setText(QString("%1 fps").arg(
        static_cast<double>(sample.fps_actual), 0, 'f', 1));
    lbl_bitrate_->setText(QString("%1 kbps").arg(sample.bitrate_kbps));

    if (sample.frame_drop_pct > 0) {
        lbl_status_->setText(
            QString("Drop: %1%").arg(sample.frame_drop_pct));
    }
}

namespace {
// Özellik#1: RjMetricId → (insan-okunur ad, birim) eşlemesi UI-yerel — Rust yalnız
// sayısal id + değer + eşik gönderir, yerelleştirilebilir cümle burada kurulur.
// `MetricNone` (LogOnly / koşul parse edilemedi) → boş QString: UI açıklama
// satırını atlar. Örn. çıktı: "GPU Sıcaklığı: 87°C (eşik 85°C)".
QString formatActionExplanation(const RjActionEvent& ev) {
    QString name;
    QString unit;
    switch (ev.metric_id) {
        case GpuTempC:       name = QObject::tr("GPU Sıcaklığı");    unit = QStringLiteral("°C");  break;
        case CpuTempC:       name = QObject::tr("CPU Sıcaklığı");    unit = QStringLiteral("°C");  break;
        case FrameDropPct:   name = QObject::tr("Kare düşüşü");      unit = QStringLiteral("%");   break;
        case MemoryUsagePct: name = QObject::tr("Bellek kullanımı"); unit = QStringLiteral("%");   break;
        case CpuLoadPct:     name = QObject::tr("CPU yükü");         unit = QStringLiteral("%");   break;
        case GpuLoadPct:     name = QObject::tr("GPU yükü");         unit = QStringLiteral("%");   break;
        case NetworkRttMs:   name = QObject::tr("Ağ gecikmesi");     unit = QStringLiteral(" ms"); break;
        case NetworkLossPct: name = QObject::tr("Paket kaybı");      unit = QStringLiteral("%");   break;
        case MetricNone:
        default:
            return QString();  // açıklanamayan aksiyon → açıklama satırı yok
    }
    // Not: QString::arg en düşük numaralı %n'in TÜM geçişlerini değiştirir; %3
    // (birim) iki kez kullanılır → tek .arg(unit) çağrısı ikisini de doldurur.
    QString line = QObject::tr("%1: %2%3 (eşik %4%3)")
        .arg(name)
        .arg(ev.current_value)
        .arg(unit)
        .arg(ev.threshold_value);
    // Özellik#5: eşik çalışma-zamanı kalibrasyonundan geliyorsa "[kalibre]" ekle.
    // Kullanıcı `rules.json`'daki statik değeri (örn. 85) bilip açıklamada farklı
    // bir eşik (örn. 83) görünce "yazılım yanlış mı söylüyor" şüphesine düşmesin —
    // Özellik#1'in güven inşası amacı korunur (calibrated bayrağı FFI'dan gelir).
    if (ev.calibrated != 0) {
        line += QObject::tr(" [kalibre]");
    }
    return line;
}
} // namespace

// ---------------------------------------------------------------------------
// Healing action poll — drains rj_action_dequeue every 200 ms and feeds
// HealingOverlay.  Non-blocking: returns immediately when queue is empty.
// ---------------------------------------------------------------------------
void MainWindow::pollHealingActions() {
    // V8/I33 (I11): aktüatör kuyruğu (rj_action_dequeue) yerine AYRI UI event
    // kuyruğundan (rj_action_event_dequeue) çekilir — UI artık aktüatörle aynı
    // kuyruğu yarıştırmaz.
    RjActionEvent ev{};
    if (rj_action_event_dequeue(&ev) == 0) return;

    // V8/I33: Invalidated (Rust pending TTL doldu / mod değişti) → UI temizliği.
    if (ev.kind == RJ_ACTION_EVENT_INVALIDATED) {
        if (healing_overlay_) healing_overlay_->onActionInvalidated(ev.id);
        return;
    }

    reji::ActionEvent event{};
    event.id               = ev.id;
    // V8/I33: onay kararı MOTORDAN gelir (overlay yeniden hesaplamaz).
    event.require_approval = (ev.require_approval != 0);
    event.timestamp        = QDateTime::currentDateTime().toString("HH:mm:ss");

    switch (ev.action_type) {
        case RJ_ACTION_BITRATE_REDUCE:
            event.type        = reji::ActionType::BitrateReduce;
            event.description = tr("Bitrate düşürülüyor → %1 kbps").arg(ev.param1);
            break;
        case RJ_ACTION_BITRATE_RECOVER:
            event.type        = reji::ActionType::BitrateRecover;
            event.description = tr("Bitrate normale döndürülüyor → %1 kbps").arg(ev.param1);
            break;
        case RJ_ACTION_SCALE_RESOLUTION:
            event.type        = reji::ActionType::ResolutionScale;
            event.description = tr("Çözünürlük düşürülüyor (%1%)").arg(ev.param1);
            break;
        case RJ_ACTION_RESTORE_RESOLUTION:
            event.type        = reji::ActionType::ResolutionRestore;
            event.description = tr("Çözünürlük normale döndürülüyor");
            break;
        case RJ_ACTION_CAP_FPS:
            event.type        = reji::ActionType::FpsLimit;
            event.description = tr("FPS sınırlanıyor → %1 fps").arg(ev.param1);
            break;
        case RJ_ACTION_RESTORE_FPS:
            event.type        = reji::ActionType::FpsRestore;
            event.description = tr("FPS sınırı kaldırılıyor");
            break;
        default:
            event.type        = reji::ActionType::LogOnly;
            event.description = tr("Kayıt (log-only)");
            break;
    }

    // Özellik#1: aksiyonu tetikleyen metrik/değer/eşik açıklamasını ekle (varsa).
    // Tek satırda kalması için " — " ayırıcı; description hem geçmiş listesinde,
    // hem banner'da, hem CoPilot onay checkbox'ında gösterildiğinden üçünde de görünür.
    const QString explain = formatActionExplanation(ev);
    if (!explain.isEmpty()) {
        event.description += QStringLiteral(" — ") + explain;
    }

    if (healing_overlay_) {
        // SIYAH_KUTU_REGRESYON: yayın yokken bilgi banner'ı açılmaz. Boşta
        // her hysteresis penceresinde tetiklenen kurallar (örn. stabilite
        // profilinin frame_drop_recovery'si, 6000ms) sahne panelinin üstünde
        // periyodik kutu üretiyordu. Event geçmişe yine işlenir; onay
        // prompt'ları overlay tarafında bu bayraktan bağımsız gösterilir.
        const bool show_banner = stream_active_ || event.require_approval;
        healing_overlay_->move(
            width() - healing_overlay_->width() - 16,
            menuBar()->height() + 8);
        healing_overlay_->onActionEvent(event, show_banner);
    }
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
        healing_overlay_->showMessage(message, rj::constants::kHealingBannerTimeoutMs);
    }
    lbl_status_->setText(message);
}

// ---------------------------------------------------------------------------
// Close event
// ---------------------------------------------------------------------------
void MainWindow::closeEvent(QCloseEvent* ev) {
    saveWindowState();
    stopFrameThread();
    ev->accept();
}

// ===========================================================================
#else // !QT6_AVAILABLE — stub implementation
// ===========================================================================

bool MainWindow::initPipeline(const rj::Pipeline::Config& cfg) {
    pipeline_cfg_ = cfg;
    if (!pipeline_) pipeline_ = std::make_shared<rj::Pipeline>();
    return pipeline_->init(cfg);
}

#endif // QT6_AVAILABLE
