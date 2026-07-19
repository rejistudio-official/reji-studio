// Donanım profili önerisi — saf karar mantığı (suggest_profile) birim testi.
// Faz 2 / Commit 3 (TDD). profile_advisor.cpp doğrudan derlenir (Qt/pipeline
// gerekmez); yalnız saf fonksiyon test edilir, I/O toplama fonksiyonları değil.
#include <gtest/gtest.h>

#include "profile_advisor.h"

using reji::HwSignals;
using reji::ProfileId;
using reji::suggest_profile;

namespace {
// Güçlü, AC'ye takılı bir masaüstü temeli — her test tek ekseni değiştirir.
HwSignals strong_desktop() {
    HwSignals s;
    s.vendor_id    = 0x10DE;   // NVIDIA
    s.vram_mb      = 12288;    // 12 GB
    s.total_ram_mb = 32768;    // 32 GB
    s.on_battery   = false;
    return s;
}
}  // namespace

TEST(ProfileAdvisor, StrongDesktopOnAcSuggestsPerformance) {
    EXPECT_EQ(suggest_profile(strong_desktop()), ProfileId::Performance);
}

TEST(ProfileAdvisor, OnBatterySuggestsEfficiencyEvenWithStrongGpu) {
    auto s = strong_desktop();
    s.on_battery = true;  // güçlü GPU olsa bile kural 1 (güç) önce gelir
    EXPECT_EQ(suggest_profile(s), ProfileId::Efficiency);
}

TEST(ProfileAdvisor, LowVramOnAcSuggestsStability) {
    auto s = strong_desktop();
    s.vram_mb = 3072;  // 3 GB < 4 GB eşiği
    EXPECT_EQ(suggest_profile(s), ProfileId::Stability);
}

TEST(ProfileAdvisor, LowRamOnAcSuggestsStability) {
    auto s = strong_desktop();
    s.total_ram_mb = 4096;  // 4 GB < 8 GB eşiği (VRAM yüksek olsa da)
    EXPECT_EQ(suggest_profile(s), ProfileId::Stability);
}

TEST(ProfileAdvisor, VramExactlyAtThresholdIsPerformance) {
    auto s = strong_desktop();
    s.vram_mb = 4096;  // tam sınır — '<' kesin, sınırda Performans
    EXPECT_EQ(suggest_profile(s), ProfileId::Performance);
}

TEST(ProfileAdvisor, RamExactlyAtThresholdIsPerformance) {
    auto s = strong_desktop();
    s.total_ram_mb = 8192;  // tam sınır — Performans
    EXPECT_EQ(suggest_profile(s), ProfileId::Performance);
}

TEST(ProfileAdvisor, BatteryTakesPrecedenceOverLowHardware) {
    // Batarya + düşük donanım: kural 1 önce → Efficiency (Stability değil).
    HwSignals s;
    s.vram_mb      = 2048;
    s.total_ram_mb = 4096;
    s.on_battery   = true;
    EXPECT_EQ(suggest_profile(s), ProfileId::Efficiency);
}

// ===== Preset (bitrate/FPS) — Faz 1 onaylanan sayılar kilitlenir =====

TEST(ProfileAdvisor, PresetPerformanceIs12000At60) {
    const auto p = reji::preset_for(ProfileId::Performance);
    EXPECT_EQ(p.bitrate_kbps, 12000u);
    EXPECT_EQ(p.fps, 60u);
}

TEST(ProfileAdvisor, PresetStabilityIs6000At30) {
    const auto p = reji::preset_for(ProfileId::Stability);
    EXPECT_EQ(p.bitrate_kbps, 6000u);
    EXPECT_EQ(p.fps, 30u);
}

TEST(ProfileAdvisor, PresetEfficiencyIs4500At30) {
    const auto p = reji::preset_for(ProfileId::Efficiency);
    EXPECT_EQ(p.bitrate_kbps, 4500u);
    EXPECT_EQ(p.fps, 30u);
}

TEST(ProfileAdvisor, ResourceNamesMatchEmbeddedFiles) {
    // ":/config/profiles/<kök>.json" — qrc gömme adlarıyla birebir eşleşmeli.
    EXPECT_STREQ(reji::profile_resource_name(ProfileId::Performance), "performance");
    EXPECT_STREQ(reji::profile_resource_name(ProfileId::Stability),   "stability");
    EXPECT_STREQ(reji::profile_resource_name(ProfileId::Efficiency),  "efficiency");
}
