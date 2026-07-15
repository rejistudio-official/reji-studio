//! Özellik#5: Kalibre edilmiş temel çizgi — donanıma özgü "normal" aralığı
//! öğrenip statik eşiği buna göre ayarlar.
//!
//! Bu modül YALNIZ istatistik + politikadır (saf, test edilebilir, saat/kaynak
//! bilmez). Pencere zamanlaması ve metrik besleme sürücüde (`HealingMonitor`).
//! MVP kapsamı: tek metrik (`memory_usage_pct`).
//!
//! Tasarım kararları (Faz 1, onaylı):
//! - Algoritma basit: online ortalama + standart sapma (Welford), eşik =
//!   ortalama + K×sigma. Karmaşık istatistik/ML YAGNI gereği kapsam dışı.
//! - **Stub/sabit-değer koruması (kritik):** pencere boyunca metrik hiç
//!   değişmediyse (`max == min`) bu bir stub/donmuş kaynak işaretidir; kalibrasyon
//!   IPTAL edilir ve statik varsayılana düşülür (Healing Plumbing'in termal-guard
//!   dersinin kalibrasyondaki karşılığı — kalibrasyon sahte bir "normal" öğrenmez).
//! - Yetersiz örnek → iptal (erken/güvensiz kalibrasyon yok).
//! - Kalibre eşik makul bir aralığa clamp'lenir (saçma değer üretilmez).

use std::time::Duration;

/// Kalibrasyon penceresi: sistem açıldıktan sonra ham örneklerin toplandığı süre.
/// Bu süre boyunca kurallar STATİK varsayılanlarla çalışır (korumasız pencere yok).
pub const CALIBRATION_WINDOW: Duration = Duration::from_secs(180);

/// Eşik = ortalama + `K_SIGMA` × sigma. 3.0 ≈ normal dağılımın ~%99.7'sini kapsar;
/// gündelik dalgalanma alarm üretmez, gerçek anomali eşiği aşar.
pub const K_SIGMA: f64 = 3.0;

/// Kalibrasyonu sonuçlandırmak için gereken minimum örnek. Pencere 180s, örnekleme
/// ~1Hz → sağlıklı çalışmada ~180 örnek beklenir; 60 alt-sınır erken/eksik
/// pencerede (geç başlama, düşük tick) güvensiz kalibrasyonu engeller.
pub const MIN_SAMPLES: u64 = 60;

/// `memory_usage_pct` için kalibre eşiğin clamp'leneceği alt/üst sınır. Kalibrasyon
/// bu aralık dışına çıkamaz — ne anlamsız-düşük (sürekli alarm) ne anlamsız-yüksek
/// (hiç tetiklenmeyen) eşik. MVP tek metrik olduğundan sabit; genelleme sonraki tur.
pub const MEM_THRESHOLD_MIN: i32 = 50;
pub const MEM_THRESHOLD_MAX: i32 = 95;

/// Kalibrasyonun neden iptal edildiği — kullanıcı-görünür log/işaret için.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CancelReason {
    /// Metrik pencere boyunca hiç değişmedi (`max == min`) → stub/donmuş kaynak.
    ConstantValue,
    /// Pencere yeterli örnek toplayamadı.
    InsufficientSamples,
}

/// Kalibrasyon sonucu. `Calibrated` → statik eşik yerine bu değer kullanılır;
/// `Cancelled` → statik varsayılan korunur (görünür şekilde).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CalibrationOutcome {
    Calibrated { threshold: i32 },
    Cancelled(CancelReason),
}

/// Tek metrik için online temel-çizgi toplayıcı. Ham örnekleri saklamaz; Welford
/// ile ortalama/varyansı akışta günceller (bellek O(1), immutability-dostu: yalnız
/// kendi akümülatörünü günceller, paylaşılan girdiyi değiştirmez).
#[derive(Debug, Clone)]
pub struct BaselineCalibrator {
    count: u64,
    mean: f64,
    m2: f64,
    min: u32,
    max: u32,
}

impl BaselineCalibrator {
    pub fn new() -> Self {
        Self {
            count: 0,
            mean: 0.0,
            m2: 0.0,
            min: u32::MAX,
            max: 0,
        }
    }

    /// Bir ham örneği pencereye ekler (Welford online güncellemesi + min/max).
    pub fn observe(&mut self, value: u32) {
        self.count += 1;
        let x = value as f64;
        let delta = x - self.mean;
        self.mean += delta / self.count as f64;
        let delta2 = x - self.mean;
        self.m2 += delta * delta2;

        self.min = self.min.min(value);
        self.max = self.max.max(value);
    }

    pub fn sample_count(&self) -> u64 {
        self.count
    }

    /// Şu ana kadarki ortalama (örnek yoksa 0).
    pub fn mean(&self) -> f64 {
        self.mean
    }

    /// Popülasyon standart sapması (pencere tam veri olarak alınır; örnek yoksa 0).
    pub fn std_dev(&self) -> f64 {
        if self.count == 0 {
            return 0.0;
        }
        (self.m2 / self.count as f64).sqrt()
    }

    /// Politikayı uygular ve kalibrasyonu sonuçlandırır:
    /// 1. Yetersiz örnek → `Cancelled(InsufficientSamples)`.
    /// 2. `max == min` (hiç değişmedi) → `Cancelled(ConstantValue)` — stub koruması.
    /// 3. Aksi halde `ortalama + K_SIGMA×sigma`, `[MEM_THRESHOLD_MIN, MAX]`'e clamp.
    pub fn finalize(&self) -> CalibrationOutcome {
        if self.count < MIN_SAMPLES {
            return CalibrationOutcome::Cancelled(CancelReason::InsufficientSamples);
        }
        if self.max == self.min {
            return CalibrationOutcome::Cancelled(CancelReason::ConstantValue);
        }
        let raw = self.mean + K_SIGMA * self.std_dev();
        let threshold = (raw.round() as i32).clamp(MEM_THRESHOLD_MIN, MEM_THRESHOLD_MAX);
        CalibrationOutcome::Calibrated { threshold }
    }
}

impl Default for BaselineCalibrator {
    fn default() -> Self {
        Self::new()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// N kez aynı değeri gözlemle (sabit/stub senaryosu için yardımcı).
    fn observe_n(cal: &mut BaselineCalibrator, value: u32, n: u64) {
        for _ in 0..n {
            cal.observe(value);
        }
    }

    #[test]
    fn mean_and_std_dev_on_known_data() {
        // [70, 90] eşit tekrar → ortalama 80, popülasyon varyansı 100, sigma 10.
        let mut cal = BaselineCalibrator::new();
        for _ in 0..50 {
            cal.observe(70);
            cal.observe(90);
        }
        assert!((cal.mean() - 80.0).abs() < 1e-9, "ortalama 80 olmalı");
        assert!((cal.std_dev() - 10.0).abs() < 1e-9, "sigma 10 olmalı");
    }

    #[test]
    fn finalize_computes_mean_plus_k_sigma() {
        // ortalama 80, sigma 10, K=3 → 80 + 30 = 110 → clamp üst sınıra (95).
        let mut cal = BaselineCalibrator::new();
        for _ in 0..50 {
            cal.observe(70);
            cal.observe(90);
        }
        // 110 üst sınırı aşıyor → 95'e clamp'lenir (aşağıdaki clamp testi ayrı).
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Calibrated { threshold: MEM_THRESHOLD_MAX }
        );
    }

    #[test]
    fn finalize_uncapped_threshold_in_range() {
        // ortalama ~40, sigma ~2 → 40 + 6 = 46 → alt sınırın (50) altında → clamp 50.
        let mut cal = BaselineCalibrator::new();
        for _ in 0..30 {
            cal.observe(38);
            cal.observe(42);
        }
        // ortalama 40, sigma 2 → 46 → clamp alt sınır 50.
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Calibrated { threshold: MEM_THRESHOLD_MIN }
        );
    }

    #[test]
    fn finalize_threshold_within_bounds_uses_computed_value() {
        // Hesaplanan eşik [50,95] içindeyse aynen kullanılır.
        // Değerler 60 civarı, sigma küçük: ortalama 60, sigma ~3 → 60+9 = 69.
        let mut cal = BaselineCalibrator::new();
        for _ in 0..40 {
            cal.observe(57);
            cal.observe(63);
        }
        // ortalama 60, sigma 3 → 60 + 9 = 69.
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Calibrated { threshold: 69 }
        );
    }

    #[test]
    fn constant_value_cancels_calibration() {
        // Stub koruması: metrik hiç değişmedi (hepsi 55) → ConstantValue iptal.
        let mut cal = BaselineCalibrator::new();
        observe_n(&mut cal, 55, 100);
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Cancelled(CancelReason::ConstantValue)
        );
    }

    #[test]
    fn constant_zero_cancels_calibration() {
        // Klasik stub: kaynak hep 0 döndürüyor → sabit değer → iptal (sahte
        // "normal 0" öğrenilmez). Healing Plumbing termal-guard dersinin karşılığı.
        let mut cal = BaselineCalibrator::new();
        observe_n(&mut cal, 0, 100);
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Cancelled(CancelReason::ConstantValue)
        );
    }

    #[test]
    fn insufficient_samples_cancels_calibration() {
        // MIN_SAMPLES'ın altında → InsufficientSamples (erken/güvensiz kalibrasyon yok).
        let mut cal = BaselineCalibrator::new();
        cal.observe(60);
        cal.observe(80);
        assert!(cal.sample_count() < MIN_SAMPLES);
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Cancelled(CancelReason::InsufficientSamples)
        );
    }

    #[test]
    fn insufficient_takes_priority_over_constant() {
        // Az sayıda ama sabit örnek → önce yetersiz-örnek kontrolü döner
        // (güvensiz pencereyi sabit-değerden ayırmak sürücü loglaması için net).
        let mut cal = BaselineCalibrator::new();
        observe_n(&mut cal, 55, 5);
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Cancelled(CancelReason::InsufficientSamples)
        );
    }

    #[test]
    fn empty_calibrator_is_insufficient() {
        let cal = BaselineCalibrator::new();
        assert_eq!(cal.mean(), 0.0);
        assert_eq!(cal.std_dev(), 0.0);
        assert_eq!(
            cal.finalize(),
            CalibrationOutcome::Cancelled(CancelReason::InsufficientSamples)
        );
    }
}
