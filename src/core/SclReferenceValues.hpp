#pragma once

#include "SclSearch.hpp"

namespace iec61850sim
{
enum class SclReferenceAnchor { Node, Link, Candidate, Signal, Mms, Field, Hit };

// The view supplies original record identity, never rendered table cells.
struct SclReferenceRequest
{
    SclReferenceAnchor anchor = SclReferenceAnchor::Node;
    Scl::NodeId node = Scl::NoNode;
    int record = -1; // Link or signal index, as specified by anchor.
    Scl::NodeId input = Scl::NoNode, control = Scl::NoNode;
    bool candidateInput = false;
    bool limited = false;
    SclMmsKey mms;
    SclMmsDetailField field;
    SclHit hit{};
    std::vector<SclReferenceValue> prepared;
};

struct SclReferenceValues
{
    std::vector<SclReferenceValue> entries;
    bool limited = false;
};

// Worker-only projection for one row. Every value belongs to the supplied snapshot.
SclReferenceValues CollectSclReferences(const SclSnapshot&, const SclReferenceRequest&, Scl::TaskControl*);
QString SclReferenceLabel(SclReferenceValueKind);
} // namespace iec61850sim
