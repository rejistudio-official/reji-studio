#include "healing_overlay.h"
#ifdef QT6_AVAILABLE

#include "settings_dialog.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QAbstractItemModel>
#include <algorithm>

namespace reji {

class HealingOverlay::Impl {
public:
    // Legacy fields
    QLabel*      lbl_message{nullptr};
    QLabel*      lbl_countdown{nullptr};
    QPushButton* btn_undo{nullptr};
    QTimer*      timer{nullptr};
    int          remaining_ms{0};

    // New fields for action events
    HealingMode  healing_mode{HealingMode::CoPilot};
    QListWidget* action_list{nullptr};          // Co-Pilot checkboxes
    QListWidget* history_list{nullptr};         // Last 10 actions
    QTimer*      co_pilot_timeout{nullptr};     // 30s timeout per action
    uint32_t     current_action_id{0};          // Track current approval-pending
    bool         manual_mode_warned{false};     // Manual mode one-time warning

    // Yaklaşım C: Settings dialog reference
    reji::SettingsDialog* settings_dialog{nullptr};
};

HealingOverlay::HealingOverlay(QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>())
{
    setAutoFillBackground(false);
    setFixedWidth(380);

    // ===== Legacy: message + countdown + undo =====
    d_->lbl_message = new QLabel(this);
    d_->lbl_message->setWordWrap(true);
    d_->lbl_message->setStyleSheet("color:#ffffff;font-size:13px;background:transparent;");

    d_->lbl_countdown = new QLabel(this);
    d_->lbl_countdown->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    d_->lbl_countdown->setStyleSheet("color:#aaaaaa;font-size:11px;background:transparent;");

    d_->btn_undo = new QPushButton(tr("Geri Al"), this);
    d_->btn_undo->setStyleSheet(
        "QPushButton{background:#555555;color:white;border-radius:4px;"
        "            padding:3px 10px;font-size:12px;}"
        "QPushButton:hover{background:#777777;}");
    connect(d_->btn_undo, &QPushButton::clicked, this, &HealingOverlay::undoRequested);
    connect(d_->btn_undo, &QPushButton::clicked, this, &HealingOverlay::hide);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(d_->lbl_countdown, 1);
    row->addWidget(d_->btn_undo, 0);

    // ===== New: action list (Co-Pilot checkboxes) =====
    d_->action_list = new QListWidget(this);
    d_->action_list->setMaximumHeight(120);
    d_->action_list->setStyleSheet(
        "QListWidget{background:#1a1a1a;color:#ffffff;border:1px solid #555;}"
        "QListWidget::item{padding:4px;}"
        "QListWidget::item:hover{background:#333;}");
    d_->action_list->hide();

    // ===== New: action history (last 10) =====
    d_->history_list = new QListWidget(this);
    d_->history_list->setMaximumHeight(100);
    d_->history_list->setStyleSheet(
        "QListWidget{background:#0d0d0d;color:#aaaaaa;border:1px solid #333;font-size:10px;}"
        "QListWidget::item{padding:2px;}");
    d_->history_list->hide();

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(14, 12, 14, 10);
    vbox->setSpacing(8);
    vbox->addWidget(d_->lbl_message);
    vbox->addLayout(row);
    vbox->addWidget(d_->action_list);
    vbox->addWidget(d_->history_list);

    // ===== Timers =====
    d_->timer = new QTimer(this);
    d_->timer->setInterval(1000);
    connect(d_->timer, &QTimer::timeout, this, &HealingOverlay::onTick);

    d_->co_pilot_timeout = new QTimer(this);
    d_->co_pilot_timeout->setInterval(30000);
    connect(d_->co_pilot_timeout, &QTimer::timeout, this, &HealingOverlay::onCoPilotTimeout);
}

HealingOverlay::~HealingOverlay() = default;

void HealingOverlay::showMessage(const QString& msg, int timeout_ms) {
    d_->lbl_message->setText(msg);
    d_->remaining_ms = timeout_ms;
    d_->lbl_countdown->setText(QString("%1s").arg(timeout_ms / 1000));
    adjustSize();
    show();
    raise();
    d_->timer->start();
}

void HealingOverlay::onTick() {
    d_->remaining_ms -= 1000;
    if (d_->remaining_ms <= 0) {
        d_->timer->stop();
        hide();
        return;
    }
    d_->lbl_countdown->setText(QString("%1s").arg(d_->remaining_ms / 1000));
}

void HealingOverlay::onActionEvent(const ActionEvent& event) {
    // Add to history
    d_->history_list->insertItem(0, QString("[%1] %2")
        .arg(event.timestamp, event.description));

    // Keep only last 10
    while (d_->history_list->count() > 10) {
        delete d_->history_list->takeItem(d_->history_list->count() - 1);
    }

    // Mode-specific behavior
    switch (d_->healing_mode) {
        case HealingMode::AutoPilot: {
            d_->lbl_message->setText(QString("Auto: %1").arg(event.description));
            d_->history_list->show();
            show();
            raise();
            d_->remaining_ms = 5000;
            d_->lbl_countdown->setText("5s");
            d_->timer->start();
            break;
        }

        case HealingMode::CoPilot: {
            // Yaklaşım C: Check settings for this action type
            bool is_auto = false;
            if (d_->settings_dialog) {
                switch (event.type) {
                    case ActionType::BitrateReduce:
                        is_auto = d_->settings_dialog->isBitrateAuto();
                        break;
                    case ActionType::SourceReconnect:
                        is_auto = d_->settings_dialog->isSourceAuto();
                        break;
                    case ActionType::ResolutionScale:
                        is_auto = d_->settings_dialog->isResolutionAuto();
                        break;
                    case ActionType::FpsLimit:
                        is_auto = d_->settings_dialog->isFpsAuto();
                        break;
                }
            }

            if (is_auto) {
                // Auto-execute immediately
                emit actionApproved(event.id);
                d_->lbl_message->setText(QString("Auto: %1").arg(event.description));
                d_->history_list->show();
                show();
                raise();
                d_->remaining_ms = 3000;
                d_->lbl_countdown->setText("3s");
                d_->timer->start();
            } else {
                // Manual: show checkbox, 30s timeout → iptal
                d_->lbl_message->setText(tr("Eylem onayı (30s, timeout → iptal):"));
                d_->action_list->clear();
                d_->action_list->show();

                auto* item = new QListWidgetItem(event.description);
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
                item->setCheckState(Qt::Unchecked);
                d_->action_list->addItem(item);

                d_->current_action_id = event.id;

                connect(d_->action_list->model(), &QAbstractItemModel::dataChanged,
                        this, [this, id = event.id](const QModelIndex&, const QModelIndex&) {
                    auto item = d_->action_list->item(0);
                    if (item && item->checkState() == Qt::Checked) {
                        emit actionApproved(id);
                        d_->action_list->clear();
                        d_->action_list->hide();
                        d_->co_pilot_timeout->stop();
                    }
                });

                show();
                raise();
                d_->co_pilot_timeout->start();
            }
            break;
        }

        case HealingMode::Assist: {
            d_->lbl_message->setText(tr("Kayıt (Assist modu):"));
            d_->history_list->show();
            show();
            raise();
            d_->remaining_ms = 3000;
            d_->lbl_countdown->setText("3s");
            d_->timer->start();
            break;
        }

        case HealingMode::Manual: {
            d_->history_list->show();
            if (!d_->manual_mode_warned) {
                d_->manual_mode_warned = true;
                QMessageBox::warning(this, tr("Healing Disabled"),
                    tr("Healing is disabled (Manual mode).\nYou must adjust settings manually."));
            }
            break;
        }
    }
}

void HealingOverlay::onCoPilotTimeout() {
    d_->co_pilot_timeout->stop();
    d_->action_list->clear();
    d_->action_list->hide();
    d_->lbl_message->setText(tr("Eylem zaman aşımına uğradı (iptal edildi)"));
    d_->remaining_ms = 2000;
    d_->lbl_countdown->setText("2s");
    d_->timer->start();
}

void HealingOverlay::onVulkanInitFailed() {
    showMessage(tr("Vulkan desteklenmiyor, OpenGL uyumlu modunda..."), 5000);
}

void HealingOverlay::setHealingMode(HealingMode mode) {
    d_->healing_mode = mode;
    if (mode == HealingMode::Manual) {
        d_->manual_mode_warned = false;
    }
}

HealingMode HealingOverlay::healingMode() const {
    return d_->healing_mode;
}

QStringList HealingOverlay::actionHistory(int limit) const {
    QStringList result;
    int count = std::min(limit, d_->history_list->count());
    for (int i = 0; i < count; ++i) {
        auto item = d_->history_list->item(i);
        result << item->text();
    }
    return result;
}

void HealingOverlay::setSettingsDialog(SettingsDialog* dialog) {
    d_->settings_dialog = dialog;
}

void HealingOverlay::onActionCheckboxToggled(uint32_t action_id, bool checked) {
    if (checked) {
        emit actionApproved(action_id);
        d_->action_list->clear();
        d_->action_list->hide();
        d_->co_pilot_timeout->stop();
    }
}

void HealingOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x14, 0x14, 0x14, 220));
    p.setPen(QPen(QColor(0xFF, 0xA0, 0x00, 200), 1.5));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8.0, 8.0);
}

} // namespace reji
#endif // QT6_AVAILABLE
