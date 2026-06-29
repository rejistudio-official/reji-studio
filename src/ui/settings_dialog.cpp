#include "settings_dialog.h"
#ifdef QT6_AVAILABLE

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QPushButton>
#include <QGroupBox>
#include <QSettings>
#include <QSpinBox>
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

    // v0.4+ Hot-reload
    QCheckBox* chk_auto_reload{nullptr};
    QPushButton* btn_edit_rules{nullptr};

    // SRT çıkış ayarları
    QLineEdit* srt_host_edit{nullptr};
    QSpinBox*  srt_port_spin{nullptr};
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

    // ===== v0.4+ Hot-Reload Rules =====
    auto* grp_hotreload = new QGroupBox(tr("Kural Yönetimi (v0.4+)"), this);
    auto* layout_hotreload = new QVBoxLayout(grp_hotreload);

    d_->btn_edit_rules = new QPushButton(tr("Kuralları Düzenle..."), this);
    connect(d_->btn_edit_rules, &QPushButton::clicked,
            this, &SettingsDialog::onEditRulesClicked);
    layout_hotreload->addWidget(d_->btn_edit_rules);

    d_->chk_auto_reload = new QCheckBox(tr("Otomatik yeniden yükle (dosya değiştiğinde)"), this);
    d_->chk_auto_reload->setChecked(false);
    connect(d_->chk_auto_reload, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &SettingsDialog::onAutoReloadToggled);
    layout_hotreload->addWidget(d_->chk_auto_reload);

    auto* lbl_rules_path = new QLabel(tr("Kurallar: ~/.reji/rules.json (JSON/TOML)"), this);
    lbl_rules_path->setStyleSheet("color:#888;font-size:11px;");
    layout_hotreload->addWidget(lbl_rules_path);

    // ===== SRT Çıkış Ayarları =====
    auto* grp_srt  = new QGroupBox(tr("SRT Çıkış Ayarları"), this);
    auto* srt_layout = new QFormLayout(grp_srt);

    d_->srt_host_edit = new QLineEdit(this);
    d_->srt_host_edit->setPlaceholderText("örn: 192.168.1.100");

    d_->srt_port_spin = new QSpinBox(this);
    d_->srt_port_spin->setRange(1024, 65535);

    srt_layout->addRow(tr("Host:"), d_->srt_host_edit);
    srt_layout->addRow(tr("Port:"), d_->srt_port_spin);

    // QSettings'ten kalıcı değerleri yükle
    {
        QSettings qs("RejiStudio", "RejiStudio");
        d_->srt_host_edit->setText(qs.value("srt/host", "127.0.0.1").toString());
        d_->srt_port_spin->setValue(qs.value("srt/port", 9000).toInt());
    }

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
    layout_main->addWidget(grp_hotreload);
    layout_main->addWidget(grp_srt);
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
    QSettings qs("RejiStudio", "RejiStudio");
    qs.setValue("srt/host", d_->srt_host_edit->text());
    qs.setValue("srt/port", d_->srt_port_spin->value());

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

// v0.4+ Hot-reload
bool SettingsDialog::isAutoReloadEnabled() const {
    return d_->chk_auto_reload && d_->chk_auto_reload->isChecked();
}

void SettingsDialog::setAutoReloadEnabled(bool enabled) {
    if (d_->chk_auto_reload) {
        d_->chk_auto_reload->setChecked(enabled);
    }
}

void SettingsDialog::onEditRulesClicked() {
    emit editRulesRequested();
}

void SettingsDialog::onAutoReloadToggled(int state) {
    emit autoReloadToggled(state == Qt::Checked);
}

QString SettingsDialog::srtHost() const {
    return d_->srt_host_edit ? d_->srt_host_edit->text() : QStringLiteral("127.0.0.1");
}

uint16_t SettingsDialog::srtPort() const {
    return d_->srt_port_spin
        ? static_cast<uint16_t>(d_->srt_port_spin->value())
        : 9000u;
}

} // namespace reji
#endif // QT6_AVAILABLE
