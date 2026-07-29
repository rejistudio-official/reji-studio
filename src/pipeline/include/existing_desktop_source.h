// src/pipeline/include/existing_desktop_source.h
//
// ExistingDesktopSource — ISource kontratının ilk implementasyonu: mevcut
// WGC/DXGI capture yolunu (IScreenCapture) ISource'a uyarlayan ince adapter
// (ROADMAP Faz 3; tasarım: docs/talimatlar/TALIMAT_ISOURCE_ARAYUZ_TASARIMI.md).
//
// ISource wiring TAMAMLANDI (docs/TALIMAT_EXISTINGDESKTOPSOURCE_WIRING.md):
// Pipeline::Impl/run_frame() artık bu sınıfı kullanır; CaptureSubsystem
// silindi (davranışı buraya birebir devralındı).
//
// Delegasyon (silinen CaptureSubsystem/pipeline.cpp akışıyla birebir):
//  init()       → IScreenCapture::create() + capture_->init(cfg_)
//  next_frame() → capture_->next_frame() → SourceFrame alan eşlemesi
//                 (desktop_source_logic.h); format DXGI'de surface_format()'tan,
//                 WGC'de frame pool sabitinden; timestamp DXGI'de acquire-anı QPC
//  metadata()   → width/height/format + karelerin yaşadığı D3D11 cihazı
//  state()      → NullStreakTracker (60-kare eşiği) → NeedsReinit sinyali;
//                 kurtarma KARARI orkestratörde kalır (RecoveryCoordinator)
//  shutdown()   → capture_ reset (RAII teardown)
//
// Windows'a özel: yalnızca _WIN32 altında include edilmelidir
// (DxgiCapturePipeline / ID3D11* bağımlılığı).
#pragma once
#include <functional>
#include <memory>
#include <d3d11.h>          // ID3D11Texture2D (WGC staging), ComPtr hedefi
#include <wrl/client.h>     // Microsoft::WRL::ComPtr

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

    // ── Kontrat-dışı WGC preview yüzeyi (wiring Faz 0 karar 1, onaylı) ──
    // CaptureSubsystem'den taşındı; ISource kontratına BİLEREK dahil değil
    // (preview orkestrasyon meselesi). Staging texture'ın yaşam döngüsü
    // capture cihazına bağlı olduğundan kaynakla birlikte yaşar.
    using PreviewCallback = std::function<void(const void* bgra, int width,
                                               int height, int row_pitch)>;

    // CaptureSubsystem::is_wgc() ile birebir: capture var, DXGI cast'i null.
    bool is_wgc() const noexcept { return capture_ != nullptr && dxgi_ == nullptr; }

    // WGC path CPU staging preview: shared texture yoksa GPU→staging kopyala,
    // map'le, preview_cb'ye BGRA gönder. preview_cb orkestratörden geçilir (UI
    // bilgisi yok); frame_w/frame_h WGC CapturedFrame dims. Orkestratör yalnızca
    // WGC+preview_cb varken çağırır (tetikleme kararı orkestratörde kalır).
    void emit_wgc_preview(const PreviewCallback& preview_cb, ID3D11Texture2D* tex,
                          uint32_t frame_w, uint32_t frame_h);

private:
    Config                          cfg_;
    std::unique_ptr<IScreenCapture> capture_;
    std::unique_ptr<IScreenCapture> injected_;      // test seam (init'te tüketilir)
    reji::DxgiCapturePipeline*      dxgi_ = nullptr; // raw cache, capture_'dan cast
    uint32_t                        format_ = 0;     // ham DXGI_FORMAT, init'te sabitlenir
    uint64_t                        qpc_freq_ = 0;   // init'te bir kez sorgulanır
    NullStreakTracker               streak_;         // frame thread; tek thread
    // WGC path CPU staging — ilk preview frame'de lazy oluşturulur, çözünürlük
    // değişiminde reset edilir (emit_wgc_preview içinde yönetilir). shutdown()
    // BİLEREK dokunmaz — CaptureSubsystem davranış paritesi (yalnız yıkımda).
    Microsoft::WRL::ComPtr<ID3D11Texture2D> wgc_staging_tex_;
};

} // namespace rj
