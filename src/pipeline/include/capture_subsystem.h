// src/pipeline/include/capture_subsystem.h
//
// CaptureSubsystem — ekran yakalama alt sistemi (Aşama 8'de Pipeline::Impl'den
// çıkarıldı). IScreenCapture yaşam döngüsünü (WGC tercihli / DXGI fallback),
// frame alımını (hem DXGI capture_next() hem WGC next_frame() dalları) ve
// null-frame streak sayacını sarmalar.
//
// ORKESTRASYON NOTU: next_frame() yalnızca CapturedFrame döndürür — capture
// sonrası orkestrasyon (encode, GpuInterop d3d11_frame_cb, preview çağrıları)
// Impl::run_frame()'de kalır. handle_null_frame() sayacı yönetir ve "reinit
// gerekli mi" bilgisini döndürür; gerçek handle_device_lost() çağrısı
// orkestratörde kalır (recovery Aşama 9'a ait).
//
// Windows'a özel: DxgiCapturePipeline/DxgiScreenCapture ve ID3D11* tipleri
// Windows'a bağlıdır; bu başlık yalnızca _WIN32 altında include edilmelidir.
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <d3d11.h>          // ID3D11Texture2D (WGC staging), ComPtr hedefi
#include <wrl/client.h>     // Microsoft::WRL::ComPtr
#include "i_screen_capture.h"   // rj::IScreenCapture, rj::CapturedFrame

// DxgiCapturePipeline'a yalnızca pointer olarak dokunulur (encode_gpu, shared_texture,
// capture_next, map_preview_frame, gpu_scan çağrıları .cpp/orkestratörde) — forward decl.
namespace reji { class DxgiCapturePipeline; }

namespace rj {

class CaptureSubsystem {
public:
    using Config = rj::IScreenCapture::Config;
    // Pipeline::PreviewCallback ile aynı imza (subsystem UI'ı bilmez — callback
    // orkestratörden parametre olarak geçilir).
    using PreviewCallback = std::function<void(const void* bgra, int width,
                                               int height, int row_pitch)>;

    // IScreenCapture::create() (WGC tercihli, DXGI fallback) + init + dxgi cast.
    // Başarısızlıkta capture_ reset, capture_dxgi_ null, false döner.
    bool init(const Config& cfg);

    // Tek frame al — DXGI path'te capture_next() sonucu handle'a yazılır, WGC
    // path'te next_frame() aynen döner. Geçerli handle'da null-streak sıfırlanır.
    rj::CapturedFrame next_frame();

    // Null-frame streak sayacı: ++streak; eşiğe ulaşırsa reset edip true döner
    // (reinit gerekli). Gerçek handle_device_lost() çağrısı orkestratörde kalır.
    bool handle_null_frame() noexcept;

    // WGC path CPU staging preview: shared texture yoksa GPU→staging kopyala, map'le,
    // preview_cb'ye BGRA gönder. preview_cb orkestratörden geçilir (UI bilgisi yok);
    // frame_w/frame_h WGC CapturedFrame dims. Orkestratör yalnızca WGC+preview_cb
    // varken çağırır (preview tetikleme kararı orkestratörde kalır).
    void emit_wgc_preview(const PreviewCallback& preview_cb, ID3D11Texture2D* tex,
                          uint32_t frame_w, uint32_t frame_h);

    // RAII teardown — capture_ reset + capture_dxgi_ null.
    void shutdown() noexcept;

    bool has_capture() const noexcept { return capture_ != nullptr; }
    ID3D11Device* d3d_device() const noexcept {
        return capture_ ? capture_->d3d_device() : nullptr;
    }
    // DXGI pipeline (encode_gpu / shared_texture / preview map-unmap / gpu_scan
    // erişimi için). WGC path'te nullptr.
    reji::DxgiCapturePipeline* dxgi() const noexcept { return capture_dxgi_; }
    bool is_wgc() const noexcept { return capture_ != nullptr && capture_dxgi_ == nullptr; }
    uint32_t width()  const noexcept { return capture_ ? capture_->width()  : 0; }
    uint32_t height() const noexcept { return capture_ ? capture_->height() : 0; }

private:
    // 60 ardışık null frame → capture kaybı kabul edilip reinit tetiklenir.
    static constexpr int kNullStreakReinit = 60;

    std::unique_ptr<rj::IScreenCapture>     capture_;
    reji::DxgiCapturePipeline*              capture_dxgi_ = nullptr;  // raw cache, capture_'dan cast
    int                                     null_streak_  = 0;        // frame thread; tek thread
    // WGC path CPU staging — ilk preview frame'de lazy oluşturulur, çözünürlük
    // değişiminde reset edilir (emit_wgc_preview içinde yönetilir).
    Microsoft::WRL::ComPtr<ID3D11Texture2D> wgc_staging_tex_;
};

} // namespace rj
