#pragma once

#include "SclReferenceResolver.hpp"
#include <array>

namespace iec61850sim
{
enum class SclCategory { DataSets, Objects, Communication, Templates, Count };

struct SclBrowseIndex
{
    struct Branch { int first = 0; int count = 0; int row = 0; };
    struct Members { int first = 0; int count = 0; };

    explicit SclBrowseIndex(const Scl::Document& document, Scl::TaskControl* task);
    std::vector<Branch> branches;
    std::vector<Scl::NodeId> children;
    std::vector<Scl::NodeId> iedOwners;
    std::vector<Scl::NodeId> addressRows;
    std::vector<Scl::NodeId> memberRows;
    QHash<Scl::NodeId, Members> dataSetMembers;
    std::array<std::vector<Scl::NodeId>, size_t(SclCategory::Count)> categories;
};
} // namespace iec61850sim
