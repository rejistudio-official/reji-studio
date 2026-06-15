const std = @import("std");

// ABI doğrulama — derleme zamanında
comptime {
    // RjMetricSample boyutu 56 byte olmalı
    // ffi_auto.h'dan import edilecek
}

export fn rj_ffi_version() u32 {
    return 0x0001_0000; // v1.0.0 — ffi_bridge.h RJ_FFI_VERSION ile eşleşmeli
}

export fn rj_metrics_poll(out: ?*anyopaque) i32 {
    _ = out;
    return 0; // stub — Rust implementasyonu override eder
}
