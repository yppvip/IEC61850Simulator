#include "SclBrowseIndex.hpp"
#include <QSet>
#include <algorithm>

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
        {
            iedOwners[id] = id;
            ieds.push_back(id);
        }
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

void SclBrowseIndex::BuildScopes(const Scl::ReferenceAnalysis& analysis, Scl::TaskControl* task)
{
    const auto& doc = *analysis.document;
    // Communication is outside the IED subtree. Only a unique declaration is
    // accepted as ownership evidence; duplicate names remain engineering scope.
    QHash<QString, std::vector<Scl::NodeId>> names;
    for (auto ied : ieds)
    {
        const auto name = doc.attribute(ied, QStringLiteral("name"));
        if (!name.isEmpty())
            names[name].push_back(ied);
    }
    for (Scl::NodeId id = 0; id < int(doc.nodes.size()); ++id)
    {
        Scl::checkpoint(task);
        const auto& node = doc.nodes[id];
        const auto parent = node.parent;
        const auto grandparent = parent == Scl::NoNode ? Scl::NoNode : doc.nodes[parent].parent;
        if (node.isScl() && node.name == "ConnectedAP" && grandparent != Scl::NoNode
            && doc.nodes[parent].isScl() && doc.nodes[parent].name == "SubNetwork"
            && doc.nodes[grandparent].isScl() && doc.nodes[grandparent].name == "Communication"
            && doc.nodes[grandparent].parent == 0)
        {
            const auto found = names.constFind(doc.attribute(id, QStringLiteral("iedName")));
            if (found != names.cend() && found->size() == 1)
                iedOwners[id] = found->front();
        }
        else if (iedOwners[id] == Scl::NoNode && node.parent != Scl::NoNode)
            iedOwners[id] = iedOwners[node.parent];
    }

    templateOwners.assign(doc.nodes.size(), Scl::NoNode);
    const auto& templates = categories[size_t(SclCategory::Templates)];
    for (auto root : templates)
        templateOwners[root] = root;
    for (Scl::NodeId id = 0; id < int(doc.nodes.size()); ++id)
    {
        Scl::checkpoint(task);
        if (templateOwners[id] == Scl::NoNode && doc.nodes[id].parent != Scl::NoNode)
            templateOwners[id] = templateOwners[doc.nodes[id].parent];
    }
    QHash<Scl::NodeId, std::vector<Scl::NodeId>> edges, seeds;
    for (const auto& ref : analysis.references)
    {
        Scl::checkpoint(task);
        if (ref.state != Scl::LinkState::Resolved || ref.candidates.size() != 1
            || (ref.kind != Scl::LinkKind::LogicalNodeType && ref.kind != Scl::LinkKind::MemberType))
            continue;
        const auto target = templateOwners[ref.candidates.front()];
        if (target == Scl::NoNode)
            continue;
        const auto owner = iedOwners[ref.source];
        if (owner != Scl::NoNode)
            seeds[owner].push_back(target);
        else if (templateOwners[ref.source] != Scl::NoNode)
            edges[templateOwners[ref.source]].push_back(target);
    }
    for (auto ied : ieds)
    {
        auto pending = seeds.value(ied);
        QSet<Scl::NodeId> seen;
        while (!pending.empty())
        {
            Scl::checkpoint(task);
            const auto id = pending.back();
            pending.pop_back();
            if (seen.contains(id))
                continue;
            seen.insert(id);
            const auto next = edges.constFind(id);
            if (next != edges.cend())
                pending.insert(pending.end(), next->begin(), next->end());
        }
        auto& used = usedTemplates[ied];
        used.assign(seen.begin(), seen.end());
        std::sort(used.begin(), used.end());
        scopedRows.insert(ied, {});
    }
    const auto add = [&](SclView view, int row, Scl::NodeId node) {
        Scl::checkpoint(task);
        if (node < 0)
            return;
        const auto owner = iedOwners[node];
        if (owner != Scl::NoNode)
            scopedRows[owner][size_t(view)].push_back(row);
        else if (templateOwners[node] != Scl::NoNode)
        {
            for (auto ied : ieds)
                if (InScope(node, ied))
                    scopedRows[ied][size_t(view)].push_back(row);
        }
    };
    for (int row = 0; row < int(analysis.reports.size()); ++row)
        add(SclView::Reports, row, analysis.reports[row].source);
    for (int row = 0; row < int(analysis.findings.size()); ++row)
        add(SclView::Findings, row, analysis.findings[row].node);
    for (size_t category = 0; category < size_t(SclCategory::Count); ++category)
        for (int row = 0; row < int(categories[category].size()); ++row)
            add(SclView(int(category) + 1), row, categories[category][row]);
}

bool SclBrowseIndex::InScope(Scl::NodeId node, Scl::NodeId ied) const
{
    if (ied == Scl::NoNode)
        return true;
    if (node < 0 || size_t(node) >= iedOwners.size())
        return false;
    if (iedOwners[node] == ied)
        return true;
    if (templateOwners.empty() || templateOwners[node] == Scl::NoNode)
        return false;
    const auto used = usedTemplates.constFind(ied);
    return used != usedTemplates.cend() && std::binary_search(used->begin(), used->end(), templateOwners[node]);
}
} // namespace iec61850sim
