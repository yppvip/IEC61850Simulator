#include "SclRelationGraph.hpp"

#include <QSet>

namespace iec61850sim
{
SclRelationGraph ProjectSclRelation(const SclSnapshot& snapshot, const SclConnection& connection,
    const SclConnectionMember& root, int depth, Scl::TaskControl* task)
{
    using Role = SclGraphRole;
    using Relation = SclGraphRelation;
    using namespace Scl;
    const auto& doc = *snapshot.analysis.document;
    SclRelationGraph graph;
    checkpoint(task);
    const auto node = [&](Role role, NodeId evidence, int layer, const QString& label,
                          NodeId member = NoNode, NodeId input = NoNode) {
        if (evidence == NoNode)
            return -1;
        if (graph.nodes.size() == SclRelationGraph::NodeLimit)
        {
            graph.limited = true;
            return -1;
        }
        graph.nodes.push_back({role, evidence, layer, label.left(512), member, input});
        return int(graph.nodes.size()) - 1;
    };
    const auto edge = [&](int from, int to, Relation relation, NodeId evidence) {
        if (from < 0 || to < 0)
            return;
        if (graph.edges.size() == SclRelationGraph::EdgeLimit)
        {
            graph.limited = true;
            return;
        }
        graph.edges.push_back({from, to, relation, evidence});
    };
    const int sender = node(Role::Sender, connection.sender, 0, connection.sourceName);
    const int stream = node(Role::Publication, connection.control, 1,
        connection.service + ' ' + connection.controlName + '\n' + connection.identifier);
    const int member = node(Role::Member, root.member, 2, root.cells.value(1), root.member, root.input);
    const int receiver = node(Role::Receiver, connection.receiver, 4, connection.targetName);
    edge(sender, stream, Relation::Ownership, connection.control);
    edge(stream, member, Relation::Membership, root.member);

    const auto addInput = [&](NodeId id, Relation relation) {
        auto label = doc.attribute(id, QStringLiteral("intAddr"));
        if (label.isEmpty())
            label = QStringLiteral("未声明内部地址");
        const auto input = node(Role::Input, id, 3, label, root.member, id);
        edge(member, input, relation, id);
        edge(input, receiver, Relation::Ownership, id);
        return input >= 0;
    };

    if (connection.control == NoNode && root.input != NoNode)
        addInput(root.input, Relation::Candidate);
    else
    {
        QSet<NodeId> seen;
        // Keep the table's selected mapping visible even when the expansion fills the graph.
        for (const auto index : connection.links)
        {
            checkpoint(task);
            const auto& link = snapshot.tasks.memberLinks[index];
            if (snapshot.analysis.references[link.memberReference].source != root.member
                || snapshot.tasks.inputs[link.input].node != root.input)
                continue;
            addInput(root.input, link.bound ? Relation::Bound
                : link.aggregate ? Relation::ContainmentCandidate : Relation::Candidate);
            seen.insert(root.input);
            break;
        }
        int visited = 0;
        for (const auto index : connection.links)
        {
            checkpoint(task);
            if (++visited > 250000)
            {
                graph.limited = true;
                break;
            }
            const auto& link = snapshot.tasks.memberLinks[index];
            if (snapshot.analysis.references[link.memberReference].source != root.member)
                continue;
            const auto input = snapshot.tasks.inputs[link.input].node;
            if (seen.contains(input))
                continue;
            seen.insert(input);
            if ((depth == 0 && input != root.input) || !addInput(input, link.bound ? Relation::Bound
                    : link.aggregate ? Relation::ContainmentCandidate : Relation::Candidate))
                ++graph.hiddenMappings;
        }
    }
    if (depth > 0 && member >= 0)
    {
        QSet<NodeId> seen;
        for (const auto& field : root.description.fields)
        {
            checkpoint(task);
            if (field.node == NoNode || field.node == root.member || seen.contains(field.node))
                continue;
            seen.insert(field.node);
            edge(member, node(Role::Definition, field.node, 3, SclDescriptionLabel(field.source), root.member, root.input),
                Relation::Evidence, field.node);
        }
    }
    return graph;
}
}
