#include "rust_bridge.h"
#ifdef QT6_AVAILABLE

#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QDebug>

// ---------------------------------------------------------------------------
// v0.1 no-op implementations — linker stubs until Rust ships UserEvent FFI.
// Replace in v0.2 by deleting these and implementing #[no_mangle] in ffi.rs.
// ---------------------------------------------------------------------------
extern "C" {
    void rj_user_event_scene_switch(uint32_t) {}
    void rj_user_event_stream_start()          {}
    void rj_user_event_stream_stop()           {}
}

namespace reji {

RustBridge::RustBridge(QObject* parent) : QObject(parent) {}

void RustBridge::startMonitor() {
    QMutexLocker lock(&ffi_mutex_);
    rj_start_monitor();
}

int RustBridge::pipelineStatus() {
    QMutexLocker lock(&ffi_mutex_);
    return rj_pipeline_status();
}

void RustBridge::notifyConnectionLost(const char* reason) {
    QMutexLocker lock(&ffi_mutex_);
    rj_connection_lost(reason);
}

void RustBridge::sendSceneSwitchEvent(uint32_t scene_id) {
    QMutexLocker lock(&ffi_mutex_);
    rj_user_event_scene_switch(scene_id);
}

void RustBridge::sendStreamStartEvent() {
    QMutexLocker lock(&ffi_mutex_);
    rj_user_event_stream_start();
}

void RustBridge::sendStreamStopEvent() {
    QMutexLocker lock(&ffi_mutex_);
    rj_user_event_stream_stop();
}

void RustBridge::postHealingEventFromRust(const QString& eventType, const QString& jsonData) {
    QJsonDocument doc = QJsonDocument::fromJson(jsonData.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        qWarning("[RustBridge] malformed HealingEvent JSON: %s",
                 qPrintable(jsonData));
        return;
    }
    QJsonObject obj = doc.object();

    if (eventType == QLatin1String("ReduceBitrate")) {
        uint32_t kbps   = static_cast<uint32_t>(obj.value("target_kbps").toInt(0));
        QString  reason = obj.value("reason").toString(tr("unknown"));
        emit reduceBitrate(kbps, reason);
    } else if (eventType == QLatin1String("RestoreNormal")) {
        emit restoreNormal();
    }
}

} // namespace reji
#endif // QT6_AVAILABLE
