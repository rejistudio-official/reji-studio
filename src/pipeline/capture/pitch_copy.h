#pragma once
// V8/I4 — CPU fallback transfer() için satır-pitch güvenli kopya.
// Bilerek D3D11'den bağımsız (yalnız <cstring>/<algorithm>/<cstdint>): birim
// testi gerçek GPU/D3D11 olmadan sentetik pitch'lerle doğrulayabilsin diye ayrıldı.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace reji {

// Kaynak (display GPU'dan map) ve hedef (encode GPU'ya map) FARKLI adapter/
// sürücü olabilir → D3D11_MAPPED_SUBRESOURCE.RowPitch'leri eşit olmak zorunda
// DEĞİL (farklı satır alignment/tiling). Bu yüzden eski düz tek-blok
// memcpy(dst, src, src_pitch * height) iki şekilde bozuk:
//   - dst_pitch < src_pitch ise dst tamponunun sonunu aşar (buffer overrun),
//   - pitch'ler farklıysa satırlar kayar (görüntü bozulması).
// Çözüm: her satırı iki tarafın KENDİ pitch'iyle adresle, satır başına iki
// pitch'in küçüğü kadar byte taşı. min(src_pitch, dst_pitch) güvenli çünkü her
// iki pitch de >= gerçek satır byte'ı (width * bpp); dolayısıyla tüm piksel
// verisi kopyalanır, yalnız bir tarafın padding'i kırpılabilir (zararsız). Bu
// yaklaşım yeni bir format→bpp hesabı gerektirmez → ek hata riski almaz
// (talimattaki tercih). std::min parantez içinde: windows.h min makrosuna karşı.
inline void copy_mapped_rows(void*       dst, uint32_t dst_pitch,
                             const void* src, uint32_t src_pitch,
                             uint32_t    height) noexcept {
    const size_t copy_bytes = (std::min)(static_cast<size_t>(src_pitch),
                                         static_cast<size_t>(dst_pitch));
    auto*       d = static_cast<uint8_t*>(dst);
    const auto* s = static_cast<const uint8_t*>(src);
    for (uint32_t y = 0; y < height; ++y) {
        std::memcpy(d + static_cast<size_t>(y) * dst_pitch,
                    s + static_cast<size_t>(y) * src_pitch,
                    copy_bytes);
    }
}

} // namespace reji
