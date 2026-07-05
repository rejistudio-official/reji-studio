// tests/test_output_subsystem.cpp
//
// OutputSubsystem birim testleri (Faz2/Aşama1 — ITransport soyutlaması).
// Davranış sözleşmesi korunmalı (Aşama 4 çıkarmasındaki semantik):
//   - init() başarısızlığında alt sistem inaktif kalır (transport_ reset).
//   - send() aktif transport yokken true döner (drop SAYILMAZ).
// Hem gerçek SRT build'inde (init geçersiz hosta bağlanamaz → false) hem
// stub build'inde (stub SrtOutput::init her zaman false) aynı sonucu verir.
#include <gtest/gtest.h>
#include "../src/pipeline/include/output_subsystem.h"

namespace {

rj::OutputSubsystem::Config invalid_config() {
    rj::OutputSubsystem::Config cfg{};
    cfg.host        = "invalid.host.invalid";   // çözümlenemez — init false döner
    cfg.port        = 1;
    cfg.caller_mode = true;
    return cfg;
}

} // namespace

TEST(OutputSubsystemTest, SendWithoutActiveTransportReturnsTrue) {
    // Arrange: init hiç çağrılmadı — aktif transport yok.
    rj::OutputSubsystem out;
    const uint8_t payload[4] = {1, 2, 3, 4};

    // Act + Assert: drop sayılmama sözleşmesi.
    EXPECT_TRUE(out.send(payload, sizeof(payload), 0));
}

TEST(OutputSubsystemTest, InitFailureLeavesSubsystemInactive) {
    // Arrange
    rj::OutputSubsystem out;

    // Act: stub build'de her zaman, gerçek build'de geçersiz host ile başarısız.
    const bool ok = out.init(invalid_config());

    // Assert: başarısız init'te transport_ reset edilir.
    EXPECT_FALSE(ok);
    EXPECT_FALSE(out.is_active());
    EXPECT_EQ(out.raw(), nullptr);
}

TEST(OutputSubsystemTest, SendAfterFailedInitStillReturnsTrue) {
    // Arrange
    rj::OutputSubsystem out;
    (void)out.init(invalid_config());
    const uint8_t payload[4] = {1, 2, 3, 4};

    // Act + Assert: init başarısız → aktif çıkış yok → true (drop sayılmaz).
    EXPECT_TRUE(out.send(payload, sizeof(payload), 0));
}

TEST(OutputSubsystemTest, SetStreamingWithoutTransportKeepsSendTrue) {
    // Arrange: transport yokken set_streaming(true) nullptr yayınlar.
    rj::OutputSubsystem out;
    out.set_streaming(true);
    const uint8_t payload[4] = {1, 2, 3, 4};

    // Act + Assert
    EXPECT_TRUE(out.send(payload, sizeof(payload), 0));

    // Teardown yolu da güvenli olmalı.
    out.set_streaming(false);
    out.shutdown();
}
