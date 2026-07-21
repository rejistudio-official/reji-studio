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
    QPushButton* btn_reject{nullptr};           // V8/I33: CoPilot "Reddet" (explicit reject)
    QListWidget* history_list{nullptr};         // Last 10 actions
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

    // ===== V8/I33: Co-Pilot "Reddet" butonu (explicit reject → kural cooldown) ==
    d_->btn_reject = new QPushButton(tr("Reddet"), this);
    d_->btn_reject->setStyleSheet(
        "QPushButton{background:#7a2f2f;color:white;border-radius:4px;"
        "            padding:3px 10px;font-size:12px;}"
        "QPushButton:hover{background:#9a3a3a;}");
    d_->btn_reject->hide();
    connect(d_->btn_reject, &QPushButton::clicked, this, [this] {
        // Explicit reddetme: Rust'a bildir (kural cooldown uygulanır), prompt'u
        // temizle. Timeout'tan FARKLI — timeout cooldown UYGULAMAZ (yalnız TTL).
        if (d_->current_action_id != 0) {
            emit actionRejected(d_->current_action_id);
            clearApprovalPrompt();
            d_->lbl_message->setText(tr("Eylem reddedildi"));
            d_->remaining_ms = 2000;
            d_->lbl_countdown->setText("2s");
            d_->timer->start();
        }
    });

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->addWidget(d_->lbl_countdown, 1);
    row->addWidget(d_->btn_reject, 0);
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

    // V8/I33: UI-yerel co_pilot_timeout KALDIRILDI. Onay prompt'unun süresi
    // dolduğunda temizlik TEK KAYNAKTAN gelir: Rust pending TTL süpürmesi →
    // kind=Invalidated event → onActionInvalidated(). Böylece "UI temizledi ama
    // Rust'ta pending duruyor" iki-saat yarışı yok (kaynak-of-truth Rust deposu).
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

void HealingOverlay::onActionEvent(const ActionEvent& event, bool show_banner) {
    // Add to history
    d_->history_list->insertItem(0, QString("[%1] %2")
        .arg(event.timestamp, event.description));

    // Keep only last 10
    while (d_->history_list->count() > 10) {
        delete d_->history_list->takeItem(d_->history_list->count() - 1);
    }

    // V8/I33: Onay kapısı MOTORDA (Rust) — require_approval alanı yetkilidir;
    // overlay artık per-kategori ayarları YENİDEN HESAPLAMAZ (kapı tek yerde).
    if (event.require_approval) {
        // CoPilot manuel-kategori: aksiyon Rust'ta pending, onay bekliyor.
        // show_banner'dan bağımsız — pending karar kullanıcıya ulaşmalı.
        showApprovalPrompt(event);
        return;
    }

    // SIYAH_KUTU: bilgi banner'ı bastırıldı (örn. yayın yokken) — geçmişe
    // işlendi, görsel açılmaz.
    if (!show_banner) {
        return;
    }

    // Bilgi event'i: aksiyon Rust'ta ZATEN aktüatöre gitti (uygulanıyor). Yalnız
    // görüntüle — actionApproved EMIT ETME (aksiyon zaten uygulanıyor; eskiden
    // CoPilot-auto burada approve emit ediyordu, artık Rust doğrudan aktüatöre
    // yazıyor).
    QString prefix;
    switch (d_->healing_mode) {
        case HealingMode::Assist:  prefix = tr("Kayıt (Assist)"); break;
        case HealingMode::Manual:  prefix = tr("Kayıt");          break; // normalde gelinmez
        default:                   prefix = tr("Otomatik");       break; // AutoPilot / CoPilot-auto
    }
    d_->lbl_message->setText(QString("%1: %2").arg(prefix, event.description));
    d_->history_list->show();
    show();
    raise();
    d_->remaining_ms = 3000;
    d_->lbl_countdown->setText("3s");
    d_->timer->start();
}

// V8/I33: CoPilot onay prompt'u — checkbox (approve) + "Reddet" (reject) + 30s
// UI timeout. Yalnız require_approval=true event'inde açılır.
void HealingOverlay::showApprovalPrompt(const ActionEvent& event) {
    d_->lbl_message->setText(tr("Eylem onayı (%1s, timeout → iptal):")
        .arg(rj::constants::kCoPilotApprovalTimeoutMs / 1000));
    d_->action_list->clear();
    d_->action_list->show();

    auto* item = new QListWidgetItem(event.description);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(Qt::Unchecked);
    d_->action_list->addItem(item);

    d_->current_action_id = event.id;

    disconnect(d_->action_list->model(), &QAbstractItemModel::dataChanged,
               this, nullptr);
    connect(d_->action_list->model(), &QAbstractItemModel::dataChanged,
            this, [this, id = event.id](const QModelIndex&, const QModelIndex&) {
        auto* it = d_->action_list->item(0);
        if (it && it->checkState() == Qt::Checked) {
            emit actionApproved(id);   // Rust: pending → aktüatör kuyruğu
            clearApprovalPrompt();
            d_->lbl_message->setText(tr("Eylem onaylandı"));
            d_->remaining_ms = 2000;
            d_->lbl_countdown->setText("2s");
            d_->timer->start();
        }
    });

    d_->btn_reject->show();
    d_->history_list->show();
    show();
    raise();
    // Not: UI zaman aşımı sayacı YOK — süre dolumu Rust TTL'inden (Invalidated) gelir.
}

// V8/I33: onay prompt'unu temizle — approve/reject/invalidate ortak yolu.
void HealingOverlay::clearApprovalPrompt() {
    d_->action_list->clear();
    d_->action_list->hide();
    d_->btn_reject->hide();
    d_->current_action_id = 0;
}

void HealingOverlay::onActionInvalidated(uint32_t action_id) {
    // V8/I33: Rust pending TTL doldu / mod değişti. Ekranda o aksiyon
    // gösteriliyorsa görsel temizlik yap. Aksiyon zaten Rust'ta düştü —
    // cooldown YOK (explicit reject'ten farkı budur).
    if (action_id == d_->current_action_id) {
        clearApprovalPrompt();
        d_->lbl_message->setText(tr("Eylem zaman aşımına uğradı (iptal edildi)"));
        d_->remaining_ms = 2000;
        d_->lbl_countdown->setText("2s");
        d_->timer->start();
    }
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

void HealingOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(QColor(0x14, 0x14, 0x14, 220));
    p.setPen(QPen(QColor(0xFF, 0xA0, 0x00, 200), 1.5));
    p.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 8.0, 8.0);
}

} // namespace reji
#endif // QT6_AVAILABLE
