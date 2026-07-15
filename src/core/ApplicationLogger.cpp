#include "ApplicationLogger.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

namespace
{
QMutex LogMutex;

QString MessageTypeName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("UNKNOWN");
}

void WriteLogMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    QMutexLocker locker(&LogMutex);
    const QString logDirectory = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("logs"));
    QDir().mkpath(logDirectory);

    QFile logFile(QDir(logDirectory).filePath(QStringLiteral("IEC61850Simulator.log")));
    if (!logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    const QString category = context.category == nullptr ? QStringLiteral("default") : QString::fromLatin1(context.category);
    const QString line = QStringLiteral("%1 [%2] [%3] %4\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), MessageTypeName(type), category, message);
    logFile.write(line.toUtf8());
}
} // namespace

namespace iec61850sim
{
void InstallApplicationLogger()
{
    qInstallMessageHandler(WriteLogMessage);
}
} // namespace iec61850sim
