#include "main_window.h"

#ifdef QT6_AVAILABLE
#include <QApplication>

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
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
    std::fputs("Reji Studio: Qt6 bulunamadı, UI devre dışı.\n", stderr);
    return 1;
}

#endif // QT6_AVAILABLE
