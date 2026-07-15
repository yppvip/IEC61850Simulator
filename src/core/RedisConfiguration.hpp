#pragma once

#include <QString>

namespace iec61850sim
{
struct RedisConfiguration
{
    QString host;
    quint16 port = 0;
    int startupTimeoutMs = 0;
    bool autoStart = false;
    QString redisWorkingDirectory;

    [[nodiscard]] static RedisConfiguration Load(QString* errorMessage = nullptr);
    [[nodiscard]] static RedisConfiguration LoadFromFile(const QString& configurationPath, QString* errorMessage = nullptr);
};
} // namespace iec61850sim
