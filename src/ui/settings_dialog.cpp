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
#include <QTabWidget>
#include <QSettings>
#include <QSpinBox>
#include <QTextEdit>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

#include "../pipeline/audio/audio_device_enum.h"  // Ses Ayarları: cihaz enumerasyonu

namespace reji {

class SettingsDialog::Impl {
public:
    QComboBox* combo_mode{nullptr};
    QTextEdit* text_info{nullptr};
    HealingMode current_mode{HealingMode::CoPilot};

    // Co-Pilot action settings (Yaklaşım C)
    QCheckBox* chk_bitrate_auto{nullptr};        // varsayılan: açık
    // V8/I34: chk_source_auto kaldırıldı — karşılık gelen source-switch aksiyon tipi
    // pipeline'da yok (I33c auto-onay yalnız bitrate/resolution/fps'e bağlı), inert'ti.
    QCheckBox* chk_resolution_auto{nullptr};     // varsayılan: kapalı
    QCheckBox* chk_fps_auto{nullptr};            // varsayılan: kapalı

    // v0.4+ Hot-reload
    QCheckBox* chk_auto_reload{nullptr};
    QPushButton* btn_edit_rules{nullptr};
    QPushButton* btn_export_rules{nullptr};
    QPushButton* btn_import_rules{nullptr};

    // Video ayarları (bitrate + FPS)
    QSpinBox*  bitrate_spin{nullptr};     // 500–50000 kbps
    QComboBox* combo_fps{nullptr};        // 30 / 60 / 120

    // Yayın çıkış ayarları (Faz2/Aşama2.2: SRT + RTMP protokol seçimi)
    QComboBox* combo_protocol{nullptr};   // 0=SRT, 1=RTMP (rj::TransportProtocol)
    QLineEdit* srt_host_edit{nullptr};
    QSpinBox*  srt_port_spin{nullptr};
    QLineEdit* rtmp_url_edit{nullptr};    // rtmp://host/app (stream key HARİÇ)
    QLineEdit* rtmp_key_edit{nullptr};    // stream key — Password echo modu

    // V8/I8: WebSocket kontrol parolası (boş = auth kapalı) — Password echo modu
    QLineEdit* ws_password_edit{nullptr};

    // WS görünürlüğü: dinlenen port + anlık aktif bağlantı (salt-okunur).
    // setWsStatus() günceller; MainWindow dialog açılışında FFI'dan besler.
    QLabel* ws_port_value{nullptr};
    QLabel* ws_conn_value{nullptr};

    // Ses Ayarları (RTMP/FLV AAC MVP)
    QCheckBox* chk_audio_enabled{nullptr};   // varsayılan: kapalı
    QComboBox* combo_audio_device{nullptr};  // "Sistem varsayılanı" + enumerate edilenler

    // Kural görünürlüğü (salt-okunur MVP): Ad / Koşul / Aksiyon kolonları.
    // setRules() doldurur; MainWindow dialog açılışında FFI'dan besler.
    QTableWidget* rules_table{nullptr};
};

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent), d_(std::make_unique<Impl>())
{
    // Başlık sade "Ayarlar" — pencere artık çok kategorili (sekmeli); tek bir
    // kategoriyi ("Healing Modu") başlığa taşımak yanıltıcı olurdu.
    setWindowTitle(tr("Ayarlar"));
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

    d_->chk_resolution_auto = new QCheckBox(tr("Çözünürlük düşür"), this);
    d_->chk_resolution_auto->setChecked(false);  // varsayılan: kapalı
    layout_copilot->addWidget(d_->chk_resolution_auto);

    d_->chk_fps_auto = new QCheckBox(tr("FPS sınırla"), this);
    d_->chk_fps_auto->setChecked(false);  // varsayılan: kapalı
    layout_copilot->addWidget(d_->chk_fps_auto);

    // ===== v0.4+ Hot-Reload Rules =====
    auto* grp_hotreload = new QGroupBox(tr("Kural Yönetimi (v0.4+)"), this);
    auto* layout_hotreload = new QVBoxLayout(grp_hotreload);

    auto* layout_rule_btns = new QHBoxLayout;

    d_->btn_edit_rules = new QPushButton(tr("Kuralları Düzenle..."), this);
    connect(d_->btn_edit_rules, &QPushButton::clicked,
            this, &SettingsDialog::onEditRulesClicked);
    layout_rule_btns->addWidget(d_->btn_edit_rules);

    d_->btn_export_rules = new QPushButton(tr("Dışa Aktar..."), this);
    connect(d_->btn_export_rules, &QPushButton::clicked,
            this, &SettingsDialog::onExportRulesClicked);
    layout_rule_btns->addWidget(d_->btn_export_rules);

    d_->btn_import_rules = new QPushButton(tr("İçe Aktar..."), this);
    connect(d_->btn_import_rules, &QPushButton::clicked,
            this, &SettingsDialog::onImportRulesClicked);
    layout_rule_btns->addWidget(d_->btn_import_rules);

    layout_rule_btns->addStretch();
    layout_hotreload->addLayout(layout_rule_btns);

    d_->chk_auto_reload = new QCheckBox(tr("Otomatik yeniden yükle (dosya değiştiğinde)"), this);
    d_->chk_auto_reload->setChecked(false);
    connect(d_->chk_auto_reload, QOverload<int>::of(&QCheckBox::stateChanged),
            this, &SettingsDialog::onAutoReloadToggled);
    layout_hotreload->addWidget(d_->chk_auto_reload);

    auto* lbl_rules_path = new QLabel(tr("Kurallar: ~/.reji/rules.json (JSON)"), this);
    lbl_rules_path->setStyleSheet("color:#888;font-size:11px;");
    layout_hotreload->addWidget(lbl_rules_path);

    // ===== Video Ayarları (Bitrate / FPS) =====
    // Bu değerler encoder init'e gider ve cfg_in.bitrate_kbps üzerinden healing'in
    // referans noktalarını (original/max/atomic bitrate) tek kaynaktan besler.
    auto* grp_video   = new QGroupBox(tr("Video Ayarları"), this);
    auto* video_layout = new QFormLayout(grp_video);

    d_->bitrate_spin = new QSpinBox(this);
    d_->bitrate_spin->setRange(500, 50000);     // NVENC init'i bozmayan makul aralık
    d_->bitrate_spin->setSingleStep(500);
    d_->bitrate_spin->setSuffix(tr(" kbps"));

    d_->combo_fps = new QComboBox(this);
    // Yalnız set_fps_limit'in (tavan 120) desteklediği değerler — 144 sunulmaz,
    // yoksa healing'in FPS sınırlama aksiyonu sessizce çalışmazdı.
    d_->combo_fps->addItem(QStringLiteral("30"),  30);
    d_->combo_fps->addItem(QStringLiteral("60"),  60);
    d_->combo_fps->addItem(QStringLiteral("120"), 120);

    video_layout->addRow(tr("Bitrate:"), d_->bitrate_spin);
    video_layout->addRow(tr("FPS:"),     d_->combo_fps);

    // ===== Yayın Çıkış Ayarları (SRT / RTMP) =====
    auto* grp_srt  = new QGroupBox(tr("Yayın Çıkış Ayarları"), this);
    auto* srt_layout = new QFormLayout(grp_srt);

    d_->combo_protocol = new QComboBox(this);
    d_->combo_protocol->addItem(tr("SRT"), 0);
    d_->combo_protocol->addItem(tr("RTMP (düz rtmp:// — RTMPS henüz yok)"), 1);

    d_->srt_host_edit = new QLineEdit(this);
    d_->srt_host_edit->setPlaceholderText("örn: 192.168.1.100");

    d_->srt_port_spin = new QSpinBox(this);
    d_->srt_port_spin->setRange(1024, 65535);

    d_->rtmp_url_edit = new QLineEdit(this);
    d_->rtmp_url_edit->setPlaceholderText("örn: rtmp://live.twitch.tv/app");

    d_->rtmp_key_edit = new QLineEdit(this);
    d_->rtmp_key_edit->setEchoMode(QLineEdit::Password);   // ekranda sızdırma
    d_->rtmp_key_edit->setPlaceholderText(tr("stream key"));

    srt_layout->addRow(tr("Protokol:"), d_->combo_protocol);
    srt_layout->addRow(tr("SRT Host:"), d_->srt_host_edit);
    srt_layout->addRow(tr("SRT Port:"), d_->srt_port_spin);
    srt_layout->addRow(tr("RTMP URL:"), d_->rtmp_url_edit);
    srt_layout->addRow(tr("Stream Key:"), d_->rtmp_key_edit);

    // ===== V8/I8: Uzaktan Kontrol (WebSocket) parolası =====
    auto* grp_ws = new QGroupBox(tr("Uzaktan Kontrol (WebSocket)"), this);
    auto* ws_layout = new QFormLayout(grp_ws);
    d_->ws_password_edit = new QLineEdit(this);
    d_->ws_password_edit->setEchoMode(QLineEdit::Password);
    d_->ws_password_edit->setPlaceholderText(tr("boş = kimlik doğrulama kapalı"));
    ws_layout->addRow(tr("Parola:"), d_->ws_password_edit);

    // WS görünürlüğü: salt-okunur port + aktif bağlantı (dialog açılışında MainWindow
    // setWsStatus ile besler; başlangıç metni sunucu-hazır-değil durumunu yansıtır).
    d_->ws_port_value = new QLabel(tr("—"), this);
    d_->ws_conn_value = new QLabel(tr("—"), this);
    ws_layout->addRow(tr("Port:"), d_->ws_port_value);
    ws_layout->addRow(tr("Aktif bağlantı:"), d_->ws_conn_value);

    // ===== Ses Ayarları (RTMP/FLV AAC MVP) =====
    // Etkinleştir + cihaz seçimi. Ses MVP'de yalnız RTMP çıkışında gönderilir
    // (SRT konteynersiz — ayrı tur). Cihaz listesi loopback (sistem sesi/hoparlör)
    // endpoint'lerinden gelir; boş seçim = sistem varsayılanı.
    auto* grp_audio = new QGroupBox(tr("Ses Ayarları"), this);
    auto* audio_layout = new QFormLayout(grp_audio);

    d_->chk_audio_enabled = new QCheckBox(tr("Sesi etkinleştir (yalnız RTMP)"), this);
    d_->chk_audio_enabled->setChecked(false);  // varsayılan: kapalı

    d_->combo_audio_device = new QComboBox(this);
    d_->combo_audio_device->addItem(tr("Sistem varsayılanı"), QString());  // boş id
    for (const auto& dev : reji::pipeline::audio::enumerate_audio_devices(/*loopback*/true)) {
        d_->combo_audio_device->addItem(QString::fromStdWString(dev.name),
                                        QString::fromStdWString(dev.id));
    }

    audio_layout->addRow(d_->chk_audio_enabled);
    audio_layout->addRow(tr("Ses cihazı:"), d_->combo_audio_device);

    // Seçili protokole göre alanları etkinleştir/pasifleştir
    auto update_protocol_fields = [this](int index) {
        const bool rtmp = (index == 1);
        d_->srt_host_edit->setEnabled(!rtmp);
        d_->srt_port_spin->setEnabled(!rtmp);
        d_->rtmp_url_edit->setEnabled(rtmp);
        d_->rtmp_key_edit->setEnabled(rtmp);
    };
    connect(d_->combo_protocol, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, update_protocol_fields);

    // QSettings'ten kalıcı değerleri yükle
    {
        QSettings qs("RejiStudio", "RejiStudio");
        // Video ayarları — varsayılan 6000 kbps / 60 fps (Pipeline::Config ile aynı)
        d_->bitrate_spin->setValue(qs.value("video/bitrate_kbps", 6000).toInt());
        const int saved_fps = qs.value("video/fps", 60).toInt();
        const int fps_idx = d_->combo_fps->findData(saved_fps);
        d_->combo_fps->setCurrentIndex(fps_idx >= 0 ? fps_idx : 1);  // fallback: 60
        d_->srt_host_edit->setText(qs.value("srt/host", "127.0.0.1").toString());
        d_->srt_port_spin->setValue(qs.value("srt/port", 9000).toInt());
        d_->rtmp_url_edit->setText(qs.value("rtmp/url", "").toString());
        d_->rtmp_key_edit->setText(qs.value("rtmp/key", "").toString());
        d_->ws_password_edit->setText(qs.value("ws/password", "").toString());  // V8/I8
        const int proto = qs.value("output/protocol", 0).toInt();
        d_->combo_protocol->setCurrentIndex(proto == 1 ? 1 : 0);
        update_protocol_fields(d_->combo_protocol->currentIndex());

        // Ses ayarları — varsayılan kapalı / sistem varsayılan cihazı.
        d_->chk_audio_enabled->setChecked(qs.value("audio/enabled", false).toBool());
        const QString saved_audio_dev = qs.value("audio/device_id", "").toString();
        const int adev_idx = d_->combo_audio_device->findData(saved_audio_dev);
        d_->combo_audio_device->setCurrentIndex(adev_idx >= 0 ? adev_idx : 0);
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

    // ===== Main Layout (QTabWidget ile kategorize) =====
    // Gruplar sekmelere dağıtılır; her sekme kendi QVBoxLayout'unda ilgili
    // grupları + sona addStretch() barındırır. addTab, grupları sekme
    // sayfasına reparent eder — sinyal/slot bağlantıları pointer-tabanlı
    // olduğundan bu reparent'tan etkilenmez (talimat Faz 0 doğrulaması).
    // Tamam/İptal buton satırı sekmelerin dışında altta kalır; akış değişmez.
    auto* tabs = new QTabWidget(this);

    // Self-Healing — varsayılan açılış sekmesi (index 0).
    auto* tab_healing = new QWidget(this);
    auto* tab_healing_layout = new QVBoxLayout(tab_healing);
    tab_healing_layout->addWidget(grp_healing);
    tab_healing_layout->addWidget(grp_copilot);
    tab_healing_layout->addWidget(grp_hotreload);
    tab_healing_layout->addStretch();
    tabs->addTab(tab_healing, tr("Self-Healing"));

    auto* tab_video = new QWidget(this);
    auto* tab_video_layout = new QVBoxLayout(tab_video);
    tab_video_layout->addWidget(grp_video);
    tab_video_layout->addStretch();
    tabs->addTab(tab_video, tr("Video"));

    auto* tab_audio = new QWidget(this);
    auto* tab_audio_layout = new QVBoxLayout(tab_audio);
    tab_audio_layout->addWidget(grp_audio);
    tab_audio_layout->addStretch();
    tabs->addTab(tab_audio, tr("Ses"));

    auto* tab_output = new QWidget(this);
    auto* tab_output_layout = new QVBoxLayout(tab_output);
    tab_output_layout->addWidget(grp_srt);
    tab_output_layout->addStretch();
    tabs->addTab(tab_output, tr("Yayın Çıkışı"));

    // Uzaktan Kontrol — WS bir kontrol kanalı, yayın çıkışından ayrı sekme.
    auto* tab_remote = new QWidget(this);
    auto* tab_remote_layout = new QVBoxLayout(tab_remote);
    tab_remote_layout->addWidget(grp_ws);
    tab_remote_layout->addStretch();
    tabs->addTab(tab_remote, tr("Uzaktan Kontrol"));

    // Kurallar — kural motorunun aktif kural listesi (salt-okunur MVP). Motor
    // GUI'den "kör kutu"ydu; bu sekme id/koşul/aksiyon'u görünür kılar. Düzenleme
    // (ekle/sil/değiştir) bu turun kapsamı DIŞINDA — ayrı bir gelecek tur.
    auto* tab_rules = new QWidget(this);
    auto* tab_rules_layout = new QVBoxLayout(tab_rules);
    d_->rules_table = new QTableWidget(0, 3, this);
    d_->rules_table->setHorizontalHeaderLabels({tr("Ad"), tr("Koşul"), tr("Aksiyon")});
    d_->rules_table->setEditTriggers(QAbstractItemView::NoEditTriggers);  // salt-okunur
    d_->rules_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    d_->rules_table->verticalHeader()->setVisible(false);
    d_->rules_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);  // Koşul en geniş
    tab_rules_layout->addWidget(d_->rules_table);
    tabs->addTab(tab_rules, tr("Kurallar"));

    auto* layout_main = new QVBoxLayout(this);
    layout_main->addWidget(tabs);
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
    qs.setValue("video/bitrate_kbps", d_->bitrate_spin->value());
    qs.setValue("video/fps", d_->combo_fps->currentData().toInt());
    qs.setValue("srt/host", d_->srt_host_edit->text());
    qs.setValue("srt/port", d_->srt_port_spin->value());
    qs.setValue("output/protocol", d_->combo_protocol->currentIndex());
    qs.setValue("rtmp/url", d_->rtmp_url_edit->text());
    // Not: stream key QSettings'e (registry) düz metin yazılır — OBS ile aynı
    // yaklaşım; işletim sistemi kullanıcı profili koruması varsayılır.
    qs.setValue("rtmp/key", d_->rtmp_key_edit->text());
    // V8/I8: WS parolası — RTMP key ile aynı yaklaşım (QSettings/registry düz metin,
    // OBS ile tutarlı; OS kullanıcı profili koruması varsayılır, keychain kapsam dışı).
    qs.setValue("ws/password", d_->ws_password_edit->text());
    // Ses ayarları — enabled + seçili cihaz id'si (boş = sistem varsayılanı).
    qs.setValue("audio/enabled", d_->chk_audio_enabled->isChecked());
    qs.setValue("audio/device_id", d_->combo_audio_device->currentData().toString());

    emit healingModeChanged(d_->current_mode);
    accept();
}

// Video ayarları — encoder init'e ve healing referans noktalarına giden değerler.
uint32_t SettingsDialog::videoBitrateKbps() const {
    return d_->bitrate_spin
        ? static_cast<uint32_t>(d_->bitrate_spin->value())
        : 6000u;
}

uint32_t SettingsDialog::videoFps() const {
    return d_->combo_fps
        ? static_cast<uint32_t>(d_->combo_fps->currentData().toInt())
        : 60u;
}

// Yaklaşım C: Co-Pilot action setting getters
bool SettingsDialog::isBitrateAuto() const {
    return d_->chk_bitrate_auto && d_->chk_bitrate_auto->isChecked();
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

void SettingsDialog::onExportRulesClicked() {
    emit exportRulesRequested();
}

void SettingsDialog::onImportRulesClicked() {
    emit importRulesRequested();
}

void SettingsDialog::onAutoReloadToggled(int state) {
    emit autoReloadToggled(state == Qt::Checked);
}

bool SettingsDialog::isAudioEnabled() const {
    return d_->chk_audio_enabled && d_->chk_audio_enabled->isChecked();
}

QString SettingsDialog::audioDeviceId() const {
    return d_->combo_audio_device ? d_->combo_audio_device->currentData().toString()
                                  : QString();
}

QString SettingsDialog::srtHost() const {
    return d_->srt_host_edit ? d_->srt_host_edit->text() : QStringLiteral("127.0.0.1");
}

uint16_t SettingsDialog::srtPort() const {
    return d_->srt_port_spin
        ? static_cast<uint16_t>(d_->srt_port_spin->value())
        : 9000u;
}

uint32_t SettingsDialog::transportProtocol() const {
    return (d_->combo_protocol && d_->combo_protocol->currentIndex() == 1) ? 1u : 0u;
}

QString SettingsDialog::rtmpUrl() const {
    if (!d_->rtmp_url_edit) return QString();
    QString url = d_->rtmp_url_edit->text().trimmed();
    while (url.endsWith(QLatin1Char('/'))) url.chop(1);
    return url;
}

QString SettingsDialog::rtmpStreamKey() const {
    return d_->rtmp_key_edit ? d_->rtmp_key_edit->text().trimmed() : QString();
}

// V8/I8: WS kontrol parolası. Trim YAPILMAZ — parola bütünlüğü (baştaki/sondaki
// boşluk anlamlı olabilir). Boş string → Rust tarafında auth kapalı (None).
QString SettingsDialog::wsPassword() const {
    return d_->ws_password_edit ? d_->ws_password_edit->text() : QString();
}

// WS görünürlüğü: port/bağlantı salt-okunur alanlarını günceller. port=0 → sunucu
// henüz bind olmadı ("—" gösterilir). Anlık durum: MainWindow dialog her açılışında
// FFI'dan taze okuyup çağırır (bir kerelik sorgu; canlı poll yok — YAGNI).
void SettingsDialog::setWsStatus(uint16_t port, uint32_t connectionCount) {
    if (d_->ws_port_value) {
        d_->ws_port_value->setText(port != 0 ? QString::number(port) : tr("—"));
    }
    if (d_->ws_conn_value) {
        d_->ws_conn_value->setText(QString::number(connectionCount));
    }
}

void SettingsDialog::setRules(const QString& rulesJson) {
    if (!d_->rules_table) {
        return;
    }
    auto* table = d_->rules_table;
    table->setRowCount(0);

    // Tek satırlık uyarı ekleyen yardımcı — motor init değil / snapshot bozuk /
    // hiç kural yok gibi durumları görünür kılar (I10: sessizce yutma yok; boş
    // tablo "hiç kural yok" izlenimi bırakmasın).
    auto showNotice = [table](const QString& text) {
        table->setRowCount(1);
        auto* item = new QTableWidgetItem(text);
        table->setItem(0, 0, item);
        table->setSpan(0, 0, 1, 3);
    };

    QJsonParseError parseErr;
    const QJsonDocument doc = QJsonDocument::fromJson(rulesJson.toUtf8(), &parseErr);
    if (parseErr.error != QJsonParseError::NoError || !doc.isArray()) {
        showNotice(tr("Kural okunamadı (motor hazır değil veya veri geçersiz)"));
        return;
    }

    const QJsonArray rules = doc.array();
    if (rules.isEmpty()) {
        showNotice(tr("Tanımlı kural yok"));
        return;
    }

    table->setRowCount(rules.size());
    for (int row = 0; row < rules.size(); ++row) {
        const QJsonObject rule = rules.at(row).toObject();
        const QString id        = rule.value(QStringLiteral("id")).toString();
        const QString condition = rule.value(QStringLiteral("condition")).toString();
        const QString action    = rule.value(QStringLiteral("action")).toString();
        const QString desc      = rule.value(QStringLiteral("description")).toString();

        auto* id_item   = new QTableWidgetItem(id);
        auto* cond_item = new QTableWidgetItem(condition);
        auto* act_item  = new QTableWidgetItem(action);
        // description tooltip'e — MVP'de kolon değil (params/modes de gizli, YAGNI).
        if (!desc.isEmpty()) {
            id_item->setToolTip(desc);
        }
        table->setItem(row, 0, id_item);
        table->setItem(row, 1, cond_item);
        table->setItem(row, 2, act_item);
    }
}

} // namespace reji
#endif // QT6_AVAILABLE
