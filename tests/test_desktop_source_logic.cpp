// tests/test_desktop_source_logic.cpp
//
// ExistingDesktopSource saf çekirdek testleri (desktop_source_logic.h).
//
// İki saf parça test edilir (D3D11/GPU gerekmez, sentetik veri):
//  1) map_captured_frame() — CapturedFrame → SourceFrame alan eşlemesi
//     (handle/type/dims aynen, format kaynak-düzeyi sabitten, timestamp
//     capture doldurmadıysa fallback QPC değerinden).
//  2) NullStreakTracker — null-frame streak → NeedsReinit eşiği.
//     CaptureSubsystem::handle_null_frame()'deki 60-kare davranışının
//     ISource::state() karşılığı; eşik sabiti birebir kilitlenir.
//
// Kurtarma KARARI burada test edilmez — o orkestratörde kalır
// (RecoveryCoordinator deseni, bkz. i_source.h SourceState yorumu).

#include <gtest/gtest.h>

#include "desktop_source_logic.h"
#include "reinit_trigger_policy.h"

#include <cstdint>

namespace {

rj::CapturedFrame make_frame(void* handle, uint32_t w, uint32_t h,
                             uint64_t ts_us) {
    rj::CapturedFrame f{};
    f.type         = rj::CapturedFrame::HandleType::D3D11;
    f.handle       = handle;
    f.width        = w;
    f.height       = h;
    f.timestamp_us = ts_us;
    return f;
}

}  // namespace

// ── map_captured_frame ──────────────────────────────────────────────────────

TEST(MapCapturedFrame, CopiesHandleTypeAndDimensions) {
    int dummy = 0;
    const auto in = make_frame(&dummy, 1920, 1080, 12345);

    const rj::SourceFrame out =
        rj::map_captured_frame(in, rj::kWgcFramePoolFormat, /*fallback=*/999);

    EXPECT_EQ(out.handle, &dummy);
    EXPECT_EQ(out.type, rj::SourceFrame::HandleType::D3D11);
    EXPECT_EQ(out.width, 1920u);
    EXPECT_EQ(out.height, 1080u);
}

TEST(MapCapturedFrame, SetsFormatFromSourceLevelConstant) {
    int dummy = 0;
    const auto in = make_frame(&dummy, 640, 480, 1);

    // DXGI yolunda surface_format() buradan geçer — jenerik bir değerle doğrula.
    constexpr uint32_t kDxgiSurfaceFormat = 28;  // örn. R8G8B8A8_UNORM
    const rj::SourceFrame out =
        rj::map_captured_frame(in, kDxgiSurfaceFormat, /*fallback=*/0);

    EXPECT_EQ(out.format, kDxgiSurfaceFormat);
}

TEST(MapCapturedFrame, PreservesCaptureTimestampWhenPresent) {
    int dummy = 0;
    const auto in = make_frame(&dummy, 640, 480, /*ts_us=*/777'000);

    const rj::SourceFrame out =
        rj::map_captured_frame(in, rj::kWgcFramePoolFormat, /*fallback=*/111);

    // WGC yolu: SystemRelativeTime'dan gelen değer aynen korunur.
    EXPECT_EQ(out.timestamp_us, 777'000u);
}

TEST(MapCapturedFrame, FillsFallbackTimestampWhenCaptureLeftItZero) {
    int dummy = 0;
    const auto in = make_frame(&dummy, 640, 480, /*ts_us=*/0);

    const rj::SourceFrame out =
        rj::map_captured_frame(in, rj::kWgcFramePoolFormat, /*fallback=*/424242);

    // DXGI yolu: DxgiScreenCapture::next_frame() timestamp doldurmaz (0 kalır)
    // → acquire-anı QPC fallback'i kullanılır.
    EXPECT_EQ(out.timestamp_us, 424242u);
}

TEST(MapCapturedFrame, NullHandlePassesThroughAsNoNewFrame) {
    const auto in = make_frame(nullptr, 0, 0, 0);

    const rj::SourceFrame out =
        rj::map_captured_frame(in, rj::kWgcFramePoolFormat, /*fallback=*/5);

    EXPECT_EQ(out.handle, nullptr);  // null = bu tikte yeni kare yok
}

// ── NullStreakTracker ───────────────────────────────────────────────────────

TEST(NullStreakTracker, ThresholdMatchesCaptureSubsystemConstant) {
    // Davranış birebirliği: silinen CaptureSubsystem::kNullStreakReinit == 60
    // wiring'de buraya devralındı. Sabit değişirse eski davranışla parite
    // (ve re-arm cadence'ı) sessizce bozulur — bilinçli kilit.
    EXPECT_EQ(rj::kNullStreakReinitThreshold, 60);
}

TEST(NullStreakTracker, StaysRunningBelowThreshold) {
    rj::NullStreakTracker t;
    for (int i = 0; i < rj::kNullStreakReinitThreshold - 1; ++i)
        t.on_frame(/*has_frame=*/false);
    EXPECT_FALSE(t.needs_reinit());
}

TEST(NullStreakTracker, SignalsNeedsReinitAtThreshold) {
    rj::NullStreakTracker t;
    for (int i = 0; i < rj::kNullStreakReinitThreshold; ++i)
        t.on_frame(/*has_frame=*/false);
    EXPECT_TRUE(t.needs_reinit());
}

TEST(NullStreakTracker, RemainsSignaledWhileNullsContinue) {
    rj::NullStreakTracker t;
    for (int i = 0; i < rj::kNullStreakReinitThreshold + 10; ++i)
        t.on_frame(/*has_frame=*/false);
    EXPECT_TRUE(t.needs_reinit());
}

TEST(NullStreakTracker, ValidFrameResetsStreak) {
    rj::NullStreakTracker t;
    for (int i = 0; i < rj::kNullStreakReinitThreshold - 1; ++i)
        t.on_frame(/*has_frame=*/false);
    t.on_frame(/*has_frame=*/true);   // geçerli kare → streak sıfırlanır
    t.on_frame(/*has_frame=*/false);  // tek null yeniden eşiğe götürmez
    EXPECT_FALSE(t.needs_reinit());
}

TEST(NullStreakTracker, ResetClearsSignal) {
    rj::NullStreakTracker t;
    for (int i = 0; i < rj::kNullStreakReinitThreshold; ++i)
        t.on_frame(/*has_frame=*/false);
    ASSERT_TRUE(t.needs_reinit());
    t.reset();  // shutdown()+init() (reinit) sonrası temiz başlangıç
    EXPECT_FALSE(t.needs_reinit());
}

// ── ReinitTriggerPolicy ─────────────────────────────────────────────────────
// Orkestratörün level→edge dönüşümü (wiring Faz 0, karar 3 — tasarım A):
// state() NeedsReinit'e GEÇTİĞİNDE bir kez tetikler; NeedsReinit SÜRDÜKÇE her
// kNullStreakReinitThreshold karede yeniden tetikler (re-arm). Bugünkü
// handle_null_frame() cadence'ı (her 60 null'da bir deneme) birebir korunur.
// Re-arm'ın varlık nedeni: handle_device_lost() cihaz sağlıklıyken no-op
// false döner → shutdown()+init() olmaz → tracker sıfırlanmaz; salt
// geçiş-bazlı tetik bir daha hiç ateşlemezdi (bkz. FAZ0_RAPOR_WIRING.md).

TEST(ReinitTriggerPolicy, RearmPeriodLockedToNullStreakThreshold) {
    // Cadence birebirliği eşik sabitine bağlı — sabit ayrışırsa test kırılsın.
    EXPECT_EQ(rj::ReinitTriggerPolicy::kRearmPeriodFrames,
              rj::kNullStreakReinitThreshold);
}

TEST(ReinitTriggerPolicy, NoTriggerWhileRunningOrUninitialized) {
    rj::ReinitTriggerPolicy p;
    EXPECT_FALSE(p.on_state(rj::SourceState::Uninitialized));
    for (int i = 0; i < 100; ++i)
        EXPECT_FALSE(p.on_state(rj::SourceState::Running));
}

TEST(ReinitTriggerPolicy, TriggersOnceOnTransitionToNeedsReinit) {
    rj::ReinitTriggerPolicy p;
    p.on_state(rj::SourceState::Running);
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));   // geçiş → tetik
    EXPECT_FALSE(p.on_state(rj::SourceState::NeedsReinit));  // hemen tekrar YOK
}

TEST(ReinitTriggerPolicy, FirstObservedStateNeedsReinitAlsoTriggers) {
    // Savunmacı: Running hiç görülmeden NeedsReinit gelirse de geçiş sayılır.
    rj::ReinitTriggerPolicy p;
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));
}

TEST(ReinitTriggerPolicy, RearmsEveryPeriodWhileNeedsReinitPersists) {
    // No-op deneme senaryosu (statik ekran / cihaz sağlıklı): reinit
    // gerçekleşmez, state NeedsReinit'te kalır → her-tik fırtınası OLMAMALI,
    // yalnız her 60 karede bir yeniden deneme (bugünkü davranışla aynı).
    rj::ReinitTriggerPolicy p;
    p.on_state(rj::SourceState::Running);
    ASSERT_TRUE(p.on_state(rj::SourceState::NeedsReinit));   // kare 60 (geçiş)

    for (int i = 0; i < rj::ReinitTriggerPolicy::kRearmPeriodFrames - 1; ++i)
        EXPECT_FALSE(p.on_state(rj::SourceState::NeedsReinit));
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));   // kare 120 (re-arm)

    for (int i = 0; i < rj::ReinitTriggerPolicy::kRearmPeriodFrames - 1; ++i)
        EXPECT_FALSE(p.on_state(rj::SourceState::NeedsReinit));
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));   // kare 180 (re-arm)
}

TEST(ReinitTriggerPolicy, ValidFrameClearsAndNextTransitionTriggersAgain) {
    // Geçerli kare → kaynak Running'e döner (tracker kendiliğinden temizlenir);
    // sonraki gerçek eşik aşımı yeni bir geçiştir ve yeniden tetikler.
    rj::ReinitTriggerPolicy p;
    p.on_state(rj::SourceState::Running);
    ASSERT_TRUE(p.on_state(rj::SourceState::NeedsReinit));
    EXPECT_FALSE(p.on_state(rj::SourceState::Running));
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));
}

TEST(ReinitTriggerPolicy, SuccessfulReinitStartsCleanCycle) {
    // Gerçek reinit: shutdown()+init() → state Running'e döner (tracker
    // reset). Politika da temiz başlar; yarım kalmış re-arm sayacı taşınmaz.
    rj::ReinitTriggerPolicy p;
    p.on_state(rj::SourceState::Running);
    ASSERT_TRUE(p.on_state(rj::SourceState::NeedsReinit));
    // Re-arm periyodunun ortasındayken reinit başarılı olsun:
    for (int i = 0; i < 30; ++i)
        p.on_state(rj::SourceState::NeedsReinit);
    p.on_state(rj::SourceState::Running);                    // reinit sonrası
    ASSERT_TRUE(p.on_state(rj::SourceState::NeedsReinit));   // yeni geçiş → tetik
    // Yeni periyot tam 60 kare olmalı (eski sayaçtan artık taşınmamalı):
    for (int i = 0; i < rj::ReinitTriggerPolicy::kRearmPeriodFrames - 1; ++i)
        EXPECT_FALSE(p.on_state(rj::SourceState::NeedsReinit));
    EXPECT_TRUE(p.on_state(rj::SourceState::NeedsReinit));
}
