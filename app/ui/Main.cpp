#include <QApplication>
#include <QMessageBox>
#include <QStatusBar>
#include <QTimer>

#include "ApplicationLogger.hpp"
#include "MainWindow.hpp"
#include "RedisAvailabilityChecker.hpp"

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    iec61850sim::InstallApplicationLogger();

    iec61850sim::MainWindow mainWindow;
    mainWindow.statusBar()->showMessage(QStringLiteral("正在检查 Redis 服务…"));

    iec61850sim::RedisAvailabilityChecker redisChecker(&mainWindow);
    QObject::connect(&redisChecker, &iec61850sim::RedisAvailabilityChecker::StateChanged, &mainWindow,
        [&mainWindow](iec61850sim::RedisConnectionState state, const QString& detail) {
            mainWindow.statusBar()->showMessage(detail);
            if (state == iec61850sim::RedisConnectionState::Unavailable)
            {
                QMessageBox::warning(&mainWindow, QStringLiteral("Redis"), QStringLiteral("Redis 数据库不可用"));
            }
        });
    mainWindow.show();
    QTimer::singleShot(0, &redisChecker, &iec61850sim::RedisAvailabilityChecker::Start);
    return application.exec();
}
