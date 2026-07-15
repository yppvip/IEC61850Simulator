#pragma once

#include "RedisConfiguration.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>

namespace iec61850sim
{
enum class RedisConnectionState
{
    Checking,
    Starting,
    Available,
    Unavailable
};

class RedisAvailabilityChecker final : public QObject
{
    Q_OBJECT

public:
    explicit RedisAvailabilityChecker(QObject* parent = nullptr);

    void Start();

signals:
    void StateChanged(iec61850sim::RedisConnectionState state, const QString& detail);

private:
    void BeginPing();
    void HandleFailure(const QString& detail);
    void SetState(RedisConnectionState state, const QString& detail);
    [[nodiscard]] bool CanStartLocalRedis() const;
    [[nodiscard]] QString FindRedisServiceExecutable() const;

    RedisConfiguration m_configuration;
    QTcpSocket m_socket;
    QTimer m_connectTimeout;
    QElapsedTimer m_startupElapsed;
    QByteArray m_response;
    bool m_startAttempted = false;
};
} // namespace iec61850sim
