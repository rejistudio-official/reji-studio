#pragma once

#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QString>

namespace reji::ui {

// MainWindow::armRulesWatch()'ın saf (Qt-widget'sız) çekirdeği.
//
// Verilen watcher'a YALNIZCA hâlihazırda var olan yolları — kural dosyasının
// üst dizinini ve dosyanın kendisini — idempotent biçimde ekler:
//   * Üst dizin: atomic-save (sil+yeniden-yaz) ve dosya-oluşturma olaylarını
//     yakalar; dosya geçici olarak yok olsa bile.
//   * Dosya: QFileSystemWatcher var olmayan bir yolu reddettiği için yalnızca
//     mevcutsa eklenir.
//
// KURULUŞ-SIRASI DEĞİŞMEZİ (regresyon kilidi): Bu rutin, dosya/dizin henüz
// oluşmadan çağrılırsa watcher'a HİÇBİR yol eklemez. Kullanıcı "Otomatik
// yeniden yükle"yi dosya var olmadan işaretlerse (doğal ilk-kullanım sırası:
// önce checkbox, sonra "Kuralları Düzenle" ile dosyanın ilk kez oluşması)
// watcher boş kalır ve hiçbir değişiklik tetiklenmez. Bu yüzden dosya
// oluştuktan sonra rutin YENİDEN çağrılmalıdır (re-arm). `!contains` koruması
// tekrar çağrıları zararsız (idempotent) kılar.
//
// V10/L4 (etkinlik değişmezi): `enabled=false` iken HİÇBİR yol eklenmez —
// çağıran, "Otomatik yeniden yükle" checkbox durumunu geçirir. Parametre
// bilinçli olarak varsayılansız: yeni çağrı yerleri durumu düşünmeden arm
// edemesin. Aksi halde import/manuel-reload yolundaki re-arm çağrıları,
// toggle-off'un temizlediği path'leri geri ekleyip checkbox kapalıyken
// sessiz hot-reload'a yol açıyordu.
inline void armRulesWatchOn(QFileSystemWatcher& watcher, const QString& filePath,
                            bool enabled) {
    if (!enabled) return;

    const QString dir = QFileInfo(filePath).absolutePath();

    if (QFileInfo::exists(dir) && !watcher.directories().contains(dir)) {
        watcher.addPath(dir);
    }
    if (QFileInfo::exists(filePath) && !watcher.files().contains(filePath)) {
        watcher.addPath(filePath);
    }
}

}  // namespace reji::ui
