#include "healing_overlay.h"
#ifdef QT6_AVAILABLE

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

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

void HealingOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x14, 0x14, 0x14, 220));
    p.setPen(QPen(QColor(0xFF, 0xA0, 0x00, 200), 1.5));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8.0, 8.0);
}

} // namespace reji
#endif // QT6_AVAILABLE
