//! Metrik toplama — C++ pipeline'dan gelen telemetri verilerini işler.

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;

/// Anlık metrik snapshot — C++ tarafından ring buffer üzerinden gelir
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct MetricSample {
    pub magic_head:   u32,
    pub timestamp_us: u64,
    pub bitrate_kbps: u32,
    pub fps_actual:   f32,
    pub cpu_percent:  f32,
    pub frame_drops:  u32,
    pub magic_tail:   u32,
}

impl MetricSample {
    pub const MAGIC: u32 = 0xEEFF1234;

    /// Canary doğrulama
    pub fn is_valid(&self) -> bool {
        self.magic_head == Self::MAGIC && self.magic_tail == Self::MAGIC
    }
}

/// Paylaşımlı metrik durumu — atomic, lock-free
#[derive(Debug)]
pub struct MetricState {
    pub bitrate_kbps: AtomicU32,
    pub fps_actual:   AtomicU32,   // f32 * 100 olarak saklanır
    pub cpu_percent:  AtomicU32,   // f32 * 100 olarak saklanır
    pub frame_drops:  AtomicU64,   // toplam kare kaybı
}

impl MetricState {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            bitrate_kbps: AtomicU32::new(0),
            fps_actual:   AtomicU32::new(0),
            cpu_percent:  AtomicU32::new(0),
            frame_drops:  AtomicU64::new(0),
        })
    }

    /// Yeni sample ile güncelle
    pub fn update(&self, sample: &MetricSample) {
        self.bitrate_kbps.store(sample.bitrate_kbps, Ordering::Relaxed);
        self.fps_actual.store((sample.fps_actual * 100.0) as u32, Ordering::Relaxed);
        self.cpu_percent.store((sample.cpu_percent * 100.0) as u32, Ordering::Relaxed);
        self.frame_drops.fetch_add(sample.frame_drops as u64, Ordering::Relaxed);
    }

    /// Bitrate oku
    pub fn bitrate(&self) -> u32 {
        self.bitrate_kbps.load(Ordering::Relaxed)
    }

    /// FPS oku
    pub fn fps(&self) -> f32 {
        self.fps_actual.load(Ordering::Relaxed) as f32 / 100.0
    }

    /// CPU yüzdesi oku
    pub fn cpu(&self) -> f32 {
        self.cpu_percent.load(Ordering::Relaxed) as f32 / 100.0
    }

    /// Toplam kare kaybı
    pub fn frame_drops(&self) -> u64 {
        self.frame_drops.load(Ordering::Relaxed)
    }
}

impl Default for MetricState {
    fn default() -> Self {
        Self {
            bitrate_kbps: AtomicU32::new(0),
            fps_actual:   AtomicU32::new(0),
            cpu_percent:  AtomicU32::new(0),
            frame_drops:  AtomicU64::new(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_metric_state_update() {
        let state = MetricState::new();
        let sample = MetricSample {
            magic_head:   MetricSample::MAGIC,
            timestamp_us: 1000000,
            bitrate_kbps: 6000,
            fps_actual:   59.97,
            cpu_percent:  32.5,
            frame_drops:  0,
            magic_tail:   MetricSample::MAGIC,
        };
        assert!(sample.is_valid());
        state.update(&sample);
        assert_eq!(state.bitrate(), 6000);
        assert!((state.fps() - 59.97).abs() < 0.01);
        assert!((state.cpu() - 32.5).abs() < 0.01);
    }

    #[test]
    fn test_canary_validation() {
        let mut sample = MetricSample {
            magic_head:   MetricSample::MAGIC,
            timestamp_us: 0,
            bitrate_kbps: 0,
            fps_actual:   0.0,
            cpu_percent:  0.0,
            frame_drops:  0,
            magic_tail:   MetricSample::MAGIC,
        };
        assert!(sample.is_valid());
        sample.magic_head = 0xDEADBEEF;
        assert!(!sample.is_valid());
    }
}