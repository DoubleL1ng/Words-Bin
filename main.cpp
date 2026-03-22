#include "MainWindow.h"
#include "AppSettings.h"
#include <QApplication>
#include <QDir>
#include <QIcon>
#include <QLockFile>
#include <QMessageBox>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    a.setOrganizationName(AppSettings::kOrganization);
    a.setApplicationName(QStringLiteral("Silo"));
    a.setApplicationDisplayName(QStringLiteral("\u4fa7\u5f71"));

    const QString lockPath =
        QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
            .filePath(QStringLiteral("Silo_single_instance.lock"));
    QLockFile singleInstanceLock(lockPath);
    singleInstanceLock.setStaleLockTime(5000);
    if (!singleInstanceLock.tryLock(100)) {
        QMessageBox::information(nullptr,
                                 QStringLiteral("\u63d0\u793a"),
                                 QStringLiteral("\u4fa7\u5f71\u5df2\u5728\u8fd0\u884c\uff0c\u8bf7\u52ff\u91cd\u590d\u542f\u52a8\u3002"));
        return 0;
    }

    const QIcon appIcon(QStringLiteral(":/icons/silo_logo.ico"));
    if (!appIcon.isNull()) {
        a.setWindowIcon(appIcon);
    }
    a.setQuitOnLastWindowClosed(false);// 允许关闭主窗口后继续在系统托盘运行
    MainWindow w;
    w.show(); // 启动时显示主窗口，用户可以选择将其靠边隐藏
    return a.exec();
}
