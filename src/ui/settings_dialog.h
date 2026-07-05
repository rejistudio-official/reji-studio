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
    bool isSourceAuto() const;
    bool isResolutionAuto() const;
    bool isFpsAuto() const;

    // v0.4+ Hot-reload
    bool isAutoReloadEnabled() const;
    void setAutoReloadEnabled(bool enabled);

    // Yayın çıkış ayarları (Faz2/Aşama2.2: SRT + RTMP)
    QString  srtHost() const;
    uint16_t srtPort() const;
    uint32_t transportProtocol() const;   // rj::TransportProtocol değeri (0=SRT, 1=RTMP)
    QString  rtmpUrl() const;             // URL + stream key birleşik (rtmp://host/app/KEY)

signals:
    void healingModeChanged(HealingMode mode);
    void editRulesRequested();
    void autoReloadToggled(bool enabled);

private slots:
    void onModeChanged(int index);
    void onOkClicked();
    void onEditRulesClicked();
    void onAutoReloadToggled(int state);

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
