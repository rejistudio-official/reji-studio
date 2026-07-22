// tests/test_frame_drop_policy.cpp
//
// SIYAH_KUTU S1-ek4: "hangi kare sonucu frame drop sayılır" saf kararı.
//
// Canlı bulgu: null capture frame'i ("yeni kare yok" — durağan ekranda
// DXGI AcquireNextFrame timeout / WGC boş dönüş NORMAL durumdur) frame_drops
// sayacına yazılıyordu. Boşta (START'sız) bu sayaç FrameDropped event'leriyle
// predictive healing'i besleyip encoder'ı gerçekten 3500'e düşürtüyor, oluşan
// (3500, original) açığı da recovery banner döngüsünü tetikliyordu.
//
// Bu test drop semantiğini kilitler: yalnız GERÇEK kayıplar (encode hatası)
// sayılır; "yeni kare yok" bir kayıp değildir. Gönderim hatası ayrı noktada
// (on_packet, streaming'e kapılı) sayılır — bu politikanın kapsamı dışında.
#include <gtest/gtest.h>

#include "frame_drop_policy.h"

using rj::FrameOutcome;
using rj::counts_as_frame_drop;

TEST(FrameDropPolicyTest, NoNewFrameIsNotCountedAsDrop) {
    // Arrange + Act + Assert — durağan ekranda null frame normal pacing'dir;
    // drop sayılırsa boşta predictive healing sahte beslenir (S1-ek4 kök neden).
    EXPECT_FALSE(counts_as_frame_drop(FrameOutcome::NoNewFrame));
}

TEST(FrameDropPolicyTest, EncodeFailureIsCountedAsDrop) {
    // Gerçek kayıp: geçerli kare alındı ama encode edilemedi.
    EXPECT_TRUE(counts_as_frame_drop(FrameOutcome::EncodeFailed));
}

TEST(FrameDropPolicyTest, EncodedFrameIsNotCountedAsDrop) {
    // Mutlu yol: kare encode edildi — kayıp yok.
    EXPECT_FALSE(counts_as_frame_drop(FrameOutcome::Encoded));
}
