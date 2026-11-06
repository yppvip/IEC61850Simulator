#pragma once

#include "SclReader.hpp"
#include <QList>
#include <QMultiHash>
#include <QStringList>

namespace Scl {

enum class LinkKind { LogicalNodeType, MemberType, ControlDataSet, Fcda, ConnectedAp,
                      NetworkControl, ExtRefData, ExtRefControl };
enum class LinkState { Resolved, Missing, Ambiguous, Unbound, External, Unsupported, Blocked, Partial };
enum class Severity { Error, Warning, Info };
enum class Coverage { Executed, NotApplicable, Blocked, NotImplemented };

enum class FcdaField { Ied, LdInst, LogicalNode, DoName, DaName, Fc, Ix };

struct FcdaTrace {
    struct Step { FcdaField field; NodeId node; };
    QList<Step> steps;
    FcdaField stoppedAt = FcdaField::LdInst; // Relevant when the reference did not resolve.
};

struct Reference {
    LinkKind kind;
    LinkState state;
    NodeId source;
    QString declared; // Display form. Original attributes are available on source.
    QString detail;
    QList<NodeId> candidates;
    NodeId logicalNode = NoNode;
    int memberOrdinal = 0; // One-based among FCDA children.
    QString objectReference;
    QString mmsDomain;
    QString mmsItem;
    int arrayIndex = -1;
    QString mmsComponent; // Alternate access component following the indexed member.
    NodeId instanceNode = NoNode;
    NodeId instanceDescription = NoNode; // Val of the owning DO instance's dU.
};

struct ReportMapping {
    NodeId source;
    QString rptId;
    QString declaredDataSet;
    QString dataSetReference;
    QString definitionReference;
    QString mmsDomain;
    QString mmsItem;
};

struct RuleCoverage {
    QString id;
    Coverage state = Coverage::NotApplicable;
    int evaluated = 0;
    int limited = 0; // Cases with unavailable scope or unsupported form.
};

struct Finding {
    QString rule;
    Severity severity;
    NodeId node;
    SourceLocation location;
    QString objectPath;
    QString detail;
    QString suggestion;
    QString scope;
};

struct ReferenceAnalysis {
    QString ruleSetVersion = QStringLiteral("0.2");
    QByteArray sha256; // Hex digest of the source snapshot.
    std::shared_ptr<const Document> document;
    std::shared_ptr<const SourceIndex> source;
    std::vector<Reference> references;
    QMultiHash<NodeId, int> outgoing; // Values are indexes in references.
    QMultiHash<NodeId, int> incoming; // Resolved targets only.
    QHash<NodeId, FcdaTrace> fcdaTraces;
    std::vector<ReportMapping> reports;
    std::vector<Finding> findings;
    std::vector<RuleCoverage> coverage;
};

class ReferenceResolver {
public:
    static ReferenceAnalysis analyze(const Result &input, TaskControl *task = nullptr);
};

} // namespace Scl
