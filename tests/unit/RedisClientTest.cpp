#include "RedisClient.hpp"

#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtTest>

class RedisClientTest final : public QObject
{
    Q_OBJECT

private slots:
    void Replies_data();
    void Replies();
    void RefusedConnection();
    void TimeoutKeepsEventLoopResponsive();
    void DestroyDuringPing();
};

void RedisClientTest::Replies_data()
{
    QTest::addColumn<QByteArray>("response");
    QTest::addColumn<bool>("available");
    QTest::newRow("pong") << QByteArray("+PONG\r\n") << true;
    QTest::newRow("redis-error") << QByteArray("-NOAUTH Authentication required\r\n") << false;
    QTest::newRow("wrong-status") << QByteArray("+OK\r\n") << false;
    QTest::newRow("wrong-type") << QByteArray("$4\r\nPONG\r\n") << false;
    QTest::newRow("malformed") << QByteArray("?invalid\r\n") << false;
}

void RedisClientTest::Replies()
{
    QFETCH(QByteArray, response);
    QFETCH(bool, available);
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    QByteArray command;
    connect(&server, &QTcpServer::newConnection, &server, [&]() {
        auto* socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [&, socket]() {
            command += socket->readAll();
            if (command.endsWith("PING\r\n"))
            {
                socket->write(response.left(2));
                QTimer::singleShot(20, socket, [socket, response]() { socket->write(response.mid(2)); });
            }
        });
    });
    iec61850sim::RedisClient client;
    QSignalSpy finished(&client, &iec61850sim::RedisClient::PingFinished);
    client.Ping(QStringLiteral("127.0.0.1"), server.serverPort(), 500);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
    QCOMPARE(command, QByteArray("*1\r\n$4\r\nPING\r\n"));
    QCOMPARE(finished.first().at(0).toBool(), available);
    QVERIFY(!finished.first().at(1).toString().isEmpty());
}

void RedisClientTest::RefusedConnection()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    const auto port = server.serverPort();
    server.close();
    iec61850sim::RedisClient client;
    QSignalSpy finished(&client, &iec61850sim::RedisClient::PingFinished);
    client.Ping(QStringLiteral("127.0.0.1"), port, 100);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
    QVERIFY(!finished.first().at(0).toBool());
}

void RedisClientTest::TimeoutKeepsEventLoopResponsive()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    int ticks = 0;
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, &heartbeat, [&]() { ++ticks; });
    heartbeat.start(10);
    iec61850sim::RedisClient client;
    QSignalSpy finished(&client, &iec61850sim::RedisClient::PingFinished);
    client.Ping(QStringLiteral("127.0.0.1"), server.serverPort(), 150);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
    QVERIFY(!finished.first().at(0).toBool());
    QVERIFY(ticks >= 3);
}

void RedisClientTest::DestroyDuringPing()
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    auto* client = new iec61850sim::RedisClient;
    client->Ping(QStringLiteral("127.0.0.1"), server.serverPort(), 150);
    QElapsedTimer elapsed;
    elapsed.start();
    delete client;
    QVERIFY(elapsed.elapsed() < 100);
    QTest::qWait(250);
}

QTEST_GUILESS_MAIN(RedisClientTest)
#include "RedisClientTest.moc"
