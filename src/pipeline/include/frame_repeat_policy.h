// src/pipeline/include/frame_repeat_policy.h
//
// VFR/CFR Faz 2: kare tekrarı + zaman bazlı yedek IDR saf karar çekirdeği.
//
// Bağlam: WGC yalnız değişen kareyi teslim eder → null tick'lerde encoder'a
// hiçbir şey gitmiyordu (VFR). Platformlar CFR varsayar (Twitch/YouTube 2-4 sn
// keyframe kadansı); tam statik ekranda ses de video callback'ine bağlı
// olduğundan RTMP'ye hiç veri akmıyordu (BULGULAR_VFR_CFR_FAZ0.md A4/A5).
//
// Kararlar (TASARIM_VFR_CFR_FAZ1.md):
// - K2: her null tick'te tekrar — eşik/üst sınır yok; N-sınırı çözülen hatayı
//   sınırın ötesinde aynen geri getirirdi. NeedsReinit'te de tekrar sürer
//   (yayın kopmasın); bu yüzden karar source state'i BİLMEZ.
// - K3: yedek IDR — gopLength birincil, bu katman yalnız gerçekleşen keyframe
//   kadansı >= 2 sn gecikince devreye girer. Normal akışta hiç tetiklenmez.
//
// Saf/test-edilebilir: yalnız <cstdint>; pts'ler çağırandan (pacer epoch, µs).
// Tek thread (frame thread) varsayımı — kilit yok.
#pragma once
#include <cstdint>

namespace rj {

// Tekrar kararı: kopya mevcut × bu tick null. Kopya önkoşulu zorunlu — WGC
// null döndürmeden ÖNCE canlı texture'ı bırakır (capture_wgc.cpp:171), yani
// kopyasızken tekrar edilecek kare fiziksel olarak yoktur.
constexpr bool should_repeat_frame(bool have_copy, bool is_null_tick) noexcept {
    return have_copy && is_null_tick;
}

inline constexpr int64_t kDefaultIdrIntervalUs = 2'000'000;  // platform sınırı: 2 sn

// Zaman bazlı yedek IDR kadansı. Epoch GERÇEKLEŞEN keyframe'lerle ölçülür
// (on_packet is_keyframe) — isteklere göre değil; istek kaybolursa da kadans
// doğru kalır. Tetikledikten sonra keyframe görülene dek yeniden tetiklemez
// (request_idr atomic'ini her tick sahte beslemek IDR fırtınası olurdu).
class IdrCadence {
public:
    explicit IdrCadence(int64_t interval_us = kDefaultIdrIntervalUs) noexcept
        : interval_us_(interval_us) {}

    // Gerçek keyframe gözlendi — epoch yenile, bekleyen isteği temizle.
    void on_keyframe(int64_t pts_us) noexcept {
        last_keyframe_pts_us_ = pts_us;
        have_epoch_ = true;
        pending_    = false;
    }

    // Bu tick'te yedek IDR istenmeli mi? true döndürdüğünde istek "bekliyor"
    // sayılır; sonraki on_keyframe()'e kadar tekrar true dönmez.
    bool should_force_idr(int64_t now_pts_us) noexcept {
        if (!have_epoch_ || pending_) return false;
        if (now_pts_us - last_keyframe_pts_us_ < interval_us_) return false;
        pending_ = true;
        return true;
    }

private:
    int64_t interval_us_;
    int64_t last_keyframe_pts_us_ = 0;
    bool    have_epoch_ = false;  // start_stream IDR'ı ilk epoch'u kurar
    bool    pending_    = false;
};

} // namespace rj
