#pragma once
#ifdef QT6_AVAILABLE

#include <QWidget>
#include <memory>

namespace reji {

// ---------------------------------------------------------------------------
// HealingOverlay — semi-transparent notification panel that floats over
// MainWindow's central area.  Shows a message, a live countdown, and an
// Undo button.  Hides automatically when the countdown reaches zero.
// ---------------------------------------------------------------------------
class HealingOverlay : public QWidget {
    Q_OBJECT
public:
    explicit HealingOverlay(QWidget* parent = nullptr);
    ~HealingOverlay() override;

    // Show the overlay with a message and auto-hide after timeout_ms.
    // Calling again while visible resets the countdown.
    void showMessage(const QString& msg, int timeout_ms = 10000);

signals:
    void undoRequested();

protected:
    void paintEvent(QPaintEvent* ev) override;

private slots:
    void onTick();

private:
    class Impl;
    std::unique_ptr<Impl> d_;
};

} // namespace reji
#endif // QT6_AVAILABLE
