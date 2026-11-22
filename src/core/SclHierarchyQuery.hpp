#pragma once

#include "SclDescriptionResolver.hpp"
#include <QObject>
#include <optional>

namespace iec61850sim
{
struct SclSnapshot;

struct SclHierarchyScope
{
    Scl::NodeId ied = Scl::NoNode;
    Scl::NodeId accessPoint = Scl::NoNode;
    Scl::NodeId server = Scl::NoNode;

    bool operator==(const SclHierarchyScope& other) const
    {
        return ied == other.ied && accessPoint == other.accessPoint && server == other.server;
    }
};

enum class SclHierarchyKind { DataSets, Members, Users, Summary };
enum class SclHierarchyOrder { Declaration, PathAscending, PathDescending };
enum class SclHierarchyRowKind { DataSet, Member, Report, Gse, Sv };
enum class SclHierarchyParent { NotRequested, Available, OutsideScope, Unavailable };

struct SclHierarchyRequest
{
    SclHierarchyKind kind = SclHierarchyKind::DataSets;
    SclHierarchyScope scope;
    Scl::NodeId dataSet = Scl::NoNode;
    Scl::NodeId locate = Scl::NoNode; // Original NodeId in the current snapshot.
    std::optional<SclHierarchyRowKind> userKind;
    SclHierarchyOrder order = SclHierarchyOrder::Declaration;
    QString text, iedName, type, fc;
    bool exact = false;
    int severity = -1;
    int offset = 0;
    int pageSize = 200;
    int workLimit = 100000;
};

struct SclHierarchyMember
{
    int ordinal = 0; // One-based occurrence in the parent data set.
    QString ldInst, prefix, lnClass, lnInst, doName, daName, fc, ix;
    Scl::LinkState state = Scl::LinkState::Blocked;
    Scl::NodeId logicalNode = Scl::NoNode;
    Scl::NodeId target = Scl::NoNode; // Only a unique resolved target.
    int arrayIndex = -1;
    SclDescription description;
};

struct SclHierarchyRow
{
    SclHierarchyRowKind kind = SclHierarchyRowKind::DataSet;
    Scl::NodeId node = Scl::NoNode;
    Scl::NodeId dataSet = Scl::NoNode;
    QString name, path, description;
    int declaredMembers = 0; // DataSet rows only.
    int reference = -1; // Index in snapshot->analysis.references, with original state and candidates.
    std::optional<SclHierarchyMember> member;
};

struct SclHierarchyResult
{
    std::shared_ptr<const SclSnapshot> snapshot;
    quint64 requestId = 0;
    qint64 workerUs = 0;
    SclHierarchyRequest request;
    SclHierarchyParent parent = SclHierarchyParent::NotRequested;
    QString parentPath;
    std::vector<SclHierarchyRow> rows;
    int offset = 0;
    int scopeTotal = 0; // Before text filtering.
    int total = 0; // Matching rows. Both counts are observed lower bounds if limited.
    int resolved = 0, ambiguous = 0, unresolved = 0; // Summary only.
    bool hasMore = false; // More matching rows have been observed.
    bool locateFound = false;
    bool limited = false;
};

// Worker-only entry. Consumes a complete snapshot and throws TaskCancelled on cancellation.
SclHierarchyResult QuerySclHierarchy(std::shared_ptr<const SclSnapshot>,
    SclHierarchyRequest, Scl::TaskControl*);

// Use independent sessions for the list, expansion, users and identity lookup.
// Call on the owning event-loop thread. One active worker and one latest pending
// request per session. Cancel on hide/collapse and clear the context on close.
class SclHierarchyQuerySession final : public QObject
{
    Q_OBJECT
public:
    explicit SclHierarchyQuerySession(QObject* parent = nullptr);
    ~SclHierarchyQuerySession() override;

    void SetContext(std::shared_ptr<const SclSnapshot>, SclHierarchyScope scope = {});
    quint64 Query(SclHierarchyRequest);
    void Cancel();
    std::shared_ptr<const SclHierarchyResult> result() const { return m_result; }

signals:
    void Ready();

private:
    void Start();
    std::shared_ptr<const SclSnapshot> m_snapshot;
    SclHierarchyScope m_scope;
    std::optional<SclHierarchyRequest> m_pending;
    std::shared_ptr<Scl::TaskControl> m_control;
    std::shared_ptr<const SclHierarchyResult> m_result;
    quint64 m_generation = 0;
};
} // namespace iec61850sim
