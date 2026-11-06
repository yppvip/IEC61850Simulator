#include "SclTopology.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryFile>
#include <QDir>
#include <QSet>
#include <algorithm>

static bool InitSclTopologyResource()
{
    Q_INIT_RESOURCE(SclTopologyResources);
    return true;
}

namespace iec61850sim
{
namespace
{
using namespace Scl;
constexpr int GroupLimit = 24;
constexpr int NodeLimit = 768;
constexpr int ViewLimit = 160;

QString Bounded(const QString& text, int limit)
{
    if (text.size() <= limit)
        return text;
    return text.left(limit - text.at(limit - 1).isHighSurrogate()) + QStringLiteral("…");
}

QString State(LinkState state)
{
    switch (state)
    {
    case LinkState::Resolved: return QStringLiteral("已解析");
    case LinkState::Missing: return QStringLiteral("缺失");
    case LinkState::Ambiguous: return QStringLiteral("有歧义");
    case LinkState::Unbound: return QStringLiteral("未绑定");
    case LinkState::External: return QStringLiteral("文件外");
    case LinkState::Unsupported: return QStringLiteral("未覆盖");
    case LinkState::Blocked: return QStringLiteral("前置条件失败");
    case LinkState::Partial: return QStringLiteral("部分解析");
    }
    return {};
}

QString Field(FcdaField field)
{
    switch (field)
    {
    case FcdaField::Ied: return QStringLiteral("IED 作用域");
    case FcdaField::LdInst: return QStringLiteral("ldInst");
    case FcdaField::LogicalNode: return QStringLiteral("prefix / lnClass / lnInst");
    case FcdaField::DoName: return QStringLiteral("doName");
    case FcdaField::DaName: return QStringLiteral("daName");
    case FcdaField::Fc: return QStringLiteral("fc");
    case FcdaField::Ix: return QStringLiteral("ix / 内联索引");
    }
    return {};
}

struct Group
{
    Group(QString name) : label(std::move(name)) {}
    QString label;
    int total = 0;
    QJsonArray edges;

    void Add(int from, int to, const char* kind, const QString& name, const QString& detail = {})
    {
        ++total;
        if (from >= 0 && to >= 0)
            edges.append(QJsonObject{{"from", from}, {"to", to}, {"kind", QString::fromLatin1(kind)},
                                     {"label", name}, {"detail", Bounded(detail, 1024)}});
    }
};

class Graph
{
public:
    explicit Graph(const SclSnapshot& snapshot)
        : m_snapshot(snapshot), m_analysis(snapshot.analysis), m_document(*m_analysis.document) {}

    QJsonObject Build(NodeId id)
    {
        const int focus = Xml(id, true);
        for (size_t i = 0; i < m_queue.size(); ++i)
        {
            const auto pending = m_queue[i];
            auto groups = pending.object ? ObjectView(m_analysis.references[pending.id], pending.graph)
                                         : NodeView(pending.id, pending.graph);
            m_views.insert(QString::number(pending.graph), groups);
        }
        return {{"version", 1}, {"file", QFileInfo(m_snapshot.path).fileName()},
                {"sha256", QString::fromLatin1(m_analysis.sha256)}, {"focus", focus},
                {"nodes", m_nodes}, {"views", m_views}, {"visibleLimit", 80}};
    }

private:
    struct Pending { int id; int graph; bool object; };

    QString Attr(NodeId id, const char* name) const
    {
        return m_document.attribute(id, QString::fromLatin1(name));
    }

    QString Title(NodeId id) const
    {
        const auto& node = m_document.nodes[id];
        for (const auto* key : {"name", "inst", "id", "lnClass", "doName"})
        {
            const auto name = Attr(id, key);
            if (!name.isEmpty())
                return node.name + QStringLiteral(" · ") + Bounded(name, 90);
        }
        return node.name;
    }

    QString Scope(NodeId id) const
    {
        QStringList parts;
        for (; id != NoNode && parts.size() < 12; id = m_document.nodes[id].parent)
            parts.prepend(Title(id));
        return (id == NoNode ? QString() : QStringLiteral("… / ")) + parts.join(QStringLiteral(" / "));
    }

    QJsonObject Position(NodeId id) const
    {
        const int line = m_document.nodes[id].source.line;
        QJsonArray excerpt;
        if (line > 0)
        {
            for (int n = qMax(1, line - 1); n <= qMin(m_analysis.source->lineCount(), line + 1); ++n)
            {
                auto source = m_analysis.source->line(n);
                while (source.text.endsWith('\r') || source.text.endsWith('\n'))
                    source.text.chop(1);
                excerpt.append(QJsonObject{{"line", n}, {"text", Bounded(source.text, 420)},
                    {"clipped", source.truncated || source.text.size() > 420}});
            }
        }
        return {{"line", line}, {"scope", Scope(id)}, {"excerpt", excerpt}};
    }

    int Add(QJsonObject node)
    {
        if (m_nodes.size() >= NodeLimit)
            return -1;
        const int id = m_nodes.size();
        m_nodes.append(node);
        return id;
    }

    int Annotation(const QString& title, const char* lane, const QString& detail, NodeId source,
                   const QString& copy = {})
    {
        auto node = Position(source);
        node.insert("title", Bounded(title, 140));
        node.insert("lane", QString::fromLatin1(lane));
        node.insert("detail", Bounded(detail, 2048));
        if (!copy.isEmpty() && copy.size() <= 8192)
            node.insert("copy", copy);
        else if (copy.size() > 8192)
            node.insert("copyLimited", true);
        return Add(std::move(node));
    }

    void Enqueue(int id, int graph, bool object)
    {
        if (graph >= 0 && m_queue.size() < ViewLimit && !m_scheduled.contains(graph))
        {
            m_scheduled.insert(graph);
            m_queue.push_back({id, graph, object});
        }
    }

    int Xml(NodeId id, bool expand = false)
    {
        if (id == NoNode)
            return -1;
        if (!m_xml.contains(id) && m_nodes.size() < NodeLimit)
        {
            const auto& element = m_document.nodes[id];
            auto data = Position(id);
            data.insert("title", Title(id));
            data.insert("kind", element.name);
            QStringList evidence;
            for (const auto* field : {"name", "inst", "ldName", "prefix", "lnClass", "lnType", "type",
                                      "fc", "count", "datSet", "rptID"})
            {
                const auto value = Attr(id, field);
                if (!value.isNull())
                    evidence.push_back(QString::fromLatin1(field) + '=' + Bounded(value, 160));
            }
            if (!evidence.isEmpty())
                data.insert("detail", evidence.join('\n'));
            const bool declaration = element.name == "ReportControl" || element.name == "DataSet" || element.name == "FCDA";
            data.insert("lane", declaration ? "declaration" : "evidence");
            data.insert("node", id);
            m_xml.insert(id, Add(std::move(data)));
        }
        const int graph = m_xml.value(id, -1);
        if (expand)
            Enqueue(id, graph, false);
        return graph;
    }

    int FcdaRef(NodeId id) const
    {
        const auto found = m_analysis.outgoing.constFind(id);
        if (found != m_analysis.outgoing.cend() && m_analysis.references[*found].kind == LinkKind::Fcda)
            return *found;
        return -1;
    }

    QString ObjectKey(const Reference& ref) const
    {
        QStringList key{QString::number(ref.logicalNode), QString::number(ref.arrayIndex)};
        const auto trace = m_analysis.fcdaTraces.constFind(ref.source);
        if (trace != m_analysis.fcdaTraces.cend())
            for (const auto& step : trace->steps)
                if (step.field == FcdaField::DoName || step.field == FcdaField::DaName || step.field == FcdaField::Ix)
                    key.push_back(QString::number(step.node));
        return key.join('/') + ':' + Attr(ref.source, "fc");
    }

    int Object(int reference)
    {
        const auto& ref = m_analysis.references[reference];
        const auto key = ObjectKey(ref);
        if (const auto found = m_objects.constFind(key); found != m_objects.cend())
            return *found;
        const auto title = ref.objectReference.isEmpty() ? QStringLiteral("已解析对象实例") : ref.objectReference;
        const auto detail = QStringLiteral("实例：%1\nFC：%2\n数组下标：%3\nMMS 组件：%4").arg(
            Scope(ref.logicalNode), Attr(ref.source, "fc"),
            ref.arrayIndex < 0 ? QStringLiteral("未指定") : QString::number(ref.arrayIndex), ref.mmsComponent);
        const int graph = Annotation(title, "derived", detail, ref.source, ref.objectReference);
        if (graph >= 0)
            m_objects.insert(key, graph);
        Enqueue(reference, graph, true);
        return graph;
    }

    static void Append(QJsonArray& output, Group group)
    {
        if (group.total)
            output.append(QJsonObject{{"label", group.label}, {"total", group.total}, {"edges", group.edges}});
    }

    QJsonArray ObjectView(const Reference& ref, int focus)
    {
        QJsonArray groups;
        Group target(QStringLiteral("对象实例和模板定义"));
        const auto definition = ref.candidates.front();
        target.Add(focus, Xml(definition, true), "reference", QStringLiteral("采用模板定义"));
        target.Add(focus, Xml(ref.logicalNode), "reference", QStringLiteral("所属 LN 实例"));
        Append(groups, std::move(target));
        Group peers(QStringLiteral("引用同一对象实例的 FCDA"));
        const auto key = ObjectKey(ref);
        const auto range = m_analysis.incoming.equal_range(definition);
        for (auto it = range.first; it != range.second; ++it)
        {
            const auto& other = m_analysis.references[*it];
            if (other.kind != LinkKind::Fcda || other.logicalNode != ref.logicalNode || ObjectKey(other) != key)
                continue;
            const bool room = peers.total < GroupLimit;
            peers.Add(room ? Xml(other.source, true) : -1, focus, "reference",
                QStringLiteral("FCDA #%1").arg(other.memberOrdinal));
        }
        Append(groups, std::move(peers));
        return groups;
    }

    void ReferenceGroup(QJsonArray& groups, const Reference& ref, int focus)
    {
        const auto label = ref.kind == LinkKind::ControlDataSet ? QStringLiteral("控制块引用数据集")
            : QStringLiteral("引用 · %1").arg(Bounded(ref.declared, 100));
        Group group(label);
        const auto state = State(ref.state);
        if (ref.state == LinkState::Resolved && !ref.candidates.isEmpty())
            group.Add(focus, Xml(ref.candidates.front(), true), "reference", state, ref.declared);
        else
        {
            const int status = Annotation(state, "evidence", ref.declared + '\n' + ref.detail, ref.source, ref.declared);
            group.Add(focus, status, "status", state, ref.detail);
            for (int i = 0; i < ref.candidates.size(); ++i)
                group.Add(status, i < GroupLimit ? Xml(ref.candidates[i]) : -1, "candidate", QStringLiteral("候选"), ref.detail);
        }
        Append(groups, std::move(group));
    }

    QString FieldValue(const Reference& ref, FcdaField field) const
    {
        switch (field)
        {
        case FcdaField::Ied: return QStringLiteral("由数据集所属服务器确定");
        case FcdaField::LdInst:
        {
            const auto value = Attr(ref.source, "ldInst");
            return value.isEmpty() ? QStringLiteral("使用数据集所属 LD") : value;
        }
        case FcdaField::LogicalNode:
        {
            QStringList parts;
            for (const auto* key : {"prefix", "lnClass", "lnInst"})
            {
                const auto value = Attr(ref.source, key);
                parts.push_back(QString::fromLatin1(key) + '=' + (value.isNull() ? QStringLiteral("未声明") : value));
            }
            return parts.join('\n');
        }
        case FcdaField::DoName: return Attr(ref.source, "doName");
        case FcdaField::DaName: return Attr(ref.source, "daName");
        case FcdaField::Fc: return Attr(ref.source, "fc");
        case FcdaField::Ix:
        {
            const auto value = Attr(ref.source, "ix");
            if (!value.isNull())
                return value;
            for (const auto* key : {"doName", "daName"})
                if (const auto name = Attr(ref.source, key); name.contains('('))
                    return QString::fromLatin1(key) + '=' + name;
            return {};
        }
        }
        return {};
    }

    void FcdaGroups(QJsonArray& groups, int reference, int focus)
    {
        const auto& ref = m_analysis.references[reference];
        const auto trace = m_analysis.fcdaTraces.constFind(ref.source);
        Group scope(QStringLiteral("所属数据集"));
        scope.Add(Xml(m_document.nodes[ref.source].parent, true), focus, "contains",
            QStringLiteral("FCDA #%1").arg(ref.memberOrdinal));
        Append(groups, std::move(scope));
        if (ref.state == LinkState::Resolved)
        {
            Group derived(QStringLiteral("对象实例和派生映射"));
            const int object = Object(reference);
            derived.Add(focus, object, "derived", QStringLiteral("解析到对象实例"));
            for (const auto& field : {qMakePair(QStringLiteral("MMS domain"), ref.mmsDomain),
                                      qMakePair(QStringLiteral("MMS item"), ref.mmsItem),
                                      qMakePair(QStringLiteral("MMS 组件"), ref.mmsComponent)})
                if (!field.second.isEmpty())
                    derived.Add(object, Annotation(field.first + QStringLiteral(" · ") + field.second, "derived",
                        QStringLiteral("离线推导值\n") + field.second, ref.source, field.second), "derived", field.first);
            Append(groups, std::move(derived));
        }
        if (trace == m_analysis.fcdaTraces.cend())
            return;
        for (const auto field : {FcdaField::Ied, FcdaField::LdInst, FcdaField::LogicalNode,
                                FcdaField::DoName, FcdaField::DaName, FcdaField::Fc, FcdaField::Ix})
        {
            const auto value = FieldValue(ref, field);
            const bool stopped = ref.state != LinkState::Resolved && field == trace->stoppedAt;
            if (field == FcdaField::Ix && value.isEmpty() && !stopped)
                continue;
            const auto name = Field(field);
            Group group(name);
            const auto detail = value.isEmpty() ? QStringLiteral("未声明") : value;
            const bool inferred = field == FcdaField::Ied || (field == FcdaField::LdInst && Attr(ref.source, "ldInst").isEmpty());
            const int origin = Annotation(name + QStringLiteral(" · ") + detail.simplified(), "declaration",
                                          name + '\n' + detail, ref.source, inferred ? QString() : value);
            group.Add(focus, origin, "contains", QStringLiteral("字段"));
            int stepIndex = 0;
            int previous = origin;
            for (const auto& step : trace->steps)
            {
                if (step.field != field)
                    continue;
                const int matched = Xml(step.node);
                group.Add(previous, matched, "reference", QStringLiteral("匹配 %1").arg(++stepIndex));
                previous = matched;
            }
            if (stopped)
            {
                const auto state = State(ref.state);
                const int status = Annotation(state, "evidence", ref.detail, ref.source);
                group.Add(previous, status, "status", state, ref.detail);
                for (int i = 0; i < ref.candidates.size(); ++i)
                    group.Add(status, i < GroupLimit ? Xml(ref.candidates[i]) : -1, "candidate", QStringLiteral("候选"));
            }
            Append(groups, std::move(group));
        }
    }

    QJsonArray NodeView(NodeId id, int focus)
    {
        QJsonArray groups;
        const int member = FcdaRef(id);
        if (member >= 0)
            FcdaGroups(groups, member, focus);
        if (const auto found = m_snapshot.browse.dataSetMembers.constFind(id);
            found != m_snapshot.browse.dataSetMembers.cend())
        {
            Group members(QStringLiteral("数据集成员 · 原文顺序"));
            for (int i = 0; i < qMin(found->count, GroupLimit); ++i)
                members.Add(focus, Xml(m_snapshot.browse.memberRows[found->first + i], true),
                    "contains", QStringLiteral("FCDA #%1").arg(i + 1));
            members.total = found->count;
            Append(groups, std::move(members));
        }
        const auto outgoing = m_analysis.outgoing.equal_range(id);
        for (auto it = outgoing.first; it != outgoing.second; ++it)
            if (*it != member)
                ReferenceGroup(groups, m_analysis.references[*it], focus);
        Group incoming(QStringLiteral("使用方"));
        Group templates(QStringLiteral("引用同一模板定义的 FCDA · 可跨实例"));
        const auto sources = m_analysis.incoming.equal_range(id);
        for (auto it = sources.first; it != sources.second; ++it)
        {
            const auto& ref = m_analysis.references[*it];
            auto& group = ref.kind == LinkKind::Fcda ? templates : incoming;
            const bool room = group.total < GroupLimit;
            const auto detail = room && ref.kind == LinkKind::Fcda ? Scope(ref.logicalNode) : QString();
            group.Add(room ? Xml(ref.source, true) : -1, focus, "reference",
                ref.kind == LinkKind::Fcda ? QStringLiteral("FCDA #%1").arg(ref.memberOrdinal) : QStringLiteral("引用"), detail);
        }
        Append(groups, std::move(incoming));
        Append(groups, std::move(templates));
        return groups;
    }

    const SclSnapshot& m_snapshot;
    const ReferenceAnalysis& m_analysis;
    const Document& m_document;
    QJsonArray m_nodes;
    QJsonObject m_views;
    QHash<NodeId, int> m_xml;
    QHash<QString, int> m_objects;
    QSet<int> m_scheduled;
    std::vector<Pending> m_queue;
};
} // namespace

bool CanShowSclTopology(const SclSnapshot& snapshot, Scl::NodeId node)
{
    return node >= 0 && size_t(node) < snapshot.analysis.document->nodes.size()
        && (snapshot.browse.dataSetMembers.contains(node) || snapshot.analysis.outgoing.contains(node)
            || snapshot.analysis.incoming.contains(node));
}

QJsonObject BuildSclTopology(const SclSnapshot& snapshot, Scl::NodeId focus)
{
    return CanShowSclTopology(snapshot, focus) ? Graph(snapshot).Build(focus) : QJsonObject{};
}

QString WriteSclTopology(const SclSnapshot& snapshot, Scl::NodeId focus, const QString& directory, QString& error)
{
    error.clear();
    const auto graph = BuildSclTopology(snapshot, focus);
    if (graph.isEmpty())
    {
        error = QStringLiteral("该对象没有可展示的引用关系。");
        return {};
    }
    static const bool resourceInitialized = InitSclTopologyResource();
    Q_UNUSED(resourceInitialized);
    QFile page(QStringLiteral(":/scl-topology/SclTopology.html"));
    if (!page.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("无法读取引用拓扑页面资源。");
        return {};
    }
    const auto html = page.readAll();
    const auto marker = QByteArrayLiteral("<!--SCL_GRAPH_DATA-->");
    const auto offset = html.indexOf(marker);
    if (offset < 0)
    {
        error = QStringLiteral("引用拓扑页面资源格式无效。");
        return {};
    }
    const auto encoded = QJsonDocument(graph).toJson(QJsonDocument::Compact).toBase64();
    QTemporaryFile file(QDir(directory).filePath(QStringLiteral("topology-XXXXXX.html")));
    if (!file.open() || file.write(html.left(offset)) != offset || file.write(encoded) != encoded.size()
        || file.write(html.mid(offset + marker.size())) != html.size() - offset - marker.size() || !file.flush())
    {
        error = file.errorString();
        return {};
    }
    file.setAutoRemove(false);
    return file.fileName();
}
} // namespace iec61850sim
