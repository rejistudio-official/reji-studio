// V8/I4 — CPU fallback transfer() satır-pitch farkı düzeltmesinin sentetik testi.
//
// Gerçek bug'ı donanımla tetiklemek zor (iki farklı GPU'nun farklı RowPitch
// döndürmesi gerekir; AMD 780M + RTX 4070 bunu her zaman yapmayabilir). Bunun
// yerine copy_mapped_rows'u (pitch_copy.h — D3D11'den bağımsız) sentetik pitch
// çiftleriyle doğrularız: satır kayması yok + dst tamponu aşılmıyor.
#include "pitch_copy.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using reji::copy_mapped_rows;

namespace {
constexpr uint8_t kGuard = 0xAB;

// Kaynak tamponu satır satır tanınabilir değerlerle doldur (ilk row_bytes veri).
std::vector<uint8_t> make_src(uint32_t pitch, uint32_t height, uint32_t row_bytes) {
    std::vector<uint8_t> src(static_cast<size_t>(pitch) * height, 0);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < row_bytes; ++x)
            src[static_cast<size_t>(y) * pitch + x] =
                static_cast<uint8_t>(y * 10 + x + 1);
    return src;
}
}  // namespace

// dst_pitch > src_pitch: her satır dst'nin KENDİ pitch'inde doğru offset'te,
// satır kayması olmadan kopyalanmalı.
TEST(GpuResourcePitch, WiderDestPitchCopiesRowsWithoutShear) {
    constexpr uint32_t height = 4, row_bytes = 6, src_pitch = 8, dst_pitch = 16;

    const auto src = make_src(src_pitch, height, row_bytes);
    std::vector<uint8_t> dst(static_cast<size_t>(dst_pitch) * height, 0);

    copy_mapped_rows(dst.data(), dst_pitch, src.data(), src_pitch, height);

    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < row_bytes; ++x)
            EXPECT_EQ(dst[static_cast<size_t>(y) * dst_pitch + x],
                      static_cast<uint8_t>(y * 10 + x + 1))
                << "satır kayması: y=" << y << " x=" << x;
}

// dst_pitch < src_pitch: eski düz-blok memcpy(src_pitch*height) burada dst'nin
// sonunu aşardı. Yeni kod guard bölgesine dokunmamalı + piksel verisi korunmalı.
TEST(GpuResourcePitch, NarrowerDestPitchDoesNotOverrun) {
    constexpr uint32_t height = 4, row_bytes = 6, src_pitch = 16, dst_pitch = 8;

    const auto src = make_src(src_pitch, height, row_bytes);

    constexpr size_t dst_size   = static_cast<size_t>(dst_pitch) * height;  // 32
    constexpr size_t guard_size = 32;
    std::vector<uint8_t> dst(dst_size + guard_size, kGuard);

    copy_mapped_rows(dst.data(), dst_pitch, src.data(), src_pitch, height);

    // 1) Overrun kontrolü: guard bölgesi bozulmadıysa dst sınırları aşılmadı.
    for (size_t i = dst_size; i < dst.size(); ++i)
        EXPECT_EQ(dst[i], kGuard) << "guard bozuldu i=" << i << " → overrun";

    // 2) Piksel verisi korundu (row_bytes <= dst_pitch olduğundan kayıp yok).
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < row_bytes; ++x)
            EXPECT_EQ(dst[static_cast<size_t>(y) * dst_pitch + x],
                      static_cast<uint8_t>(y * 10 + x + 1))
                << "veri kaybı: y=" << y << " x=" << x;

    // 3) Sayısal kanıt: eski yaklaşım (src_pitch*height) dst kapasitesini aşardı.
    EXPECT_GT(static_cast<size_t>(src_pitch) * height, dst_size);
}

// Eşit pitch: birebir kopya (regresyon güvenlik ağı — eski davranışın korunduğu hâl).
TEST(GpuResourcePitch, EqualPitchCopiesExactly) {
    constexpr uint32_t height = 3, pitch = 12;
    std::vector<uint8_t> src(static_cast<size_t>(pitch) * height);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i + 1);

    std::vector<uint8_t> dst(src.size(), 0);
    copy_mapped_rows(dst.data(), pitch, src.data(), pitch, height);

    EXPECT_EQ(dst, src);
}
