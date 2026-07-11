// V8/I10 — paylaşımlı SEH filtresinin karar mantığının birim testi.
//
// Gerçek AV/stack-overflow tetiklemek CI'ı çökertirdi; onun yerine filtrenin
// SAF karar seam'lerini doğrularız:
//   * seh_is_passthrough(code) — hangi kodlar yukarı iletilir (yutulmaz).
//   * seh_register_av(site, now_ms) — site-başı pencere/eşik sayacı; now_ms
//     enjekte edilir, gerçek zamana/fault'a ihtiyaç yok.
//
// seh_filter.cpp doğrudan bu hedefe derlenir (reji_pipeline/Rust link gerekmez;
// dosya yalnız windows.h'e bağlı, kendi içinde __try içermez).
#include "seh_filter.h"

#include <gtest/gtest.h>

#include <cstdint>

using rj::SehSite;
using rj::seh_is_passthrough;
using rj::seh_register_av;

// GetTickCount64 boot'tan beri ms döndürür (büyük değer); ilk AV her zaman taze
// pencere açar. Testler bu semantiği büyük zaman damgalarıyla yansıtır.
namespace {
constexpr uint64_t kBase = 1'000'000;  // ~16.6 dk (gerçekçi tick)
}

// ── seh_is_passthrough: ciddi/kurtarılamaz kodlar yukarı iletilir ──────────
TEST(SehIsPassthrough, SeriousCodesPropagate) {
    EXPECT_TRUE(seh_is_passthrough(EXCEPTION_STACK_OVERFLOW));
    EXPECT_TRUE(seh_is_passthrough(EXCEPTION_BREAKPOINT));
    EXPECT_TRUE(seh_is_passthrough(EXCEPTION_SINGLE_STEP));
}

TEST(SehIsPassthrough, CatchableCodesAreNotPassthrough) {
    EXPECT_FALSE(seh_is_passthrough(EXCEPTION_ACCESS_VIOLATION));
    EXPECT_FALSE(seh_is_passthrough(EXCEPTION_ILLEGAL_INSTRUCTION));
    EXPECT_FALSE(seh_is_passthrough(EXCEPTION_INT_DIVIDE_BY_ZERO));
    EXPECT_FALSE(seh_is_passthrough(0));
}

// ── seh_register_av: pencere/eşik valfi ────────────────────────────────────
// Her test AYRI site kullanır — statik sayaç dizileri süreç boyunca kalıcıdır.

TEST(SehRegisterAv, SingleAvDoesNotEscalate) {
    EXPECT_FALSE(seh_register_av(SehSite::CmdDrain, kBase));
}

TEST(SehRegisterAv, ThirdAvInWindowEscalates) {
    EXPECT_FALSE(seh_register_av(SehSite::WsDequeue, kBase));            // 1
    EXPECT_FALSE(seh_register_av(SehSite::WsDequeue, kBase + 10'000));  // 2
    EXPECT_TRUE(seh_register_av(SehSite::WsDequeue, kBase + 20'000));   // 3 → eskale
}

TEST(SehRegisterAv, WindowExpiryResetsCount) {
    EXPECT_FALSE(seh_register_av(SehSite::GetWsPort, kBase));                  // 1
    EXPECT_FALSE(seh_register_av(SehSite::GetWsPort, kBase + 10'000));        // 2
    // Pencere (60 sn) aşıldı → sayaç sıfırlanır, yeni pencerede 1. AV.
    EXPECT_FALSE(seh_register_av(SehSite::GetWsPort, kBase + 70'001));        // reset → 1
    EXPECT_FALSE(seh_register_av(SehSite::GetWsPort, kBase + 80'000));        // 2
}

TEST(SehRegisterAv, DistinctSitesAreIndependent) {
    // Bir sitede eşik aşımı, başka sitenin sayacını etkilemez.
    EXPECT_FALSE(seh_register_av(SehSite::MetricsPush, kBase));
    EXPECT_FALSE(seh_register_av(SehSite::MetricsPush, kBase + 1'000));
    EXPECT_TRUE(seh_register_av(SehSite::MetricsPush, kBase + 2'000));   // MetricsPush eskale
    // SrtSend hâlâ temiz:
    EXPECT_FALSE(seh_register_av(SehSite::SrtSend, kBase + 2'000));      // SrtSend 1. AV
}

TEST(SehRegisterAv, ThresholdBoundaryIsExactlyThree) {
    // kSehAvThreshold == 3 sözleşmesini sabitler.
    EXPECT_EQ(rj::kSehAvThreshold, 3u);
    EXPECT_FALSE(seh_register_av(SehSite::ConnectionLost, kBase));           // 1
    EXPECT_FALSE(seh_register_av(SehSite::ConnectionLost, kBase + 100));     // 2
    EXPECT_TRUE(seh_register_av(SehSite::ConnectionLost, kBase + 200));      // 3
}
