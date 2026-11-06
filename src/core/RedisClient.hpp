#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QString>

namespace iec61850sim
{
struct RedisPingResult
{
    bool available = false;
    QString detail;
};

// Each operation owns its hiredis context entirely inside a worker thread.
class RedisClient final : public QObject
{
    Q_OBJECT

public:
    explicit RedisClient(QObject* parent = nullptr);
    void Ping(const QString& host, quint16 port, int timeoutMs);
    [[nodiscard]] bool IsRunning() const;

signals:
    void PingFinished(bool available, const QString& detail);

private:
    QFutureWatcher<RedisPingResult> m_watcher;
    bool m_running = false;
};
} // namespace iec61850sim
