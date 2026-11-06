#include "RedisAvailabilityChecker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QDebug>

namespace
{
constexpr int PingTimeoutMs = 1000;
constexpr int RetryIntervalMs = 250;
} // namespace

namespace iec61850sim
{
RedisAvailabilityChecker::RedisAvailabilityChecker(QObject* parent)
    : QObject(parent)
{
    connect(&m_client, &RedisClient::PingFinished, this, [this](bool available, const QString& detail) {
        if (available)
        {
            SetState(RedisConnectionState::Available, detail);
        }
        else
        {
            HandleFailure(detail);
        }
    });
}

void RedisAvailabilityChecker::Start()
{
    // A startup cycle includes both the worker operation and delayed retries.
    if (m_checking)
    {
        return;
    }
    m_checking = true;
    QString configurationError;
    m_configuration = RedisConfiguration::Load(&configurationError);
    m_startAttempted = false;
    m_startupElapsed.invalidate();

    if (!configurationError.isEmpty())
    {
        SetState(RedisConnectionState::Unavailable, configurationError);
        return;
    }

    BeginPing();
}

void RedisAvailabilityChecker::BeginPing()
{
    SetState(RedisConnectionState::Checking,
        QStringLiteral("正在检查 Redis 服务：%1:%2…").arg(m_configuration.host).arg(m_configuration.port));
    m_client.Ping(m_configuration.host, m_configuration.port, PingTimeoutMs);
}

void RedisAvailabilityChecker::HandleFailure(const QString& detail)
{
    if (!m_startAttempted && CanStartLocalRedis())
    {
        m_startAttempted = true;
        const QString executable = FindRedisServiceExecutable();
        if (executable.isEmpty())
        {
            SetState(RedisConnectionState::Unavailable, QStringLiteral("未找到 tools/Redis/RedisService.exe。"));
            return;
        }

        const QString serviceDirectory = QFileInfo(executable).absolutePath();
        const QString redisConfiguration = QDir(serviceDirectory).filePath(QStringLiteral("redis.conf"));
        if (!QFileInfo::exists(redisConfiguration))
        {
            SetState(RedisConnectionState::Unavailable,
                QStringLiteral("未找到 Redis 服务配置：%1").arg(redisConfiguration));
            return;
        }

        const QStringList arguments = {
            QStringLiteral("run"),
            QStringLiteral("--config"),
            redisConfiguration,
            QStringLiteral("--port"),
            QString::number(m_configuration.port)
        };
        if (!QProcess::startDetached(executable, arguments, serviceDirectory))
        {
            SetState(RedisConnectionState::Unavailable, QStringLiteral("无法启动 RedisService.exe：%1").arg(detail));
            return;
        }

        m_startupElapsed.start();
        SetState(RedisConnectionState::Starting, QStringLiteral("正在启动本地 Redis 服务…"));
        QTimer::singleShot(RetryIntervalMs, this, &RedisAvailabilityChecker::BeginPing);
        return;
    }

    if (m_startAttempted && m_startupElapsed.isValid() && m_startupElapsed.elapsed() < m_configuration.startupTimeoutMs)
    {
        SetState(RedisConnectionState::Starting, QStringLiteral("正在等待本地 Redis 服务响应…"));
        QTimer::singleShot(RetryIntervalMs, this, &RedisAvailabilityChecker::BeginPing);
        return;
    }

    SetState(RedisConnectionState::Unavailable, QStringLiteral("Redis 数据库不可用：%1").arg(detail));
}

void RedisAvailabilityChecker::SetState(RedisConnectionState state, const QString& detail)
{
    if (state == RedisConnectionState::Available || state == RedisConnectionState::Unavailable)
    {
        m_checking = false;
    }
    if (state == RedisConnectionState::Unavailable)
    {
        qWarning().noquote() << detail;
    }
    else
    {
        qInfo().noquote() << detail;
    }
    emit StateChanged(state, detail);
}

bool RedisAvailabilityChecker::CanStartLocalRedis() const
{
    if (!m_configuration.autoStart)
    {
        return false;
    }

    return m_configuration.host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0
        || m_configuration.host == QStringLiteral("127.0.0.1")
        || m_configuration.host == QStringLiteral("::1");
}

QString RedisAvailabilityChecker::FindRedisServiceExecutable() const
{
    const QString candidate = QDir(m_configuration.redisWorkingDirectory).filePath(QStringLiteral("RedisService.exe"));
    return QFileInfo::exists(candidate) ? candidate : QString{};
}
} // namespace iec61850sim
