---
name: ffi-safety-review
description: Reji Studio C++ ↔ Rust FFI sınırında güvenli değişiklik yapma ve gözden geçirme prosedürü. src/ffi/, src/orchestrator/src/ffi.rs, ffi_bridge.h, cbindgen.toml veya ffi_auto.h dosyalarına dokunan HER görevde bu skill'i kullan — yeni FFI fonksiyonu ekleme, struct alanı değiştirme, enum genişletme, RjCommand/RjAction/MetricSample düzenleme, ABI hatası ayıklama, "sizeof mismatch" veya link hatası çözme dahil. Kullanıcı "FFI", "ABI", "köprü", "bridge", "extern C" veya "Rust-C++ sınırı" dediğinde de tetiklenir.
---

# FFI Safety Review — Reji Studio

C++ (pipeline/UI) ↔ Rust (orchestrator) sınırı projenin en kırılgan noktasıdır.
CLAUDE.md'deki "DOKUNMA" uyarısı mutlak yasak değil; **bu prosedür dışında dokunma** demektir.

## Sınırın haritası

| Dosya | Rol | Kim düzenler |
|---|---|---|
| `src/orchestrator/src/ffi.rs` | Rust tarafı `extern "C"` exportlar, `#[repr(C)]` structlar | Sen (elle) |
| `src/orchestrator/cbindgen.toml` | ffi_auto.h üretim ayarları | Sen (elle) |
| `ffi_auto.h` | cbindgen çıktısı | **ASLA elle** — build.rs üretir |
| `src/ffi/ffi_bridge.h` | Köprüye özel sabitler, compat makrolar, elle bildirimler | Sen (elle) |
| `src/ffi/sizeof_check.cpp` + `sizeof_check.zig` | Derleme zamanı ABI boyut doğrulama | Yeni tip eklenince güncelle |

## Değişmez kurallar (ihlal = geri al)

1. **Her paylaşılan struct `#[repr(C)]`** (Rust) ve alan sırası/tipleri `ffi_bridge.h`
   ile bire bir eşleşir. Örnek sözleşme: `RjCommand { cmd_type: u32,
   timestamp_us: u64, param_u32: u32, param_f32: f32 }`.
2. **Enum'lar `#[repr(u32)]`** — `repr(C)` enum discriminant'ı
   implementation-defined olduğundan yasak (repodaki E1 kararı).
   cbindgen tarafında `enum_class = false` korunur; compat makrolar
   (`RJ_ACTION_*`) unscoped enum değerlerine bağlıdır.
3. **FFI fonksiyonları bloklamaz.** Kuyruk sözleşmesi:
   - C++ → Rust: `metric_ring` (256-slot lock-free ArrayQueue)
   - Rust → C++: `command_queue` (64-slot lock-free ArrayQueue)
   Dolu kuyrukta drop/overwrite politikası neyse korunur; yeni bekleme eklenmez.
4. **Panic sınırı geçemez.** Her Rust export gövdesi
   `catch_unwind(AssertUnwindSafe(...))` ile sarılır; panic'te güvenli
   hata kodu döner.
5. **Ownership tek tarafta.** Pointer alan fonksiyonlarda sahiplik yorumu
   fonksiyon dokümanına yazılır; Rust'ın C++ belleğini free etmesi (veya tersi) yasak.
6. **Sürüm sabiti:** ABI'yi kıran her değişiklikte `RJ_FFI_VERSION` yükseltilir
   (`ffi_bridge.h`, şu an `0x00010000`). `MetricSample` magic'i
   `RJ_METRIC_MAGIC = 0xEEFF1234` değişmez.

## Yeni FFI fonksiyonu/tipi ekleme prosedürü

1. `ffi.rs` içinde tipi `#[repr(C)]` (+ enum ise `#[repr(u32)]`) tanımla,
   fonksiyonu `catch_unwind` sarmalıyla yaz.
2. Tip cbindgen tarafından üretilecekse `cbindgen.toml [export] include`
   listesine ekle; köprüye özelse `ffi_bridge.h`'a elle bildir.
3. `sizeof_check.cpp` ve `sizeof_check.zig`'e yeni tipin boyut/offset
   doğrulamasını ekle (MetricSample örneğini kopyala; güncel değerler için
   `sizeof_check.cpp`'deki `static_assert`'lere bak — yorum satırlarındaki
   sabit sayılara güvenme, onlar eskiyebilir [bkz. FABLE5_BUG_PLAN_V8.md I22]).
4. Gerekirse `src/ui/rust_bridge.cpp/.h`'da C++ sarmalayıcıyı ekle.
5. Doğrulama zinciri — üçü de geçmeden commit yok:
   ```
   just abi-check      # PowerShell: C++ sizeof vs Rust const_assert
   zig build abi-check # Zig comptime boyut doğrulama
   just test           # Rust birim testleri
   ```
6. C++ testi: `tests/test_ffi_boundary.cpp`'ye yeni fonksiyon için
   en az bir sınır testi ekle; `ctest --test-dir build` çalıştır.

## Gözden geçirme kontrol listesi (diff incelerken)

- [ ] `ffi_auto.h`'a elle dokunulmuş mu? → reddet, kaynak `ffi.rs`/cbindgen.toml
- [ ] Yeni struct alanı ortaya mı eklendi? → ABI kırıldı; sona ekle veya
      `RJ_FFI_VERSION` yükselt + her iki taraf senkron güncelle
- [ ] `String`, `Vec`, `Box`, referans gibi non-FFI-safe tip sınırda mı? → reddet
      (`*const c_char` + `CStr`, ham pointer + uzunluk kullan)
- [ ] Rust tarafında `unsafe` bloğun gerekçe yorumu var mı?
- [ ] Bloklayan çağrı (mutex bekleme, tokio `block_on`) export içinde mi? → reddet;
      runtime işi `OnceLock`'lu global runtime'a spawn ile devret
- [ ] Compat makrolar (`RJ_ACTION_*`) yeni enum değerini kapsıyor mu?

## Bilinen hata modları

- **Link hatası `unresolved external rj_...`** → cbindgen üretimi eskimiş;
  `cargo build` ile `build.rs`'i tetikle, sonra CMake.
- **sizeof mismatch** → çoğunlukla padding: alan sırasını Rust'ta değiştirip
  C++'ta değiştirmemek. `abi-check` çıktısındaki offset'i iki başlıkla karşılaştır.
- **Sessiz veri bozulması** → enum discriminant uyumsuzluğu; `#[repr(u32)]`
  ve header'daki sayısal değerleri tek tek eşle.
- **Yorum satırları yalan söyleyebilir** — `ffi_auto.h`/`ffi_bridge.h`'daki
  el yazısı boyut/offset yorumları koddan bağımsız eskiyebilir (bkz.
  FABLE5_BUG_PLAN_V8.md I22: 56B/+51 yorumu gerçekte 64B/+55 idi, iki
  bağımsız model bunu ayrı ayrı buldu). ABI doğrulaması için HER ZAMAN
  `static_assert`/`const_assert`'e güven, yorum satırına değil.
