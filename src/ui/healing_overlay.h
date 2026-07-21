#pragma once
#ifdef QT6_AVAILABLE

#include <QWidget>
#include <QListWidget>
#include <memory>
#include <queue>
#include "reji_constants.h"

namespace reji {

class SettingsDialog;  // Forward declaration

// Healing mode enum (matches RjHealingMode from FFI)
enum class HealingMode {
    AutoPilot = 0,   // All actions auto-execute
    CoPilot = 1,     // User selects via checkbox
    Assist = 2,      // Critical auto, others log-only
    Manual = 3       // All suppressed, log only
};

// Action type enum
enum class ActionType {
    BitrateReduce,
    BitrateRecover,       // bitrate normale dönerken
    ResolutionScale,
    ResolutionRestore,    // çözünürlük normale dönerken
    FpsLimit,
    FpsRestore,           // fps sınırı kaldırılırken
    LogOnly               // yalnızca kayıt, kullanıcıya eylem gerekmez
};

// Action structure for display
struct ActionEvent {
    uint32_t id;
    ActionType type;
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
    void showMessage(const QString& msg, int timeout_ms = rj::constants::kHealingBannerTimeoutMs);

    // New API: action event notification (thread-safe via Qt::QueuedConnection)
    // SIYAH_KUTU: show_banner=false → bilgi event'i yalnız geçmişe işlenir,
    // overlay AÇILMAZ (yayın yokken boşta tetiklenen kurallar sahne paneli
    // üstünde periyodik kutu üretiyordu). Onay prompt'ları (require_approval)
    // bu bayraktan ETKİLENMEZ — Rust'ta pending karar her durumda gösterilir.
    void onActionEvent(const ActionEvent& event, bool show_banner = true);

    // V8/I33: Rust pending TTL doldu / mod değişti → o aksiyonun onay prompt'unu
    // temizle (id == mevcut onay bekleyen aksiyon ise). require_approval=false
    // event'lerde çağrılmaz — yalnız kind==Invalidated akışı için.
    void onActionInvalidated(uint32_t action_id);

    // Vulkan init failure notification (graceful degradation)
    void onVulkanInitFailed();

    // Set healing mode (e.g., from SettingsDialog)
    void setHealingMode(HealingMode mode);
    HealingMode healingMode() const;

    // Get action history (last N items)
    QStringList actionHistory(int limit = 10) const;

    // Yaklaşım C: Link to SettingsDialog for Co-Pilot action settings
    void setSettingsDialog(SettingsDialog* dialog);

signals:
    void undoRequested();
    void actionApproved(uint32_t action_id);  // emitted when user checks Co-Pilot checkbox
    void actionRejected(uint32_t action_id);  // V8/I33: emitted when user clicks "Reddet"

protected:
    void paintEvent(QPaintEvent* ev) override;

private slots:
    void onTick();

private:
    // V8/I33: CoPilot onay prompt'unu göster/temizle (approve/reject/timeout/
    // invalidate ortak temizlik yolu). Kapı MOTORDA — prompt yalnız
    // require_approval=true event'inde açılır.
    void showApprovalPrompt(const ActionEvent& event);
    void clearApprovalPrompt();

    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
