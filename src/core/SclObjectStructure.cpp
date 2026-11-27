#include "SclObjectStructure.hpp"

namespace iec61850sim {
using namespace Scl;

QString SclMemberLevel(const Document& doc, NodeId declaration)
{
    if (declaration == NoNode) return QStringLiteral("尚未确定");
    const auto& n = doc.nodes[declaration];
    if (n.name == "DO" || n.name == "SDO") return QStringLiteral("DO / SDO 成员（覆盖同 FC 属性）");
    if (doc.attribute(declaration,"bType") == "Struct") return QStringLiteral("结构属性成员（覆盖下级同 FC 属性）");
    if (n.name == "DA" || n.name == "BDA") return QStringLiteral("精确属性成员");
    return QStringLiteral("尚未确定");
}

QHash<NodeId, SclObjectStructure> BuildSclObjectStructures(
    const ReferenceAnalysis& a, TaskControl* task, bool& limited)
{
    const auto& doc = *a.document;
    QHash<NodeId,SclObjectStructure> result;
    QHash<NodeId,NodeId> types;
    QHash<NodeId,LinkState> states;
    for (const auto& ref : a.references) {
        checkpoint(task);
        if (ref.kind != LinkKind::LogicalNodeType && ref.kind != LinkKind::MemberType) continue;
        if (states.contains(ref.source)) {
            states[ref.source] = LinkState::Ambiguous; types[ref.source] = NoNode;
        } else {
            states.insert(ref.source,ref.state);
            types.insert(ref.source,ref.state == LinkState::Resolved && ref.candidates.size() == 1 ? ref.candidates.front() : NoNode);
        }
    }
    // Index names once. Duplicate DA names (including different FCs) remain
    // ambiguous for DOI/SDI/DAI, which carry no FC discriminator.
    QHash<NodeId,QHash<QString,NodeId>> names;
    for (NodeId n = 0; n < int(doc.nodes.size()); ++n) {
        checkpoint(task);
        const auto& node = doc.nodes[n];
        if (!node.isScl() || (node.name != "DO" && node.name != "SDO" && node.name != "DA" && node.name != "BDA")) continue;
        auto& entries = names[node.parent]; const auto name = doc.attribute(n,"name");
        if (entries.contains(name)) entries[name] = NoNode; else entries.insert(name,n);
    }
    for (NodeId n = 0; n < int(doc.nodes.size()); ++n) {
        checkpoint(task);
        const auto& node = doc.nodes[n];
        if (!node.isScl()) continue;
        const auto& tag = node.name;
        const bool instance = tag == "DOI" || tag == "SDI" || tag == "DAI";
        const bool declaration = tag == "DO" || tag == "SDO" || tag == "DA" || tag == "BDA";
        if (!instance && !declaration && tag != "LN" && tag != "LN0" && tag != "DOType"
            && tag != "LNodeType" && tag != "DAType" && tag != "EnumType") continue;
        if (result.size() >= 500000) { limited = true; break; }
        SclObjectStructure item;
        if (tag == "LN" || tag == "LN0") {
            item.declaration = n; item.type = types.value(n,NoNode); item.state = states.value(n,LinkState::Missing);
        } else if (!instance && !declaration) {
            item.type = n; item.state = LinkState::Resolved;
            if (tag == "DOType") item.cdcType = n;
        } else {
            if (instance) {
                const auto parent = result.constFind(node.parent);
                if (parent != result.cend() && parent->type != NoNode && parent->state == LinkState::Resolved) {
                    const auto entries = names.constFind(parent->type);
                    if (entries != names.cend()) {
                        const auto match = entries->constFind(doc.attribute(n,"name"));
                        if (match != entries->cend()) {
                            item.declaration = *match;
                            item.state = *match == NoNode ? LinkState::Ambiguous : LinkState::Resolved;
                        } else item.state = LinkState::Missing;
                    }
                    if (item.declaration != NoNode) {
                        const auto& d = doc.nodes[item.declaration];
                        const bool object = d.name == "DO" || d.name == "SDO";
                        const auto expected = d.name == "DO" ? QStringLiteral("DOI")
                            : object || doc.attribute(item.declaration,"bType") == "Struct" ? QStringLiteral("SDI") : QStringLiteral("DAI");
                        if (tag != expected) { item.declaration = NoNode; item.state = LinkState::Blocked; }
                    }
                    item.objectPath = parent->objectPath; item.attributePath = parent->attributePath;
                    item.cdcType = parent->cdcType; item.fcDeclaration = parent->fcDeclaration;
                } else item.state = parent == result.cend() ? LinkState::Blocked : parent->state;
            } else { item.declaration = n; item.state = LinkState::Resolved; }
            if (item.declaration != NoNode) {
                const auto& d = doc.nodes[item.declaration];
                const auto name = doc.attribute(item.declaration,"name");
                item.type = types.value(item.declaration,NoNode);
                if (d.name == "DO" || d.name == "SDO") {
                    item.objectPath += (item.objectPath.isEmpty() ? QString() : QStringLiteral(".")) + name;
                    item.cdcType = item.type;
                } else {
                    item.attributePath += (item.attributePath.isEmpty() ? QString() : QStringLiteral(".")) + name;
                    if (d.name == "DA") item.fcDeclaration = item.declaration;
                }
                if (states.contains(item.declaration) && states[item.declaration] != LinkState::Resolved)
                    item.state = states[item.declaration];
                if (item.objectPath.size() + item.attributePath.size() > 2048) {
                    limited = true; item.objectPath.clear(); item.attributePath.clear(); item.state = LinkState::Unsupported;
                }
            }
        }
        result.insert(n,std::move(item));
    }
    for (const auto& ref : a.references) {
        checkpoint(task);
        if (ref.kind != LinkKind::Fcda) continue;
        if (result.size() >= 500000) { limited = true; break; }
        SclObjectStructure item; item.state = ref.state;
        // Keep only the resolved trace, never upgrade a candidate into a target.
        if (ref.state == LinkState::Resolved) {
            const auto trace = a.fcdaTraces.constFind(ref.source);
            if (trace != a.fcdaTraces.cend()) for (const auto& step : trace->steps) {
                checkpoint(task);
                if (step.field != FcdaField::DoName && step.field != FcdaField::DaName) continue;
                const auto& d = doc.nodes[step.node];
                item.declaration = step.node; item.type = types.value(step.node,NoNode);
                auto& path = step.field == FcdaField::DoName ? item.objectPath : item.attributePath;
                path += (path.isEmpty() ? QString() : QStringLiteral(".")) + doc.attribute(step.node,"name");
                if (d.name == "DO" || d.name == "SDO") item.cdcType = item.type;
                if (d.name == "DA") item.fcDeclaration = step.node;
            }
        }
        result.insert(ref.source,std::move(item));
    }
    return result;
}
}
