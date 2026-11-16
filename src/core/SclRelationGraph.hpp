#pragma once

#include "SclConnections.hpp"

namespace iec61850sim
{
enum class SclGraphRole { Sender, Publication, Member, Input, Receiver, Definition };
enum class SclGraphRelation { Ownership, Membership, Bound, Candidate, ContainmentCandidate, Evidence };

struct SclGraphNode
{
    SclGraphRole role;
    Scl::NodeId evidence;
    int layer;
    QString label;
    Scl::NodeId member = Scl::NoNode;
    Scl::NodeId input = Scl::NoNode;
};

struct SclGraphEdge
{
    int from, to;
    SclGraphRelation relation;
    Scl::NodeId evidence;
};

struct SclRelationGraph
{
    static constexpr int NodeLimit = 80, EdgeLimit = 160;
    std::vector<SclGraphNode> nodes;
    std::vector<SclGraphEdge> edges;
    int hiddenMappings = 0;
    bool limited = false;
};

SclRelationGraph ProjectSclRelation(const SclSnapshot& snapshot, const SclConnection& connection,
    const SclConnectionMember& root, int depth, Scl::TaskControl* task);
}
