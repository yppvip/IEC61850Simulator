#include "RedisClient.hpp"

#include <QtConcurrent/QtConcurrentRun>
#include <hiredis.h>

#include <cstring>
#include <memory>

namespace
{
iec61850sim::RedisPingResult ExecutePing(const QString& host, quint16 port, int timeoutMs)
{
    if (host.isEmpty() || port == 0 || timeoutMs <= 0)
    {
        return {false, QStringLiteral("Redis PING 参数无效。")};
    }

    const QByteArray address = host.toUtf8();
    const timeval timeout = {timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    redisOptions options{};
    REDIS_OPTIONS_SET_TCP(&options, address.constData(), port);
    options.connect_timeout = &timeout;
    options.command_timeout = &timeout;
    const std::unique_ptr<redisContext, decltype(&redisFree)> context(redisConnectWithOptions(&options), redisFree);
    if (!context)
    {
        return {false, QStringLiteral("无法分配 Redis 连接。")};
    }
    if (context->err)
    {
        return {false, QStringLiteral("连接 Redis 失败：%1").arg(QString::fromUtf8(context->errstr))};
    }

    const std::unique_ptr<redisReply, decltype(&freeReplyObject)> reply(
        static_cast<redisReply*>(redisCommand(context.get(), "PING")), freeReplyObject);
    if (!reply)
    {
        return {false, QStringLiteral("Redis PING 失败：%1").arg(QString::fromUtf8(context->errstr))};
    }
    if (reply->type == REDIS_REPLY_STATUS && reply->len == 4 && std::memcmp(reply->str, "PONG", 4) == 0)
    {
        return {true, QStringLiteral("Redis 已连接：%1:%2。").arg(host).arg(port)};
    }
    if (reply->type == REDIS_REPLY_ERROR)
    {
        return {false, QStringLiteral("Redis PING 被拒绝：%1")
            .arg(QString::fromUtf8(reply->str, static_cast<int>(reply->len)))};
    }
    return {false, QStringLiteral("Redis PING 返回非预期响应（类型 %1）。").arg(reply->type)};
}
} // namespace

namespace iec61850sim
{
RedisClient::RedisClient(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<RedisPingResult>::finished, this, [this]() {
        const auto result = m_watcher.result();
        m_running = false;
        emit PingFinished(result.available, result.detail);
    });
}

void RedisClient::Ping(const QString& host, quint16 port, int timeoutMs)
{
    if (!IsRunning())
    {
        m_running = true;
        m_watcher.setFuture(QtConcurrent::run(ExecutePing, host, port, timeoutMs));
    }
}

bool RedisClient::IsRunning() const
{
    return m_running;
}
} // namespace iec61850sim
