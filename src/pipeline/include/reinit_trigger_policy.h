// src/pipeline/include/reinit_trigger_policy.h
//
// Orkestratörün level→edge dönüşümü — ISource::state()'in level semantiğini
// (NeedsReinit null'lar sürdükçe kalır) run_frame'in "bu tikte
// handle_device_lost() çağır" edge kararına çevirir. frame_drop_policy.h gibi
// saf ve header-only: run_frame'e gömülü kalırsa no-op recovery senaryosu
// (cihaz sağlıklı → reinit yok → tracker sıfırlanmaz) test edilemezdi.
//
// Tasarım A (FAZ0_RAPOR_WIRING.md, karar 3 — onaylı): geçişte bir kez tetik +
// NeedsReinit sürdükçe her kRearmPeriodFrames karede yeniden tetik. Cadence,
// CaptureSubsystem::handle_null_frame()'in bugünkü davranışıyla (her 60
// null'da bir deneme) birebir aynıdır — wiring turu davranış-koruyucudur.
// Re-arm'sız salt geçiş tetiği, no-op recovery sonrası bir daha hiç
// ateşlemezdi (ön-kontrol bulgusu). Hot-path güvenli: heap yok, stack POD.
#pragma once

#include "desktop_source_logic.h"  // kNullStreakReinitThreshold
#include "i_source.h"

namespace rj {

// Tek thread (frame thread) varsayımı — NullStreakTracker ile aynı.
class ReinitTriggerPolicy {
public:
    /// Re-arm periyodu null-streak eşiğiyle kilitli: kaynak eşiğe 60 null'da
    /// ulaşır, no-op denemeden sonra 60 karede bir yeniden denenir (bugünkü
    /// handle_null_frame() cadence'ı). Sabitler ayrışırsa test kırılır.
    static constexpr int kRearmPeriodFrames = kNullStreakReinitThreshold;

    /// Her karede kaynak durumuyla beslenir; true = bu tikte recovery tetikle.
    /// NeedsReinit'e geçişte bir kez, NeedsReinit sürdükçe her periyotta bir.
    bool on_state(SourceState s) noexcept {
        const bool needs   = (s == SourceState::NeedsReinit);
        const bool entered = needs && !was_needs_reinit_;
        was_needs_reinit_  = needs;
        if (!needs) {
            frames_since_trigger_ = 0;  // Running/reinit → temiz döngü
            return false;
        }
        if (entered) {
            frames_since_trigger_ = 0;
            return true;
        }
        if (++frames_since_trigger_ >= kRearmPeriodFrames) {
            frames_since_trigger_ = 0;
            return true;
        }
        return false;
    }

private:
    bool was_needs_reinit_    = false;
    int  frames_since_trigger_ = 0;
};

}  // namespace rj
