#include "main_window.h"

#ifdef QT6_AVAILABLE
#include <QApplication>
#include <QSurfaceFormat>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    QSurfaceFormat fmt;
    fmt.setVersion(3, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setSwapInterval(0);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    QSurfaceFormat::setDefaultFormat(fmt);
    QApplication::setApplicationName("Reji Studio");
    QApplication::setOrganizationName("RejiStudio");
    QApplication::setApplicationVersion("0.1.0");

    MainWindow w;
    w.show();

    return app.exec();
}

#else

#include <cstdio>
int main() {
    std::fputs("Reji Studio: Qt6 bulunamadÄ±, UI devre dÄ±ÅŸÄ±.\n", stderr);
    return 1;
}

#endif // QT6_AVAILABLE
