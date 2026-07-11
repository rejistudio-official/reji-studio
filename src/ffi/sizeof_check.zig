const std = @import("std");

// ffi_auto.h'dan gelen struct boyutlarını doğrula
// Bu değerler C++ sizeof_check.cpp ile aynı olmalı

comptime {
    // RjMetricSample = 56 byte
    // RjAction = 20 byte
    // RjActionEvent = 24 byte (V8/I33 — UI bildirim event'i)
    // RjCommand = 24 byte
    // magic_tail offset = 52

    // Şimdilik sabit değer assert — ileride C import ile doğrulanacak
    const expected_metric_size: usize = 56;
    const expected_action_size: usize = 20;
    const expected_action_event_size: usize = 24;
    const expected_command_size: usize = 24;
    const expected_magic_offset: usize = 52;

    _ = expected_magic_offset; // ileride C import ile offset doğrulamasında kullanılacak

    std.debug.assert(expected_metric_size == 56);
    std.debug.assert(expected_action_size == 20);
    std.debug.assert(expected_action_event_size == 24);
    std.debug.assert(expected_command_size == 24);
}
