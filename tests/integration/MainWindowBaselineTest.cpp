#include "MainWindow.hpp"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QToolBar>
#include <QtTest>
#include <DockManager.h>
#include <DockAreaWidget.h>
#include <DockWidget.h>

namespace
{
void IsolateSettings(const QString& path)
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
}

int LayoutProbe(bool write)
{
    iec61850sim::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto* manager = window.findChild<ads::CDockManager*>();
    if (!manager)
        return 10;
    auto* server = manager->findDockWidget(QStringLiteral("服务器"));
    auto* client = manager->findDockWidget(QStringLiteral("客户端"));
    if (!server || !client)
        return 11;
    if (write)
    {
        if (server->dockAreaWidget() == client->dockAreaWidget())
            return 12;
        manager->addDockWidget(ads::CenterDockWidgetArea, client, server->dockAreaWidget());
        client->raise();
        QApplication::processEvents();
    }
    if (server->dockAreaWidget() != client->dockAreaWidget())
        return 13;
    if (manager->dockWidgetsMap().size() != 3)
        return 14;
    window.close();
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    if (settings.format() != QSettings::IniFormat || settings.status() != QSettings::NoError)
        return 15;
    return settings.value(QStringLiteral("MainWindow/DockLayout")).toByteArray().isEmpty() ? 16 : 0;
}
}

class MainWindowBaselineTest final : public QObject
{
    Q_OBJECT
private slots:
    void ListsAndDetails();
    void LayoutAcrossProcesses();
};

void MainWindowBaselineTest::ListsAndDetails()
{
    QTemporaryDir settings;
    QVERIFY(settings.isValid());
    IsolateSettings(settings.path());
    iec61850sim::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto* manager = window.findChild<ads::CDockManager*>();
    QVERIFY(manager);
    QCOMPARE(manager->dockWidgetsMap().size(), 3);
    auto* serverDock = manager->findDockWidget(QStringLiteral("服务器"));
    auto* clientDock = manager->findDockWidget(QStringLiteral("客户端"));
    QVERIFY(serverDock && clientDock);
    auto* servers = qobject_cast<QListWidget*>(serverDock->widget());
    auto* clients = qobject_cast<QListWidget*>(clientDock->widget());
    QVERIFY(servers && clients);
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("MainToolBar"));
    QVERIFY(toolbar);
    const auto trigger = [&](const QString& text) {
        for (auto* action : toolbar->actions())
            if (action->text() == text)
            {
                action->trigger();
                return true;
            }
        return false;
    };
    QVERIFY(trigger(QStringLiteral("新建服务器")));
    QVERIFY(trigger(QStringLiteral("新建客户端")));
    QCOMPARE(servers->count(), 1);
    QCOMPARE(clients->count(), 1);
    QCOMPARE(servers->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("server-1"));
    QCOMPARE(clients->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("client-1"));
    auto* detail = manager->findDockWidget(QStringLiteral("客户端 1"));
    QVERIFY(detail);
    auto* label = qobject_cast<QLabel*>(detail->widget());
    QVERIFY(label);
    QVERIFY(label->text().contains(QStringLiteral("名称：客户端 1")));
    QCOMPARE(manager->dockWidgetsMap().size(), 5);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("客户端 1")));
    // Selection activates the existing detail instead of adding a duplicate.
    QTest::mouseClick(servers->viewport(), Qt::LeftButton, Qt::NoModifier,
        servers->visualItemRect(servers->item(0)).center());
    QApplication::processEvents();
    auto* serverDetail = manager->findDockWidget(QStringLiteral("服务器 1"));
    QVERIFY(serverDetail);
    QVERIFY(serverDetail->isVisible());
    QCOMPARE(serverDetail->dockAreaWidget()->currentDockWidget(), serverDetail);
    QCOMPARE(manager->dockWidgetsMap().size(), 5);
    QVERIFY(trigger(QStringLiteral("新建服务器")));
    QCOMPARE(servers->count(), 2);
    QCOMPARE(clients->count(), 1);
    QCOMPARE(manager->dockWidgetsMap().size(), 6);
    QDir().mkpath(QString::fromUtf8(BASELINE_ARTIFACT_DIR));
    QVERIFY(window.grab().save(QString::fromUtf8(BASELINE_ARTIFACT_DIR) + QStringLiteral("/MainWindow.png")));
    window.close();
}

void MainWindowBaselineTest::LayoutAcrossProcesses()
{
    QTemporaryDir settings;
    QVERIFY(settings.isValid());
    for (const auto& mode : {QStringLiteral("--write-layout"), QStringLiteral("--read-layout")})
    {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {mode, settings.path()});
        QVERIFY(process.waitForStarted());
        QVERIFY(process.waitForFinished(10000));
        QCOMPARE(process.exitStatus(), QProcess::NormalExit);
        QVERIFY2(process.exitCode() == 0, qPrintable(QString::fromUtf8(process.readAllStandardError())
            + QStringLiteral(" probe exit=%1").arg(process.exitCode())));
    }
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1].endsWith(QStringLiteral("-layout")))
    {
        IsolateSettings(arguments[2]);
        return LayoutProbe(arguments[1] == QStringLiteral("--write-layout"));
    }
    MainWindowBaselineTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "MainWindowBaselineTest.moc"
