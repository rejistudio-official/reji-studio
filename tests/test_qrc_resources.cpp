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
#include <QFile>
#include <QJsonDocument>
#include <QString>

#include "../src/ui/resource_init.h"

namespace {

void expectResourceReadableJson(const char* path) {
    QFile f(QString::fromLatin1(path));
    ASSERT_TRUE(f.open(QIODevice::ReadOnly))
        << path << " açılamadı: " << f.errorString().toStdString();
    const QByteArray data = f.readAll();
    EXPECT_FALSE(data.isEmpty()) << path << " boş okundu";
    QJsonParseError perr{};
    QJsonDocument::fromJson(data, &perr);
    EXPECT_EQ(perr.error, QJsonParseError::NoError)
        << path << " geçerli JSON değil: " << perr.errorString().toStdString();
}

}  // namespace

// Üretim init'i çağrıldıktan sonra üç gömülü profil + kural şablonu okunabilir
// olmalı — applyProfile/seedRulesFromTemplate'in kaynak tarafı.
TEST(QrcResourcesTest, ProductionInitRegistersEmbeddedResources) {
    reji::ui::ensureResourcesRegistered();

    expectResourceReadableJson(":/config/profiles/performance.json");
    expectResourceReadableJson(":/config/profiles/stability.json");
    expectResourceReadableJson(":/config/profiles/efficiency.json");

    // Şablonun içeriği JSON olmayabilir (template) — yalnız okunabilirlik.
    QFile tpl(QStringLiteral(":/config/rules.json.template"));
    EXPECT_TRUE(tpl.open(QIODevice::ReadOnly))
        << "rules.json.template açılamadı: " << tpl.errorString().toStdString();
    EXPECT_FALSE(tpl.readAll().isEmpty());
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
