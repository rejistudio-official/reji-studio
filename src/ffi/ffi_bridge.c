/**
 * ffi_bridge.c — C++ / Rust FFI köprüsü
 * Ring buffer fonksiyonları Rust tarafından implement edilir.
 * Bu dosya C++ tarafının kullandığı stub tanımlarını içerir.
 */

#include "ffi_bridge.h"
#include <string.h>

/* Versiyon kontrolü */
uint32_t rj_ffi_version(void) {
    return RJ_FFI_VERSION;
}

/* ============================================================================
 * Runtime Adaptation L3 (v0.4+) Stubs
 * Rust tarafından implement edilir. C++ tarafı bu fonksiyonları çağırır.
 * ============================================================================ */

/* Poll latest metrics from pipeline.
 * Returns: 1 if metrics available, 0 if not */
__declspec(noinline) int rj_metrics_poll(RjMetricSample* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(RjMetricSample));
    out->magic_head = RJ_METRIC_MAGIC;
    out->magic_tail = RJ_METRIC_MAGIC;
    return 1;  /* stub: always success */
}

/* Dequeue next adaptation action (FIFO).
 * Returns: 1 if action available, 0 if queue empty */
__declspec(noinline) int rj_action_dequeue(RjAction* out) {
    if (!out) return 0;
    memset(out, 0, sizeof(RjAction));
    return 0;  /* stub: no actions */
}

/* Approve pending action (Co-Pilot mode).
 * Returns: 1 if action approved, 0 if not found */
__declspec(noinline) int rj_action_approve(uint32_t action_id) {
    (void)action_id;
    return 0;  /* stub: no-op */
}

/* Set healing mode.
 * Returns: 1 if successful, 0 if error */
__declspec(noinline) int rj_set_healing_mode(RjHealingMode mode) {
    (void)mode;
    return 1;  /* stub: always success */
}

/* Get current healing mode.
 * Returns: RjHealingMode value */
__declspec(noinline) int rj_get_healing_mode(void) {
    return RJ_MODE_AUTO_PILOT;  /* stub: default to auto-pilot */
}

/* Reload rules from file (async).
 * Returns: 1 if reload requested, 0 if error */
__declspec(noinline) int rj_reload_rules(const char* path) {
    (void)path;
    return 1;  /* stub: always success */
}