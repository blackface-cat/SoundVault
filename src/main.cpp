#include <QApplication>
#include <QIcon>

#include "ui/MainWindow.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Sound Vault"));
    QApplication::setApplicationDisplayName(QStringLiteral("Sound Vault"));
    QApplication::setApplicationVersion(QStringLiteral("1.4.0"));
    QApplication::setOrganizationName(QStringLiteral("SoundVault"));

    // 窗口图标（任务栏/标题栏）：随 exe 内嵌资源
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/logo.png")));

    MainWindow window;
    window.show();
    return app.exec();
}
