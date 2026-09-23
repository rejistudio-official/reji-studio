# reji-orchestrator fuzz — `rules.json` parser ve doğrulama katmanı

cargo-fuzz (libFuzzer) hedefleri. Kaynak: `docs/TALIMAT_CARGO_FUZZ.md`.
Bu crate kök workspace'in **dışındadır** (`[workspace]` tablosu) — CI'daki
`cargo clippy --all-targets` / `cargo test` onu görmez, kök `Cargo.lock`
etkilenmez. **Fuzz altyapısı CI'a alınmaz** (nightly + ASan + dakikalık
derleme); crash'lerden üretilen regresyon testleri normal `cargo test`'e girer.

| Hedef | Giriş noktası | İnvariantlar |
|---|---|---|
| `cond` | `rules::eval_condition*`, `explanation_for*` | panic yok |
| `rules-content` | `rules::parse_rules_content` (bb110c4) | panic yok; **roundtrip** (parse→serialize→parse sabit nokta); **`evaluate` asla panic etmez** (fuzz kurallar × fuzz metrikler × her mod, kalibre eşik + cooldown dahil); `snapshot_json` yeniden ayrıştırılır |

## Windows/MSVC'de çalıştırma — dört koşul (2026-09-23 ölçüldü, varsayma)

1. **`cargo +nightly fuzz …`** — cargo-fuzz nightly'yi kendisi seçmez; stable'da
   `-Z` bayrağı hatasıyla düşer. ASan **açık kalmalı**: `-s none` MSVC'de
   `__start___sancov_cntrs` çözümlenmemiş sembolüyle linklenmez.
2. **ASan DLL'i yalnız KOŞUMDA PATH'te** — ikili
   `clang_rt.asan_dynamic-x86_64.dll` ister, yoksa `0xc0000135`
   (STATUS_DLL_NOT_FOUND). Dizin MSVC'nin altında:
   `C:\Program Files\Microsoft Visual Studio\18\Community\VC\Tools\MSVC\<sürüm>\bin\Hostx64\x64`.
   **Derlemede PATH'e ekleme:** cl.exe PATH'te görünürse cc-rs INCLUDE
   ortamı kurulmuş sanıp cl'yi doğrudan çağırır ve libfuzzer-sys derlenmez.
3. **`abort()` panik kancası** — libfuzzer-sys'in kancası `__fastfail`
   (`0xc0000409`) ile çıkar; libFuzzer yakalayamaz, **crash artifact'i
   yazılmaz, `tmin` çalışmaz**. `fuzz_targets/common.rs` UCRT `abort()`
   çağıran kancayı kurar (SIGABRT → libFuzzer yakalayıcısı → `crash-*`).
   `std::process::exit` işe yaramaz (atexit çalışmaz).
4. **Kısa `--target-dir`** — nesne dosyası yolu MAX_PATH'i (260) aşarsa
   cl.exe sessizce başarısız olur (scratchpad'de 262 karakterle görüldü).
   Repo yolunda 161; yine de `C:/Temp/rjft` gibi kısa bir dizin güvenli.

```bash
# derleme (MSVC bin PATH'te DEĞİLKEN)
cd C:/reji-studio/src/orchestrator
cargo +nightly fuzz build --target-dir C:/Temp/rjft

# koşum (DLL dizini yalnız burada PATH'e eklenir; seeds/ ek corpus dizini olarak verilir).
# libFuzzer ilk corpus dizininin VAR olmasını ister — bir kez oluştur:
mkdir -p fuzz/corpus/cond fuzz/corpus/rules-content
ASAN_BIN="/c/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231/bin/Hostx64/x64"
PATH="$ASAN_BIN:$PATH" cargo +nightly fuzz run --target-dir C:/Temp/rjft rules-content \
    fuzz/corpus/rules-content fuzz/seeds/rules_content -- -max_total_time=900 -max_len=4096
PATH="$ASAN_BIN:$PATH" cargo +nightly fuzz run --target-dir C:/Temp/rjft cond \
    fuzz/corpus/cond fuzz/seeds/cond -- -max_total_time=900 -max_len=256

# crash bulununca
PATH="$ASAN_BIN:$PATH" cargo +nightly fuzz tmin --target-dir C:/Temp/rjft <hedef> fuzz/artifacts/<hedef>/crash-…
```

## Seed corpus

`seeds/` commit'lenir (talimat: yalnız seed'ler + hedef kaynakları). İçerik:
`docs/config/rules.json.template`, `docs/config/profiles/*.json` (tam dosya →
`rules-content`; `condition` alanları → `cond`) ve `tests/rules_test.rs`
biçimindeki bileşik koşullar. `corpus/`, `artifacts/`, `coverage/` ve
`target/` gitignore'dadır — **commit'lenmez**.

## Bilinen bulgular

- **2026-09-23, rules-content, roundtrip:** 82 haneli `fps_limit` literal'i
  serde_json'da f64'e düşer ve iki geçişte 1 ulp farklı serileşir
  (`3.0222222222222226e+81` → `3.022222222222222e+81`). Kök neden serde_json'un
  `float_roundtrip` özelliği olmadan doğru yuvarlamaması (std::parse ile
  ölçüldü). Ürün etkisi yok (`fps_limit` `as_i64` → `None` → 0, SB-9);
  harness float'ları ≤4 ulp toleransla karşılaştırır. Ürün tarafı kararı
  (`serde_json` `float_roundtrip` açmak, ~2× parse maliyeti) açık.

## Bulgu disiplini

Crash → `tmin` ile küçült → rapor (minimal girdi, stack trace, kök neden
tahmini, hangi kullanıcı senaryosunda tetiklenir) → onay → düzeltme turunda
**önce regresyon testi** (`cargo test`, TDD RED) → düzeltme.
