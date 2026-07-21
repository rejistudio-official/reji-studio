#pragma once
#include <cstdint>
#include "../pipeline/include/pipeline.h"       // rj::Pipeline, rj::Pipeline::Config
#include "../pipeline/copy_optimizer.h"        // GpuCopyOptimizer
#include "../pipeline/include/metrics_collector.h"  // rj::MetricsCollector

// ---------------------------------------------------------------------------
// QT6_AVAILABLE is defined by CMakeLists.txt when find_package(Qt6) succeeds.
// Without it, MainWindow compiles as a no-op stub that preserves the public API.
// ---------------------------------------------------------------------------
#ifdef QT6_AVAILABLE

#include <QMainWindow>
#include <QSettings>

class QCloseEvent;
class QFileSystemWatcher;
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
    enum class HealingMode;   // tanım: healing_overlay.h (Madde 6/A imzası için)
    enum class ProfileId;     // tanım: profile_advisor.h (applyProfile imzası için)
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

    /// TEK pipeline init yolu (V10/L5): config'i saklar, Pipeline::init(cfg)
    /// çağırır; başarıda GUI wiring + frame thread + ilk-kurulum profil önerisi
    /// (singleShot). Ctor da burayı kullanır. Yeniden init desteklenmez
    /// (frame thread varken false döner). Başarısızlıkta false.
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
    /// SettingsDialog::editRulesRequested → rules.json'u sistemin varsayılan
    /// editöründe açar. Dosya yoksa gömülü şablondan tohumlar (Commit 1).
    void openRulesInEditor();
    /// SettingsDialog::autoReloadToggled → dosya izlemeyi aç/kapat + durumu
    /// QSettings'e yaz. Açıkken rules.json değişince rj_reload_rules çağrılır.
    void onAutoReloadToggled(bool enabled);
    /// Watcher (file/directory)Changed → debounce zamanlayıcısını (yeniden)
    /// başlatır; tek kayıt birden çok olay üretir, birleştirilir.
    void onRulesPathChanged(const QString& path);
    /// Debounce dolunca: yolu (atomic-save sonrası) yeniden ekler ve
    /// rj_reload_rules çağırıp sonucu lbl_rules_'a yazar.
    void reloadRulesNow();
    /// Mevcut rules.json'ı kullanıcının seçtiği konuma kopyalar.
    void exportRules();
    /// Seçilen dosyayı doğrulayıp (geçici konumda) rules.json üzerine yazar;
    /// yedek rules.json.backup alır, reload'u watcher'a veya elle tetikler.
    void importRules();
    /// Önerilen/seçilen donanım profilini uygular: gömülü kural setini
    /// (:/config/profiles/<id>.json) writeValidatedRules ile yazar + bitrate/FPS
    /// preset'ini SettingsDialog'a uygular. Başarıda true. İlk-kurulum akışı (Commit 5)
    /// bunu çağırır. Yalnız ÖNERİ uygular — kullanıcı seçimiyle tetiklenir.
    bool applyProfile(reji::ProfileId id);
    /// 200 ms poll: V8/I33 — drains rj_action_event_dequeue (UI event kuyruğu,
    /// aktüatörden ayrı) and feeds HealingOverlay.
    void pollHealingActions();

private:
    /// V8/I33c: SettingsDialog auto-onay checkbox'larını Rust motoruna senkronlar.
    void syncAutoApproveToRust();
    /// V8/I8: SettingsDialog WS parolasını Rust motoruna senkronlar (boş = auth kapalı).
    void syncWsPasswordToRust();
    /// Madde 6/A: Ayarlar OK'inden sonra, ayarların kaydedildiğini ve etkisinin
    /// bir sonraki healing aksiyonunda görüneceğini lbl_status_'ta kısa süreliğine
    /// bildirir (kHealingSettingsNotifyMs sonra "Hazır"a döner). Mevcut durum-widget
    /// desenini kullanır — yeni mekanizma icat edilmez.
    void notifyHealingSettingsSaved(reji::HealingMode mode);
    void buildMenuBar();
    void buildCentralWidget();
    void buildStatusBar();
    /// Kural dosyasının kanonik yolu: %USERPROFILE%\.reji\rules.json.
    /// QDir::homePath() Windows'ta USERPROFILE döner — Rust tarafındaki
    /// yol hesabıyla (ffi.rs) birebir eşleşir.
    QString rulesFilePath() const;
    /// Gömülü şablonu (:/config/rules.json.template) hedef yola yazar.
    /// Üst dizini gerekirse oluşturur. Başarıda true.
    bool seedRulesFromTemplate(const QString& targetPath);
    /// İçe-aktarım + profil uygulama ORTAK çekirdeği (Sütun 3 güvenlik akışı):
    /// `src` (kullanıcı dosyası VEYA gömülü qrc kaynağı) geçici konumda
    /// rj_validate_rules ile DOĞRULANIR (motora ve rules.json'a dokunulmadan) →
    /// geçerliyse rules.json.backup alınır (yedek başarısızsa iptal) →
    /// rules.json'a QSaveFile ile atomik yazılır → reload watcher'a/elle
    /// tetiklenir. Başarıda true; hatada `errMsg` doldurulur ve motor + disk
    /// ESKİ hâlinde kalır (V10/L1). UI/mesajlaşma çağırana aittir
    /// (import: kullanıcı QMessageBox'ı; profil: otomatik durum satırı).
    bool writeValidatedRules(const QString& src, QString& errMsg);
    /// `src` kural dosyasını geçici kopya üzerinde rj_validate_rules ile
    /// doğrular — asıl rules.json'a VE canlı motora dokunmaz (V10/L1: eski
    /// rj_reload_rules yolu her doğrulamada motoru geçici-dosya kurallarına
    /// çeviriyordu; hata yolunda "eski kurallar korunuyor" mesajı yanlıştı).
    /// Başarıda true; hatada `errMsg` doldurulur. writeValidatedRules'ın 1. adımı
    /// ve exportRules'un kör-kopya koruması ortak kullanır.
    bool validateRulesFile(const QString& src, QString& errMsg);
    /// İlk kurulumda bir kez (QSettings "profile/asked" bayrağı): donanım
    /// sinyallerini toplar (pipeline GpuScan vendor/VRAM + RAM/batarya), profil
    /// önerir, tetikleyen sinyalleri gösteren bir diyalogla sunar (Uygula / Başka
    /// profil seç / Şimdilik atla). Seçime göre applyProfile. initPipeline
    /// başarısından sonra event-loop'a ertelenerek çağrılır.
    void maybeSuggestProfileOnFirstRun();
    /// rules_watcher_ + debounce zamanlayıcısını tembel (lazy) oluşturur.
    void ensureRulesWatcher();
    /// rules.json'u (varsa) ve üst dizinini watcher'a ekler. Üst-dizin izlemesi
    /// atomic-save (sil+yeniden-yaz) ve dosya-oluşturma olaylarını yakalar.
    void armRulesWatch();
    /// Sahne isimlerini Rust'a (rj_push_scene_names) bildirir — GetSceneList için.
    void pushSceneNamesToRust();
    void saveWindowState();
    void loadWindowState();
    void stopFrameThread();
    // V10/L5: initPipeline'ın parçaları — init-sonrası GUI wiring (preview/
    // callback bağlantıları) ve frame thread başlatma (idempotent). Ctor
    // dahil TÜM init'ler initPipeline üzerinden geçer; iki yan etki kümesi
    // (öneri singleShot'ı vs frame thread) bir daha ayrışamaz.
    void wireUpPipeline();
    void startFrameThread();

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
    /// Kural yeniden-yükleme durumu — lbl_status_'tan AYRI kalıcı widget.
    /// Genel durum mesajlarınca ezilmez; hata mesajı yalnızca sonraki başarılı
    /// reload ile temizlenir (yapışkan). Auto-reload kapalıyken gizli.
    QLabel* lbl_rules_{nullptr};

    // ── Kural hot-reload (auto-reload açıkken) ──────────────────────────────
    QFileSystemWatcher* rules_watcher_{nullptr};
    QTimer*             rules_reload_debounce_{nullptr};

    // ── Rust bridge + self-healing overlay ─────────────────────────────────
    reji::RustBridge*     rust_bridge_{nullptr};
    reji::HealingOverlay* healing_overlay_{nullptr};
    reji::SettingsDialog* settings_dialog_{nullptr};
    QTimer*               metrics_timer_{nullptr};

    // ── Pipeline ───────────────────────────────────────────────────────────
    std::shared_ptr<rj::Pipeline> pipeline_;
    rj::Pipeline::Config          pipeline_cfg_{};
    bool                 stream_active_{false};
    GpuCopyOptimizer     copy_optimizer_;              // v0.5.1: GPU-only blit + timeline sem
    bool                 copy_optimizer_initialized_{false};
    rj::MetricsCollector metrics_collector_;           // system load — polled at 1 Hz

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
    std::shared_ptr<rj::Pipeline> pipeline_;
    rj::Pipeline::Config          pipeline_cfg_{};
};

#endif // QT6_AVAILABLE
