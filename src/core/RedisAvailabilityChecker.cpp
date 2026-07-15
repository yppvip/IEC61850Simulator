#include "RedisAvailabilityChecker.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>

namespace
{
constexpr int PingTimeoutMs = 1000;
constexpr int RetryIntervalMs = 250;
constexpr auto PingCommand = "*1\r\n$4\r\nPING\r\n";
} // namespace

namespace iec61850sim
{
RedisAvailabilityChecker::RedisAvailabilityChecker(QObject* parent)
    : QObject(parent)
{
    m_connectTimeout.setSingleShot(true);

    connect(&m_socket, &QTcpSocket::connected, this, [this]() {
        m_socket.write(PingCommand);
    });
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]() {
        m_response.append(m_socket.readAll());
        if (m_response.startsWith("+PONG\r\n"))
        {
            m_connectTimeout.stop();
            m_socket.disconnectFromHost();
            SetState(RedisConnectionState::Available,
                QStringLiteral("Redis 已连接：%1:%2。").arg(m_configuration.host).arg(m_configuration.port));
        }
        else if (m_response.startsWith('-'))
        {
            HandleFailure(QStringLiteral("Redis PING 被拒绝：%1").arg(QString::fromUtf8(m_response).trimmed()));
        }
    });
    connect(&m_socket, &QTcpSocket::errorOccurred, this, [this](QTcpSocket::SocketError) {
        HandleFailure(m_socket.errorString());
    });
    connect(&m_connectTimeout, &QTimer::timeout, this, [this]() {
        HandleFailure(QStringLiteral("连接 Redis 超时。"));
    });
}

void RedisAvailabilityChecker::Start()
{
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
    m_response.clear();
    m_connectTimeout.stop();
    m_socket.abort();
    SetState(RedisConnectionState::Checking,
        QStringLiteral("正在检查 Redis 服务：%1:%2…").arg(m_configuration.host).arg(m_configuration.port));
    m_socket.connectToHost(m_configuration.host, m_configuration.port);
    m_connectTimeout.start(PingTimeoutMs);
}

void RedisAvailabilityChecker::HandleFailure(const QString& detail)
{
    m_connectTimeout.stop();
    m_socket.abort();

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
