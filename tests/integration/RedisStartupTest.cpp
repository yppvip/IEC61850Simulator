#include "RedisClient.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
QString ApplicationPath;
QString RedisServerPath;
}

class RedisStartupTest final : public QObject
{
    Q_OBJECT

private slots:
    void Startup_data();
    void Startup();
};

void RedisStartupTest::Startup_data()
{
    QTest::addColumn<bool>("available");
    QTest::newRow("redis-unavailable") << false;
    QTest::newRow("existing-redis") << true;
}

void RedisStartupTest::Startup()
{
    QFETCH(bool, available);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QTcpServer portReservation;
    QVERIFY(portReservation.listen(QHostAddress::LocalHost, 0));
    const auto port = portReservation.serverPort();

    // Only this fixture's child is cleaned up; never launch RedisService or
    // touch a user's Redis instance/configuration.
    QProcess redis;
    if (available)
    {
        if (!QFile::exists(RedisServerPath))
        {
            QSKIP("Local Redis test executable is not available.");
        }
        QFile configuration(directory.filePath(QStringLiteral("redis.conf")));
        QVERIFY(configuration.open(QIODevice::WriteOnly));
        configuration.write(QStringLiteral("bind 127.0.0.1\nport %1\nsave \"\"\nappendonly no\nlogfile \"\"\n")
            .arg(port).toUtf8());
        configuration.close();
        portReservation.close();
        redis.setWorkingDirectory(directory.path());
        redis.start(RedisServerPath, {QStringLiteral("redis.conf")});
        QVERIFY2(redis.waitForStarted(), qPrintable(redis.errorString()));
        iec61850sim::RedisClient client;
        bool ready = false;
        connect(&client, &iec61850sim::RedisClient::PingFinished, &client,
            [&](bool success, const QString&) { ready = success; });
        QElapsedTimer timer;
        timer.start();
        while (!ready && timer.elapsed() < 5000 && redis.state() != QProcess::NotRunning)
        {
            client.Ping(QStringLiteral("127.0.0.1"), port, 100);
            QTest::qWait(100);
        }
        QVERIFY2(ready, redis.readAllStandardOutput().constData());
    }
    // Hold a silent TCP listener in the failure case for a repeatable timeout.

    const auto executable = directory.filePath(QFileInfo(ApplicationPath).fileName());
    QVERIFY(QFile::copy(ApplicationPath, executable));
    const QDir runtimeDirectory(QFileInfo(ApplicationPath).absolutePath());
    for (const auto& library : runtimeDirectory.entryList({QStringLiteral("*.dll")}, QDir::Files))
    {
        QVERIFY(QFile::copy(runtimeDirectory.filePath(library), directory.filePath(library)));
    }
    QVERIFY(QDir(directory.path()).mkpath(QStringLiteral("config")));
    QFile configuration(directory.filePath(QStringLiteral("config/Redis.json")));
    QVERIFY(configuration.open(QIODevice::WriteOnly));
    configuration.write(QJsonDocument(QJsonObject{
        {QStringLiteral("host"), QStringLiteral("127.0.0.1")},
        {QStringLiteral("port"), port},
        {QStringLiteral("startupTimeoutMs"), 1000},
        {QStringLiteral("autoStart"), false},
        {QStringLiteral("redisWorkingDirectory"), directory.path()}
    }).toJson());
    configuration.close();

    for (int run = 0; run < 2; ++run)
    {
        QProcess application;
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
        environment.insert(QStringLiteral("QT_QPA_PLATFORM_PLUGIN_PATH"), runtimeDirectory.filePath(QStringLiteral("platforms")));
        environment.insert(QStringLiteral("PATH"), QFileInfo(ApplicationPath).absolutePath()
            + QDir::listSeparator() + environment.value(QStringLiteral("SystemRoot")) + QStringLiteral("/System32")
            + QDir::listSeparator() + environment.value(QStringLiteral("SystemRoot")));
        application.setProcessEnvironment(environment);
        application.setWorkingDirectory(directory.path());
        application.start(executable, {});
        QVERIFY2(application.waitForStarted(), qPrintable(application.errorString()));
        const QString expected = available ? QStringLiteral("Redis 已连接：") : QStringLiteral("Redis 数据库不可用：");
        QString log;
        const auto readLog = [&]() {
            QFile file(directory.filePath(QStringLiteral("logs/IEC61850Simulator.log")));
            if (file.open(QIODevice::ReadOnly))
            {
                log = QString::fromUtf8(file.readAll());
            }
            return log.count(expected) >= run + 1;
        };
        QElapsedTimer startup;
        startup.start();
        while (!readLog() && startup.elapsed() < 7000 && application.state() != QProcess::NotRunning)
        {
            QTest::qWait(50);
        }
        QVERIFY2(readLog(), qPrintable(log + QString::fromUtf8(application.readAllStandardError())
            + QStringLiteral(" process exit: %1").arg(application.exitCode())));
        QTest::qWait(100);
        QCOMPARE(application.state(), QProcess::Running);
        application.kill();
        QVERIFY(application.waitForFinished());
        if (available)
        {
            QCOMPARE(redis.state(), QProcess::Running);
        }
    }
    if (available)
    {
        redis.kill();
        QVERIFY(redis.waitForFinished());
    }
}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    if (argc < 3)
    {
        return 2;
    }
    ApplicationPath = QString::fromLocal8Bit(argv[1]);
    RedisServerPath = QString::fromLocal8Bit(argv[2]);
    RedisStartupTest test;
    auto arguments = application.arguments();
    arguments.removeAt(1);
    arguments.removeAt(1);
    return QTest::qExec(&test, arguments);
}

#include "RedisStartupTest.moc"
