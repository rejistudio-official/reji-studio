// src/pipeline/existing_desktop_source.cpp
//
// ExistingDesktopSource implementasyonu. Windows'a özel (yalnızca WIN32
// altında derlenir). Davranış, silinen CaptureSubsystem::init / next_frame /
// handle_null_frame / shutdown akışından birebir devralındı; saf çekirdek
// (alan eşlemesi + streak) desktop_source_logic.h'de test edilir.
#include "existing_desktop_source.h"

#include <cstdio>                  // fprintf (WGC staging tanı logu)
#include <windows.h>               // QueryPerformanceCounter/Frequency

#include "capture_dxgi_screen.h"   // reji::DxgiScreenCapture (dynamic_cast + pipeline())
#include "capture_dxgi.h"          // reji::DxgiCapturePipeline (surface_format, encode_gpu)

namespace rj {

namespace {

// Acquire-anı QPC → mikrosaniye. Bölme-önce-çarpma overflow'suz
// (metrics_subsystem ticks_to_us kalıbı).
uint64_t qpc_now_us(uint64_t freq) noexcept {
    if (freq == 0) return 0;
    LARGE_INTEGER c{};
    QueryPerformanceCounter(&c);
    const uint64_t ticks = static_cast<uint64_t>(c.QuadPart);
    return (ticks / freq) * 1'000'000ULL + (ticks % freq) * 1'000'000ULL / freq;
}

} // namespace

ExistingDesktopSource::ExistingDesktopSource(const Config& cfg) : cfg_(cfg) {}

ExistingDesktopSource::ExistingDesktopSource(
    const Config& cfg, std::unique_ptr<IScreenCapture> capture_for_test)
    : cfg_(cfg), injected_(std::move(capture_for_test)) {}

ExistingDesktopSource::~ExistingDesktopSource() { shutdown(); }

bool ExistingDesktopSource::init() {
    capture_ = injected_ ? std::move(injected_) : IScreenCapture::create();
    dxgi_    = nullptr;
    if (!capture_) {
        return false;
    }
    if (!capture_->init(cfg_)) {
        capture_.reset();
        return false;
    }
    // Typed DXGI pipeline cache (CaptureSubsystem::init kalıbı; WGC'de null).
    auto* dsc = dynamic_cast<reji::DxgiScreenCapture*>(capture_.get());
    dxgi_ = dsc ? dsc->pipeline() : nullptr;

    // Format kaynak-düzeyi sabittir: DXGI'de duplication yüzeyinden, WGC'de
    // frame pool kuruluş formatından (capture_wgc.cpp B8G8R8A8).
    format_ = dxgi_ ? static_cast<uint32_t>(dxgi_->surface_format())
                    : kWgcFramePoolFormat;

    LARGE_INTEGER f{};
    qpc_freq_ = QueryPerformanceFrequency(&f) ? static_cast<uint64_t>(f.QuadPart) : 0;

    streak_.reset();
    return true;
}

SourceFrame ExistingDesktopSource::next_frame() {
    if (!capture_) return SourceFrame{};
    // DXGI yolunda DxgiScreenCapture::next_frame() capture_next()'i sarar ve
    // dims doldurur; WGC aynen döner — CaptureSubsystem'in iki dalıyla eşdeğer.
    const CapturedFrame frame = capture_->next_frame();
    streak_.on_frame(frame.handle != nullptr);
    return map_captured_frame(frame, format_, qpc_now_us(qpc_freq_));
}

SourceMetadata ExistingDesktopSource::metadata() const {
    SourceMetadata md{};
    if (!capture_) return md;
    md.width  = capture_->width();
    md.height = capture_->height();
    md.format = format_;
    // Karelerin yaşadığı cihaz (i_source.h): WGC kendi cihazı (d3d_device
    // override'ı), DXGI encode-GPU cihazı — DxgiScreenCapture d3d_device()
    // override etmediği için encode_gpu() üzerinden alınır (pipeline.cpp
    // encode_device seçimiyle aynı sıra).
    md.device = (dxgi_ && dxgi_->encode_gpu()) ? dxgi_->encode_gpu()->d3d_device()
                                               : capture_->d3d_device();
    return md;
}

SourceState ExistingDesktopSource::state() const noexcept {
    if (!capture_) return SourceState::Uninitialized;
    return streak_.needs_reinit() ? SourceState::NeedsReinit
                                  : SourceState::Running;
}

bool ExistingDesktopSource::emit_wgc_preview(const PreviewCallback& preview_cb,
                                             ID3D11Texture2D* tex,
                                             uint32_t frame_w, uint32_t frame_h) {
    // RTMP_DARBOGAZ Faz 3(c): 2-slot staging ring + DO_NOT_WAIT try-map.
    // Eski akış (tek staging + bloklu Map) GPU kopyasını frame thread'inde
    // bekliyordu (~11-14ms, [SendDiag] prev). Yeni akış: bu kareyi yazma
    // slotuna kopyala (yalnız submit), BİR ÖNCEKİ karenin slotunu beklemesiz
    // map'le — kopya ~16ms önce submit edildiğinden normalde hazırdır; değilse
    // kare atlanır (false → çağıran prev_miss sayar). Preview 1 kare geriden
    // gelir; encode yolu etkilenmez.
    D3D11_TEXTURE2D_DESC current{};
    tex->GetDesc(&current);

    // Çözünürlük değişimi: her iki slot da geçersiz — bayat boyutlu map önlenir
    for (auto& slot : wgc_staging_) {
        if (!slot) continue;
        D3D11_TEXTURE2D_DESC existing{};
        slot->GetDesc(&existing);
        if (existing.Width != current.Width || existing.Height != current.Height) {
            wgc_staging_[0].Reset();
            wgc_staging_[1].Reset();
            wgc_staging_valid_[0] = wgc_staging_valid_[1] = false;
            break;
        }
    }
    // NVIDIA device'da staging slotlarını oluştur (lazy, bir kez)
    if (!wgc_staging_[0] || !wgc_staging_[1]) {
        D3D11_TEXTURE2D_DESC desc = current;
        desc.Usage          = D3D11_USAGE_STAGING;
        desc.BindFlags      = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags      = 0;
        ID3D11Device* dev = nullptr;
        tex->GetDevice(&dev);
        if (dev) {
            for (auto& slot : wgc_staging_)
                if (!slot) dev->CreateTexture2D(&desc, nullptr, &slot);
            dev->Release();
        }
        if (!wgc_staging_[0] || !wgc_staging_[1]) return false;
    }

    bool emitted = false;
    ID3D11Device* dev = nullptr;
    tex->GetDevice(&dev);
    if (dev) {
        ID3D11DeviceContext* ctx = nullptr;
        dev->GetImmediateContext(&ctx);

        // 1) Bu kare → yazma slotu (submit; tamamlanması BEKLENMEZ)
        const uint32_t w = wgc_write_idx_;
        ctx->CopyResource(wgc_staging_[w].Get(), tex);
        wgc_staging_valid_[w] = true;

        // 2) Önceki karenin slotu → beklemesiz map dene. Hazır değilse
        //    DXGI_ERROR_WAS_STILL_DRAWING döner — kare atlanır, bloklama yok.
        const uint32_t r = w ^ 1u;
        if (wgc_staging_valid_[r]) {
            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (SUCCEEDED(ctx->Map(wgc_staging_[r].Get(), 0, D3D11_MAP_READ,
                                   D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped))) {
                static int wgc_prev_cnt = 0;
                if (++wgc_prev_cnt <= 3)
                    fprintf(stderr, "[WgcStaging] preview frame #%d %ux%u pitch=%u\n",
                            wgc_prev_cnt, frame_w, frame_h,
                            (unsigned)mapped.RowPitch);
                preview_cb(mapped.pData,
                           static_cast<int>(frame_w),
                           static_cast<int>(frame_h),
                           static_cast<int>(mapped.RowPitch));
                ctx->Unmap(wgc_staging_[r].Get(), 0);
                emitted = true;
            }
        }
        wgc_write_idx_ = r;  // slotları takas et
        ctx->Release();
        dev->Release();
    }
    return emitted;
}

void ExistingDesktopSource::shutdown() {
    // RAII teardown — CaptureSubsystem::shutdown ile aynı model.
    // wgc_staging_[] BİLEREK reset edilmez (parite): yalnız çözünürlük
    // değişiminde (emit_wgc_preview) veya yıkımda serbest bırakılır.
    dxgi_ = nullptr;
    capture_.reset();
}

} // namespace rj
