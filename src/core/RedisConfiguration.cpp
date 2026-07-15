#include "RedisConfiguration.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace
{
QString FindConfigurationPath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config/Redis.json"));
}
} // namespace

namespace iec61850sim
{
RedisConfiguration RedisConfiguration::Load(QString* errorMessage)
{
    const QString configurationPath = FindConfigurationPath();
    if (!QFile::exists(configurationPath))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("未找到运行时 Redis 配置：%1").arg(configurationPath);
        }
        return {};
    }

    return LoadFromFile(configurationPath, errorMessage);
}

RedisConfiguration RedisConfiguration::LoadFromFile(const QString& configurationPath, QString* errorMessage)
{
    QFile configurationFile(configurationPath);
    if (!configurationFile.open(QIODevice::ReadOnly))
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("无法读取 Redis 配置：%1").arg(configurationPath);
        }
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(configurationFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Redis 配置格式无效：%1").arg(parseError.errorString());
        }
        return {};
    }

    const QJsonObject object = document.object();
    RedisConfiguration configuration;
    configuration.host = object.value(QStringLiteral("host")).toString();
    const int port = object.value(QStringLiteral("port")).toInt();
    configuration.port = static_cast<quint16>(port);
    configuration.startupTimeoutMs = object.value(QStringLiteral("startupTimeoutMs")).toInt();
    configuration.autoStart = object.value(QStringLiteral("autoStart")).toBool();
    const QString configuredWorkingDirectory = object.value(QStringLiteral("redisWorkingDirectory")).toString();
    if (QDir::isAbsolutePath(configuredWorkingDirectory))
    {
        configuration.redisWorkingDirectory = QDir::cleanPath(configuredWorkingDirectory);
    }
    else
    {
        configuration.redisWorkingDirectory = QDir::cleanPath(
            QFileInfo(configurationPath).dir().absoluteFilePath(configuredWorkingDirectory));
    }

    if (configuration.host.isEmpty() || port <= 0 || port > 65535 || configuration.startupTimeoutMs <= 0
        || configuredWorkingDirectory.isEmpty())
    {
        if (errorMessage != nullptr)
        {
            *errorMessage = QStringLiteral("Redis 配置必须包含有效的 host、port、startupTimeoutMs 和 redisWorkingDirectory。");
        }
        return {};
    }

    return configuration;
}
} // namespace iec61850sim
