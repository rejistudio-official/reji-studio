//! Fuzz hedeflerinin ortak parçası. Her hedef `mod common;` ile içerir.
//!
//! Windows/MSVC'de libfuzzer-sys'in varsayılan panik kancası `__fastfail`
//! (0xc0000409) ile çıkar; libFuzzer bunu yakalayamaz, crash artifact'i
//! YAZILMAZ ve `cargo fuzz tmin` çalışmaz. UCRT `abort()` SIGABRT üretir,
//! libFuzzer'ın yakalayıcısı devreye girer → crash-* dosyası + tmin.
//! Ayrıntı: fuzz/README.md.
#![allow(dead_code)]

use std::sync::Once;

use reji_orchestrator::rules::RuleMetrics;

static HOOK: Once = Once::new();

unsafe extern "C" {
    fn abort() -> !;
}

/// Panik kancasını bir kez kurar (Windows'ta artifact üretimi için şart).
pub fn install_panic_hook() {
    HOOK.call_once(|| {
        std::panic::set_hook(Box::new(|info| {
            eprintln!("PANIC: {info}");
            unsafe { abort() }
        }));
    });
}

/// Girdiden türetilmiş deterministik metrik seti: seed corpus'un (gerçek
/// kural dosyaları) anlamı bozulmasın diye metrikler ayrı bayt alanından
/// değil içeriğin hash'inden üretilir. Sınır değerler de üretilir.
pub fn metrics_from(data: &[u8]) -> [RuleMetrics; 4] {
    let h = fnv1a(data);
    let derived = RuleMetrics {
        frame_drop_pct:   (h & 0x7f) as u32,
        gpu_temp_c:       ((h >> 8) & 0xff) as i16 - 40,
        cpu_temp_c:       ((h >> 16) & 0x7f) as i16,
        memory_usage_pct: ((h >> 24) & 0x7f) as u32,
        cpu_load_pct:     ((h >> 32) & 0x7f) as u32,
        gpu_load_pct:     ((h >> 40) & 0x7f) as u32,
        network_rtt_ms:   ((h >> 48) & 0xffff) as u16,
        network_loss_pct: ((h >> 56) & 0x7f) as u8,
    };
    let max = RuleMetrics {
        frame_drop_pct: u32::MAX,
        gpu_temp_c: i16::MAX,
        cpu_temp_c: i16::MAX,
        memory_usage_pct: u32::MAX,
        cpu_load_pct: u32::MAX,
        gpu_load_pct: u32::MAX,
        network_rtt_ms: u16::MAX,
        network_loss_pct: u8::MAX,
    };
    let min = RuleMetrics {
        gpu_temp_c: i16::MIN,
        cpu_temp_c: i16::MIN,
        ..RuleMetrics::default()
    };
    [RuleMetrics::default(), derived, max, min]
}

fn fnv1a(data: &[u8]) -> u64 {
    let mut h: u64 = 0xcbf2_9ce4_8422_2325;
    for b in data {
        h ^= *b as u64;
        h = h.wrapping_mul(0x0100_0000_01b3);
    }
    h
}
