#pragma once
#include <cstdint>

// Frame-drop sayacı politikasının saf (yan-etkisiz) parçası.
// run_frame'e gömülü kalırsa yalnız karakterizasyon testinin varsayılan
// senaryosuyla dolaylı örtülür; null-frame dalının semantiği sessizce
// regresyona uğrayabilir (bkz. bitrate_policy.h / *_for_sample deseni).
namespace rj {

/// run_frame'in tek kare sonucu — frame_drops sayacını besleme kararı için.
/// Gönderim hatası burada YOK: on_packet'te, streaming'e kapılı ayrı sayılır.
enum class FrameOutcome : uint8_t {
    Encoded,       ///< geçerli kare alındı ve encode edildi
    EncodeFailed,  ///< geçerli kare alındı ama encode başarısız
    NoNewFrame,    ///< capture yeni kare döndürmedi (null — durağan ekranda normal)
};

/// Bu kare sonucu frame_drops sayacına yazılır mı?
///
/// NoNewFrame drop DEĞİLDİR: durağan ekranda DXGI AcquireNextFrame timeout /
/// WGC boş dönüş normal pacing'dir. Drop sayılınca boşta (START'sız) sayaç
/// FrameDropped event'leriyle predictive healing'i besliyor, encoder gerçekten
/// 3500'e düşüyor ve oluşan (3500, original) açığı recovery banner döngüsünü
/// tetikliyordu (S1-ek4 kök neden). Capture-loss tespiti bu sayaçtan bağımsız
/// (CaptureSubsystem null-streak sayacı, eşik 60).
inline bool counts_as_frame_drop(FrameOutcome outcome) {
    return outcome == FrameOutcome::EncodeFailed;
}

} // namespace rj
