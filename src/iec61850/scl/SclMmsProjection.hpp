#pragma once

#include "SclReferenceResolver.hpp"
#include <QMap>
#include <QVariant>

namespace Scl {

enum class MmsProfile { Unknown, Explicit2007B4 };
enum class MmsValueState { Missing, Empty, Explicit, Invalid, Limited, Derived };

// All identities below belong to this projection's sha256/document. Locations
// identify an element, never a guessed byte offset for an attribute.
struct MmsEvidence {
    NodeId node = NoNode;
    QString attribute;
    QString rule;
};

struct MmsField {
    bool rawPresent = false;
    QString raw;
    QVariant value; // Invalid QVariant means no interpreted value.
    MmsValueState state = MmsValueState::Missing;
    MmsEvidence evidence;
    QString unit;
    QString limitation;
};

struct MmsResolution {
    LinkState state = LinkState::Unbound;
    QList<NodeId> candidates;
    QString detail;
    NodeId target() const { return state == LinkState::Resolved && candidates.size() == 1 ? candidates[0] : NoNode; }
};

struct MmsParameterGroup {
    NodeId node = NoNode;
    QMap<QString, MmsField> fields;
    QStringList unknownAttributes;
};

struct MmsReportDefinition {
    NodeId node = NoNode, ied = NoNode, server = NoNode, logicalNode = NoNode;
    QMap<QString, MmsField> fields;
    QList<MmsParameterGroup> triggers, options, enabled; // Duplicates preserved.
    MmsResolution dataSet;
    int memberCount = 0; // Original FCDA occurrences, not unique signals.
    QString dataSetReference, dataSetMmsName;
    QString definitionReference, mmsDomain, definitionMmsItem;
    MmsValueState mappingState = MmsValueState::Limited;
    MmsValueState instanceState = MmsValueState::Limited;
    QString instanceLimitation;
    QStringList unknownAttributes;
    QList<NodeId> unknownChildren;
};

struct MmsReportInstance {
    NodeId definition = NoNode;
    int ordinal = 0; // Stable key = (definition, ordinal), not an XML NodeId.
    QString name, reference, mmsDomain, mmsItem;
    MmsField rptId; // Explicit custom value unchanged; default remains limited.
    QList<MmsEvidence> evidence;
};

enum class MmsClientKind { ClientLn, Association, ReportExtRef };
struct MmsClientDeclaration {
    MmsClientKind kind = MmsClientKind::ClientLn;
    NodeId node = NoNode, ownerServer = NoNode, report = NoNode;
    int ordinal = 0; // XML position within its own RptEnabled.
    int expectedInstanceOrdinal = 0; // Zero means no confirmed assignment.
    QMap<QString, MmsField> fields;
    MmsResolution target; // Client LN, or source report for ReportExtRef.
    MmsResolution dataTarget; // Existing ExtRef object evidence, never a guessed report.
    QString allocationLimitation;
};

struct MmsDomain {
    NodeId device = NoNode;
    QString name;
    MmsValueState state = MmsValueState::Limited;
    MmsEvidence evidence;
};

struct MmsServer {
    NodeId ied = NoNode, accessPoint = NoNode;
    MmsResolution model; // Multiple access paths can share one Server.
    QList<NodeId> path; // AP / ServerAt / ... / Server, in traversal order.
    QList<MmsDomain> domains;
    QList<NodeId> services; // Raw IED/AP declarations, no unverified precedence.
};

struct MmsNetworkAddress {
    NodeId node = NoNode;
    QList<MmsParameterGroup> parameters; // One per P; duplicate types keep separate identities.
};

struct MmsEndpoint {
    NodeId node = NoNode, subNetwork = NoNode;
    MmsResolution accessPoint; // Reuses REF003; its findings are not duplicated.
    NodeId server = NoNode;
    QList<MmsNetworkAddress> addresses; // Only direct Address/P, never GSE/SMV.
};

struct MmsCapability {
    NodeId services = NoNode, owner = NoNode;
    QList<NodeId> declarations; // All direct service children, including unsupported ones.
    QList<MmsParameterGroup> parameters;
    QString limitation; // Capacity/configurability is not a live count or setting.
};

struct MmsLimits {
    // Combined index entries, fields, records, findings and access-path work;
    // this is a resource budget, not a UI row/page limit.
    int records = 250'000;
    int serverPath = 256;
};

struct MmsProjection {
    QString ruleSetVersion = QStringLiteral("mms-0.1");
    QByteArray sha256;
    std::shared_ptr<const Document> document;
    std::shared_ptr<const SourceIndex> source;
    MmsProfile profile = MmsProfile::Unknown;
    bool complete = false;
    bool resourceLimited = false;
    QString limitation;
    std::vector<MmsReportDefinition> reports;
    std::vector<MmsReportInstance> instances;
    std::vector<MmsClientDeclaration> clients;
    std::vector<MmsServer> servers;
    std::vector<MmsEndpoint> endpoints;
    std::vector<MmsCapability> capabilities;
    std::vector<Finding> findings; // Incremental findings only; merge with the base analysis later.
    std::vector<RuleCoverage> coverage;
};

class MmsProjector {
public:
    // Qt Core only. Call in a worker. Cancellation throws TaskCancelled; resource
    // failure returns an explicitly blocked projection with no partial records.
    static MmsProjection Build(const ReferenceAnalysis &analysis, TaskControl *task = nullptr,
                               const MmsLimits &limits = {});
};

} // namespace Scl
