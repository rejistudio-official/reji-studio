/**
 * ffi_bridge.c — C++ / Rust FFI köprüsü
 * Ring buffer fonksiyonları Rust tarafından implement edilir.
 * Bu dosya C++ tarafının kullandığı stub tanımlarını içerir.
 *
 * Not: ffi_auto.h C++ syntax'ı içerdiğinden buraya include edilmez.
 * RJ_FFI_VERSION burada yerel olarak tanımlanır; ffi_bridge.h ile aynı değer olmalı.
 */

#include <stdint.h>

#define RJ_FFI_VERSION_LOCAL 0x00010000u  /* 1.0.0 — ffi_bridge.h ile eşleşmeli */

/* Versiyon kontrolü */
uint32_t rj_ffi_version(void) {
    return RJ_FFI_VERSION_LOCAL;
}
