#pragma once

#include "SclReferenceResolver.hpp"

namespace iec61850sim {
// Snapshot-local, worker-built structural facts. No instance address is inferred
// from an unbound template. Paths are relative to the actual declaring template.
struct SclObjectStructure {
    Scl::NodeId declaration = Scl::NoNode, type = Scl::NoNode;
    Scl::NodeId cdcType = Scl::NoNode, fcDeclaration = Scl::NoNode;
    Scl::LinkState state = Scl::LinkState::Unbound;
    QString objectPath, attributePath;
};
QHash<Scl::NodeId, SclObjectStructure> BuildSclObjectStructures(
    const Scl::ReferenceAnalysis&, Scl::TaskControl*, bool& limited);
QString SclMemberLevel(const Scl::Document&, Scl::NodeId declaration);
}
