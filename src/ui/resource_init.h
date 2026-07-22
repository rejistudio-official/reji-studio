// src/ui/resource_init.h — "ui_" öneki bilinçli KULLANILMADI: AUTOUIC,
// "ui_*.h" include'larını uic çıktısı sanıp karşılık gelen .ui dosyasını arar.
//
// V10/L1-ek (ACIL_L1_QRC_REGRESYON): rules_template.qrc, STATİK reji_ui.lib
// içinde gömülü (AUTORCC). MSVC linker, hiçbir sembolü referanslanmayan qrc
// nesnesini kütüphaneden hiç çekmez — qrc self-registration (global ctor)
// çalışmaz ve ":/config/..." yolları runtime'da bulunamaz (canlıda
// "Doğrulama için kopyalanamadı: :/config/profiles/stability.json").
// ensureResourcesRegistered, Q_INIT_RESOURCE referansıyla nesnenin
// link'lenmesini ve kaydı zorlar. İdempotent; MainWindow ctor'u erken
// çağırır, qrc tüketen testler de doğrudan çağırabilir.
#pragma once

namespace reji::ui {

void ensureResourcesRegistered();

}  // namespace reji::ui
