/**
 * ffi_bridge.c — C++ / Rust FFI köprüsü
 * Ring buffer fonksiyonları Rust tarafından implement edilir.
 * Bu dosya C++ tarafının kullandığı stub tanımlarını içerir.
 *
 * Not: ffi_auto.h C++ syntax'ı içerdiğinden buraya include edilmez.
 * RJ_FFI_VERSION burada yerel olarak tanımlanır; ffi_bridge.h ile aynı değer olmalı.
 */

#include <stdint.h>
#include <string.h>

#define RJ_FFI_VERSION_LOCAL 0x00010000u  /* 1.0.0 — ffi_bridge.h ile eşleşmeli */

/* Versiyon kontrolü */
uint32_t rj_ffi_version(void) {
    return RJ_FFI_VERSION_LOCAL;
}

/* MetricSample ABI — ffi_auto.h C++ syntax içerdiğinden burada yerel tanım */
typedef struct {
    uint32_t magic_head;
    uint64_t timestamp_us;
    uint32_t bitrate_kbps;
    float    fps_actual;
    float    cpu_percent;
    uint32_t frame_drops;
    uint32_t frame_drop_pct;
    int16_t  gpu_temp_c;
    int16_t  cpu_temp_c;
    uint32_t memory_usage_pct;
    uint32_t cpu_load_pct;
    uint16_t network_rtt_ms;
    uint8_t  network_loss_pct;
    uint8_t  _reserved;
    uint32_t magic_tail;
} RjMetricSampleC;

/* Stub: Rust ring buffer hazır olana kadar 0 (veri yok) döndürür */
__declspec(noinline) int rj_metrics_poll(RjMetricSampleC* out) {
    (void)out;
    return 0;
}
