#include "app/MainWindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("AmigaIDE"));
    QApplication::setApplicationName(QStringLiteral("AmigaIDE"));

    MainWindow window;
    window.show();

    return QApplication::exec();
}
