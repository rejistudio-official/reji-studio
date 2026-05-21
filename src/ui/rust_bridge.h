#pragma once
#ifdef QT6_AVAILABLE

#include "ffi_bridge.h"  // RjMetricSample, RjCommand, rj_start_monitor, rj_command_drain, etc.
#include <QMutex>
#include <QObject>
#include <QString>
#include <cstdint>

// ---------------------------------------------------------------------------
// v0.1 stub declarations — compiled as no-ops in rust_bridge.cpp.
// v0.2: Rust implements these with #[no_mangle] in src/orchestrator/src/ffi.rs.
// ---------------------------------------------------------------------------
extern "C" {
    void rj_user_event_scene_switch(uint32_t scene_id);
    void rj_user_event_stream_start();
    void rj_user_event_stream_stop();
}

namespace reji {

// ---------------------------------------------------------------------------
// RustBridge — single choke-point for all UI ↔ Rust FFI calls.
// Every extern "C" call is serialised through ffi_mutex_ so Qt slots on
// different threads (e.g. pipeline callbacks) cannot race with the UI thread.
// ---------------------------------------------------------------------------
class RustBridge : public QObject {
    Q_OBJECT
public:
    explicit RustBridge(QObject* parent = nullptr);

    // ── Wrappers for ffi_bridge.h symbols ─────────────────────────────────
    void startMonitor();
    int  pipelineStatus();
    void notifyConnectionLost(const char* reason);

    // ── User-event wrappers (rj_user_event_* — v0.1 no-op stubs) ──────────
    void sendSceneSwitchEvent(uint32_t scene_id);
    void sendStreamStartEvent();
    void sendStreamStopEvent();

public slots:
    // v0.2: driven by cxx-qt polling; v0.1: available for manual testing.
    // eventType: "ReduceBitrate" | "RestoreNormal"
    // jsonData:  JSON payload, e.g. {"target_kbps": 3000, "reason": "cpu_high"}
    void postHealingEventFromRust(const QString& eventType, const QString& jsonData);

signals:
    void reduceBitrate(uint32_t target_kbps, const QString& reason);
    void restoreNormal();

private:
    QMutex ffi_mutex_;
};

} // namespace reji
#endif // QT6_AVAILABLE
