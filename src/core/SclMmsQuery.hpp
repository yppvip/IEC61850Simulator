#pragma once

#include "SclMmsProjection.hpp"
#include "SclDescriptionResolver.hpp"
#include <QHash>
#include <QObject>
#include <QFutureWatcher>
#include <optional>

namespace iec61850sim {
struct SclSnapshot;

// Lightweight indexes only: member data and descriptions stay shared in the
// original snapshot. All integer record keys below are snapshot-local.
struct SclMmsIndex {
    bool limited = false;
    QHash<Scl::NodeId, int> reports;
    QHash<Scl::NodeId, std::vector<int>> reportsByDataSet, clientsByReport,
        clientsByTarget, instancesByReport;
    QHash<Scl::NodeId, int> memberReferences;
    static SclMmsIndex Build(const Scl::MmsProjection&, const Scl::ReferenceAnalysis&, Scl::TaskControl*);
};

struct SclMmsScope {
    Scl::NodeId ied = Scl::NoNode, accessPoint = Scl::NoNode, server = Scl::NoNode;
    bool operator==(const SclMmsScope& b) const {
        return ied == b.ied && accessPoint == b.accessPoint && server == b.server;
    }
};
enum class SclMmsQueryKind { Reports, Instances, Members, Clients, Servers, Endpoints,
    Capabilities, Signals, Addresses, ReverseClients, ReverseSignal, Search, Graph, Findings, Detail };
enum class SclMmsRecordKind { Report, Instance, Member, Client, Server, Endpoint, Capability, Signal, Address, DataSet, Reference, Finding };
enum class SclMmsAddressShape { None, Scalar, Structure, ArrayTemplate, ObjectMember, AlternateAccess };

struct SclMmsKey {
    SclMmsRecordKind kind = SclMmsRecordKind::Report;
    Scl::NodeId node = Scl::NoNode;
    int record = -1; // Projection/catalog index; never a displayed row.
    int ordinal = 0;
    QString field; // Stable attribute path for addresses.
};
struct SclMmsRow {
    SclMmsKey key;
    Scl::NodeId report = Scl::NoNode, dataSet = Scl::NoNode;
    QStringList cells; // Full values; no DisplayRole truncation.
    Scl::LinkState state = Scl::LinkState::Unbound;
    SclMmsAddressShape shape = SclMmsAddressShape::None;
    QString domain, item, component, fc, limitation;
    int arrayIndex = -1;
    QList<Scl::MmsEvidence> evidence;
    std::optional<SclDescription> description;
};
struct SclMmsRequest {
    SclMmsQueryKind kind = SclMmsQueryKind::Reports;
    SclMmsScope scope;
    Scl::NodeId report = Scl::NoNode, target = Scl::NoNode;
    int signal = -1;
    QString text, fc;
    int offset = 0, pageSize = 200;
    int workLimit = 100000; // Hard ceiling in QuerySclMms: 100000 steps.
    int graphLimit = 128; // Hard ceiling: 128 nodes / 256 edges.
    bool presentation = false;
    SclMmsKey key;
    std::optional<SclMmsKey> locate; // Locate a stable identity's page in the worker, after scope/filter.
};
struct SclMmsDetailField {
    QString key, label, value, explanation;
    bool present = true;
    Scl::MmsEvidence evidence;
};
enum class SclMmsEdgeKind { UsesDataSet, ExpectedInstance, DeclaredClient, SourceReport, DataSetMember, MemberObject };
struct SclMmsEdge { int from, to; SclMmsEdgeKind kind; Scl::LinkState state; };
struct SclMmsResult {
    QByteArray sha256;
    std::shared_ptr<const Scl::Document> document;
    std::shared_ptr<const Scl::SourceIndex> source;
    quint64 requestId = 0;
    SclMmsRequest request;
    std::vector<SclMmsRow> rows;
    std::vector<SclMmsEdge> edges;
    int total = 0; // Exact when !limited; otherwise observed lower bound.
    bool limited = false, hasMore = false;
    bool locateFound = false;
    QString limitation;
    QString title, summary;
    std::vector<SclMmsDetailField> fields;
};

// Worker-only entry. No XML parsing, I/O, or online queries; evidence and
// descriptions always refer to snapshot.analysis.document/source.
SclMmsResult QuerySclMms(const SclSnapshot&, const SclMmsRequest&, Scl::TaskControl*);
void PresentSclMms(const SclSnapshot&, SclMmsResult&, Scl::TaskControl*);
SclMmsResult DescribeSclMms(const SclSnapshot&, const SclMmsRequest&, Scl::TaskControl*);
QString SclMmsStateText(Scl::LinkState);

// One instance per independent consumer (list, detail, address, search, graph,
// copy). Owning event-loop thread only. At most one worker plus latest pending
// request. Hide/close => Cancel; scope/file changes => SetContext.
class SclMmsQuerySession final : public QObject {
    Q_OBJECT
public:
    explicit SclMmsQuerySession(QObject* parent = nullptr);
    ~SclMmsQuerySession() override;
    void SetContext(std::shared_ptr<const SclSnapshot>, SclMmsScope = {});
    quint64 Query(SclMmsRequest);
    void Cancel();
    std::shared_ptr<const SclMmsResult> result() const { return m_result; }
signals:
    void Ready();
private:
    void Start();
    std::shared_ptr<const SclSnapshot> m_snapshot;
    SclMmsScope m_scope;
    std::optional<SclMmsRequest> m_pending;
    std::shared_ptr<Scl::TaskControl> m_control;
    std::shared_ptr<const SclMmsResult> m_result;
    quint64 m_generation = 0;
};
}
