#include "SclTaskIndex.hpp"
#include "SclBrowseIndex.hpp"

#include <algorithm>
#include <tuple>
#include <QSet>

namespace iec61850sim
{
SclTaskIndex SclTaskIndex::Build(const Scl::ReferenceAnalysis& analysis, const SclBrowseIndex& browse,
                                 Scl::TaskControl* task)
{
    using namespace Scl;
    SclTaskIndex index;
    index.catalog = SignalCatalog::Build(analysis, task);
    const auto& doc = *analysis.document;
    for (int row = 0; row < int(index.catalog.instances.size()); ++row)
    {
        checkpoint(task);
        index.signalsByIed[browse.iedOwners[index.catalog.instances[row].logicalNode]].push_back(row);
    }
    constexpr int relationLimit = 250000;
    int relations = 0;
    QHash<NodeId, NodeId> controlDataSets;
    QHash<NodeId, std::vector<NodeId>> endpoints;
    QHash<NodeId, Input> inputs;
    const auto resolved = [](const Reference& ref) {
        return ref.state == LinkState::Resolved && ref.candidates.size() == 1 ? ref.candidates.front() : NoNode;
    };
    for (int i = 0; i < int(analysis.references.size()); ++i)
    {
        checkpoint(task);
        const auto& ref = analysis.references[i];
        const auto target = resolved(ref);
        switch (ref.kind)
        {
        case LinkKind::ControlDataSet:
            if (target != NoNode && relations++ < relationLimit)
            {
                controlDataSets.insert(ref.source, target);
                index.controlsByDataSet[target].push_back(ref.source);
            }
            break;
        case LinkKind::NetworkControl:
            if (target != NoNode && relations++ < relationLimit)
                endpoints[target].push_back(ref.source);
            break;
        case LinkKind::Fcda:
            if (target != NoNode && ref.logicalNode != NoNode && relations++ < relationLimit)
                index.usages[{ref.logicalNode, doc.attribute(ref.source, QStringLiteral("doName"))}]
                    .push_back({i, doc.nodes[ref.source].parent});
            break;
        case LinkKind::ExtRefData:
            if (relations++ < relationLimit)
            {
                auto& input = inputs[ref.source];
                input.node = ref.source;
                input.dataReference = i;
            }
            break;
        case LinkKind::ExtRefControl:
            if (relations++ < relationLimit)
            {
                auto& input = inputs[ref.source];
                input.node = ref.source;
                input.controlReference = i;
            }
            break;
        default:
            break;
        }
    }

    QHash<NodeId, std::vector<NodeId>> receivers;
    using ControlKey = std::tuple<NodeId, QString, QString, QString, QString, QString>;
    std::map<ControlKey, std::vector<NodeId>> controls;
    const auto attribute = [&](NodeId id, const char* key) {
        return doc.attribute(id, QString::fromLatin1(key));
    };
    const auto addControl = [&](NodeId control) {
        checkpoint(task);
        const auto ln = doc.nodes[control].parent;
        if (ln == NoNode)
            return;
        const auto ld = doc.nodes[ln].parent;
        if (browse.iedOwners[control] == NoNode || ld == NoNode || doc.nodes[ld].name != "LDevice")
            return;
        controls[{browse.iedOwners[control], attribute(ld, "inst"), attribute(ln, "prefix"),
                  attribute(ln, "lnClass"), attribute(ln, "inst"), attribute(control, "name")}]
            .push_back(control);
    };
    for (const auto& report : analysis.reports)
        addControl(report.source);
    for (const auto node : browse.categories[size_t(SclCategory::Communication)])
        if (doc.nodes[node].name == "GSEControl" || doc.nodes[node].name == "SampledValueControl")
            addControl(node);
    int memberComparisons = 0;
    for (auto it = inputs.begin(); it != inputs.end(); ++it)
    {
        checkpoint(task);
        auto& input = it.value();
        if (input.dataReference >= 0)
        {
            const auto& data = analysis.references[input.dataReference];
            auto control = input.controlReference < 0 ? NoNode : resolved(analysis.references[input.controlReference]);
            if (control == NoNode && resolved(data) != NoNode && data.logicalNode != NoNode
                && attribute(input.node, "serviceType").isEmpty())
            {
                const auto sourceLd = attribute(input.node, "srcLDInst");
                const auto sourceClass = attribute(input.node, "srcLNClass");
                const auto sourceControl = attribute(input.node, "srcCBName");
                if (!sourceLd.isEmpty() && !sourceClass.isEmpty() && !sourceControl.isEmpty())
                {
                    const auto candidates = controls.find({browse.iedOwners[data.logicalNode], sourceLd,
                        attribute(input.node, "srcPrefix"), sourceClass, attribute(input.node, "srcLNInst"), sourceControl});
                    if (candidates != controls.end())
                    {
                        input.ambiguousControl = candidates->second.size() > 1;
                        if (!input.ambiguousControl)
                        {
                            control = candidates->second.front();
                            input.inferred = true;
                        }
                    }
                }
            }
            const auto dataSet = controlDataSets.value(control, NoNode);
            const auto key = std::make_pair(data.logicalNode, doc.attribute(input.node, QStringLiteral("doName")));
            const auto uses = index.usages.find(key);
            if (dataSet != NoNode && resolved(data) != NoNode && uses != index.usages.end())
            {
                const auto dataAttribute = attribute(input.node, "daName");
                for (const auto& use : uses->second)
                {
                    checkpoint(task);
                    if (memberComparisons++ >= relationLimit)
                    {
                        index.relationsLimited = true;
                        break;
                    }
                    const auto& member = analysis.references[use.reference];
                    if (use.dataSet == dataSet && member.arrayIndex < 0
                        && attribute(member.source, "daName") == dataAttribute)
                    {
                        input.matchedStream = control;
                        receivers[control].push_back(input.node);
                        break;
                    }
                }
            }
            if (input.matchedStream == NoNode)
                input.inferred = false;
        }
        index.inputs.push_back(input);
    }
    const auto ordered = [](auto& ids) { std::sort(ids.begin(), ids.end()); };
    std::sort(index.inputs.begin(), index.inputs.end(), [](const auto& a, const auto& b) { return a.node < b.node; });
    for (int i = 0; i < int(index.inputs.size()); ++i)
    {
        checkpoint(task);
        auto& input = index.inputs[i];
        if (input.dataReference < 0)
            continue;
        const auto& ref = analysis.references[input.dataReference];
        if (resolved(ref) != NoNode && ref.logicalNode != NoNode)
            index.inputsBySignal[{ref.logicalNode, attribute(input.node, "doName")}].push_back(i);
    }
    // Missing source-control fields are common in earlier engineering exports.
    // Keep member-based candidates separate from confirmed stream bindings.
    int comparisons = 0;
    for (int i = 0; i < int(index.inputs.size()); ++i)
    {
        checkpoint(task);
        auto& input = index.inputs[i];
        if (input.dataReference < 0)
            continue;
        const auto& data = analysis.references[input.dataReference];
        if (resolved(data) == NoNode || data.logicalNode == NoNode)
            continue;
        const bool hasControl = !attribute(input.node, "srcCBName").isEmpty()
            || !attribute(input.node, "srcLDInst").isEmpty() || !attribute(input.node, "srcLNClass").isEmpty()
            || !attribute(input.node, "srcPrefix").isEmpty() || !attribute(input.node, "srcLNInst").isEmpty();
        const auto uses = index.usages.find({data.logicalNode, attribute(input.node, "doName")});
        if (uses == index.usages.end() || (hasControl && input.matchedStream == NoNode))
            continue;
        const auto da = attribute(input.node, "daName");
        const auto service = attribute(input.node, "serviceType");
        QSet<NodeId> publishersSeen;
        for (const auto& use : uses->second)
        {
            checkpoint(task);
            if (++comparisons > relationLimit)
            {
                index.relationsLimited = true;
                break;
            }
            const auto& member = analysis.references[use.reference];
            const auto memberDa = attribute(member.source, "daName");
            // Unknown array selection never establishes a virtual-terminal mapping.
            if (member.arrayIndex >= 0 || !attribute(member.source, "ix").isNull()
                || !attribute(input.node, "ix").isNull())
                continue;
            const bool aggregate = da != memberDa && (da.isEmpty() || memberDa.isEmpty());
            if (da != memberDa && !aggregate)
                continue;
            // The resolver records the actual DA functional constraint in the MMS
            // mapping. A DO-level FCDA contains only that FC's attributes.
            const auto dataFc = data.mmsItem.section('$', 1, 1);
            if (!da.isEmpty() && ((!dataFc.isEmpty() && dataFc != attribute(member.source, "fc"))
                || (memberDa.isEmpty() && dataFc.isEmpty())))
                continue;
            const auto declaredFc = attribute(input.node, "fc");
            if (!declaredFc.isEmpty() && declaredFc != attribute(member.source, "fc"))
                continue;
            const auto publishers = index.controlsByDataSet.constFind(use.dataSet);
            if (publishers == index.controlsByDataSet.cend())
                continue;
            for (const auto control : *publishers)
            {
                checkpoint(task);
                if (++comparisons > relationLimit || index.memberLinks.size() >= relationLimit)
                {
                    index.relationsLimited = true;
                    break;
                }
                const auto& kind = doc.nodes[control].name;
                if (kind != "GSEControl" && kind != "SampledValueControl")
                    continue;
                if ((!service.isEmpty() && service != (kind == "GSEControl" ? "GOOSE" : "SMV"))
                    || (hasControl && control != input.matchedStream))
                    continue;
                const int link = int(index.memberLinks.size());
                index.memberLinks.push_back({control, use.reference, i,
                    control == input.matchedStream && !aggregate, aggregate});
                index.linksByControl[control].push_back(link);
                index.linksByInput[input.node].push_back(link);
                if (!publishersSeen.contains(control))
                {
                    publishersSeen.insert(control);
                    input.publisherCandidates.push_back(control);
                }
            }
        }
    }
    for (auto id : browse.categories[size_t(SclCategory::Communication)])
    {
        checkpoint(task);
        const auto& name = doc.nodes[id].name;
        if (browse.iedOwners[id] == NoNode || (name != "GSEControl" && name != "SampledValueControl"))
            continue;
        auto& list = name == "GSEControl" ? index.goose : index.sv;
        if (relations++ >= relationLimit)
            break;
        auto stream = Stream{id, controlDataSets.value(id, NoNode), endpoints.take(id), receivers.take(id)};
        ordered(stream.endpoints);
        ordered(stream.receivers);
        list.push_back(std::move(stream));
    }
    index.relationsLimited |= relations > relationLimit;
    return index;
}
}
