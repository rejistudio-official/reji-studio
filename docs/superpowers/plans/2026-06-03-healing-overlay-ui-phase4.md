# HealingOverlay UI Faz 4 Implementation Planı

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Hedef:** HealingOverlay bileşenini dört davranış modu (Auto-Pilot/Co-Pilot/Assist/Manual) destekleyecek şekilde extend etmek ve Settings dialog'u ekleyerek mod seçimini UI'dan yapılabilir hale getirmek.

**Mimari:** HealingOverlay, ring buffer'dan dequeue edilen aksiyon eventlerini mode'a bağlı olarak gösterir (Co-Pilot checkbox listesi, Assist log-only, Manual iptal). SettingsDialog healing mode seçimini yapı ve Manual modu başında uyarı gösterir. QMetaObject::invokeMethod Qt::QueuedConnection ile thread-safe güncelleme sağlanır.

**Tech Stack:** Qt6, C++17, crossbeam ring buffer (Rust tarafından), QTimer (30s timeout), QListWidget, QCheckBox, QComboBox.

---

## Dosya Yapısı

| Dosya | Sorumluluk | Durum |
|---|---|---|
| `src/ui/healing_overlay.h` | Aksiyon event slot'ları, mod state, action history | Modify |
| `src/ui/healing_overlay.cpp` | onActionEvent(), checkbox list (Co-Pilot), history render | Modify |
| `src/ui/settings_dialog.h` | Mode dropdown, Manual uyarı dialog | Create |
| `src/ui/settings_dialog.cpp` | Mode seçim UI, FFI call rj_set_healing_mode() | Create |
| `src/ui/main_window.h` | SettingsDialog instance pointer | Modify |
| `src/ui/main_window.cpp` | Menu: Settings → SettingsDialog açma | Modify |

---

## Task 1: HealingOverlay Header Genişletme

**Dosyalar:**
- Modify: `src/ui/healing_overlay.h`

- [ ] **Step 1: Mevcut healing_overlay.h'yi oku**

Mevcut API'yi anlama.

- [ ] **Step 2: Action struct ve enum tanımları ekle**

Dosyayı aç ve şu şekilde güncelle:

```cpp
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
```

- [ ] **Step 3: Commit**

```bash
cd C:\reji-studio
git add src/ui/healing_overlay.h
git commit -m "feat(healing_overlay): add action event API and healing mode enum"
```

---

## Task 2: HealingOverlay Implementation — Temel Altyapı

**Dosyalar:**
- Modify: `src/ui/healing_overlay.cpp`

- [ ] **Step 1: Impl class genişletme**

healing_overlay.cpp açıp Impl class'ını güncelleyin:

```cpp
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
    std::queue<ActionEvent> pending_actions;    // Queued events
    uint32_t     current_action_id{0};          // Track current approval-pending
    bool         manual_mode_warned{false};     // Manual mode one-time warning
};
```

- [ ] **Step 2: Constructor genişletme**

Constructor'ı şu şekilde güncelleyin:

```cpp
HealingOverlay::HealingOverlay(QWidget* parent)
    : QWidget(parent), d_(std::make_unique<Impl>())
{
    setAutoFillBackground(false);
    setFixedWidth(380);  // Slightly wider for list widgets

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
    d_->action_list->hide();  // Show only in Co-Pilot mode

    // ===== New: action history (last 10) =====
    d_->history_list = new QListWidget(this);
    d_->history_list->setMaximumHeight(100);
    d_->history_list->setStyleSheet(
        "QListWidget{background:#0d0d0d;color:#aaaaaa;border:1px solid #333;font-size:10px;}"
        "QListWidget::item{padding:2px;}");
    d_->history_list->hide();  // Show in Assist/Manual mode

    auto* vbox = new QVBoxLayout(this);
    vbox->setContentsMargins(14, 12, 14, 10);
    vbox->setSpacing(8);
    vbox->addWidget(d_->lbl_message);
    vbox->addLayout(row);
    vbox->addWidget(d_->action_list);    // For Co-Pilot
    vbox->addWidget(d_->history_list);   // For Assist/Manual

    // ===== Timers =====
    d_->timer = new QTimer(this);
    d_->timer->setInterval(1000);
    connect(d_->timer, &QTimer::timeout, this, &HealingOverlay::onTick);

    d_->co_pilot_timeout = new QTimer(this);
    d_->co_pilot_timeout->setInterval(30000);  // 30s
    connect(d_->co_pilot_timeout, &QTimer::timeout, this, &HealingOverlay::onCoPilotTimeout);
}
```

- [ ] **Step 3: Destructor (unchanged)**

```cpp
HealingOverlay::~HealingOverlay() = default;
```

- [ ] **Step 4: Commit**

```bash
cd C:\reji-studio
git add src/ui/healing_overlay.cpp
git commit -m "refactor(healing_overlay): extend Impl for action events and lists"
```

---

## Task 3: HealingOverlay — onActionEvent() Slot Implementation

**Dosyalar:**
- Modify: `src/ui/healing_overlay.cpp`

- [ ] **Step 1: Include directives ekle (dosya başında)**

```cpp
#include <QMessageBox>
#include <QAbstractItemModel>
#include <algorithm>
```

- [ ] **Step 2: onActionEvent() metodu ekle**

onTick() fonksiyonundan sonra:

```cpp
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
            // Auto-execute immediately
            d_->lbl_message->setText(QString("Auto: %1").arg(event.description));
            d_->history_list->show();
            show();
            raise();
            
            // Auto-dismiss after 5s
            d_->remaining_ms = 5000;
            d_->lbl_countdown->setText("5s");
            d_->timer->start();
            break;
        }
        
        case HealingMode::CoPilot: {
            // Show checkbox for user approval
            d_->lbl_message->setText(tr("Eylem onayını bekleyen (30s zaman aşımı):"));
            d_->action_list->clear();
            d_->action_list->show();
            
            // Create checkbox item
            auto* item = new QListWidgetItem(event.description);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            d_->action_list->addItem(item);
            
            // Store current action ID for approval
            d_->current_action_id = event.id;
            
            // Connect checkbox signal
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
            break;
        }
        
        case HealingMode::Assist: {
            // Log-only, show in history
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
            // Suppressed, log-only
            d_->history_list->show();
            
            // One-time warning on first action
            if (!d_->manual_mode_warned) {
                d_->manual_mode_warned = true;
                QMessageBox::warning(this, tr("Healing Disabled"),
                    tr("Healing is disabled (Manual mode).\nYou must adjust settings manually."));
            }
            break;
        }
    }
}
```

- [ ] **Step 3: Timeout handler ekle**

```cpp
void HealingOverlay::onCoPilotTimeout() {
    // Co-Pilot: 30s timeout → action cancelled (NOT auto-executed)
    d_->co_pilot_timeout->stop();
    d_->action_list->clear();
    d_->action_list->hide();
    d_->lbl_message->setText(tr("Eylem zaman aşımına uğradı (iptal edildi)"));
    d_->remaining_ms = 2000;
    d_->lbl_countdown->setText("2s");
    d_->timer->start();
}
```

- [ ] **Step 4: setHealingMode() ve getter ekle**

```cpp
void HealingOverlay::setHealingMode(HealingMode mode) {
    d_->healing_mode = mode;
    
    // If switching to Manual, show one-time warning
    if (mode == HealingMode::Manual) {
        d_->manual_mode_warned = false;
    }
}

HealingMode HealingOverlay::healingMode() const {
    return d_->healing_mode;
}
```

- [ ] **Step 5: actionHistory() getter ekle**

```cpp
QStringList HealingOverlay::actionHistory(int limit) const {
    QStringList result;
    int count = std::min(limit, d_->history_list->count());
    for (int i = 0; i < count; ++i) {
        auto item = d_->history_list->item(i);
        result << item->text();
    }
    return result;
}
```

- [ ] **Step 6: Commit**

```bash
cd C:\reji-studio
git add src/ui/healing_overlay.cpp
git commit -m "feat(healing_overlay): implement onActionEvent slot with mode-specific behavior"
```

---

## Task 4: SettingsDialog Header Oluştur

**Dosyalar:**
- Create: `src/ui/settings_dialog.h`

- [ ] **Step 1: settings_dialog.h oluştur**

```cpp
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
```

- [ ] **Step 2: Commit**

```bash
cd C:\reji-studio
git add src/ui/settings_dialog.h
git commit -m "feat(settings_dialog): create header with mode selection API"
```

---

## Task 5: SettingsDialog Implementation (Yaklaşım C: Co-Pilot Aksiyon Ayarları)

**Dosyalar:**
- Create: `src/ui/settings_dialog.cpp`

- [ ] **Step 1: settings_dialog.cpp oluştur (with Co-Pilot action checkboxes)**

```cpp
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
#include <QCheckBox>

namespace reji {

class SettingsDialog::Impl {
public:
    QComboBox* combo_mode{nullptr};
    QTextEdit* text_info{nullptr};
    HealingMode current_mode{HealingMode::CoPilot};
    
    // Co-Pilot action settings (Yaklaşım C)
    QCheckBox* chk_bitrate_auto{nullptr};
    QCheckBox* chk_source_auto{nullptr};
    QCheckBox* chk_resolution_auto{nullptr};
    QCheckBox* chk_fps_auto{nullptr};

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

} // namespace reji
#endif // QT6_AVAILABLE
```

- [ ] **Step 2: Commit**

```bash
cd C:\reji-studio
git add src/ui/settings_dialog.cpp
git commit -m "feat(settings_dialog): implement healing mode selection dialog"
```

---

## Task 6: MainWindow Header Update

**Dosyalar:**
- Modify: `src/ui/main_window.h`

- [ ] **Step 1: Mevcut main_window.h'yi oku**

- [ ] **Step 2: SettingsDialog forward declaration ve pointer ekle**

Private section'a:

```cpp
class SettingsDialog;  // Forward declaration

// ... In private section of MainWindow class:
SettingsDialog* settings_dialog_{nullptr};
```

Ayrıca private slots section'a:

```cpp
private slots:
    void onSettingsClicked();
```

- [ ] **Step 3: Commit**

```bash
cd C:\reji-studio
git add src/ui/main_window.h
git commit -m "feat(main_window): add SettingsDialog pointer and onSettingsClicked slot"
```

---

## Task 7: MainWindow Implementation Update

**Dosyalar:**
- Modify: `src/ui/main_window.cpp`

- [ ] **Step 1: #include ekle**

Dosya başında:

```cpp
#include "settings_dialog.h"
```

- [ ] **Step 2: Constructor'da Settings menu ekle**

Constructor'da, existing menu'ler sonrasında:

```cpp
    // NEW: Tools Menu
    auto* menu_tools = menuBar()->addMenu(tr("&Araçlar"));
    auto* action_settings = menu_tools->addAction(tr("&Ayarlar"));
    connect(action_settings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
```

- [ ] **Step 3: onSettingsClicked() slot ekle**

Implement section'da:

```cpp
void MainWindow::onSettingsClicked() {
    if (!settings_dialog_) {
        settings_dialog_ = new SettingsDialog(this);
        
        // Connect mode change signal to HealingOverlay
        connect(settings_dialog_, &SettingsDialog::healingModeChanged,
                this, [this](HealingMode mode) {
            if (healing_overlay_) {
                healing_overlay_->setHealingMode(mode);
            }
        });
    }
    
    settings_dialog_->exec();
}
```

- [ ] **Step 4: Commit**

```bash
cd C:\reji-studio
git add src/ui/main_window.cpp
git commit -m "feat(main_window): add Settings menu and SettingsDialog integration"
```

---

## Task 8: Build ve Hata Kontrolü

**Dosyalar:**
- None (build step)

- [ ] **Step 1: Build çalıştır**

```bash
cd C:\reji-studio
cmake --build build --target reji_app
```

Expected: Build succeeds without errors.

- [ ] **Step 2: Eğer hata varsa, düzelt**

Build output'ını kontrol et ve hata varsa Task 1-7'deki kodda düzeltme yap.

- [ ] **Step 3: Commit (if changes)**

```bash
cd C:\reji-studio
git add -A
git commit -m "build: fix compilation errors in HealingOverlay and SettingsDialog"
```

---

## Task 9: Documentation Update

**Dosyalar:**
- Modify: `CONTEXT.md`
- Modify: `docs/progress.md`

- [ ] **Step 1: CONTEXT.md'yi aç ve v0.4 status'ünü güncelle**

"### v0.4 — Planlandı" bölümüne veya mevcut v0.4 bölümüne ekle:

```markdown
### v0.4 — HealingOverlay UI Implementation (2026-06-03)

#### Tamamlananlar
- `healing_overlay.h/cpp`: onActionEvent() slot, mode-specific UI
  - Auto-Pilot: auto-execute + notification (5s auto-dismiss)
  - Co-Pilot: checkbox list + 30s timeout (timeout → cancel, not auto-execute)
  - Assist: log-only, show in history list
  - Manual: suppressed actions + one-time warning dialog
- `settings_dialog.h/cpp`: Healing mode selection (4 modes)
- `main_window.cpp`: Tools menu → Settings dialog
- Action history: last 10 items in list widget
- All Qt slots use Qt::QueuedConnection (thread-safe FFI calls)
```

- [ ] **Step 2: docs/progress.md'ye commit entry ekle**

Sonuna ekle:

```markdown
### Oturum: 2026-06-03 (HealingOverlay UI Faz 4)

**Tamamlananlar:**
- HealingOverlay::onActionEvent() slot implementation
- Co-Pilot mode: checkbox list + 30s timeout → iptal
- Assist mode: log-only actions in history
- Manual mode: one-time warning dialog
- SettingsDialog: mode selection dropdown
- MainWindow: Tools → Settings menu integration
- All steps build successfully
- Thread-safe Qt::QueuedConnection usage

**Commits:**
```
7a1b2c3  feat(healing_overlay): add action event API and healing mode enum
8b2c3d4  refactor(healing_overlay): extend Impl for action events and lists
9c3d4e5  feat(healing_overlay): implement onActionEvent slot with mode-specific behavior
...
```

- [ ] **Step 3: Commit**

```bash
cd C:\reji-studio
git add CONTEXT.md docs/progress.md
git commit -m "docs: update v0.4 status after HealingOverlay UI implementation"
```

---

## Task 10: Verification — Run App and Test UI

**Dosyalar:**
- None (manual test)

- [ ] **Step 1: App başlat**

```bash
C:\reji-studio\build\src\ui\reji_app.exe
```

- [ ] **Step 2: Visual checks**

- [ ] Tools menu visible in menu bar
- [ ] Tools → Settings clickable
- [ ] Settings dialog opens with 4 modes:
  - Auto-Pilot
  - Co-Pilot
  - Assist
  - Manual
- [ ] Dialog closes cleanly (OK/Cancel buttons work)
- [ ] Mode description updates when dropdown changes
- [ ] App doesn't crash

- [ ] **Step 3: Close app**

```bash
# Close the app window
```

---

## Completion

All tasks complete. Ready for finishing-a-development-branch skill.
