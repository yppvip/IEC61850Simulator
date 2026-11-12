#pragma once

#include "SclReferenceResolver.hpp"

namespace Scl
{
enum SignalCapability : unsigned
{
    Indication = 1u, Measurement = 2u, ControlCandidate = 4u, Configuration = 8u
};

struct SignalDefinition
{
    QString path;
    QString cdc;
    NodeId declaration = NoNode;
    NodeId type = NoNode;
    unsigned capabilities = 0;
    bool hasUnexpandedArray = false;
    bool composite = false;
};

struct SignalCatalog
{
    static constexpr int ObjectLimit = 500000;
    static constexpr int DefinitionLimit = 50000;
    struct Instance { NodeId logicalNode; int definition; };

    std::vector<SignalDefinition> definitions;
    std::vector<Instance> instances;
    int unresolvedLogicalNodes = 0;
    bool limited = false;

    static SignalCatalog Build(const ReferenceAnalysis& analysis, TaskControl* task);
};
}
