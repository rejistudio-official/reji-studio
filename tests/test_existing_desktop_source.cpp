// tests/test_existing_desktop_source.cpp
//
// ExistingDesktopSource adapter testleri — enjekte edilen sahte IScreenCapture
// ile (test seam kurucusu), gerçek GPU/ekran gerekmez.
//
// Saf çekirdek (alan eşlemesi, streak eşiği) test_desktop_source_logic.cpp'de;
// burada sınıfın YAPIŞTIRMA davranışı doğrulanır: init/shutdown yaşam döngüsü,
// state() geçişleri, next_frame()'in eşleme+streak'i birlikte sürmesi.
//
// Sahte capture DxgiScreenCapture DEĞİL → adapter WGC-benzeri yolu izler
// (format = kWgcFramePoolFormat, device = capture->d3d_device()).

#include <gtest/gtest.h>

#include "existing_desktop_source.h"

#include <cstdint>
#include <memory>
#include <utility>

namespace {

// Senaryolanabilir sahte IScreenCapture: her next_frame() çağrısında
// script'teki sıradaki kareyi döndürür (script biterse null kare).
class FakeScreenCapture : public rj::IScreenCapture {
public:
    bool init_result = true;
    bool init_called = false;
    bool shutdown_called = false;
    uint32_t fake_width  = 1280;
    uint32_t fake_height = 720;

    bool init(const Config&) override {
        init_called = true;
        return init_result;
    }
    rj::CapturedFrame next_frame() override {
        if (next_index_ >= script_size_) return {};
        return script_[next_index_++];
    }
    uint32_t width()  const override { return fake_width; }
    uint32_t height() const override { return fake_height; }
    void shutdown() override { shutdown_called = true; }

    void push_frame(const rj::CapturedFrame& f) {
        ASSERT_LT(script_size_, kMaxScript);
        script_[script_size_++] = f;
    }

private:
    static constexpr int kMaxScript = 4;
    rj::CapturedFrame script_[kMaxScript]{};
    int script_size_ = 0;
    int next_index_  = 0;
};

rj::CapturedFrame valid_frame(void* handle, uint64_t ts_us) {
    rj::CapturedFrame f{};
    f.type         = rj::CapturedFrame::HandleType::D3D11;
    f.handle       = handle;
    f.width        = 1280;
    f.height       = 720;
    f.timestamp_us = ts_us;
    return f;
}

// Adapter + sahibi olmayan fake işaretçisi (senaryo kurmak için).
struct Harness {
    FakeScreenCapture* fake;
    rj::ExistingDesktopSource source;

    explicit Harness(rj::IScreenCapture::Config cfg = {})
        : fake(nullptr),
          source(cfg, [this] {
              auto f = std::make_unique<FakeScreenCapture>();
              fake = f.get();
              return f;
          }()) {}
};

}  // namespace

TEST(ExistingDesktopSource, StartsUninitialized) {
    Harness h;
    EXPECT_EQ(h.source.state(), rj::SourceState::Uninitialized);
}

TEST(ExistingDesktopSource, InitDelegatesAndTransitionsToRunning) {
    Harness h;
    ASSERT_TRUE(h.source.init());
    EXPECT_TRUE(h.fake->init_called);
    EXPECT_EQ(h.source.state(), rj::SourceState::Running);
}

TEST(ExistingDesktopSource, InitFailurePropagatesAndStaysUninitialized) {
    Harness h;
    h.fake->init_result = false;
    EXPECT_FALSE(h.source.init());
    EXPECT_EQ(h.source.state(), rj::SourceState::Uninitialized);
}

TEST(ExistingDesktopSource, NextFrameMapsCaptureFields) {
    Harness h;
    int dummy = 0;
    ASSERT_TRUE(h.source.init());
    h.fake->push_frame(valid_frame(&dummy, /*ts_us=*/555'000));

    const rj::SourceFrame f = h.source.next_frame();

    EXPECT_EQ(f.handle, &dummy);
    EXPECT_EQ(f.type, rj::SourceFrame::HandleType::D3D11);
    EXPECT_EQ(f.width, 1280u);
    EXPECT_EQ(f.height, 720u);
    EXPECT_EQ(f.format, rj::kWgcFramePoolFormat);  // DXGI olmayan yol
    EXPECT_EQ(f.timestamp_us, 555'000u);
}

TEST(ExistingDesktopSource, NextFrameFillsQpcTimestampWhenCaptureLeftItZero) {
    Harness h;
    int dummy = 0;
    ASSERT_TRUE(h.source.init());
    h.fake->push_frame(valid_frame(&dummy, /*ts_us=*/0));

    const rj::SourceFrame f = h.source.next_frame();

    // DXGI yolu davranışı: capture 0 bıraktı → acquire-anı QPC dolduruldu.
    EXPECT_GT(f.timestamp_us, 0u);
}

TEST(ExistingDesktopSource, MetadataReflectsCaptureDimensionsAndFormat) {
    Harness h;
    ASSERT_TRUE(h.source.init());

    const rj::SourceMetadata md = h.source.metadata();

    EXPECT_EQ(md.width, 1280u);
    EXPECT_EQ(md.height, 720u);
    EXPECT_EQ(md.format, rj::kWgcFramePoolFormat);
    EXPECT_EQ(md.device, nullptr);  // fake d3d_device() override etmez
}

TEST(ExistingDesktopSource, NullStreakReachesNeedsReinitThenValidFrameRecovers) {
    Harness h;
    int dummy = 0;
    ASSERT_TRUE(h.source.init());

    // Script boş → her next_frame() null kare; eşiğe kadar sür.
    for (int i = 0; i < rj::kNullStreakReinitThreshold - 1; ++i)
        (void)h.source.next_frame();
    EXPECT_EQ(h.source.state(), rj::SourceState::Running);

    (void)h.source.next_frame();  // 60. null
    EXPECT_EQ(h.source.state(), rj::SourceState::NeedsReinit);

    h.fake->push_frame(valid_frame(&dummy, 1));
    (void)h.source.next_frame();  // geçerli kare → streak sıfır
    EXPECT_EQ(h.source.state(), rj::SourceState::Running);
}

TEST(ExistingDesktopSource, ShutdownResetsStateToUninitialized) {
    Harness h;
    ASSERT_TRUE(h.source.init());

    // RAII teardown: capture_ reset (CaptureSubsystem::shutdown ile aynı model).
    // fake bu noktada yok edildiğinden ona dokunulmaz — yalnız state gözlenir.
    h.source.shutdown();

    EXPECT_EQ(h.source.state(), rj::SourceState::Uninitialized);
}

TEST(ExistingDesktopSource, NextFrameWithoutInitReturnsNoFrame) {
    Harness h;
    const rj::SourceFrame f = h.source.next_frame();
    EXPECT_EQ(f.handle, nullptr);
}
