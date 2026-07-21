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

// Not (SIYAH_KUTU kök düzeltmesi): show_banner bastırma testleri kaldırıldı —
// parametre API'den çıktı; sahte event'ler artık kaynağında (Rust,
// recovery_has_deficit) üretilmiyor. Geometri kilitleri yukarıda duruyor.

int main(int argc, char** argv) {
    // Görüntüsüz CI/terminal koşusu — gerçek pencere sistemi gerekmez.
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    QApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
