#pragma once

#include "SclReferenceValues.hpp"

namespace iec61850sim
{
enum class SclTraceBranch {
    None, Root, LogicalDevice, LogicalNode, FunctionalConstraint, Type, TypeDefinition, Scope, Member, Array, ReportName
};

struct SclTraceNode
{
    QString label, value, explanation, relation;
    Scl::NodeId source = Scl::NoNode;
    QString attribute, path, excerpt;
    Scl::SourceLocation location;
    SclTraceBranch branch = SclTraceBranch::None;
};

struct SclReferenceTrace
{
    static constexpr int VisibleLimit = 80;
    SclTraceNode node;
    std::vector<SclTraceNode> children;
    bool limited = false;
};

// A root or one requested expansion. Node IDs and the reference must belong to snapshot.
SclReferenceTrace TraceSclReference(const SclSnapshot&, const SclReferenceValue&,
    const SclTraceNode& branch, Scl::TaskControl*);
}
