const std = @import("std");

// ABI doğrulama — derleme zamanında
comptime {
    // RjMetricSample boyutu 56 byte olmalı
    // ffi_auto.h'dan import edilecek
}

export fn rj_ffi_version() u32 {
    return 0x0001_0000; // v1.0.0 — ffi_bridge.h RJ_FFI_VERSION ile eşleşmeli
}

// V8/I14: rj_metrics_poll stub'ı kaldırıldı. Gerçek implementasyon artık
// Rust orchestrator'da (src/orchestrator/src/ffi.rs) — MetricState'ten pull.
// Burada bir stub bırakmak, aynı sembolü iki statik kütüphanede tanımlayıp
// linkte duplicate-symbol (LNK2005) üretirdi.
