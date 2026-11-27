#include "SclHierarchyQuery.hpp"
#include "SclAnalysisService.hpp"
#include <QElapsedTimer>

#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <algorithm>

namespace iec61850sim
{
namespace
{
using namespace Scl;
using RowKind = SclHierarchyRowKind;
constexpr int MaxPage = 200;
constexpr int MaxWork = 100000;
constexpr int MaxUsers = 512;

struct Entry
{
    RowKind kind;
    NodeId node;
    int ordinal = 0;
    int reference = -1;
};

std::optional<RowKind> UserKind(const QString& element)
{
    if (element == "ReportControl") return RowKind::Report;
    if (element == "GSEControl") return RowKind::Gse;
    if (element == "SampledValueControl") return RowKind::Sv;
    return {};
}

class HierarchyQuery
{
public:
    HierarchyQuery(SclHierarchyResult& result, TaskControl* task)
        : m_result(result), m_snapshot(*result.snapshot), m_doc(*m_snapshot.analysis.document),
          m_request(result.request), m_task(task)
    {
        m_page.reserve(m_request.pageSize);
    }

    void Run()
    {
        checkpoint(m_task);
        if (m_request.kind == SclHierarchyKind::DataSets)
            DataSets();
        else if (!m_snapshot.browse.dataSetMembers.contains(m_request.dataSet))
            m_result.parent = SclHierarchyParent::Unavailable;
        else if (!InScope(m_request.dataSet))
            m_result.parent = SclHierarchyParent::OutsideScope;
        else
        {
            m_result.parent = SclHierarchyParent::Available;
            m_result.parentPath = SclTaskPath(m_doc, m_request.dataSet, true);
            if (m_request.kind == SclHierarchyKind::Members)
                Members();
            else if (m_request.kind == SclHierarchyKind::Summary)
                Summary();
            else
                Users();
        }

        SortPage();
        if (m_request.locate != NoNode && !m_result.locateFound)
            return;
        m_result.hasMore = m_request.kind != SclHierarchyKind::Summary
            && m_result.total - m_result.offset > int(m_page.size());
        m_result.rows.reserve(m_page.size());
        for (const auto& entry : m_page)
        {
            checkpoint(m_task);
            m_result.rows.push_back(Project(entry));
        }
    }

private:
    bool Visit()
    {
        checkpoint(m_task);
        if (++m_work <= m_request.workLimit)
            return true;
        m_result.limited = true;
        return false;
    }

    bool InScope(NodeId node) const
    {
        const auto& scope = m_request.scope;
        if (scope.ied != NoNode && m_snapshot.browse.iedOwners[node] != scope.ied)
            return false;
        auto ap = scope.accessPoint;
        auto server = scope.server;
        for (auto parent = m_doc.nodes[node].parent;
             parent != NoNode && (ap != NoNode || server != NoNode);
             parent = m_doc.nodes[parent].parent)
        {
            if (parent == ap && m_doc.nodes[parent].name == "AccessPoint")
                ap = NoNode;
            if (parent == server && m_doc.nodes[parent].name == "Server")
                server = NoNode;
        }
        return ap == NoNode && server == NoNode;
    }

    QString Attr(NodeId node, const char* name) const
    {
        return m_doc.attribute(node, QString::fromLatin1(name));
    }

    QString MemberName(NodeId node) const
    {
        const auto object = Attr(node, "doName");
        const auto attribute = Attr(node, "daName");
        return attribute.isEmpty() ? object : object + '.' + attribute;
    }

    bool Matches(const Entry& entry) const
    {
        const auto contains = [&](const QString& value) {
            return m_request.exact ? value.compare(m_request.text, Qt::CaseInsensitive) == 0
                : value.contains(m_request.text, Qt::CaseInsensitive);
        };
        if (entry.kind == RowKind::DataSet)
        {
            const auto owner = m_snapshot.browse.iedOwners[entry.node];
            if (m_request.severity >= 0
                || (!m_request.iedName.isEmpty() && (owner == NoNode
                    || Attr(owner, "name").compare(m_request.iedName, Qt::CaseInsensitive) != 0))
                || (!m_request.type.isEmpty() && m_request.type.compare("DataSet", Qt::CaseInsensitive) != 0)
                || (!m_request.fc.isEmpty() && Attr(entry.node, "fc").compare(m_request.fc, Qt::CaseInsensitive) != 0))
                return false;
            if (!m_request.text.isEmpty() && (contains(Attr(entry.node, "name")) || contains("DataSet")
                || (owner != NoNode && contains(Attr(owner, "name")))))
                return true;
        }
        if (m_request.text.isEmpty())
            return true;
        if (entry.kind == RowKind::Member)
        {
            for (const auto field : {"ldInst", "prefix", "lnClass", "lnInst", "doName", "daName", "fc", "ix", "desc"})
                if (contains(Attr(entry.node, field)))
                    return true;
            return contains(MemberName(entry.node));
        }
        return contains(Attr(entry.node, "desc")) || contains(SclTaskPath(m_doc, entry.node, true));
    }

    // Page keys stay small, even while locating an identity after text filtering.
    bool Collect(const Entry& entry)
    {
        ++m_result.scopeTotal;
        if (!Matches(entry))
            return true;
        if (m_request.kind == SclHierarchyKind::Users && m_result.total == MaxUsers)
        {
            m_result.limited = true;
            return false;
        }
        if (entry.kind == RowKind::DataSet && m_request.order != SclHierarchyOrder::Declaration)
            m_sorted.push_back({SclTaskPath(m_doc, entry.node, true), entry});
        else
            Page(entry);
        return true;
    }

    void Page(const Entry& entry)
    {
        const int position = m_result.total++;
        if (m_request.locate != NoNode && !m_result.locateFound)
        {
            if (position % m_request.pageSize == 0)
                m_page.clear();
            m_page.push_back(entry);
            if (entry.node == m_request.locate)
            {
                m_result.locateFound = true;
                m_result.offset = position - position % m_request.pageSize;
            }
        }
        else if (position >= m_result.offset && int(m_page.size()) < m_request.pageSize)
            m_page.push_back(entry);
    }

    struct SortEntry { QString path; Entry entry; };

    void SortPage()
    {
        std::sort(m_sorted.begin(), m_sorted.end(), [this](const SortEntry& a, const SortEntry& b) {
            checkpoint(m_task);
            const auto cmp = a.path.compare(b.path, Qt::CaseInsensitive);
            if (cmp == 0)
                return a.entry.node < b.entry.node;
            return m_request.order == SclHierarchyOrder::PathAscending ? cmp < 0 : cmp > 0;
        });
        for (const auto& entry : m_sorted)
        {
            checkpoint(m_task);
            Page(entry.entry);
        }
    }

    void DataSets()
    {
        const auto& browse = m_snapshot.browse;
        const auto& rows = browse.categories[size_t(SclCategory::DataSets)];
        const auto collect = [&](NodeId node) {
            if (InScope(node))
                Collect({RowKind::DataSet, node});
        };
        if (m_request.scope.ied == NoNode)
        {
            for (const auto node : rows)
            {
                if (!Visit()) break;
                collect(node);
            }
        }
        else
        {
            const auto scoped = browse.scopedRows.constFind(m_request.scope.ied);
            if (scoped == browse.scopedRows.cend())
                return;
            for (const auto row : (*scoped)[size_t(SclView::DataSets)])
            {
                if (!Visit()) break;
                collect(rows[row]);
            }
        }
    }

    void Members()
    {
        const auto& browse = m_snapshot.browse;
        const auto range = browse.dataSetMembers.value(m_request.dataSet);
        for (int i = 0; i < range.count; ++i)
        {
            if (!Visit()) break;
            Collect({RowKind::Member, browse.memberRows[range.first + i], i + 1});
        }
    }

    int MemberReference(NodeId node) const
    {
        const auto& analysis = m_snapshot.analysis;
        const auto outgoing = analysis.outgoing.equal_range(node);
        for (auto it = outgoing.first; it != outgoing.second; ++it)
            if (analysis.references[it.value()].kind == LinkKind::Fcda)
                return it.value();
        return -1;
    }

    void Summary()
    {
        const auto& browse = m_snapshot.browse;
        const auto range = browse.dataSetMembers.value(m_request.dataSet);
        m_result.scopeTotal = range.count;
        for (int i = 0; i < range.count && Visit(); ++i)
        {
            const auto reference = MemberReference(browse.memberRows[range.first + i]);
            const auto state = reference < 0 ? LinkState::Blocked : m_snapshot.analysis.references[reference].state;
            ++m_result.total;
            if (state == LinkState::Resolved) ++m_result.resolved;
            else if (state == LinkState::Ambiguous) ++m_result.ambiguous;
            else ++m_result.unresolved;
        }
    }

    void Users()
    {
        // Incoming contains uniquely resolved references. Scope limits the parent
        // data set; each returned control keeps its own original ownership.
        const auto& analysis = m_snapshot.analysis;
        const auto incoming = analysis.incoming.equal_range(m_request.dataSet);
        for (auto it = incoming.first; it != incoming.second; ++it)
        {
            if (!Visit()) break;
            const auto& ref = analysis.references[it.value()];
            if (ref.kind != LinkKind::ControlDataSet)
                continue;
            const auto kind = UserKind(m_doc.nodes[ref.source].name);
            if (kind && (!m_request.userKind || kind == m_request.userKind))
                if (!Collect({*kind, ref.source, 0, it.value()})) break;
        }
    }

    SclHierarchyRow Project(const Entry& entry) const
    {
        SclHierarchyRow row;
        row.kind = entry.kind;
        row.node = entry.node;
        row.dataSet = entry.kind == RowKind::DataSet ? entry.node : m_request.dataSet;
        row.reference = entry.reference;
        row.description = Attr(entry.node, "desc");
        if (entry.kind == RowKind::Member)
        {
            row.name = MemberName(entry.node);
            row.member = ProjectMember(entry, row.reference);
        }
        else
        {
            row.name = Attr(entry.node, "name");
            row.path = SclTaskPath(m_doc, entry.node, true);
            if (entry.kind == RowKind::DataSet)
                row.declaredMembers = m_snapshot.browse.dataSetMembers.value(entry.node).count;
        }
        return row;
    }

    SclHierarchyMember ProjectMember(const Entry& entry, int& reference) const
    {
        const auto node = entry.node;
        SclHierarchyMember member;
        member.ordinal = entry.ordinal;
        member.ldInst = Attr(node, "ldInst");
        member.prefix = Attr(node, "prefix");
        member.lnClass = Attr(node, "lnClass");
        member.lnInst = Attr(node, "lnInst");
        member.doName = Attr(node, "doName");
        member.daName = Attr(node, "daName");
        member.fc = Attr(node, "fc");
        member.ix = Attr(node, "ix");
        const auto& analysis = m_snapshot.analysis;
        reference = MemberReference(node);
        if (reference >= 0)
        {
            const auto& ref = analysis.references[reference];
            member.state = ref.state;
            member.logicalNode = ref.logicalNode;
            member.arrayIndex = ref.arrayIndex;
            if (ref.state == LinkState::Resolved && ref.candidates.size() == 1)
                member.target = ref.candidates.front();
        }
        member.description = DescribeSclMember(analysis, node, m_task);
        const auto meta = m_snapshot.browse.objects.constFind(node);
        if (meta != m_snapshot.browse.objects.cend()) {
            member.objectPath = meta->objectPath; member.attributePath = meta->attributePath;
            if (meta->cdcType != NoNode) {
                member.cdc = Attr(meta->cdcType,"cdc"); member.objectType = Attr(meta->cdcType,"id");
            }
            if (meta->declaration != NoNode) member.basicType = Attr(meta->declaration,"bType");
            member.level = SclMemberLevel(m_doc,meta->declaration);
        } else member.level = QStringLiteral("尚未确定（结构索引未覆盖）");
        return member;
    }

    SclHierarchyResult& m_result;
    const SclSnapshot& m_snapshot;
    const Document& m_doc;
    const SclHierarchyRequest& m_request;
    TaskControl* m_task;
    std::vector<Entry> m_page;
    std::vector<SortEntry> m_sorted;
    int m_work = 0;
};
} // namespace

SclHierarchyResult QuerySclHierarchy(std::shared_ptr<const SclSnapshot> snapshot,
    SclHierarchyRequest request, Scl::TaskControl* task)
{
    request.pageSize = std::clamp(request.pageSize, 1, MaxPage);
    request.workLimit = std::clamp(request.workLimit, 1, MaxWork);
    request.offset = std::max(0, request.offset);
    SclHierarchyResult result;
    result.snapshot = std::move(snapshot);
    result.offset = request.locate == Scl::NoNode ? request.offset : 0;
    result.request = std::move(request);
    HierarchyQuery(result, task).Run();
    return result;
}

SclHierarchyQuerySession::SclHierarchyQuerySession(QObject* parent) : QObject(parent) {}
SclHierarchyQuerySession::~SclHierarchyQuerySession() { Cancel(); }

void SclHierarchyQuerySession::SetContext(std::shared_ptr<const SclSnapshot> snapshot, SclHierarchyScope scope)
{
    Cancel();
    m_snapshot = std::move(snapshot);
    m_scope = scope;
}

quint64 SclHierarchyQuerySession::Query(SclHierarchyRequest request)
{
    Cancel();
    if (m_snapshot)
    {
        request.scope = m_scope;
        m_pending = std::move(request);
        Start();
    }
    return m_generation;
}

void SclHierarchyQuerySession::Cancel()
{
    ++m_generation;
    if (m_control)
        m_control->cancelled.store(true, std::memory_order_relaxed);
    m_pending.reset();
    m_result.reset();
}

void SclHierarchyQuerySession::Start()
{
    if (m_control || !m_pending)
        return;
    const auto snapshot = m_snapshot;
    const auto request = std::move(*m_pending);
    const auto generation = m_generation;
    m_pending.reset();
    const auto control = m_control = std::make_shared<Scl::TaskControl>();
    using ResultPtr = std::shared_ptr<const SclHierarchyResult>;
    auto* watcher = new QFutureWatcher<ResultPtr>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        m_control.reset();
        auto result = watcher->result();
        watcher->deleteLater();
        const bool accepted = result && result->requestId == m_generation
            && result->snapshot == m_snapshot && result->request.scope == m_scope;
        if (accepted)
            m_result = std::move(result);
        Start();
        if (accepted)
            emit Ready();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, request, generation, control]() -> ResultPtr {
        try
        {
            QElapsedTimer timer;
            timer.start();
            auto result = QuerySclHierarchy(snapshot, request, control.get());
            result.requestId = generation;
            result.workerUs = timer.nsecsElapsed() / 1000;
            return std::make_shared<const SclHierarchyResult>(std::move(result));
        }
        catch (const Scl::TaskCancelled&)
        {
            return {};
        }
    }));
}
} // namespace iec61850sim
