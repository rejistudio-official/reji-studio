// paths.rs — Uygulama log/veri dosyaları için taşınabilir yol çözümleme.
//
// I21: Hardcoded `C:\reji-studio\...` log yolları, kullanıcıya göre değişen
// `%LOCALAPPDATA%\reji-studio\` altına taşındı. Log dosyaları olduğundan eski
// yoldan veri taşınması gerekmez (yalnız yeni loglar yeni konuma yazılır).

use std::path::PathBuf;

/// Uygulama log dizini: `%LOCALAPPDATA%\reji-studio\`.
///
/// `LOCALAPPDATA` tanımsızsa (Windows dışı / bozuk ortam) çalışma dizinine
/// düşer. Dizin best-effort oluşturulur; oluşturma başarısızlığı burada
/// yutulur (çağıran dosya açmayı denediğinde zaten hata alır).
pub fn log_dir() -> PathBuf {
    let base = std::env::var_os("LOCALAPPDATA")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from("."));
    let dir = base.join("reji-studio");
    let _ = std::fs::create_dir_all(&dir);
    dir
}

/// Verilen log dosyası adı için tam yol (`log_dir()` altında).
pub fn log_path(filename: &str) -> PathBuf {
    log_dir().join(filename)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn log_path_is_under_reji_studio_dir() {
        let p = log_path("ws_debug.log");
        assert!(p.ends_with("reji-studio/ws_debug.log") || p.ends_with("reji-studio\\ws_debug.log"),
            "beklenen reji-studio alt dizini, alınan: {:?}", p);
        assert!(p.file_name().unwrap() == "ws_debug.log");
    }

    #[test]
    fn log_dir_has_reji_studio_component() {
        let d = log_dir();
        assert!(d.components().any(|c| c.as_os_str() == "reji-studio"));
    }
}
