/**
 * ffi_bridge.c — C++ / Rust FFI köprüsü
 * Ring buffer fonksiyonları Rust tarafından implement edilir.
 * Bu dosya C++ tarafının kullandığı stub tanımlarını içerir.
 */

#include "ffi_bridge.h"

/* Versiyon kontrolü */
uint32_t rj_ffi_version(void) {
    return RJ_FFI_VERSION;
}

