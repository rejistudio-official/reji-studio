---
name: build-troubleshoot
description: Reji Studio build sorunlarını çözme rehberi — CMake configure hataları, MSVC/Ninja derleme hataları, link hataları (unresolved external), Qt 6.8 bulunamıyor, Vulkan SDK sorunları, cargo/cbindgen üretim hataları, vcvars/Windows SDK bulunamıyor, mock preset sorunları. `just build` veya `cmake --preset` başarısız olduğunda, temiz makinede kurulum yapılırken veya "derlenmiyor" şikayetinde bu skill'i kullan. Kullanıcı "build", "derleme", "link hatası", "CMake", "preset" veya "kurulum" dediğinde de tetiklenir.
---

# Build Troubleshoot — Reji Studio

Zincir: `just build` → `python scripts/build.py` → vcvars64 ortamı →
CMake preset (Ninja) → MSVC + Cargo (orchestrator) → link.
Hatanın hangi halkada olduğunu önce tespit et, sonra çöz.

## Altın kurallar

1. **Her zaman x64 Native Tools ortamı.** build.py vcvars64.bat'ı vswhere ile
   kendisi bulur; ama elle `cmake` çağırıyorsan PowerShell/Git Bash'te MSVC
   ortamı YOK — "compiler not found" hatalarının bir numaralı sebebi.
2. **İki build sistemi var:** CMake (C++) cargo'yu ExternalProject/custom
   command ile tetikler. Rust tarafı hatası C++ hatası gibi görünebilir —
   log'da `error[E` (Rust) mi `error C` (MSVC) mi ayır.
3. **`ffi_auto.h` üretilir:** yoksa/eskiyse önce
   `cd src/orchestrator && cargo build` (build.rs cbindgen'i koşar), sonra CMake.
4. **Temiz derleme son çare, ama etkili:** `just rebuild` (build/ siler).
   Preset değişimi (release↔mock) sonrası cache çakışırsa şart.

## Hata → çözüm tablosu

| Hata | Sebep / Çözüm |
|---|---|
| `vcvars64.bat not found` | VS kurulumu eksik/Insider path. build.py 2022-2026 yıllarını tarar; farklı path ise build.py `find_vcvars`'a ekle |
| `Windows SDK ... not found` | build.py `find_winsdk` Kits/10 altına bakar; SDK'yı VS Installer'dan kur (10.0.22621+) |
| `Could not find Qt6` / `Qt6Config.cmake` | `CMAKE_PREFIX_PATH` Qt 6.8.0 msvc dizinini göstermeli; preset'teki path ile kurulu sürüm eşleşiyor mu bak. Qt sürüm YÜKSELTME ayrı görevdir — geçici path değişikliğiyle çözme |
| `vulkan-1.lib` / `vulkan/vulkan.h` yok | `VULKAN_SDK` env var set mi? SDK 1.4 kurulu mu? Yeni terminalde env yenile |
| `unresolved external rj_...` | Rust staticlib eski/hiç derlenmemiş: `cargo build` → CMake. Hâlâ varsa `ffi.rs`'te fonksiyon `#[no_mangle] extern "C"` mi kontrol et |
| `sizeof mismatch` / static_assert (abi) | Build sorunu değil ABI sorunu → **ffi-safety-review skill'ine geç** |
| Ninja `multiple rules generate` | Preset karışması; `just rebuild` |
| `MSB8036` / toolset sürümü | vswhere en yeniyi buluyor ama proje eskiyle configure edilmiş; temiz build |
| Mock preset'te Vulkan sembol hatası | Vulkan'a dokunan yeni dosya `NOT REJI_VULKAN_MOCK` bloğu DIŞINA eklenmiş — `src/pipeline/CMakeLists.txt`'te doğru bloğa taşı |
| Mock preset'te `C2371` (VkDevice redefinition) | copy_optimizer.cpp/gpu_query_timing.cpp koşulsuz <vulkan/vulkan.h> include ediyor; mock modda pipeline.h'nin VkDevice=void* tanımıyla çakışıyor. Mock şu an TAMAMEN KIRIK (bilinen durum, 3a93110 öncesi de muhtemelen böyleydi — henüz düzeltilmedi) |
| cargo `linker link.exe not found` | cargo MSVC ortamı dışında çağrılmış; `just build` üzerinden git |
| CI'da geçiyor lokalde geçmiyor (veya tersi) | `.github/workflows/` içindeki sürümlerle (Qt/SDK/Rust) lokali karşılaştır; sürüm sabitleme farkı |

## Tanı prosedürü (sıra önemli)

1. Hatanın **ilk** satırını al — Ninja paralel derlediği için son satır çoğu
   zaman kurbandır, suçlu değil. `just build 2>&1 | tee build_diag.log` ile yakala
   (bu dosyayı COMMIT ETME — repo-hygiene skill'i).
2. Halkayı belirle: configure (CMake çıktısı) / compile (`error C`/`error[E`) /
   link (`LNK`) / cbindgen (build.rs stderr).
3. Tabloda eşle. Eşleşmiyorsa: aynı hedefi tek başına derle
   (`cmake --build build --target <hedef> -v`) — `-v` gerçek komut satırını
   gösterir, yanlış include/lib path'i orada görünür.
4. Mock ile çapraz test: hata release'te var mock'ta yoksa Vulkan/donanım
   bağımlılığı halkasındadır.
5. Çözüm bulundunca: **kalıcı mı geçici mi karar ver.** Path/env düzeltmesi
   makineye özgüyse README/CONTRIBUTING'e değil bu skill'e ekle;
   herkesi etkiliyorsa preset/build.py düzelt ve commit'le.

## Yasaklar

- CMakeLists/preset'e mutlak kullanıcı path'i gömme (`C:/Users/...`)
- Hata "geçsin" diye uyarı seviyesini düşürme veya `abi-check`'i atlama
- `ffi_auto.h`'ı elle düzeltme
- Build log'larını (`*.log`, `build_diag.txt` vb.) repoya commit'leme

## Skill bakımı

Yeni bir build hatası çözüldüğünde tabloya satır ekle. Tablo bu skill'in
kalbidir — `build_log.txt` gibi ham logları repoda tutmak yerine bilgi
buraya damıtılır.
