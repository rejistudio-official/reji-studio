// V10/L1-ek (ACIL_L1_QRC_REGRESYON): gömülü qrc kaynaklarının runtime kaydı.
//
// Kök neden: rules_template.qrc STATİK reji_ui.lib içinde gömülü (AUTORCC).
// MSVC linker, hiçbir sembolü referanslanmayan qrc nesnesini kütüphaneden
// hiç çekmez — self-registration (global ctor) çalışmaz ve ":/config/..."
// yolları runtime'da bulunamaz. Canlıda "Doğrulama için kopyalanamadı:
// :/config/profiles/stability.json (No such file or directory)" olarak
// çıktı (applyProfile — L5 düzeltmesine kadar ölü yoldu, testi de yoktu).
//
// Bu test reji_ui.lib'i uygulamayla AYNI şekilde link'ler ve üretim init
// fonksiyonunun (reji::ui::ensureResourcesRegistered) kaydı zorladığını
// kilitler. Test boşluğu notu: eski testler qrc'yi ya hiç kullanmıyor ya da
// kendi exe'sine gömüyordu — statik-lib link yolunu hiçbiri temsil etmiyordu.
#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QString>
#include <QTemporaryDir>

#include "../src/ffi/ffi_bridge.h"  // rj_validate_rules — motorun kendi parser'ı
#include "../src/ui/resource_init.h"

namespace {

// Cetvel düzeltmesi (fuzz Faz 0 / BOM bulgusu): bu yardımcı eskiden gömülü
// kaynağı Qt'nin QJsonDocument'ıyla ölçüyordu, şablonu ise hiç parse
// etmiyordu ("içeriği JSON olmayabilir"). Qt'nin parser'ı UTF-8 BOM'u tolere
// eder (aşağıdaki QtJsonParserToleratesBom), motorun parser'ı o güne dek
// etmiyordu — BOM'lu şablon bu testten geçip ~/.reji/rules.json olarak
// tohumlanınca motor "TOML parse error ... invalid key" ile reddediyordu.
// Doğru cetvel: kaynağı, tohumlandığı gibi diske yazıp motorun kendi
// doğrulama yolundan (rj_validate_rules → parse_rules_content) geçirmek.
void expectResourceValidByEngine(const QTemporaryDir& dir, const char* path) {
    QFile f(QString::fromLatin1(path));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly))
        << path << " açılamadı: " << f.errorString().toStdString();
    const QByteArray data = f.readAll();
    ASSERT_FALSE(data.isEmpty()) << path << " boş okundu";

    // seedRulesFromTemplate/applyProfile ile aynı: byte-aynen diske yaz.
    const QString tmpPath = dir.filePath(QString::fromLatin1(path).section('/', -1));
    QFile out(tmpPath);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(out.write(data), data.size());
    out.close();

    // FFI sözleşmesi UTF-8 (main_window.cpp toUtf8 ile aynı) — toLocal8Bit
    // ASCII-dışı kullanıcı yolunda (ör. "Çağlar") Rust tarafında bozulur.
    const QByteArray native = QDir::toNativeSeparators(tmpPath).toUtf8();
    EXPECT_EQ(rj_validate_rules(native.constData()), 1)
        << path << " motorun parser'ından (rj_validate_rules) geçmedi";
}

}  // namespace

// Üretim init'i çağrıldıktan sonra üç gömülü profil + kural şablonu okunabilir
// VE motorun kendi parser'ıyla geçerli olmalı — applyProfile /
// seedRulesFromTemplate'in kaynak tarafı, motorun tükettiği cetvelle.
TEST(QrcResourcesTest, ProductionInitRegistersEmbeddedResources) {
    reji::ui::ensureResourcesRegistered();

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    expectResourceValidByEngine(dir, ":/config/profiles/performance.json");
    expectResourceValidByEngine(dir, ":/config/profiles/stability.json");
    expectResourceValidByEngine(dir, ":/config/profiles/efficiency.json");
    expectResourceValidByEngine(dir, ":/config/rules.json.template");
}

// Cetvelin motor olduğunun kanıtı: Qt'nin "geçerli JSON" dediği ama motorun
// reddettiği içerik (bilinmeyen action, RULES_SCHEMA SB-5) testten GEÇMEMELİ.
// Eski yardımcı (QJsonDocument) bunu geçerli sayardı.
TEST(QrcResourcesTest, EngineRulerRejectsWhatQtAccepts) {
    const QByteArray data =
        "{\"rules\": [{\"id\": \"x\", \"condition\": \"cpu_load_pct > 80\","
        " \"action\": \"reduce_bitrate\", \"modes\": [\"auto-pilot\"]}]}";
    QJsonParseError perr{};
    QJsonDocument::fromJson(data, &perr);
    ASSERT_EQ(perr.error, QJsonParseError::NoError) << "Qt cetveli: geçerli JSON";

    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString tmpPath = dir.filePath(QStringLiteral("unknown_action.json"));
    QFile out(tmpPath);
    ASSERT_TRUE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(out.write(data), data.size());
    out.close();
    const QByteArray native = QDir::toNativeSeparators(tmpPath).toUtf8();
    EXPECT_EQ(rj_validate_rules(native.constData()), 0)
        << "motor cetveli: bilinmeyen action reddedilmeli";
}

// Eski cetvelin neden yanlış olduğunun kaydı: Qt'nin JSON parser'ı UTF-8
// BOM'u sessizce atlar. Motor da artık atlıyor (rules.rs parse_rules_content),
// ama gömülü kaynakların geçerliliğine motor karar verir, Qt değil.
TEST(QrcResourcesTest, QtJsonParserToleratesBom) {
    const QByteArray withBom = QByteArray("\xEF\xBB\xBF") + "{\"rules\": []}";
    QJsonParseError perr{};
    QJsonDocument::fromJson(withBom, &perr);
    EXPECT_EQ(perr.error, QJsonParseError::NoError)
        << "Qt BOM'u reddediyorsa bu testin gerekçesi güncellenmeli: "
        << perr.errorString().toStdString();
}

// Idempotenlik: MainWindow ctor'u + testler + gelecekteki çağıranlar art arda
// çağırabilir; kayıt tekrarı zararsız olmalı.
TEST(QrcResourcesTest, EnsureResourcesRegisteredIsIdempotent) {
    reji::ui::ensureResourcesRegistered();
    reji::ui::ensureResourcesRegistered();
    QFile f(QStringLiteral(":/config/profiles/stability.json"));
    EXPECT_TRUE(f.open(QIODevice::ReadOnly));
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
