// Kuruluş-sırası regresyon kilidi — "Otomatik yeniden yükle" (hot-reload).
//
// Kök neden (TALIMAT_HOTRELOAD_REGRESYON.md, Faz 0): Kullanıcı checkbox'ı
// kural dosyası HENÜZ YOKKEN işaretlerse (doğal ilk-kullanım sırası: önce
// checkbox, sonra "Kuralları Düzenle" ile dosyanın ilk kez oluşması),
// armRulesWatch() QFileSystemWatcher'a hiçbir yol ekleyemez (var olmayan yol
// reddedilir) ve dosya sonradan oluştuğunda kimse watcher'ı yeniden
// silahlandırmaz → hiçbir fileChanged/directoryChanged tetiklenmez, tam
// sessizlik. Düzeltme: openRulesInEditor() dosya kesin var olduktan sonra
// armRulesWatch()'ı yeniden çağırır.
//
// Bu test, MainWindow::armRulesWatch()'ın saf çekirdeğini (armRulesWatchOn,
// rules_watch.h) kullanıcının yaşadığı sırayla çalıştırıp değişmezi kilitler.

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemWatcher>
#include <QString>

#include "../src/ui/rules_watch.h"

namespace {

class RulesWatchTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Taze, henüz-var-olmayan bir çalışma alanı: gerçek ~/.reji'yi taklit eder.
    base_ = QDir::tempPath() + QStringLiteral("/reji_rules_watch_%1")
                .arg(QCoreApplication::applicationPid());
    QDir(base_).removeRecursively();  // önceki koşudan artık kalmasın
    rules_dir_  = base_ + QStringLiteral("/.reji");
    rules_file_ = rules_dir_ + QStringLiteral("/rules.json");
  }

  void TearDown() override { QDir(base_).removeRecursively(); }

  // "Kuralları Düzenle"nin ilk kez yaptığı iş: dizini + dosyayı oluştur (seed).
  void seedRulesFile() {
    ASSERT_TRUE(QDir().mkpath(rules_dir_));
    QFile f(rules_file_);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f.write("{\"rules\":[]}");
    f.close();
  }

  QString base_;
  QString rules_dir_;
  QString rules_file_;
};

// Kullanıcının birebir yaşadığı sıra: önce checkbox (arm), sonra dosya oluşur.
TEST_F(RulesWatchTest, ArmBeforeFileExistsThenReArmAfterCreate) {
  QFileSystemWatcher watcher;

  // 1) Checkbox işaretlendi — ama dosya/dizin daha yok. Watcher BOŞ kalmalı.
  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  EXPECT_TRUE(watcher.files().isEmpty())
      << "Dosya yokken izlenmemeli (QFileSystemWatcher var olmayan yolu reddeder)";
  EXPECT_TRUE(watcher.directories().isEmpty())
      << "Dizin de yokken izlenmemeli";

  // 2) "Kuralları Düzenle" dosyayı ilk kez oluşturur.
  seedRulesFile();

  // 3) DÜZELTME: openRulesInEditor() bu noktada armRulesWatch'ı yeniden çağırır.
  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  EXPECT_EQ(watcher.files().size(), 1)
      << "Re-arm sonrası kural dosyası izlenmeli — regresyonun düzeldiği nokta";
  EXPECT_EQ(watcher.directories().size(), 1)
      << "Üst dizin de izlenmeli (atomic-save olaylarını yakalamak için)";
}

// Dizin var ama dosya yok (örn. ~/.reji önceki oturumdan kalmış, rules.json
// silinmiş): arm yalnız dizini izler; dosya oluşup re-arm edilince ikisini de.
TEST_F(RulesWatchTest, DirExistsFileMissingThenReArm) {
  ASSERT_TRUE(QDir().mkpath(rules_dir_));
  QFileSystemWatcher watcher;

  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  EXPECT_TRUE(watcher.files().isEmpty()) << "Dosya yok — henüz izlenmemeli";
  EXPECT_EQ(watcher.directories().size(), 1) << "Var olan üst dizin izlenmeli";

  seedRulesFile();
  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  EXPECT_EQ(watcher.files().size(), 1) << "Dosya oluşup re-arm edilince izlenmeli";
  EXPECT_EQ(watcher.directories().size(), 1);
}

// Idempotency: tekrar tekrar arm çağrısı çift ekleme yapmamalı — bu güvence,
// re-arm düzeltmesini (her openRulesInEditor'da çağrı) risksiz kılan şeydir.
TEST_F(RulesWatchTest, ReArmIsIdempotent) {
  seedRulesFile();
  QFileSystemWatcher watcher;

  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);
  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/true);

  EXPECT_EQ(watcher.files().size(), 1) << "Aynı dosya birden çok kez eklenmemeli";
  EXPECT_EQ(watcher.directories().size(), 1) << "Aynı dizin birden çok kez eklenmemeli";
}

// V10/L4: auto-reload KAPALIYKEN arm hiçbir yol eklememeli. Aksi halde
// import/manuel-reload yolundaki re-arm çağrıları (writeValidatedRules/
// reloadRulesNow), toggle-off'un temizlediği path'leri geri ekliyor ve
// checkbox kapalıyken harici düzenlemeler sessizce hot-reload oluyordu
// (449c084 re-arm düzeltmesinin etkileşim alanı).
TEST_F(RulesWatchTest, DisabledArmAddsNothingEvenWhenFileExists) {
  seedRulesFile();
  QFileSystemWatcher watcher;

  reji::ui::armRulesWatchOn(watcher, rules_file_, /*enabled=*/false);
  EXPECT_TRUE(watcher.files().isEmpty())
      << "Auto-reload kapalıyken dosya izlenmemeli";
  EXPECT_TRUE(watcher.directories().isEmpty())
      << "Auto-reload kapalıyken dizin de izlenmemeli";
}

}  // namespace

// QFileSystemWatcher, iş parçacığı-afinitesi için bir QCoreApplication örneği
// bekler (sinyal döngüsü gerekmez; add/query senkron çalışır).
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
