// Merkezi sabit değerler — C++ tarafı src/pipeline/include/reji_constants.h ile eşdeğer.
// Değiştirirsen karşı tarafı da güncelle.

/// Varsayılan encode bitrate (kbps) — pipeline başlangıcı ve recovery ceiling.
pub const DEFAULT_BITRATE_KBPS: u32 = 6000;

/// Self-healing BitrateReduce hedefi (kbps).
pub const REDUCED_BITRATE_KBPS: u32 = 3500;
