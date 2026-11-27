#pragma once

#include "SclReferenceResolver.hpp"
#include <functional>

namespace iec61850sim {
struct SclSnapshot;
struct SclMmsRow;
enum class SclReferenceValueKind;
enum class SclMmsReportMatch;

enum class SclEvidenceOrigin { Declaration, ReferenceResolution, TypeExpansion, RuleCombination };
enum class SclEvidencePrecision { Unavailable, OpeningTag };
struct SclEvidenceSource {
    Scl::NodeId node = Scl::NoNode;
    QString attribute, raw;
    bool present = false;
    Scl::SourceLocation location;
    SclEvidencePrecision precision = SclEvidencePrecision::Unavailable;
};
struct SclEvidencePart {
    QString role, value, rule;
    SclEvidenceOrigin origin = SclEvidenceOrigin::Declaration;
    QList<SclEvidenceSource> sources;
    bool contributes = true; // False for supporting declarations, not an output segment.
};
struct SclEvidenceValue {
    SclReferenceValueKind kind{};
    QString value, rule, formula;
    SclEvidenceOrigin origin = SclEvidenceOrigin::RuleCombination;
    QList<SclEvidencePart> parts;
};
enum class SclInstanceLocationState { Exact, Ancestor, Ambiguous, ArraySelectionRequired, SettingGroup, Unavailable };
struct SclInstanceLocation {
    SclInstanceLocationState state = SclInstanceLocationState::Unavailable;
    SclEvidenceSource target; // Only the exact instance or its deepest proven ancestor.
    QList<SclEvidenceSource> matched, candidates;
    QString stoppedAt, reason;
    bool candidatesLimited = false;
};
struct SclEvidenceResolution {
    SclEvidenceSource source;
    Scl::LinkState state = Scl::LinkState::Missing;
    QList<SclEvidenceSource> candidates;
    bool candidatesLimited = false;
};
struct SclObjectEvidence {
    QByteArray sha256;
    std::shared_ptr<const Scl::Document> document;
    QString ruleVersion = QStringLiteral("object-evidence-0.1");
    QString baseRuleVersion, mmsRuleVersion;
    Scl::LinkState state = Scl::LinkState::Unbound;
    Scl::NodeId logicalNode = Scl::NoNode, objectType = Scl::NoNode;
    Scl::NodeId ied = Scl::NoNode, accessPoint = Scl::NoNode, server = Scl::NoNode, logicalDevice = Scl::NoNode;
    Scl::NodeId arrayDeclaration = Scl::NoNode, arraySelector = Scl::NoNode;
    int arrayIndex = -1;
    QString component;
    QString objectPath, attributePath, cdc, basicType, fc;
    QList<SclEvidenceSource> declarations, typeLinks;
    QList<SclEvidencePart> resolutions;
    QList<SclEvidenceResolution> typeResolutions;
    QList<SclEvidenceSource> targetCandidates;
    bool targetCandidatesLimited = false;
    SclEvidenceSource cdcEvidence, basicTypeEvidence, fcEvidence;
    QList<SclEvidenceSource> objectCdcs; // DO/SDO chain, not CDCs assigned to BDA leaves.
    QList<SclEvidenceValue> values;
    SclInstanceLocation instance;
    SclEvidenceSource member, dataSet;
    QString rawDoName, rawDaName, rawFc, rawIx;
    bool daNamePresent = false;
    QString limitation;
};

enum class SclDataSetConclusion { Uncertain, Referenced, NotReferenced };
struct SclDataSetCoverage {
    SclDataSetConclusion conclusion = SclDataSetConclusion::Uncertain;
    bool scanComplete = false; // Independent of page size and of whether matches exist.
    int observedMatches = 0;
    int uncertainOccurrences = 0;
    QList<SclEvidenceSource> uncertainSources; // Bounded preview; count is independent.
};
enum class SclDataSetUserKind { Report, Goose, Sv };
struct SclDataSetUser {
    SclDataSetUserKind kind;
    Scl::LinkState state;
    SclEvidenceSource declaration;
};
struct SclDataSetUsage {
    bool complete = true;
    int observed = 0;
    QList<SclDataSetUser> users; // <=512, shared across occurrences in the same query.
};
struct SclDataSetRelation {
    QByteArray sha256;
    std::shared_ptr<const Scl::Document> document;
    SclEvidenceSource member, dataSet;
    int ordinal = 0;
    SclMmsReportMatch match{};
    QString rawDoName, rawDaName, rawFc, rawIx;
    bool daNamePresent = false;
    Scl::NodeId sourceIed = Scl::NoNode, sourceAccessPoint = Scl::NoNode, sourceServer = Scl::NoNode, targetLogicalNode = Scl::NoNode;
    std::shared_ptr<const SclDataSetUsage> usage;
};

// Worker-only helpers. Caller validates snapshot identity and shares one work budget
// through tick with address lookup, projection and reverse-query traversal.
SclEvidenceSource SclEvidenceAt(const SclSnapshot&, Scl::NodeId, const QString& attribute = {});
SclObjectEvidence ProjectSclObjectEvidence(const SclSnapshot&, const SclMmsRow&, const std::function<void()>& tick);
SclObjectEvidence ProjectSclNodeEvidence(const SclSnapshot&, Scl::NodeId, const std::function<void()>& tick);
SclMmsReportMatch MatchSclDataSetMember(const SclSnapshot&, const SclMmsRow&, const Scl::Reference&,
                                     const std::function<void()>& tick);
bool CouldSclDataSetMemberAffect(const SclSnapshot&, const SclMmsRow&, const Scl::Reference&,
                               const std::function<void()>& tick);
}
