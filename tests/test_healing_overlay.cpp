// SIYAH_KUTU_REGRESYON: HealingOverlay ilk-gösterim geometrisi.
//
// Kök neden (görsel katman): onActionEvent/showApprovalPrompt,
// showMessage'dan FARKLI olarak adjustSize() çağırmıyordu. Overlay daha önce
// hiç showMessage görmediyse (gerçek kurulum akışı: bağlantı-kaybı mesajı
// yok, ilk healing event'i onActionEvent'ten gelir) child-widget varsayılan
// geometrisiyle (setFixedWidth(380) × ~30px) gösteriliyor; paintEvent tüm
// rect'i 0x141414 ile boyadığından içeriği görünmeyen SİYAH ŞERİT çıkıyordu.
//
// Tetik zinciri (neden Sprint 1 sonrası göründü): L1-ek qrc düzeltmesi
// applyProfile'ı ilk kez çalıştırdı → stability profili ilk ÇALIŞAN kural
// seti oldu (eski ~/.reji/rules.json mode adları "auto"/"co_pilot" motorun
// "auto-pilot"/"co-pilot" adlarıyla hiç eşleşmiyordu → hiçbir kural hiç
// tetiklenmemişti) → boşta frame_drop_recovery (pct<3) her hysteresis=6000ms
// ≈ ~5s'de bir event üretti → overlay ilk kez gösterildi ve latent geometri
// bug'ı görünür oldu.
#include <gtest/gtest.h>

#include <QApplication>
#include <QWidget>

#include "healing_overlay.h"

namespace {

reji::ActionEvent makeEvent(bool require_approval) {
    reji::ActionEvent ev{};
    ev.id               = 1;
    ev.type             = reji::ActionType::BitrateRecover;
    ev.description      = QStringLiteral("Bitrate normale döndürülüyor → 6000 kbps");
    ev.require_approval = require_approval;
    ev.timestamp        = QStringLiteral("12:00:00");
    return ev;
}

}  // namespace

// Bilgi (auto-uygulanan) event: ilk gösterimde yükseklik içerik boyutunda
// olmalı — varsayılan ~30px şerit değil.
TEST(HealingOverlayTest, FirstInfoEventSizesOverlayToContent) {
    QWidget parent;
    parent.resize(1280, 720);
    parent.show();
    reji::HealingOverlay overlay(&parent);

    overlay.onActionEvent(makeEvent(/*require_approval=*/false));

    EXPECT_TRUE(overlay.isVisible());
    EXPECT_GE(overlay.height(), overlay.sizeHint().height())
        << "ilk onActionEvent gösteriminde overlay içerik boyutuna ayarlanmalı "
           "(siyah şerit regresyonu — adjustSize eksik)";
}

// CoPilot onay prompt'u (require_approval=true) aynı garantiyi vermeli —
// checkbox listesi + Reddet butonu görünür alana sığmalı.
TEST(HealingOverlayTest, FirstApprovalPromptSizesOverlayToContent) {
    QWidget parent;
    parent.resize(1280, 720);
    parent.show();
    reji::HealingOverlay overlay(&parent);

    overlay.onActionEvent(makeEvent(/*require_approval=*/true));

    EXPECT_TRUE(overlay.isVisible());
    EXPECT_GE(overlay.height(), overlay.sizeHint().height())
        << "onay prompt'u gösteriminde overlay içerik boyutuna ayarlanmalı";
}

// SIYAH_KUTU düzeltmesi: banner bastırıldığında (yayın yokken bilgi
// event'i) overlay AÇILMAZ ama event geçmişe yine işlenir — kayıt
// kaybolmaz, sahne paneli üstünde periyodik kutu da çıkmaz.
TEST(HealingOverlayTest, SuppressedInfoEventRecordsHistoryWithoutShowing) {
    QWidget parent;
    parent.resize(1280, 720);
    parent.show();
    reji::HealingOverlay overlay(&parent);

    overlay.onActionEvent(makeEvent(/*require_approval=*/false),
                          /*show_banner=*/false);

    EXPECT_FALSE(overlay.isVisible())
        << "banner bastırıldığında overlay görünmemeli (boşta periyodik kutu)";
    EXPECT_EQ(overlay.actionHistory(10).size(), 1)
        << "event geçmişe yine işlenmeli";
}

// Onay prompt'ları gate'ten ETKİLENMEZ — show_banner=false olsa bile
// require_approval event'i prompt açar (Rust'ta pending karar bekliyor).
TEST(HealingOverlayTest, ApprovalPromptShowsEvenWhenBannerSuppressed) {
    QWidget parent;
    parent.resize(1280, 720);
    parent.show();
    reji::HealingOverlay overlay(&parent);

    overlay.onActionEvent(makeEvent(/*require_approval=*/true),
                          /*show_banner=*/false);

    EXPECT_TRUE(overlay.isVisible())
        << "onay bekleyen aksiyon her durumda gösterilmeli";
}

int main(int argc, char** argv) {
    // Görüntüsüz CI/terminal koşusu — gerçek pencere sistemi gerekmez.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
