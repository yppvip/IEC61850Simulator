#include "SclSearch.hpp"

#include <QSet>
#include <optional>

namespace iec61850sim
{
namespace
{
QString Attribute(const SclSnapshot& snapshot, Scl::NodeId node, const char* name)
{
    return node < 0 ? QString() : snapshot.analysis.document->attribute(node, QString::fromLatin1(name));
}

QStringList Values(const SclSnapshot& snapshot, const SclHit& hit)
{
    const auto& analysis = snapshot.analysis;
    const auto& doc = *analysis.document;
    switch (hit.field)
    {
    case MatchField::Node: {
        const auto& name = doc.nodes[hit.node].name;
        if (name == "LN")
            return {name, Attribute(snapshot, hit.node, "prefix") + Attribute(snapshot, hit.node, "lnClass")
                          + Attribute(snapshot, hit.node, "inst")};
        if (name == "LN0")
            return {name, QStringLiteral("LLN0")};
        return {name};
    }
    case MatchField::Attribute:
        return {doc.attributes[hit.detail].value};
    case MatchField::Address:
        return {doc.nodes[hit.detail].text};
    case MatchField::Owner:
        return {Attribute(snapshot, snapshot.browse.iedOwners[hit.node], "name")};
    case MatchField::Reference: {
        const auto& ref = analysis.references[hit.detail];
        return {ref.objectReference, ref.mmsDomain, ref.mmsItem, ref.declared,
                ref.mmsDomain.isEmpty() ? QString() : ref.mmsDomain + '/' + ref.mmsItem};
    }
    case MatchField::Report: {
        const auto& report = analysis.reports[hit.detail];
        return {report.rptId, report.declaredDataSet, report.definitionReference, report.dataSetReference,
                report.mmsDomain, report.mmsItem,
                report.mmsDomain.isEmpty() ? QString() : report.mmsDomain + '/' + report.mmsItem};
    }
    case MatchField::Finding: {
        const auto& issue = analysis.findings[hit.detail];
        return {issue.rule, issue.objectPath, issue.detail, issue.suggestion, issue.scope};
    }
    }
    return {};
}

bool Matches(const QString& value, const SclQuery& query)
{
    return query.text.isEmpty() || (query.exact ? value.compare(query.text, Qt::CaseInsensitive) == 0
                                               : value.contains(query.text, Qt::CaseInsensitive));
}

bool MatchesScope(const SclSnapshot& snapshot, Scl::NodeId node, const SclQuery& query)
{
    const auto& doc = *snapshot.analysis.document;
    return snapshot.browse.InScope(node, query.browseIed)
        && (query.ied.isEmpty() || (node >= 0 && (Attribute(snapshot, snapshot.browse.iedOwners[node], "name")
                                      .compare(query.ied, Qt::CaseInsensitive) == 0
                                   || Attribute(snapshot, node, "iedName").compare(query.ied, Qt::CaseInsensitive) == 0)))
        && (query.type.isEmpty() || (node >= 0 && doc.nodes[node].name.compare(query.type, Qt::CaseInsensitive) == 0))
        && (query.fc.isEmpty() || Attribute(snapshot, node, "fc").compare(query.fc, Qt::CaseInsensitive) == 0);
}

std::optional<SclHit> MatchNode(const SclSnapshot& snapshot, Scl::NodeId id, const SclQuery& query,
                                Scl::TaskControl& task, int row = -1)
{
    const auto& doc = *snapshot.analysis.document;
    const auto& node = doc.nodes[id];
    if (query.text.isEmpty())
        return SclHit{id, MatchField::Node, 0, row};

    // Search the structured model and communication address values. Raw XML
    // and unrestricted extension text remain separate from model search.
    static const QSet<QString> Fields = {
        "name", "desc", "id", "inst", "prefix", "lnClass", "lnInst", "lnType", "type", "bType",
        "fc", "rptID", "datSet", "cbName", "iedName", "apName", "ldInst", "ldName", "doName",
        "daName", "intAddr", "appID", "smvID", "srcCBName", "manufacturer"
    };
    for (int i = node.firstAttribute; i < node.firstAttribute + node.attributeCount; ++i)
    {
        Scl::checkpoint(&task);
        if (Fields.contains(doc.attributes[i].name) && Matches(doc.attributes[i].value, query))
            return SclHit{id, MatchField::Attribute, i, row};
    }
    if (Matches(node.name, query))
        return SclHit{id, MatchField::Node, 0, row};
    if (node.name == "LN" || node.name == "LN0")
    {
        const SclHit hit{id, MatchField::Node, 0, row};
        if (Matches(Values(snapshot, hit).last(), query))
            return hit;
    }
    if (node.isScl() && node.name == "P" && Matches(node.text, query))
        return SclHit{id, MatchField::Address, id, row};
    if (query.scope == SearchScope::Communication)
    {
        const auto& branch = snapshot.browse.branches[id];
        for (int i = 0; i < branch.count; ++i)
        {
            Scl::checkpoint(&task);
            const auto child = snapshot.browse.children[branch.first + i];
            if (!doc.nodes[child].isScl() || doc.nodes[child].name != "Address")
                continue;
            const auto& fields = snapshot.browse.branches[child];
            for (int j = 0; j < fields.count; ++j)
            {
                Scl::checkpoint(&task);
                const auto field = snapshot.browse.children[fields.first + j];
                if (doc.nodes[field].isScl() && doc.nodes[field].name == "P" && Matches(doc.nodes[field].text, query))
                    return SclHit{id, MatchField::Address, field, row};
            }
        }
    }
    if (query.scope != SearchScope::All && Matches(Attribute(snapshot, snapshot.browse.iedOwners[id], "name"), query))
        return SclHit{id, MatchField::Owner, 0, row};
    return {};
}

bool MatchesHit(const SclSnapshot& snapshot, const SclHit& hit, const SclQuery& query)
{
    if (query.text.isEmpty())
        return true;
    for (const auto& value : Values(snapshot, hit))
        if (Matches(value, query))
            return true;
    return false;
}
} // namespace

void SearchScl(const SclSnapshot& snapshot, const SclQuery& query, Scl::TaskControl& task,
               const std::function<void(SclHitBatch)>& publish)
{
    const auto& analysis = snapshot.analysis;
    SclHitBatch batch;
    int scanned = 0;
    const auto flush = [&] {
        if (!batch.empty())
        {
            publish(std::move(batch));
            batch.clear();
        }
    };
    const auto tick = [&] {
        Scl::checkpoint(&task);
        if (++scanned % 1024 == 0)
            flush();
    };
    const auto append = [&](SclHit hit) {
        batch.push_back(hit);
        if (batch.size() >= 512)
            flush();
    };
    const auto findings = [&] {
        for (int i = 0; i < int(analysis.findings.size()); ++i)
        {
            tick();
            const auto& issue = analysis.findings[i];
            const SclHit hit{issue.node, MatchField::Finding, i, i};
            if ((query.severity < 0 || query.severity == int(issue.severity))
                && MatchesScope(snapshot, issue.node, query) && MatchesHit(snapshot, hit, query))
                append(hit);
        }
    };
    if (query.scope == SearchScope::Findings || (query.scope == SearchScope::All && query.severity >= 0))
    {
        findings();
        flush();
        return;
    }
    if (query.severity >= 0)
        return;

    if (query.scope == SearchScope::All)
    {
        // Prioritize report IDs and derived mappings before scanning the raw
        // model so common queries can display results while the scan continues.
        std::vector<bool> seen(analysis.document->nodes.size());
        for (int i = 0; i < int(analysis.reports.size()); ++i)
        {
            tick();
            const SclHit hit{analysis.reports[i].source, MatchField::Report, i};
            if (MatchesScope(snapshot, hit.node, query) && MatchesHit(snapshot, hit, query))
            {
                seen[hit.node] = true;
                append(hit);
            }
        }
        findings();
        for (auto id : snapshot.browse.addressRows)
        {
            tick();
            const SclHit hit{id, MatchField::Address, id};
            if (MatchesScope(snapshot, id, query) && MatchesHit(snapshot, hit, query))
            {
                seen[id] = true;
                append(hit);
            }
        }
        flush();
        for (int i = 0; i < int(analysis.references.size()); ++i)
        {
            tick();
            const SclHit hit{analysis.references[i].source, MatchField::Reference, i};
            if (!seen[hit.node] && MatchesScope(snapshot, hit.node, query) && MatchesHit(snapshot, hit, query))
            {
                seen[hit.node] = true;
                append(hit);
            }
        }
        for (Scl::NodeId id = 0; id < int(seen.size()); ++id)
        {
            tick();
            if (!seen[id] && MatchesScope(snapshot, id, query))
                if (auto hit = MatchNode(snapshot, id, query, task))
                    append(*hit);
        }
        flush();
        return;
    }

    const auto scan = [&](Scl::NodeId id, int row, std::optional<SclHit> preferred = {}) {
        tick();
        if (!MatchesScope(snapshot, id, query))
            return;
        if (preferred && MatchesHit(snapshot, *preferred, query))
            append(*preferred);
        else if (auto hit = MatchNode(snapshot, id, query, task, row))
            append(*hit);
    };
    if (query.scope == SearchScope::Reports)
    {
        for (int i = 0; i < int(analysis.reports.size()); ++i)
        {
            const auto id = analysis.reports[i].source;
            scan(id, i, SclHit{id, MatchField::Report, i, i});
        }
    }
    else if (query.scope == SearchScope::Members)
    {
        const auto range = snapshot.browse.dataSetMembers.value(query.dataSet);
        for (int i = 0; i < range.count; ++i)
        {
            const auto id = snapshot.browse.memberRows[range.first + i];
            const auto ref = analysis.outgoing.constFind(id);
            scan(id, i, ref == analysis.outgoing.cend() ? std::nullopt
                : std::optional<SclHit>{SclHit{id, MatchField::Reference, *ref, i}});
        }
    }
    else
    {
        const auto category = query.scope == SearchScope::Objects ? SclCategory::Objects
            : query.scope == SearchScope::Communication ? SclCategory::Communication
            : query.scope == SearchScope::Templates ? SclCategory::Templates : SclCategory::DataSets;
        const auto& rows = snapshot.browse.categories[size_t(category)];
        for (int i = 0; i < int(rows.size()); ++i)
            scan(rows[i], i);
    }
    flush();
}

QString SearchSnippet(const SclSnapshot& snapshot, const SclHit& hit, const QString& needle)
{
    auto values = Values(snapshot, hit);
    QString value;
    int at = -1;
    for (const auto& entry : values)
    {
        if (value.isEmpty())
            value = entry;
        if (!needle.isEmpty() && (at = entry.indexOf(needle, 0, Qt::CaseInsensitive)) >= 0)
        {
            value = entry;
            break;
        }
    }
    const int start = qMax(0, at - 45);
    auto text = value.mid(start, 160);
    if (start > 0)
        text.prepend(QChar(0x2026));
    if (start + 160 < value.size())
        text.append(QChar(0x2026));
    return text;
}

QString SearchField(const SclSnapshot& snapshot, const SclHit& hit)
{
    switch (hit.field)
    {
    case MatchField::Node: return QStringLiteral("元素");
    case MatchField::Attribute: return snapshot.analysis.document->attributes[hit.detail].name;
    case MatchField::Address: return Attribute(snapshot, hit.detail, "type");
    case MatchField::Owner: return QStringLiteral("所属 IED");
    case MatchField::Reference: return QStringLiteral("引用 / MMS");
    case MatchField::Report: return QStringLiteral("报告映射");
    case MatchField::Finding: return snapshot.analysis.findings[hit.detail].rule;
    }
    return {};
}
} // namespace iec61850sim
