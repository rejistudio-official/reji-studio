//! Metrik toplama — C++ pipeline'dan gelen telemetri verilerini işler.

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;

/// Anlık metrik snapshot — C++ RjMetricSample ile birebir ABI uyumlu (#[repr(C)], 56 byte)
///
/// C++ layout (ffi_bridge.h):
///   +0   magic_head:       u32
///   [+4  implicit padding for u64 alignment]
///   +8   timestamp_us:     u64
///   +16  bitrate_kbps:     u32
///   +20  fps_actual:       f32
///   +24  cpu_percent:      f32
///   +28  frame_drops:      u32
///   +32  frame_drop_pct:   u32   [0, 100]
///   +36  gpu_temp_c:       i16
///   +38  cpu_temp_c:       i16
///   +40  memory_usage_pct: u32   [0, 100]
///   +44  cpu_load_pct:     u32   [0, 100]
///   +48  network_rtt_ms:   u16
///   +50  network_loss_pct: u8    [0, 100]
///   +51  source_id:        u8    (0=video, 1=audio)
///   +52  magic_tail:       u32
///   = 56 bytes
#[repr(C)]
#[derive(Debug, Clone, Copy)]
pub struct MetricSample {
    pub magic_head:       u32,
    pub timestamp_us:     u64,    // +8 (4-byte implicit padding before this)
    pub bitrate_kbps:     u32,
    pub fps_actual:       f32,
    pub cpu_percent:      f32,
    pub frame_drops:      u32,
    pub frame_drop_pct:   u32,
    pub gpu_temp_c:       i16,
    pub cpu_temp_c:       i16,
    pub memory_usage_pct: u32,
    pub cpu_load_pct:     u32,
    pub network_rtt_ms:   u16,
    pub network_loss_pct: u8,
    pub source_id:        u8,
    pub magic_tail:       u32,
}

const _: () = assert!(core::mem::size_of::<MetricSample>() == 56);
const _: () = assert!(core::mem::offset_of!(MetricSample, magic_tail) == 52);

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
    pub bitrate_kbps:       AtomicU32,  // video bitrate
    pub audio_bitrate_kbps: AtomicU32,  // audio bitrate
    pub fps_actual:         AtomicU32,  // f32 * 100 olarak saklanır
    pub cpu_percent:        AtomicU32,  // f32 * 100 olarak saklanır
    pub frame_drops:        AtomicU64,  // toplam kare kaybı
}

impl MetricState {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            bitrate_kbps:       AtomicU32::new(0),
            audio_bitrate_kbps: AtomicU32::new(0),
            fps_actual:         AtomicU32::new(0),
            cpu_percent:        AtomicU32::new(0),
            frame_drops:        AtomicU64::new(0),
        })
    }

    /// Yeni sample ile güncelle — source_id: 0 = video, 1 = audio
    pub fn update(&self, sample: &MetricSample) {
        if sample.source_id == 0 {
            self.bitrate_kbps.store(sample.bitrate_kbps, Ordering::Relaxed);
            self.fps_actual.store((sample.fps_actual * 100.0) as u32, Ordering::Relaxed);
            self.cpu_percent.store((sample.cpu_percent * 100.0) as u32, Ordering::Relaxed);
            self.frame_drops.fetch_add(sample.frame_drops as u64, Ordering::Relaxed);
        } else if sample.source_id == 1 {
            self.audio_bitrate_kbps.store(sample.bitrate_kbps, Ordering::Relaxed);
        }
    }

    /// Video bitrate oku
    pub fn bitrate(&self) -> u32 {
        self.bitrate_kbps.load(Ordering::Relaxed)
    }

    /// Audio bitrate oku
    pub fn audio_bitrate(&self) -> u32 {
        self.audio_bitrate_kbps.load(Ordering::Relaxed)
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
            bitrate_kbps:       AtomicU32::new(0),
            audio_bitrate_kbps: AtomicU32::new(0),
            fps_actual:         AtomicU32::new(0),
            cpu_percent:        AtomicU32::new(0),
            frame_drops:        AtomicU64::new(0),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid_sample() -> MetricSample {
        MetricSample {
            magic_head:       MetricSample::MAGIC,
            timestamp_us:     1_000_000,
            bitrate_kbps:     6000,
            fps_actual:       59.97,
            cpu_percent:      32.5,
            frame_drops:      0,
            frame_drop_pct:   0,
            gpu_temp_c:       65,
            cpu_temp_c:       55,
            memory_usage_pct: 40,
            cpu_load_pct:     32,
            network_rtt_ms:   15,
            network_loss_pct: 0,
            source_id:        0,
            magic_tail:       MetricSample::MAGIC,
        }
    }

    #[test]
    fn test_metric_sample_size() {
        assert_eq!(core::mem::size_of::<MetricSample>(), 56);
        assert_eq!(core::mem::offset_of!(MetricSample, magic_tail), 52);
    }

    #[test]
    fn test_metric_state_update() {
        let state = MetricState::new();
        let sample = valid_sample();
        assert!(sample.is_valid());
        state.update(&sample);
        assert_eq!(state.bitrate(), 6000);
        assert!((state.fps() - 59.97).abs() < 0.01);
        assert!((state.cpu() - 32.5).abs() < 0.01);
    }

    #[test]
    fn test_source_id_routing() {
        let state = MetricState::new();

        let mut video = valid_sample();
        video.source_id = 0;
        video.bitrate_kbps = 8000;
        state.update(&video);

        let mut audio = valid_sample();
        audio.source_id = 1;
        audio.bitrate_kbps = 192;
        state.update(&audio);

        assert_eq!(state.bitrate(), 8000);
        assert_eq!(state.audio_bitrate(), 192);
    }

    #[test]
    fn test_canary_validation() {
        let mut sample = valid_sample();
        assert!(sample.is_valid());
        sample.magic_head = 0xDEADBEEF;
        assert!(!sample.is_valid());

        let mut sample2 = valid_sample();
        sample2.magic_tail = 0xDEADBEEF;
        assert!(!sample2.is_valid());
    }
}
