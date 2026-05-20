# Reji Studio

Açık kaynak, ücretsiz, profesyonel canlı yayın yazılımı.

**Sürüm:** 0.1.0-dev  
**Lisans:** Apache 2.0  
**Platform:** Windows (öncelik) → macOS → Linux

## Özellikler

- Zero-copy GPU pipeline (NVENC/AMF/QSV)
- Üç katmanlı self-healing sistemi
- C ABI plugin ekosistemi
- Makro motoru ve otomasyon
- Cross-platform (tek kod tabanı, ayrı çıktılar)

## Gereksinimler

- Windows 10/11 64-bit
- NVIDIA sürücü 596+
- Visual Studio 2022 (C++ iş yükü)
- Rust 1.75+
- CMake 3.20+

## Derleme

```cmd
git clone https://github.com/reji-studio/reji-studio
cd reji-studio
cmake -B build -G "NMake Makefiles"
cmake --build build
```

## Katkı

Katkı rehberi için [CONTRIBUTING.md](CONTRIBUTING.md) dosyasına bakın.

## Mimari

Detaylı mimari tasarım için [ARCHITECTURE.md](docs/ARCHITECTURE.md) dosyasına bakın.