#pragma once
#ifdef QT6_AVAILABLE

#include <QWidget>
#include <QListWidget>
#include <memory>
#include <queue>

namespace reji {

// Healing mode enum (matches RjHealingMode from FFI)
enum class HealingMode {
    AutoPilot = 0,   // All actions auto-execute
    CoPilot = 1,     // User selects via checkbox
    Assist = 2,      // Critical auto, others log-only
    Manual = 3       // All suppressed, log only
};

// Action structure for display
struct ActionEvent {
    uint32_t id;
    QString description;
    bool require_approval;  // for Co-Pilot mode
    QString timestamp;
};

// ---------------------------------------------------------------------------
// HealingOverlay — semi-transparent notification panel that floats over
// MainWindow's central area.  Shows actions and notifications based on healing mode.
// ---------------------------------------------------------------------------
class HealingOverlay : public QWidget {
    Q_OBJECT
public:
    explicit HealingOverlay(QWidget* parent = nullptr);
    ~HealingOverlay() override;

    // Legacy API (kept for compatibility)
    void showMessage(const QString& msg, int timeout_ms = 10000);

    // New API: action event notification (thread-safe via Qt::QueuedConnection)
    void onActionEvent(const ActionEvent& event);

    // Set healing mode (e.g., from SettingsDialog)
    void setHealingMode(HealingMode mode);
    HealingMode healingMode() const;

    // Get action history (last N items)
    QStringList actionHistory(int limit = 10) const;

signals:
    void undoRequested();
    void actionApproved(uint32_t action_id);  // emitted when user checks Co-Pilot checkbox

protected:
    void paintEvent(QPaintEvent* ev) override;

private slots:
    void onTick();
    void onActionCheckboxToggled(uint32_t action_id, bool checked);
    void onCoPilotTimeout();

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
