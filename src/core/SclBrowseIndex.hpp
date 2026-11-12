#pragma once

#include "SclReferenceResolver.hpp"
#include <array>

namespace iec61850sim
{
enum class SclCategory { DataSets, Objects, Communication, Templates, Count };
enum class SclView { Reports, DataSets, Objects, Communication, Templates, Findings, Count };

struct SclBrowseIndex
{
    struct Branch { int first = 0; int count = 0; int row = 0; };
    struct Members { int first = 0; int count = 0; };

    explicit SclBrowseIndex(const Scl::Document& document, Scl::TaskControl* task);
    void BuildScopes(const Scl::ReferenceAnalysis& analysis, Scl::TaskControl* task);
    bool InScope(Scl::NodeId node, Scl::NodeId ied) const;
    using ScopeRows = std::array<std::vector<int>, size_t(SclView::Count)>;
    QHash<Scl::NodeId, ScopeRows> scopedRows;
    std::vector<Scl::NodeId> ieds;
    std::vector<Scl::NodeId> templateOwners;
    QHash<Scl::NodeId, std::vector<Scl::NodeId>> usedTemplates;
    std::vector<Branch> branches;
    std::vector<Scl::NodeId> children;
    std::vector<Scl::NodeId> iedOwners;
    std::vector<Scl::NodeId> addressRows;
    std::vector<Scl::NodeId> memberRows;
    QHash<Scl::NodeId, Members> dataSetMembers;
    std::array<std::vector<Scl::NodeId>, size_t(SclCategory::Count)> categories;
};
} // namespace iec61850sim
