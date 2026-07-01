// tests/pipeline_characterization_test.cpp
//
// Aşama-0 karakterizasyon harness'i (refactoring regresyon güvenlik ağı).
//
// Amaç: pipeline.cpp'nin alt sistemlere bölünmesinden ÖNCE, run_frame()'in
// gözlemlenebilir davranışını (fps_actual, frame_drops, bitrate_kbps) bir
// referans dosyasına (baseline_metrics.txt) kaydetmek. Her refactoring
// aşamasından sonra bu test tekrar çalıştırılıp çıktı diff'lenerek "davranış
// değişti mi" kontrol edilir.
//
// Gözlem noktası: Pipeline::get_last_metric_sample() (Aşama-0 test seam).
// rj_metrics_poll() güvenilir değildir (üç ayrı yerde tanımlı stub; Zig/C
// sürümleri out'u yok sayıp 0 döndürür — docs/reviews'da belgeli), bu yüzden
// FFI round-trip yerine doğrudan pipeline'dan okunur.
//
// ORTAM NOTU: Anlamlı (sıfırdan farklı) veri yalnızca gerçek DXGI capture +
// ekran olan dev makinesinde üretilir. GPU/ekran olmayan headless CI'da init()
// false döner, run_frame() erken çıkar ve baseline temsili olmaz — bu durum
// dosyaya "init=FAILED" olarak yazılır ve test yine de PASS eder (bu bir
// karakterizasyon testidir, deger üzerinde hard-assert yapmaz).

#include <gtest/gtest.h>

#ifndef REJI_VULKAN_MOCK
#define REJI_VULKAN_MOCK 1
#endif

#include "pipeline.h"

#include <cstdint>
#include <cstdio>
#include <fstream>

#ifndef REJI_BASELINE_PATH
#define REJI_BASELINE_PATH "baseline_metrics.txt"
#endif

namespace {

constexpr int kFrameCount   = 100;
constexpr int kSampleStride = 10;   // her 10 frame'de bir kayıt

}  // namespace

// PipelineCharacterization: 100 frame çalıştır, her 10 frame'de bir metrik
// snapshot'ını baseline_metrics.txt'e yaz. Değerler üzerinde assert YOK —
// yalnızca harness'in çalıştığını ve dosyanın yazıldığını doğrular.
TEST(PipelineCharacterization, Baseline100Frames) {
    std::ofstream out(REJI_BASELINE_PATH, std::ios::out | std::ios::trunc);
    ASSERT_TRUE(out.is_open()) << "baseline dosyası açılamadı: " << REJI_BASELINE_PATH;

    auto pipeline = std::make_unique<rj::Pipeline>();
    rj::Pipeline::Config cfg;  // varsayılan: 1920x1080@60, kDefaultBitrateKbps

    const bool init_ok = pipeline->init(cfg);

    // ── Başlık: ortam ve konfigürasyon (diff'te sabit kalmalı) ──────────────
    out << "# Reji Studio — pipeline karakterizasyon baseline'ı\n";
    out << "# Aşama-0 regresyon referansı. Kolonlar: frame run_ok has_sample "
           "fps_actual frame_drops bitrate_kbps\n";
    out << "# cfg: " << cfg.width << "x" << cfg.height << "@" << cfg.fps
        << " bitrate=" << cfg.bitrate_kbps << "kbps\n";
    out << "# init=" << (init_ok ? "OK" : "FAILED")
        << (init_ok ? "" : "  (headless/GPU yok — baseline TEMSİLİ DEĞİL)") << "\n";
    out << "# frames=" << kFrameCount << " stride=" << kSampleStride << "\n";
    out << "#----------------------------------------------------------------\n";

    for (int i = 0; i < kFrameCount; ++i) {
        const bool run_ok = pipeline->run_frame();

        // İlk frame (i==0) ve her stride'da + son frame'de kayıt al.
        const bool record = (i % kSampleStride == 0) || (i == kFrameCount - 1);
        if (!record) continue;

        RjMetricSample s{};
        const bool has_sample = pipeline->get_last_metric_sample(&s);

        char line[128];
        std::snprintf(line, sizeof(line),
                      "%-5d %-6d %-9d %8.2f %-11u %-u\n",
                      i,
                      run_ok ? 1 : 0,
                      has_sample ? 1 : 0,
                      has_sample ? s.fps_actual : 0.0f,
                      has_sample ? s.frame_drops : 0u,
                      has_sample ? s.bitrate_kbps : 0u);
        out << line;
    }

    out.flush();
    out.close();

    // Karakterizasyon: hard-assert yok — dosyanın üretildiğini garanti et.
    std::ifstream check(REJI_BASELINE_PATH);
    EXPECT_TRUE(check.good()) << "baseline dosyası yazılamadı";

    // init başarılıysa temiz kapatma da crash'siz olmalı.
    EXPECT_TRUE(pipeline->shutdown());
}
