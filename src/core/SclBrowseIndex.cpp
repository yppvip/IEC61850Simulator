#include "SclBrowseIndex.hpp"

namespace iec61850sim
{
SclBrowseIndex::SclBrowseIndex(const Scl::Document& document, Scl::TaskControl* task)
    : branches(document.nodes.size()), iedOwners(document.nodes.size(), Scl::NoNode)
{
    children.reserve(document.nodes.size());
    std::vector<bool> standard(document.nodes.size());
    for (Scl::NodeId id = 0; id < int(document.nodes.size()); ++id)
    {
        Scl::checkpoint(task);
        const auto& node = document.nodes[id];
        auto& branch = branches[id];
        branch.first = int(children.size());
        for (auto child = node.firstChild; child != Scl::NoNode; child = document.nodes[child].nextSibling)
        {
            Scl::checkpoint(task);
            branches[child].row = branch.count++;
            children.push_back(child);
        }

        const auto parent = node.parent;
        if (parent != Scl::NoNode)
            iedOwners[id] = iedOwners[parent];
        standard[id] = node.isScl() && (parent == Scl::NoNode ||
            (standard[parent] && document.nodes[parent].name != "Private"));
        if (!standard[id])
            continue;
        const auto& name = node.name;
        if (name == "IED" && parent == 0)
            iedOwners[id] = id;
        if (name == "P" && parent != Scl::NoNode && document.nodes[parent].name == "Address")
            addressRows.push_back(id);
        if (name == "DataSet")
        {
            categories[size_t(SclCategory::DataSets)].push_back(id);
            Members members{int(memberRows.size()), 0};
            for (int row = 0; row < branch.count; ++row)
            {
                Scl::checkpoint(task);
                const auto child = children[branch.first + row];
                const auto& element = document.nodes[child];
                if (element.isScl() && element.name == "FCDA")
                {
                    memberRows.push_back(child);
                    ++members.count;
                }
            }
            dataSetMembers.insert(id, members);
        }
        else if (name == "LN" || name == "LN0" || name == "DOI" || name == "SDI" || name == "DAI"
                 || name == "DO" || name == "SDO" || name == "DA" || name == "BDA")
            categories[size_t(SclCategory::Objects)].push_back(id);
        else if (name == "SubNetwork" || name == "ConnectedAP" || name == "GSE" || name == "SMV" || name == "ExtRef"
                 || name == "GSEControl" || name == "SampledValueControl")
            categories[size_t(SclCategory::Communication)].push_back(id);
        else if (name == "LNodeType" || name == "DOType" || name == "DAType" || name == "EnumType")
            categories[size_t(SclCategory::Templates)].push_back(id);
    }
}
} // namespace iec61850sim
