#include "RedisConfiguration.hpp"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class RedisConfigurationTest final : public QObject
{
    Q_OBJECT

private slots:
    void LoadsValidConfiguration();
    void RejectsInvalidPort();
};

void RedisConfigurationTest::LoadsValidConfiguration()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("Redis.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({"host":"localhost","port":6380,"startupTimeoutMs":2000,"autoStart":false,"redisWorkingDirectory":"."})");
    file.close();

    QString error;
    const auto configuration = iec61850sim::RedisConfiguration::LoadFromFile(path, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(configuration.host, QStringLiteral("localhost"));
    QCOMPARE(configuration.port, static_cast<quint16>(6380));
    QCOMPARE(configuration.startupTimeoutMs, 2000);
    QVERIFY(!configuration.autoStart);
    QCOMPARE(configuration.redisWorkingDirectory, directory.path());
}

void RedisConfigurationTest::RejectsInvalidPort()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = directory.filePath(QStringLiteral("Redis.json"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
    file.write(R"({"host":"127.0.0.1","port":70000,"startupTimeoutMs":2000,"autoStart":true,"redisWorkingDirectory":"."})");
    file.close();

    QString error;
    const auto configuration = iec61850sim::RedisConfiguration::LoadFromFile(path, &error);
    QCOMPARE(configuration.port, static_cast<quint16>(0));
    QVERIFY(!error.isEmpty());
}

QTEST_GUILESS_MAIN(RedisConfigurationTest)

#include "RedisConfigurationTest.moc"
