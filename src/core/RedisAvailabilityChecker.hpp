#pragma once

#include "RedisConfiguration.hpp"
#include "RedisClient.hpp"

#include <QElapsedTimer>
#include <QObject>
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
    RedisClient m_client;
    QElapsedTimer m_startupElapsed;
    bool m_startAttempted = false;
    bool m_checking = false;
};
} // namespace iec61850sim
