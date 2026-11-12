#pragma once

#include "SclAnalysisService.hpp"
#include <functional>

namespace iec61850sim
{
enum class SearchScope { All, Reports, DataSets, Members, Objects, Communication, Templates, Findings };
enum class MatchField { Node, Attribute, Address, Owner, Reference, Report, Finding };

struct SclQuery
{
    QString text;
    QString ied;
    QString type;
    QString fc;
    int severity = -1;
    bool exact = false;
    SearchScope scope = SearchScope::All;
    Scl::NodeId dataSet = Scl::NoNode;
    Scl::NodeId browseIed = Scl::NoNode;

    bool empty() const { return text.isEmpty() && ied.isEmpty() && type.isEmpty() && fc.isEmpty() && severity < 0; }
};

struct SclHit
{
    Scl::NodeId node;
    MatchField field;
    int detail = 0;
    int row = -1;
};

using SclHitBatch = std::vector<SclHit>;
void SearchScl(const SclSnapshot& snapshot, const SclQuery& query, Scl::TaskControl& task,
               const std::function<void(SclHitBatch)>& publish);
QString SearchSnippet(const SclSnapshot& snapshot, const SclHit& hit, const QString& needle);
QString SearchField(const SclSnapshot& snapshot, const SclHit& hit);
} // namespace iec61850sim
