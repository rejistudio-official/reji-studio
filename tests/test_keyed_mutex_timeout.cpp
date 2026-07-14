// K2 seam testi — keyed-mutex hang'ini önleyen "boundedness invariant"ı korur.
//
// Faz 0'da doğrulanan bug: Vulkan tüketicisinin keyed-mutex acquire timeout'u
// UINT32_MAX (sonsuz), execute_copy'nin önceki-submit beklemesi UINT64_MAX
// (sonsuz) idi → device-lost sırasında GPU kuyruğu + CPU thread kalıcı bloke
// olabiliyordu (app-hang). K2 düzeltmesi ikisini de sınırladı.
//
// DÜRÜST KAPSAM NOTU: execute_copy'nin timeout'ta gerçekten `false` dönmesi
// canlı bir Vulkan device + takılı submit gerektirir → headless test edilemez;
// o davranış kod incelemesiyle doğrulandı (copy_optimizer.cpp: bounded wait +
// `return false`, buffer reset ÖNCESİ). Bu test yalnız sabitlerin bir daha
// UINT32_MAX/UINT64_MAX'e (sonsuz) GERİ DÖNMEDİĞİNİ kilitler — yani K2'nin kök
// nedeninin sessizce yeniden girmesini engelleyen regresyon bekçisi.

#include <gtest/gtest.h>
#include <cstdint>
#include "../src/pipeline/include/reji_constants.h"

namespace {

// Vulkan acquire timeout (ms) sınırlı olmalı — sonsuz (UINT32_MAX) DEĞİL.
TEST(KeyedMutexTimeout, VulkanAcquireTimeoutIsBounded) {
    EXPECT_NE(rj::constants::kKeyedMutexAcquireTimeoutMs, UINT32_MAX)
        << "K2 regresyonu: acquire timeout yeniden sonsuz (UINT32_MAX)";
    EXPECT_GT(rj::constants::kKeyedMutexAcquireTimeoutMs, 0u)
        << "0ms timeout anlamsız — acquire hiç beklemezdi";
    // Makul üst sınır: steady-state'te release ~1 frame'de (<=~16ms@60fps) gelir;
    // 1s'ten büyük bir değer 'bounded' olsa da hang-önleme amacını zayıflatır.
    EXPECT_LE(rj::constants::kKeyedMutexAcquireTimeoutMs, 1000u)
        << "timeout aşırı büyük — hang'i etkili sınırlamaz";
}

// Önceki-submit CPU beklemesi (ns) sınırlı olmalı — sonsuz (UINT64_MAX) DEĞİL.
TEST(KeyedMutexTimeout, PrevSubmitWaitIsBounded) {
    EXPECT_NE(rj::constants::kCopyPrevSubmitWaitTimeoutNs, UINT64_MAX)
        << "K2 regresyonu: CPU wait yeniden sonsuz (UINT64_MAX)";
    EXPECT_GT(rj::constants::kCopyPrevSubmitWaitTimeoutNs, 0ull)
        << "0ns bekleme anlamsız";
    // 1ms alt / 5s üst makul aralık (shutdown'daki 5s idle-wait ile aynı tavan).
    EXPECT_GE(rj::constants::kCopyPrevSubmitWaitTimeoutNs, 1'000'000ull)
        << "1ms'ten kısa → transient GPU yükünde yanlış kare-düşürme";
    EXPECT_LE(rj::constants::kCopyPrevSubmitWaitTimeoutNs, 5'000'000'000ull)
        << "5s'ten uzun → app-hang'i etkili sınırlamaz";
}

}  // namespace
