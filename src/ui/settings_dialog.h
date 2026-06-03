#pragma once
#ifdef QT6_AVAILABLE

#include <QDialog>
#include <memory>
#include "healing_overlay.h"

class QComboBox;

namespace reji {

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);
    ~SettingsDialog() override;

    HealingMode healingMode() const;
    void setHealingMode(HealingMode mode);

signals:
    void healingModeChanged(HealingMode mode);

private slots:
    void onModeChanged(int index);
    void onOkClicked();

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
