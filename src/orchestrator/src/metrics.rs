//! Metrik toplama — C++ pipeline'dan gelen telemetri verilerini işler.

use std::sync::atomic::{AtomicU32, AtomicU64, Ordering};
use std::sync::Arc;
use crate::constants;

/// Anlık metrik snapshot — C++ RjMetricSample ile birebir ABI uyumlu (#[repr(C)], 64 byte)
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
///   +48  gpu_load_pct:     u32   [0, 100]
///   +52  network_rtt_ms:   u16
///   +54  network_loss_pct: u8    [0, 100]
///   +55  source_id:        u8    (0=video, 1=audio)
///   +56  magic_tail:       u32
///   [+60 implicit trailing padding: 4 bytes — u64 alignment rounds 60→64]
///   = 64 bytes
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
    pub gpu_load_pct:     u32,
    pub network_rtt_ms:   u16,
    pub network_loss_pct: u8,
    pub source_id:        u8,
    pub magic_tail:       u32,
}

const _: () = assert!(core::mem::size_of::<MetricSample>() == 64);
const _: () = assert!(core::mem::offset_of!(MetricSample, magic_tail) == 56);

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
    pub frame_drop_pct:     AtomicU32,  // V8/I14: anlık kare-kayıp yüzdesi [0,100] (kümülatif DEĞİL)
}

impl MetricState {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            bitrate_kbps:       AtomicU32::new(0),
            audio_bitrate_kbps: AtomicU32::new(0),
            fps_actual:         AtomicU32::new(0),
            cpu_percent:        AtomicU32::new(0),
            frame_drops:        AtomicU64::new(0),
            frame_drop_pct:     AtomicU32::new(0),
        })
    }

    /// Yeni sample ile güncelle — source_id: 0 = video, 1 = audio
    pub fn update(&self, sample: &MetricSample) {
        if sample.source_id == 0 {
            self.bitrate_kbps.store(sample.bitrate_kbps, Ordering::Relaxed);
            let fps = sample.fps_actual;
            if fps.is_finite() && fps >= 0.0 && fps <= 240.0 {
                self.fps_actual.store((fps * 100.0) as u32, Ordering::Relaxed);
            }
            // else: önceki değeri koru
            self.cpu_percent.store((sample.cpu_percent * 100.0) as u32, Ordering::Relaxed);
            self.frame_drops.fetch_add(sample.frame_drops as u64, Ordering::Relaxed);
            // V8/I14: frame_drop_pct anlık (kümülatif değil) → store, min(100) ile sınırla.
            self.frame_drop_pct.store(sample.frame_drop_pct.min(100), Ordering::Relaxed);
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

    /// Anlık kare-kayıp yüzdesi [0,100] (V8/I14)
    pub fn frame_drop_pct(&self) -> u32 {
        self.frame_drop_pct.load(Ordering::Relaxed)
    }

    /// V8/I14: UI pull'u (`rj_metrics_poll`) için anlık snapshot.
    /// Push'un yazdığı AYNI atomik state'ten okunur — ikinci bir metrik toplama
    /// yolu icat edilmez. source_id=0 (video); UI yalnız fps/bitrate/drop_pct
    /// gösterir. MetricState'te tutulmayan alanlar (gpu/temp/network/mem) 0'dır.
    /// Canary alanları geçerli set edilir (C++ tarafı doğrulayabilsin).
    pub fn snapshot(&self) -> MetricSample {
        MetricSample {
            magic_head:       MetricSample::MAGIC,
            timestamp_us:     0,
            bitrate_kbps:     self.bitrate(),
            fps_actual:       self.fps(),
            cpu_percent:      self.cpu(),
            frame_drops:      self.frame_drops().min(u32::MAX as u64) as u32,
            frame_drop_pct:   self.frame_drop_pct(),
            gpu_temp_c:       0,
            cpu_temp_c:       0,
            memory_usage_pct: 0,
            cpu_load_pct:     self.cpu() as u32,
            gpu_load_pct:     0,
            network_rtt_ms:   0,
            network_loss_pct: 0,
            source_id:        0,
            magic_tail:       MetricSample::MAGIC,
        }
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
            frame_drop_pct:     AtomicU32::new(0),
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
            bitrate_kbps:     constants::DEFAULT_BITRATE_KBPS,
            fps_actual:       59.97,
            cpu_percent:      32.5,
            frame_drops:      0,
            frame_drop_pct:   0,
            gpu_temp_c:       65,
            cpu_temp_c:       55,
            memory_usage_pct: 40,
            cpu_load_pct:     32,
            gpu_load_pct:     0,
            network_rtt_ms:   15,
            network_loss_pct: 0,
            source_id:        0,
            magic_tail:       MetricSample::MAGIC,
        }
    }

    #[test]
    fn test_metric_sample_size() {
        assert_eq!(core::mem::size_of::<MetricSample>(), 64);
        assert_eq!(core::mem::offset_of!(MetricSample, magic_tail), 56);
    }

    #[test]
    fn test_metric_state_update() {
        let state = MetricState::new();
        let sample = valid_sample();
        assert!(sample.is_valid());
        state.update(&sample);
        assert_eq!(state.bitrate(), constants::DEFAULT_BITRATE_KBPS);
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
    fn test_fps_nan_inf_guard() {
        let state = MetricState::new();
        let mut sample = valid_sample();
        sample.fps_actual = 30.0;
        state.update(&sample);
        assert!((state.fps() - 30.0).abs() < 0.01);

        // NaN → önceki değer korunmalı
        sample.fps_actual = f32::NAN;
        state.update(&sample);
        assert!((state.fps() - 30.0).abs() < 0.01);

        // Infinity → önceki değer korunmalı
        sample.fps_actual = f32::INFINITY;
        state.update(&sample);
        assert!((state.fps() - 30.0).abs() < 0.01);

        // 241.0 → sınır dışı, önceki değer korunmalı
        sample.fps_actual = 241.0;
        state.update(&sample);
        assert!((state.fps() - 30.0).abs() < 0.01);

        // 240.0 → geçerli, kabul edilmeli
        sample.fps_actual = 240.0;
        state.update(&sample);
        assert!((state.fps() - 240.0).abs() < 0.01);
    }

    #[test]
    fn test_frame_drop_pct_stored_and_clamped() {
        // V8/I14: frame_drop_pct anlık değer — store edilir (kümülatif değil), [0,100] sınırlanır.
        let state = MetricState::new();
        assert_eq!(state.frame_drop_pct(), 0);

        let mut sample = valid_sample();
        sample.frame_drop_pct = 12;
        state.update(&sample);
        assert_eq!(state.frame_drop_pct(), 12);

        // Yeni sample eski değeri EZER (kümülatif toplamaz).
        sample.frame_drop_pct = 5;
        state.update(&sample);
        assert_eq!(state.frame_drop_pct(), 5);

        // Sınır dışı → 100'e clamp.
        sample.frame_drop_pct = 250;
        state.update(&sample);
        assert_eq!(state.frame_drop_pct(), 100);

        // Audio sample (source_id=1) video drop_pct'yi DEĞİŞTİRMEZ.
        let mut audio = valid_sample();
        audio.source_id = 1;
        audio.frame_drop_pct = 99;
        state.update(&audio);
        assert_eq!(state.frame_drop_pct(), 100);
    }

    #[test]
    fn test_snapshot_reflects_pushed_state() {
        // V8/I14: snapshot(), update()'in yazdığı AYNI state'i yansıtmalı (pull==push kaynağı).
        let state = MetricState::new();
        let mut sample = valid_sample();
        sample.bitrate_kbps = 6000;
        sample.fps_actual   = 59.94;
        sample.cpu_percent  = 41.5;
        sample.frame_drop_pct = 7;
        state.update(&sample);

        let snap = state.snapshot();
        assert!(snap.is_valid(), "snapshot canary geçerli olmalı");
        assert_eq!(snap.source_id, 0, "snapshot video örneği (source_id=0) olmalı");
        assert_eq!(snap.bitrate_kbps, 6000);
        assert!((snap.fps_actual - 59.94).abs() < 0.01);
        assert!((snap.cpu_percent - 41.5).abs() < 0.01);
        assert_eq!(snap.frame_drop_pct, 7);
    }

    #[test]
    fn test_snapshot_empty_state_is_valid_zeros() {
        // Hiç push yokken snapshot geçerli-ama-sıfır olmalı (poll erken UI için güvenli).
        let state = MetricState::new();
        let snap = state.snapshot();
        assert!(snap.is_valid());
        assert_eq!(snap.bitrate_kbps, 0);
        assert_eq!(snap.frame_drop_pct, 0);
        assert!((snap.fps_actual - 0.0).abs() < 0.01);
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
