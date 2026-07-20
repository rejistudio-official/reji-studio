// src/pipeline/include/existing_desktop_source.h
//
// ExistingDesktopSource — ISource kontratının ilk implementasyonu: mevcut
// WGC/DXGI capture yolunu (IScreenCapture) ISource'a uyarlayan ince adapter
// (ROADMAP Faz 3; tasarım: docs/talimatlar/TALIMAT_ISOURCE_ARAYUZ_TASARIMI.md).
//
// BU TURDA PIPELINE'A BAĞLI DEĞİL: sınıf izole yaşar, run_frame() hâlâ
// CaptureSubsystem kullanır. Gerçek wiring ayrı talimatın işi
// (docs/TALIMAT_EXISTINGDESKTOPSOURCE_WIRING.md) — davranış değişikliği yok
// güvencesi bu ayrımla korunur.
//
// Delegasyon (CaptureSubsystem/pipeline.cpp'deki bugünkü akışla birebir):
//  init()       → IScreenCapture::create() + capture_->init(cfg_)
//  next_frame() → capture_->next_frame() → SourceFrame alan eşlemesi
//                 (desktop_source_logic.h); format DXGI'de surface_format()'tan,
//                 WGC'de frame pool sabitinden; timestamp DXGI'de acquire-anı QPC
//  metadata()   → width/height/format + karelerin yaşadığı D3D11 cihazı
//  state()      → NullStreakTracker (60-kare eşiği) → NeedsReinit sinyali;
//                 kurtarma KARARI orkestratörde kalır (RecoveryCoordinator)
//  shutdown()   → capture_ reset (RAII teardown)
//
// Windows'a özel: CaptureSubsystem gibi yalnızca _WIN32 altında include
// edilmelidir (DxgiCapturePipeline / ID3D11* bağımlılığı).
#pragma once
#include <memory>

#include "desktop_source_logic.h"
#include "i_screen_capture.h"
#include "i_source.h"

// DXGI pipeline'a yalnızca pointer olarak dokunulur — forward decl
// (CaptureSubsystem'deki kalıp).
namespace reji { class DxgiCapturePipeline; }

namespace rj {

class ExistingDesktopSource : public ISource {
public:
    using Config = IScreenCapture::Config;

    explicit ExistingDesktopSource(const Config& cfg);

    // Test seam: IScreenCapture::create() yerine enjekte edilmiş capture ile
    // init edilir (yalnız ilk init(); reinit gerçek factory'ye döner).
    ExistingDesktopSource(const Config& cfg,
                          std::unique_ptr<IScreenCapture> capture_for_test);

    ~ExistingDesktopSource() override;

    ExistingDesktopSource(const ExistingDesktopSource&)            = delete;
    ExistingDesktopSource& operator=(const ExistingDesktopSource&) = delete;

    bool           init() override;
    void           shutdown() override;
    SourceFrame    next_frame() override;
    SourceMetadata metadata() const override;
    SourceState    state() const noexcept override;

    // Geçiş dönemi kaçış kapısı — ISource kontratına BİLEREK dahil değil.
    // Tek-kaynak DXGI'ye özgü erişimler (shared_texture, map_preview_frame,
    // gpu_scan…) için; kapatmak gerçek kompozisyon turunun işi. WGC'de nullptr.
    reji::DxgiCapturePipeline* dxgi() const noexcept { return dxgi_; }

private:
    Config                          cfg_;
    std::unique_ptr<IScreenCapture> capture_;
    std::unique_ptr<IScreenCapture> injected_;      // test seam (init'te tüketilir)
    reji::DxgiCapturePipeline*      dxgi_ = nullptr; // raw cache, capture_'dan cast
    uint32_t                        format_ = 0;     // ham DXGI_FORMAT, init'te sabitlenir
    uint64_t                        qpc_freq_ = 0;   // init'te bir kez sorgulanır
    NullStreakTracker               streak_;         // frame thread; tek thread
};

} // namespace rj
