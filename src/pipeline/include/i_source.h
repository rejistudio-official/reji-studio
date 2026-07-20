#pragma once
#include <cstdint>

#ifdef _WIN32
struct ID3D11Device;
#endif

namespace rj {

// ---------------------------------------------------------------------------
// SourceFrame — bir kaynaktan alınan tek kare (sahiplik almayan görünüm).
// Yalnızca bir sonraki next_frame() çağrısına kadar geçerli; cache'leme.
//
// handle OPAK'tır: tüketici (bugün run_frame orkestrasyonu, gelecekte
// kompozitör) HandleType'a göre yorumlar. Bridge kavramları (VkImage,
// pool slot, NT handle) bu kontrata BİLEREK dahil değildir — Faz 5
// (external_memory_bridge instance-level geçişi) bu kontratı kırmadan
// yapılabilsin diye (bkz. docs/talimatlar/TALIMAT_ISOURCE_ARAYUZ_TASARIMI.md).
// ---------------------------------------------------------------------------
struct SourceFrame {
    enum class HandleType { D3D11, DmaBuf, IOSurface, CpuBuffer };
    HandleType type;
    void*      handle;        ///< null = bu tikte yeni kare yok (timeout/no-change)
    uint32_t   width;
    uint32_t   height;
    uint32_t   format;        ///< ham DXGI_FORMAT değeri (Windows); d3d11.h bağımlılığı yok
    uint64_t   timestamp_us;  ///< YAKALAMA zamanı (QPC-türevi, monotonik).
                              ///< Encode PTS DEĞİL — o, pacer/kompozitör sahipliğinde.
};

// ---------------------------------------------------------------------------
// SourceState — hafif yaşam döngüsü/sağlık sinyali.
// RuleEngine metriği DEĞİL: kaynak sinyal verir, iyileştirme kararını üst
// katman (RecoveryCoordinator deseni) verir. Kaynak kendi kendini iyileştirmez.
// ---------------------------------------------------------------------------
enum class SourceState : uint8_t {
    Uninitialized,  ///< init() henüz çağrılmadı veya shutdown() sonrası
    Running,        ///< normal akış
    NeedsReinit,    ///< kurtarılabilir kayıp (örn. DXGI_ERROR_ACCESS_LOST,
                    ///< null-frame streak eşiği) — shutdown()+init() bekler
    Lost,           ///< kurtarılamaz (cihaz kayıp vb.) — üst katman karar verir
};

// ---------------------------------------------------------------------------
// SourceMetadata — AKIŞ düzeyi tanım.
// Donanım/profil sistemlerinden (GpuScan, CapabilityDetector/RenderProfile,
// HwSignals) bilinçli olarak ayrı: onlar adapter/sistem düzeyi, bu kaynak düzeyi.
// ---------------------------------------------------------------------------
struct SourceMetadata {
    uint32_t width;
    uint32_t height;
    uint32_t format;          ///< ham DXGI_FORMAT değeri
#ifdef _WIN32
    ID3D11Device* device;     ///< karelerin yaşadığı D3D11 cihazı (WGC: kendi
                              ///< cihazı, DXGI: encode-GPU cihazı) — gelecekte
                              ///< kompozisyonun cihaz-kimliği ihtiyacı için
#endif
};

// ---------------------------------------------------------------------------
// ISource — tek bir görüntü kaynağının soyutlaması (ROADMAP Faz 3).
//
// Model kararları (Faz 0 envanterine dayanır):
//  - PULL, senkron: tüketici döngüsü next_frame() çağırır; impl kendi
//    timeout'unu içeride yönetir (bugünkü IScreenCapture akışıyla aynı,
//    hot-path kurallarıyla uyumlu — kaynak başına thread/callback YAGNI).
//  - Konfigürasyon somut tipe aittir (webcam config ≠ monitör config) ve
//    kurucu/factory'de verilir; init() parametresizdir. Jenerik key-value
//    config icat edilmedi (YAGNI).
//  - İki-fazlı yaşam döngüsü: init()/shutdown(). Ayrı start()/stop() YOK —
//    "sahnede yüklü ama pasif kaynak" ihtiyacı sahne düzenleyici turunda
//    doğarsa eklenir. Reinit = shutdown()+init(); ayrı reinit() metodu yok
//    (RecoveryCoordinator'ın mevcut sırası korunur).
//  - Encoder'a referans YOK: kaynaklar kompozit edilip TEK encoder'a gider
//    (kompozit-sonra-encode); encoder config'i kompozit çıktının özelliğidir.
// ---------------------------------------------------------------------------
class ISource {
public:
    virtual ~ISource() = default;

    virtual bool           init()                    = 0;
    virtual void           shutdown()                = 0;
    virtual SourceFrame    next_frame()              = 0;
    virtual SourceMetadata metadata() const          = 0;
    virtual SourceState    state()    const noexcept = 0;
};

} // namespace rj
