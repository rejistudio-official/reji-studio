#pragma once
#ifdef QT6_AVAILABLE

#include <QDialog>
#include <memory>
#include "healing_overlay.h"

class QComboBox;
class QLineEdit;
class QSpinBox;

namespace reji {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    HealingMode healingMode() const;
    void setHealingMode(HealingMode mode);

    // Yaklaşım C: Co-Pilot action settings
    bool isBitrateAuto() const;
    bool isResolutionAuto() const;
    bool isFpsAuto() const;

    // v0.4+ Hot-reload
    bool isAutoReloadEnabled() const;
    void setAutoReloadEnabled(bool enabled);

    // Video ayarları (bitrate + FPS). Encoder init'e ve — cfg_in.bitrate_kbps
    // üzerinden — healing'in referans noktalarına (original/max/atomic bitrate)
    // tek kaynaktan yansır.
    uint32_t videoBitrateKbps() const;    // 500–50000 kbps
    uint32_t videoFps() const;            // 30 / 60 / 120 (set_fps_limit tavanı 120)
    // Donanım profili ön-ayarını UI'a uygular (profil seçilince bitrate/FPS'i set eder).
    // Aralık dışı bitrate spin tarafından clamp'lenir; desteklenmeyen FPS yok sayılır.
    void setVideoBitrateKbps(uint32_t kbps);
    void setVideoFps(uint32_t fps);

    // Ses ayarları (RTMP/FLV AAC MVP). audio_enabled pipeline'a; deviceId boşsa
    // sistem varsayılan endpoint'i kullanılır (WasapiCapture GetDefaultAudioEndpoint).
    bool     isAudioEnabled() const;
    QString  audioDeviceId() const;   // seçili WASAPI endpoint id (boş = varsayılan)

    // Yayın çıkış ayarları (Faz2/Aşama2.2: SRT + RTMP)
    QString  srtHost() const;
    uint16_t srtPort() const;
    uint32_t transportProtocol() const;   // rj::TransportProtocol değeri (0=SRT, 1=RTMP)
    QString  rtmpUrl() const;             // sunucu URL'i (rtmp://host/app — key HARİÇ)
    QString  rtmpStreamKey() const;
    QString  wsPassword() const;          // V8/I8: WS kontrol parolası (boş = auth kapalı)

    // WS görünürlüğü: dinlenen port + anlık aktif bağlantı sayısı (salt-okunur).
    // MainWindow dialog açılmadan önce FFI'dan okuyup buraya iter (dialog FFI'dan
    // bağımsız kalır). port=0 → sunucu henüz bind olmadı.
    void setWsStatus(uint16_t port, uint32_t connectionCount);

    // Kural görünürlüğü (salt-okunur MVP): motorun aktif kural listesini gösterir.
    // MainWindow dialog açılışında `rj_rules_snapshot_json`'dan okuyup buraya iter
    // (dialog FFI'dan bağımsız kalır — setWsStatus deseni). `rulesJson` boş veya
    // geçersizse tabloya "Kural okunamadı" placeholder'ı düşer (sessizce yutma yok).
    void setRules(const QString& rulesJson);

signals:
    void healingModeChanged(HealingMode mode);
    void editRulesRequested();
    void autoReloadToggled(bool enabled);
    void exportRulesRequested();
    void importRulesRequested();

private slots:
    void onModeChanged(int index);
    void onOkClicked();
    void onEditRulesClicked();
    void onAutoReloadToggled(int state);
    void onExportRulesClicked();
    void onImportRulesClicked();

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
