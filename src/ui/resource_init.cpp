// src/ui/resource_init.cpp
//
// V10/L1-ek (ACIL_L1_QRC_REGRESYON): qrc kayıt zorlaması — sözleşme ve kök
// neden resource_init.h'de. AYRI bir TU olması bilinçli: test_qrc_resources
// yalnız bu nesneyi (+ referansladığı qrc nesnesini) çeker; MainWindow'un
// FFI/pipeline bağımlılık ağını sürüklemez.
#include "resource_init.h"

#include <QtGlobal>

// Q_INIT_RESOURCE namespace içinde kullanılamaz — dosya kapsamında sarmalanır.
static void rejiUiInitQrcResources() { Q_INIT_RESOURCE(rules_template); }

namespace reji::ui {

void ensureResourcesRegistered() { rejiUiInitQrcResources(); }

}  // namespace reji::ui
