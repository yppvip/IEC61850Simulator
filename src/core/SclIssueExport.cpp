#include "SclIssueExport.hpp"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace iec61850sim
{
namespace
{
QString CoverageName(Scl::Coverage state)
{
    switch (state)
    {
    case Scl::Coverage::Executed: return QStringLiteral("executed");
    case Scl::Coverage::NotApplicable: return QStringLiteral("notApplicable");
    case Scl::Coverage::Blocked: return QStringLiteral("blocked");
    case Scl::Coverage::NotImplemented: return QStringLiteral("notImplemented");
    }
    return {};
}

QString SeverityName(Scl::Severity severity)
{
    switch (severity)
    {
    case Scl::Severity::Error: return QStringLiteral("error");
    case Scl::Severity::Warning: return QStringLiteral("warning");
    case Scl::Severity::Info: return QStringLiteral("info");
    }
    return {};
}

QByteArray Json(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray Csv(QStringList cells)
{
    for (auto& cell : cells)
    {
        bool prefix = false;
        for (const auto ch : cell)
        {
            if (ch == '\t' || ch == '\r' || ch == '\n')
                prefix = true;
            if (ch.isSpace() || ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format)
                continue;
            prefix |= QStringLiteral("=+-@＝＋－＠").contains(ch);
            break;
        }
        if (prefix)
            cell.prepend('\'');
        cell.replace(QStringLiteral("\""), QStringLiteral("\"\""));
        cell = '"' + cell + '"';
    }
    return cells.join(',').toUtf8() + "\r\n";
}

bool Write(QSaveFile& file, const QByteArray& bytes)
{
    return file.write(bytes) == bytes.size();
}

bool WriteJson(QSaveFile& file, const SclSnapshot& snapshot)
{
    const auto& analysis = snapshot.analysis;
    auto metadata = Json({{"fileName", QFileInfo(snapshot.path).fileName()},
                          {"sha256", QString::fromLatin1(analysis.sha256)}, {"ruleSetVersion", analysis.ruleSetVersion}});
    metadata.chop(1);
    if (!Write(file, metadata + ",\"coverage\":["))
        return false;
    bool first = true;
    for (const auto& rule : analysis.coverage)
    {
        if ((!first && !Write(file, ",")) || !Write(file, Json({{"rule", rule.id},
            {"state", CoverageName(rule.state)}, {"evaluated", rule.evaluated}, {"limited", rule.limited}})))
            return false;
        first = false;
    }
    if (!Write(file, "],\"findings\":["))
        return false;
    first = true;
    for (const auto& issue : analysis.findings)
    {
        if ((!first && !Write(file, ",")) || !Write(file, Json({
            {"rule", issue.rule}, {"severity", SeverityName(issue.severity)}, {"node", issue.node},
            {"line", issue.location.line}, {"byteOffset", issue.location.byteOffset}, {"objectPath", issue.objectPath},
            {"detail", issue.detail}, {"suggestion", issue.suggestion}, {"scope", issue.scope}})))
            return false;
        first = false;
    }
    return Write(file, "]}\n");
}

bool WriteCsv(QSaveFile& file, const SclSnapshot& snapshot)
{
    const auto& analysis = snapshot.analysis;
    const auto hash = QString::fromLatin1(analysis.sha256);
    const auto name = QFileInfo(snapshot.path).fileName();
    const auto version = analysis.ruleSetVersion;
    if (!Write(file, QByteArray::fromHex("efbbbf") +
        "record,fileName,sha256,ruleSetVersion,rule,coverage,evaluated,limited,severity,node,line,byteOffset,objectPath,detail,suggestion,scope\r\n"))
        return false;
    if (!Write(file, Csv({"metadata", name, hash, version, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}})))
        return false;
    for (const auto& rule : analysis.coverage)
        if (!Write(file, Csv({"coverage", name, hash, version, rule.id, CoverageName(rule.state),
            QString::number(rule.evaluated), QString::number(rule.limited), {}, {}, {}, {}, {}, {}, {}, {}})))
            return false;
    for (const auto& issue : analysis.findings)
        if (!Write(file, Csv({"finding", name, hash, version, issue.rule, {}, {}, {}, SeverityName(issue.severity),
            issue.node < 0 ? QString() : QString::number(issue.node), QString::number(issue.location.line),
            issue.location.byteOffset < 0 ? QString() : QString::number(issue.location.byteOffset),
            issue.objectPath, issue.detail, issue.suggestion, issue.scope})))
            return false;
    return true;
}
} // namespace

QString ExportSclIssues(const SclSnapshot& snapshot, const QString& path, IssueFormat format)
{
    const QFileInfo target(path);
    const QFileInfo source(snapshot.path);
    const auto canonical = target.canonicalFilePath();
#ifdef Q_OS_WIN
    constexpr auto PathCase = Qt::CaseInsensitive;
#else
    constexpr auto PathCase = Qt::CaseSensitive;
#endif
    if (target.absoluteFilePath().compare(source.absoluteFilePath(), PathCase) == 0 ||
        (!canonical.isEmpty() && canonical.compare(source.canonicalFilePath(), PathCase) == 0))
        return QStringLiteral("导出路径不能指向当前 SCL 文件。");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return file.errorString();
    const bool written = format == IssueFormat::Json ? WriteJson(file, snapshot) : WriteCsv(file, snapshot);
    if (!written || !file.commit())
        return file.errorString();
    return {};
}
} // namespace iec61850sim
