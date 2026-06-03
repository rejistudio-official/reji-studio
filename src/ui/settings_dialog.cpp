#include "settings_dialog.h"
#ifdef QT6_AVAILABLE

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QGroupBox>
#include <QTextEdit>
#include <QMessageBox>

namespace reji {

class SettingsDialog::Impl {
public:
    QComboBox* combo_mode{nullptr};
    QTextEdit* text_info{nullptr};
    HealingMode current_mode{HealingMode::CoPilot};

    // Co-Pilot action settings (Yaklaşım C)
    QCheckBox* chk_bitrate_auto{nullptr};        // varsayılan: açık
    QCheckBox* chk_source_auto{nullptr};         // varsayılan: açık
    QCheckBox* chk_resolution_auto{nullptr};     // varsayılan: kapalı
    QCheckBox* chk_fps_auto{nullptr};            // varsayılan: kapalı
};

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent), d_(std::make_unique<Impl>())
{
    setWindowTitle(tr("Ayarlar — Healing Modu"));
    setMinimumWidth(400);
    setModal(true);

    // ===== Healing Mode Section =====
    auto* grp_healing = new QGroupBox(tr("Healing Modu"), this);

    auto* lbl_mode = new QLabel(tr("Mod seçin:"));
    d_->combo_mode = new QComboBox(this);
    d_->combo_mode->addItem(tr("Auto-Pilot (Tüm aksiyonlar otomatik)"),
                            static_cast<int>(HealingMode::AutoPilot));
    d_->combo_mode->addItem(tr("Co-Pilot (Kullanıcı onayı gerekli)"),
                            static_cast<int>(HealingMode::CoPilot));
    d_->combo_mode->addItem(tr("Assist (Kritik otomatik, diğerleri log)"),
                            static_cast<int>(HealingMode::Assist));
    d_->combo_mode->addItem(tr("Manual (Tüm adaptasyon kapalı)"),
                            static_cast<int>(HealingMode::Manual));
    d_->combo_mode->setCurrentIndex(static_cast<int>(HealingMode::CoPilot));

    connect(d_->combo_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onModeChanged);

    auto* layout_mode = new QHBoxLayout;
    layout_mode->addWidget(lbl_mode);
    layout_mode->addWidget(d_->combo_mode);

    // ===== Mode Description =====
    d_->text_info = new QTextEdit(this);
    d_->text_info->setReadOnly(true);
    d_->text_info->setMaximumHeight(100);
    d_->text_info->setStyleSheet("background:#1a1a1a;color:#ffffff;border:1px solid #555;");

    auto* layout_grp = new QVBoxLayout(grp_healing);
    layout_grp->addLayout(layout_mode);
    layout_grp->addWidget(d_->text_info);

    // ===== Co-Pilot Action Settings (Yaklaşım C) =====
    auto* grp_copilot = new QGroupBox(tr("Co-Pilot Aksiyon Ayarları"), this);
    auto* layout_copilot = new QVBoxLayout(grp_copilot);

    d_->chk_bitrate_auto = new QCheckBox(tr("Bitrate otomatik"), this);
    d_->chk_bitrate_auto->setChecked(true);  // varsayılan: açık
    layout_copilot->addWidget(d_->chk_bitrate_auto);

    d_->chk_source_auto = new QCheckBox(tr("Kaynak yeniden bağlan"), this);
    d_->chk_source_auto->setChecked(true);  // varsayılan: açık
    layout_copilot->addWidget(d_->chk_source_auto);

    d_->chk_resolution_auto = new QCheckBox(tr("Çözünürlük düşür"), this);
    d_->chk_resolution_auto->setChecked(false);  // varsayılan: kapalı
    layout_copilot->addWidget(d_->chk_resolution_auto);

    d_->chk_fps_auto = new QCheckBox(tr("FPS sınırla"), this);
    d_->chk_fps_auto->setChecked(false);  // varsayılan: kapalı
    layout_copilot->addWidget(d_->chk_fps_auto);

    // ===== Buttons =====
    auto* btn_ok = new QPushButton(tr("Tamam"));
    auto* btn_cancel = new QPushButton(tr("İptal"));

    connect(btn_ok, &QPushButton::clicked, this, &SettingsDialog::onOkClicked);
    connect(btn_cancel, &QPushButton::clicked, this, &QDialog::reject);

    auto* layout_buttons = new QHBoxLayout;
    layout_buttons->addStretch();
    layout_buttons->addWidget(btn_ok);
    layout_buttons->addWidget(btn_cancel);

    // ===== Main Layout =====
    auto* layout_main = new QVBoxLayout(this);
    layout_main->addWidget(grp_healing);
    layout_main->addWidget(grp_copilot);
    layout_main->addStretch();
    layout_main->addLayout(layout_buttons);

    // Initialize description
    onModeChanged(static_cast<int>(HealingMode::CoPilot));
}

SettingsDialog::~SettingsDialog() = default;

HealingMode SettingsDialog::healingMode() const {
    return d_->current_mode;
}

void SettingsDialog::setHealingMode(HealingMode mode) {
    d_->current_mode = mode;
    d_->combo_mode->setCurrentIndex(static_cast<int>(mode));
}

void SettingsDialog::onModeChanged(int index) {
    d_->current_mode = static_cast<HealingMode>(index);

    QString description;
    switch (d_->current_mode) {
        case HealingMode::AutoPilot:
            description = tr("Tüm uyarlama aksiyonları otomatik olarak uygulanır. "
                           "Bildirimler gösterilir ama kullanıcı onayı gerekmez.");
            break;
        case HealingMode::CoPilot:
            description = tr("Uyarlama aksiyonları kullanıcı onayına sunulur. "
                           "30 saniye zaman aşımı sonra iptal edilir (otomatik uygulama yok).");
            break;
        case HealingMode::Assist:
            description = tr("Kritik aksiyonlar otomatik uygulanır, "
                           "diğerleri sadece günlüğe kaydedilir.");
            break;
        case HealingMode::Manual:
            description = tr("TÜM uyarlama kapalıdır. Ayarları manuel olarak yapmanız gerekir. "
                           "Bu modu seçtiğinizde başında uyarı gösterilir.");
            break;
    }
    d_->text_info->setText(description);
}

void SettingsDialog::onOkClicked() {
    emit healingModeChanged(d_->current_mode);
    accept();
}

// Yaklaşım C: Co-Pilot action setting getters
bool SettingsDialog::isBitrateAuto() const {
    return d_->chk_bitrate_auto && d_->chk_bitrate_auto->isChecked();
}

bool SettingsDialog::isSourceAuto() const {
    return d_->chk_source_auto && d_->chk_source_auto->isChecked();
}

bool SettingsDialog::isResolutionAuto() const {
    return d_->chk_resolution_auto && d_->chk_resolution_auto->isChecked();
}

bool SettingsDialog::isFpsAuto() const {
    return d_->chk_fps_auto && d_->chk_fps_auto->isChecked();
}

} // namespace reji
#endif // QT6_AVAILABLE
