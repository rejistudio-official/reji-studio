# TALİMAT: CI'da C++ Build'inin Onarımı — 3 Parça

**Kaynak:** 2026-08-10 clippy/CI temizliği sırasında yapılan Faz 0
incelemesi (commit `51583de` gövdesinde özet). Todoist'te P3
teknik borç olarak kayıtlı.
**Hedef dosya konumu:** Tamamlanınca `docs/talimatlar/` arşivine
taşınmalı.

---

## Bağlam — neden kırık (2026-08-10 tespitleri)

`build.yml` ("Build and Test") her çalışmada CMake yapılandırma
adımında düşüyor; `quality.yml`'den C++ adımları bu nedenle çıkarıldı
(`51583de`). Üç bağımsız kök neden var ve **üçü birden** çözülmeden
CI'da C++ derlemesi yeşile dönmez:

1. **Vulkan SDK yok:** `CMakeLists.txt:33` `find_package(Vulkan
   REQUIRED)` — `windows-latest` runner'ında Vulkan SDK kurulu değil.
2. **Zig artefaktları yok:** `reji_pipeline` ve test hedefleri
   `zig-out/lib/{reji_ffi, vulkan_init_zig, ext_bridge_zig,
   rtmp_transport_zig}.lib`'e link ediyor; `zig-out/` git'te takipli
   değil ve hiçbir workflow'da Zig kurulumu/`zig build` adımı yok.
   Testler ayrıca Rust `target/release/reji_orchestrator.lib` ister.
3. **Mock modu bit-rot:** `-DREJI_VULKAN_MOCK=ON` kaçış kapısı
   çalışmıyor. Mock tasarımından SONRA eklenen `copy_optimizer.{h,cpp}`
   ve `gpu_query_timing.h` vulkan başlıklarını koşulsuz include ediyor
   ve her-zaman-derlenen kaynak listesindeler → mock ON iken
   `find_package(Vulkan)` çalışmadığından include yolu gelmiyor, yerel
   makinede (SDK kurulu!) bile `C1083: vulkan/vulkan.h açılamıyor` ile
   kırılıyor. Bugüne dek görünmedi çünkü test hedefleri mock'u yalnız
   kendi TU'larında tanımlayıp tam-derlenmiş `reji_pipeline`'a link
   ediyor (`tests/CMakeLists.txt:54,94`).

Mock'un mevcut dışlama kapsamı (referans): yalnız 5 dosya —
`gpu/vulkan_initializer.cpp` (60), `gpu/external_memory_bridge.cpp`
(100), `gpu_interop_subsystem.cpp` (78), `gpu/zig_win32_compat.c` (54),
`gpu/zig_win32_compat.asm` (26 satır, MASM hedefi). Kalan ~4.260 satır
(copy_optimizer dahil) mock'ta da derlenmeli.

## Kapsam notu (2026-08-10 ek)

- **Üçüncü workflow da kırmızı:** `ci.yml` ("CI - Reji Studio v0.5
  Vulkan Pivot") yalnız-docs commit'lerinde bile kırmızı — yani
  kırmızılık son değişikliklerden bağımsız, kalıcı bir kök nedeni var.
  **Faz 0'da netleştirilmeli:** build.yml ile aynı üç parçadan mı
  (Vulkan SDK + Zig toolchain + Rust lib) muzdarip, yoksa ayrı bir
  sorunu mu var? Statik okuma ipuçları (henüz log'la doğrulanmadı):
  ci.yml **mock modda** derliyor (`-DREJI_VULKAN_MOCK=ON`) → Parça
  3'teki bit-rot (`copy_optimizer` C1083) bu workflow'u doğrudan
  vurur; `zig build` adımı onda da yok → `reji_pipeline` link'i
  `zig-out/lib/*.lib` ister (Parça 2 onu da kapsar); ayrıca generator
  `"Visual Studio 18 2026"` runner'da mevcut olmayabilir ve koşturduğu
  `FrameProfilerTest`/`ShaderCacheTest` yerelde bilinen-2-kırık test —
  build geçse bile kırmızı kalabilir. Hangi adımda düştüğünü Actions
  log'undan saptamak triyajın ilk işi (bkz. Faz 0/#5).
- **Cargo.lock artık izleniyor** (`7337762`): onarım planı bunu
  varsayabilir — CI gerçek (kilitli) bağımlılık kümesiyle koşar,
  "taze çözümleme" sapması artık yok; audit/clippy/test sonuçları
  yerelle karşılaştırılabilir.

---

## Faz 0 — Triyaj (kod yazmadan)

| # | Doğrulanacak |
|---|---|
| 1 | Vulkan SDK CI kurulum seçenekleri: `humbletim/setup-vulkan-sdk` action'ı mı, LunarG installer'ın sessiz kurulumu mu, choco mu? Runner'a maliyeti (dakika) ölç. Yalnız header+lib yeterli (GPU sürücüsü gerekmez — link ediyoruz, çalıştırmıyoruz). |
| 2 | `build.zig` hedeflerini listele (`zig build --help`): `ffi`, `gpu`, `ext-bridge`, `rtmp` hedefleri hangi argümanları istiyor (`-Dvulkan-sdk=...`)? Hangi Zig sürümü gerekiyor (yerelde çalışan sürümü `zig version` ile sabitle)? |
| 3 | Mock bit-rot kapsamını derleyerek doğrula: `cmake -B build_mock -DREJI_VULKAN_MOCK=ON` + build — `copy_optimizer`'dan sonra başka hangi dosyalar kırılıyor? (İlk kırılan dosyada durur; tam liste için include'ları sırayla gate'leyip tekrar dene.) |
| 4 | `rc.exe`/`mt.exe` PATH sorunu CI'da var mı (yerel sandbox'a özgü olabilir) — `ilammy/msvc-dev-cmd` bunu runner'da zaten çözüyor. |
| 5 | `ci.yml` triyajı: Actions log'undan ilk düşen adımı sapta (configure mı, build mı, generator mı?). Aynı üç parçadan mı muzdarip yoksa ayrı sorun mu — karar buradan çıkar. #3'teki yerel mock build'i bunun büyük kısmını CI'a gitmeden cevaplar. Sonuca göre karar: ci.yml onarım kapsamına girer mi, build.yml ile birleştirilir mi, yoksa emekliye mi ayrılır (mock build'i Parça 3 sonrası build.yml'de bit-rot nöbetçisi olarak da koşabilir)? |

---

## Parça 1 — Vulkan SDK kurulumu (CI)

- `build.yml`'e MSVC adımından önce Vulkan SDK kurulum adımı ekle
  (Faz 0/#1'de seçilen yöntemle; sürümü sabitle, örn. yerelde çalışan
  `1.4.350.0`).
- `VULKAN_SDK` env'inin `find_package(Vulkan)` tarafından görüldüğünü
  doğrula.
- Kabul: "CMake yapılandır" adımı runner'da `Found Vulkan` satırıyla
  geçer.

## Parça 2 — Zig toolchain + build adımları (CI)

- `mlugg/setup-zig` (veya eşdeğeri) ile Faz 0/#2'de sabitlenen Zig
  sürümünü kur.
- CMake'ten ÖNCE sırayla: `zig build ffi`, `zig build gpu
  -Dvulkan-sdk=$env:VULKAN_SDK`, `zig build ext-bridge
  -Dvulkan-sdk=$env:VULKAN_SDK`, `zig build rtmp` (tam hedef adlarını
  Faz 0/#2'den al).
- `cargo build --release` adımının `reji_orchestrator.lib` ürettiğini
  doğrula (build.yml'de zaten var, quality.yml'e GEREKMEZ — C++ oradan
  çıkarıldı).
- Kabul: link aşaması `zig-out/lib/*.lib` dosyalarını bulur.
- Alternatif (daha ucuz ama bakım yükü): zig-out .lib'lerini release
  artefaktı olarak yayınlayıp CI'da indirmek. Önce toolchain yolunu
  dene; süre kabul edilemezse buna düş.

## Parça 3 — Mock bit-rot onarımı (kod)

- `copy_optimizer.h/.cpp` ve `gpu_query_timing.h` için karar: (a)
  vulkan include'larını ve Vulkan'a dokunan üyeleri
  `#ifndef REJI_VULKAN_MOCK` ile gate'le (external_memory_bridge.h'teki
  `VkDevice = void*` takma-ad deseni örnek), YA DA (b) dosyaları
  `src/pipeline/CMakeLists.txt:30` dışlama listesine ekle. (a) tercih:
  copy_optimizer 476 satır ve mock'ta derlenmesi CI koruması için
  değerli.
- Onarımdan sonra yerelde `-DREJI_VULKAN_MOCK=ON` tam build + testler
  yeşil olmalı (bu, mock'un tek gerçek doğrulaması olur).
- Kabul: mock build'i yerelde exit 0; CI'da mock'a gerek kalmadıysa
  (Parça 1 gerçek SDK kurduysa) mock yolu yine de kırılmamış kalmalı —
  isteğe bağlı olarak haftalık cron'a `-DREJI_VULKAN_MOCK=ON` build'i
  eklenebilir (bit-rot nöbetçisi).

---

## Sıralama ve bağımlılıklar

Parça 1 → Parça 2 (Zig'in `gpu`/`ext-bridge` hedefleri Vulkan SDK
ister) → build.yml yeşil. Parça 3 bağımsız, istenirse en sona.
Her parça ayrı commit; build.yml değişiklikleri tek PR'da toplanabilir.

## Kapsam dışı

- `quality.yml`'e C++ adımlarını geri eklemek (bilinçli çıkarıldı,
  `51583de`).
- SwiftShader ile CI'da gerçek Vulkan çalıştırmak (link yeterli,
  runtime gerekmiyor).
