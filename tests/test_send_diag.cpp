// tests/test_send_diag.cpp
//
// RTMP_DARBOGAZ Faz 1: SendDiag saf 1Hz agregatör testi.
//
// Canlı bulgu: frame thread capture→encode→RTMP_Write→audio-drain'i senkron
// yaptığından RTMP_Write bloklaması WGC frame pool kayıplarına dönüşüyor, hiçbir
// sayaçta görünmüyordu (ffmpeg ~20fps ölçerken iç metrik ~59 "fps" — döngü
// temposu). SendDiag bu boşluğu kapatır: per-send süreler + null/tex oranı +
// hatta gerçekten yazılan kare sayısı (wire-fps), saniyede bir satır.
//
// Bu test agregasyon sözleşmesini kilitler: pencere 1sn dolmadan flush yok;
// flush ort/max/adetleri doğru üretir ve pencereyi sıfırlar; wire-fps yalnız
// BAŞARILI video send'leri sayar; null iterasyonlar ayrı sayılır.
#include <gtest/gtest.h>

#include "send_diag.h"

using rj::SendDiag;
using rj::SendDiagStats;

namespace {
constexpr uint64_t kSecUs = 1'000'000;
}

TEST(SendDiagTest, NoFlushBeforeWindowElapses) {
    // Arrange
    SendDiag d;
    SendDiagStats s{};
    d.begin_window(0);
    d.record_capture(100, true);
    d.record_send_video(500, true);

    // Act + Assert — 1sn dolmadan flush yok
    EXPECT_FALSE(d.maybe_flush(kSecUs - 1, &s));
}

TEST(SendDiagTest, FlushAggregatesAvgMaxAndCounts) {
    // Arrange — bilinen değerlerle bir pencere doldur
    SendDiag d;
    d.begin_window(0);
    d.record_capture(100, true);
    d.record_capture(300, true);
    d.record_capture(200, false);   // null iterasyon — süre yine toplanır
    d.record_encode(1000);
    d.record_encode(3000);
    d.record_send_video(10'000, true);
    d.record_send_video(30'000, true);
    d.record_send_video(50'000, false);  // başarısız — wire-fps'e girmez
    d.record_audio_drain(2000);

    // Act
    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));

    // Assert — Faz 2: cap yalnız tex'li iterasyonları, capn null'ları toplar
    // (null'lar TryGetNextFrame poll maliyetini ayrı gösterir; karışım avg'ı
    // sulandırıyordu).
    EXPECT_EQ(s.cap_avg_us, 200u);       // (100+300)/2
    EXPECT_EQ(s.cap_max_us, 300u);
    EXPECT_EQ(s.capn_avg_us, 200u);      // tek null örneği
    EXPECT_EQ(s.capn_max_us, 200u);
    EXPECT_EQ(s.n_frames, 2u);
    EXPECT_EQ(s.n_null, 1u);
    EXPECT_EQ(s.enc_avg_us, 2000u);
    EXPECT_EQ(s.enc_max_us, 3000u);
    EXPECT_EQ(s.sendv_avg_us, 30'000u);  // başarısız send de süreye dahil
    EXPECT_EQ(s.sendv_max_us, 50'000u);
    EXPECT_EQ(s.n_sendv_ok, 2u);         // wire-fps: yalnız başarılılar
    EXPECT_EQ(s.n_sendv_fail, 1u);
    EXPECT_EQ(s.senda_avg_us, 2000u);
    EXPECT_EQ(s.n_senda, 1u);
}

TEST(SendDiagTest, FlushResetsWindowForNextSecond) {
    // Arrange — ilk pencereyi doldur ve flush'la
    SendDiag d;
    d.begin_window(0);
    d.record_send_video(10'000, true);
    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));
    EXPECT_EQ(s.n_sendv_ok, 1u);

    // Act — ikinci pencere bağımsız birikir
    d.record_send_video(20'000, true);
    d.record_send_video(40'000, true);
    SendDiagStats s2{};
    EXPECT_FALSE(d.maybe_flush(kSecUs + kSecUs - 1, &s2));  // henüz dolmadı
    ASSERT_TRUE(d.maybe_flush(2 * kSecUs, &s2));

    // Assert — önceki pencereden sızıntı yok
    EXPECT_EQ(s2.n_sendv_ok, 2u);
    EXPECT_EQ(s2.sendv_avg_us, 30'000u);
    EXPECT_EQ(s2.sendv_max_us, 40'000u);
}

TEST(SendDiagTest, Faz2AggregatesInteropPreviewTotalPace) {
    // RTMP_DARBOGAZ Faz 2: ölçüm boşluğu kapatma — d3d11_frame_cb (gpu_sub_
    // interop dahil), preview (emit_wgc_preview / DXGI map), run_frame iş
    // toplamı (pace hariç) ve pace süresi ayrı toplanır. Alt bileşen toplamı
    // ile tot farkı, hâlâ ölçülmeyen yeri gösterir.
    SendDiag d;
    d.begin_window(0);
    d.record_interop(1000);
    d.record_interop(3000);
    d.record_preview(4000);
    d.record_preview(8000);
    d.record_total(10'000);
    d.record_total(14'000);
    d.record_pace(6000);
    d.record_pace(8000);

    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));

    EXPECT_EQ(s.d3d_avg_us, 2000u);
    EXPECT_EQ(s.d3d_max_us, 3000u);
    EXPECT_EQ(s.prev_avg_us, 6000u);
    EXPECT_EQ(s.prev_max_us, 8000u);
    EXPECT_EQ(s.tot_avg_us, 12'000u);
    EXPECT_EQ(s.tot_max_us, 14'000u);
    EXPECT_EQ(s.pace_avg_us, 7000u);
    EXPECT_EQ(s.pace_max_us, 8000u);

    // Pencere sıfırlanır — ikinci pencerede sızıntı yok
    d.record_interop(500);
    SendDiagStats s2{};
    ASSERT_TRUE(d.maybe_flush(2 * kSecUs, &s2));
    EXPECT_EQ(s2.d3d_avg_us, 500u);
    EXPECT_EQ(s2.prev_avg_us, 0u);
    EXPECT_EQ(s2.tot_max_us, 0u);
    EXPECT_EQ(s2.pace_max_us, 0u);
}

TEST(SendDiagTest, FormatLineCarriesFaz2Fields) {
    // Yeni alanlar teşhis satırında görünmeli (şablon kilitlenmez, alan varlığı
    // yeter — FormatLineCarriesKeyFields ile aynı sözleşme).
    SendDiagStats s{};
    s.capn_avg_us = 100;   s.capn_max_us = 300;
    s.d3d_avg_us  = 2500;  s.d3d_max_us  = 7000;
    s.prev_avg_us = 5500;  s.prev_max_us = 12'000;
    s.tot_avg_us  = 9000;  s.tot_max_us  = 21'000;
    s.pace_avg_us = 7000;  s.pace_max_us = 9000;

    const std::string line = rj::format_send_diag(s);

    EXPECT_NE(line.find("capNull="), std::string::npos);
    EXPECT_NE(line.find("d3d=2.5"), std::string::npos);
    EXPECT_NE(line.find("prev=5.5"), std::string::npos);
    EXPECT_NE(line.find("tot=9.0"), std::string::npos);
    EXPECT_NE(line.find("pace=7.0"), std::string::npos);
}

TEST(SendDiagTest, Faz3AggregatesEncSplitAndPrevMiss) {
    // RTMP_DARBOGAZ Faz 3: enc mikro-split (encp=EncodePicture, lock=
    // LockBitstream beklemesi, ecb=on_packet) + prev_miss (DO_NOT_WAIT map
    // hazır değildi — kare atlandı) sayacı.
    SendDiag d;
    d.begin_window(0);
    d.record_enc_split(1000, 2000, 3000);
    d.record_enc_split(3000, 4000, 5000);
    d.record_preview_miss();
    d.record_preview_miss();
    d.record_preview_miss();

    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));
    EXPECT_EQ(s.encp_avg_us, 2000u);
    EXPECT_EQ(s.encp_max_us, 3000u);
    EXPECT_EQ(s.lock_avg_us, 3000u);
    EXPECT_EQ(s.lock_max_us, 4000u);
    EXPECT_EQ(s.ecb_avg_us, 4000u);
    EXPECT_EQ(s.ecb_max_us, 5000u);
    EXPECT_EQ(s.n_prev_miss, 3u);

    // Pencere sıfırlanır — sızıntı yok
    SendDiagStats s2{};
    ASSERT_TRUE(d.maybe_flush(2 * kSecUs, &s2));
    EXPECT_EQ(s2.encp_max_us, 0u);
    EXPECT_EQ(s2.n_prev_miss, 0u);
}

TEST(SendDiagTest, FormatLineCarriesFaz3Fields) {
    SendDiagStats s{};
    s.encp_avg_us = 2500; s.encp_max_us = 5000;
    s.lock_avg_us = 1500; s.lock_max_us = 9000;
    s.ecb_avg_us  = 800;  s.ecb_max_us  = 1200;
    s.n_prev_miss = 7;

    const std::string line = rj::format_send_diag(s);

    EXPECT_NE(line.find("encP=2.5"), std::string::npos);
    EXPECT_NE(line.find("lock=1.5"), std::string::npos);
    EXPECT_NE(line.find("encCb=0.8"), std::string::npos);
    // sendV'nin "(fail=N)" deseniyle tutarlı: prev=a/mms(miss=N)
    EXPECT_NE(line.find("miss=7"), std::string::npos);
}

TEST(SendDiagTest, Faz4AggregatesDupCopyIdrFallback) {
    // VFR/CFR Faz 2: kare tekrarı gözlemlenebilirliği — dup (penceredeki tekrar
    // kare sayısı), copy (CopyResource submit süresi), idrF (zaman bazlı yedek
    // IDR tetik sayısı; normal akışta 0 beklenir — >0 tekrar mekanizmasının
    // aksadığının erken uyarısıdır).
    SendDiag d;
    d.begin_window(0);
    d.record_repeat();
    d.record_repeat();
    d.record_repeat();
    d.record_copy(200);
    d.record_copy(600);
    d.record_idr_fallback();

    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));
    EXPECT_EQ(s.n_dup, 3u);
    EXPECT_EQ(s.copy_avg_us, 400u);
    EXPECT_EQ(s.copy_max_us, 600u);
    EXPECT_EQ(s.n_idr_fallback, 1u);

    // Pencere sıfırlanır — sızıntı yok
    SendDiagStats s2{};
    ASSERT_TRUE(d.maybe_flush(2 * kSecUs, &s2));
    EXPECT_EQ(s2.n_dup, 0u);
    EXPECT_EQ(s2.copy_max_us, 0u);
    EXPECT_EQ(s2.n_idr_fallback, 0u);
}

TEST(SendDiagTest, FormatLineCarriesFaz4Fields) {
    SendDiagStats s{};
    s.n_dup       = 42;
    s.copy_avg_us = 300;  s.copy_max_us = 1500;
    s.n_idr_fallback = 2;

    const std::string line = rj::format_send_diag(s);

    EXPECT_NE(line.find("dup=42"), std::string::npos);
    EXPECT_NE(line.find("copy=0.3"), std::string::npos);
    EXPECT_NE(line.find("idrF=2"), std::string::npos);
}

TEST(SendDiagTest, EmptyWindowFlushesZeros) {
    // Boş pencere (hiç örnek yok) çökmemeli; sıfırlar dönmeli.
    SendDiag d;
    d.begin_window(0);
    SendDiagStats s{};
    ASSERT_TRUE(d.maybe_flush(kSecUs, &s));
    EXPECT_EQ(s.cap_avg_us, 0u);
    EXPECT_EQ(s.sendv_max_us, 0u);
    EXPECT_EQ(s.n_sendv_ok, 0u);
    EXPECT_EQ(s.n_null, 0u);
}

TEST(SendDiagTest, FormatLineCarriesKeyFields) {
    // Format satırı: teşhiste aranacak alanlar mevcut olmalı (birebir şablon
    // kilitlenmez — alan varlığı yeter; şablon değişirse test kırılmasın).
    SendDiagStats s{};
    s.cap_avg_us = 1500;  s.cap_max_us = 4000;
    s.enc_avg_us = 2000;  s.enc_max_us = 9000;
    s.sendv_avg_us = 33'000; s.sendv_max_us = 120'000;
    s.senda_avg_us = 500;
    s.n_frames = 20; s.n_null = 40;
    s.n_sendv_ok = 19; s.n_sendv_fail = 1;
    s.n_senda = 40;

    const std::string line = rj::format_send_diag(s);

    EXPECT_NE(line.find("[SendDiag]"), std::string::npos);
    EXPECT_NE(line.find("wire_fps=19"), std::string::npos);
    EXPECT_NE(line.find("null=40"), std::string::npos);
    EXPECT_NE(line.find("frames=20"), std::string::npos);
    // ms cinsinden send süresi görünmeli (33.0ms ort)
    EXPECT_NE(line.find("33.0"), std::string::npos);
}
