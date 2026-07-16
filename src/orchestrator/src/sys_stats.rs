//! sys_stats.rs — GetStats için anlık sistem ölçümleri (process belleği + disk boş alanı).
//!
//! Bu modül obs-websocket `GetStats`'ın `memoryUsage` ve `availableDiskSpace` alanlarını
//! besler. MetricState'teki STREAMING telemetrisinden (fps/bitrate/drop) AYRIdır: bunlar
//! periyodik örneklenen bir akış değil, SORGU-ANI OS gerçeğidir (process working-set,
//! sürücü boş alanı). "Paralel metrik toplama yolu icat etme" ilkesi streaming telemetri
//! hattı içindir; bu iki değer o hatta ait değil — bilinçli, belgelenmiş bir sınır
//! (TALIMAT_GETSTATS Faz 0 onayı). Statik-link (crate-type=staticlib) → C++ app ile AYNI
//! process → process working-set = tüm uygulamanın belleği (obs `memoryUsage` semantiği, dürüst).
//!
//! Non-Windows: her iki fonksiyon 0.0 döndürür (proje Windows-only; cfg-gate yalnız
//! derleme/CI taşınabilirliği için, mevcut cfg desenleriyle uyumlu).

/// 1 MiB = obs-websocket'in MB alanları için bölen (obs kaynağı working-set/free-space'i
/// 1024*1024 ile böler; birebir uyum için aynı taban).
#[allow(dead_code)]
const BYTES_PER_MB: f64 = 1024.0 * 1024.0;

/// Bu process'in çalışma kümesi (working set) — MB. obs `memoryUsage` semantiği
/// (uygulamanın kullandığı bellek). Sorgu başarısızsa 0.0 (dürüst fallback, panik yok).
#[cfg(windows)]
pub fn process_memory_mb() -> f64 {
    use windows_sys::Win32::System::ProcessStatus::{GetProcessMemoryInfo, PROCESS_MEMORY_COUNTERS};
    use windows_sys::Win32::System::Threading::GetCurrentProcess;

    let cb = core::mem::size_of::<PROCESS_MEMORY_COUNTERS>() as u32;
    let mut counters = PROCESS_MEMORY_COUNTERS { cb, ..Default::default() };
    // SAFETY: GetCurrentProcess pseudo-handle daima geçerli; `counters` yığında ve `cb`
    // doğru boyut. Başarısızlıkta BOOL==0 → 0.0 döner (yazılmamış alan okunmaz).
    let ok = unsafe { GetProcessMemoryInfo(GetCurrentProcess(), &mut counters, cb) };
    if ok == 0 {
        return 0.0;
    }
    counters.WorkingSetSize as f64 / BYTES_PER_MB
}

/// Uygulamanın çalışma dizininin bulunduğu sürücüde çağırana açık boş alan — MB.
/// obs `availableDiskSpace` semantiği (çıktı sürücüsündeki boş alan). Reji'de ayrı bir
/// kayıt/çıktı dizini kavramı yok → çalışma dizininin sürücüsü kullanılır (belgelenmiş).
/// Sorgu başarısızsa 0.0.
#[cfg(windows)]
pub fn available_disk_mb() -> f64 {
    use std::os::windows::ffi::OsStrExt;
    use windows_sys::Win32::Storage::FileSystem::GetDiskFreeSpaceExW;

    let dir = std::env::current_dir().unwrap_or_else(|_| std::path::PathBuf::from("."));
    // PCWSTR: NUL-sonlandırmalı UTF-16.
    let wide: Vec<u16> = dir
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let mut free_to_caller: u64 = 0;
    // SAFETY: `wide` NUL-sonlu geçerli PCWSTR; boş-alan çıkış pointer'ı yığında; kullanılmayan
    // toplam/toplam-boş pointer'ları null (spec: opsiyonel). Başarısızlıkta BOOL==0 → 0.0.
    let ok = unsafe {
        GetDiskFreeSpaceExW(
            wide.as_ptr(),
            &mut free_to_caller,
            core::ptr::null_mut(),
            core::ptr::null_mut(),
        )
    };
    if ok == 0 {
        return 0.0;
    }
    free_to_caller as f64 / BYTES_PER_MB
}

/// Non-Windows fallback — proje Windows-only; yalnız derleme/CI taşınabilirliği için.
#[cfg(not(windows))]
pub fn process_memory_mb() -> f64 {
    0.0
}

/// Non-Windows fallback.
#[cfg(not(windows))]
pub fn available_disk_mb() -> f64 {
    0.0
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn sorgular_negatif_donmez() {
        // Her iki sorgu da sonlu ve negatif-olmayan olmalı (JSON serileştirme + dürüstlük).
        assert!(process_memory_mb() >= 0.0 && process_memory_mb().is_finite());
        assert!(available_disk_mb() >= 0.0 && available_disk_mb().is_finite());
    }

    #[cfg(windows)]
    #[test]
    fn windows_process_bellegi_pozitif() {
        // Çalışan test process'inin working-set'i > 0 olmalı (sıfır = sorgu koptu demek).
        assert!(
            process_memory_mb() > 0.0,
            "çalışan process working-set MB > 0 beklenir"
        );
    }
}
