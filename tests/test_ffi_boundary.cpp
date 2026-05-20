#include "../src/ffi/ffi_bridge.h"
#include <cstdio>
#include <cassert>

int main() {
    printf("=== FFI Sinir Testi ===\n");

    /* Versiyon kontrolu */
    uint32_t ver = rj_ffi_version();
    printf("FFI versiyon: 0x%08X\n", ver);
    assert(ver == RJ_FFI_VERSION);

    /* Canary sabiti kontrolu */
    assert(RJ_METRIC_MAGIC == 0xEEFF1234u);
    printf("Canary sabiti: OK\n");

    /* Struct boyut kontrolu */
    printf("RjMetricSample boyutu: %zu byte\n", sizeof(RjMetricSample));
    printf("RjCommand boyutu: %zu byte\n", sizeof(RjCommand));

    printf("=== BASARILI ===\n");
    return 0;
}