#include "SclSignalCatalog.hpp"

#include <QSet>

namespace Scl
{
namespace
{
QString ReadAttribute(const Document& doc, NodeId id, const char* name)
{
    return doc.attribute(id, QString::fromLatin1(name));
}

unsigned Classify(const Document& doc, NodeId type, const QString& cdc,
                  const QString& lnClass, const QString& path)
{
    bool status = false;
    bool measured = false;
    bool operation = false;
    bool setting = false;
    for (auto child = doc.nodes[type].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
    {
        if (!doc.nodes[child].isScl() || doc.nodes[child].name != "DA")
            continue;
        const auto fc = ReadAttribute(doc, child, "fc");
        const auto name = ReadAttribute(doc, child, "name");
        status |= fc == "ST" && name == "stVal";
        measured |= fc == "MX" && (name == "mag" || name == "cVal" || name == "instMag");
        operation |= fc == "CO" && name == "Oper";
        setting |= fc == "SP" || fc == "SG" || fc == "SE";
    }
    unsigned result = 0;
    const bool switching = (lnClass == "XCBR" || lnClass == "XSWI") && path == "Pos";
    const bool generalIndication = lnClass == "GGIO" && path.startsWith("Ind");
    if (status && (switching || generalIndication) && (cdc == "SPS" || cdc == "DPS" || cdc == "DPC" || cdc == "INS"))
        result |= Indication;
    if (measured && (cdc == "MV" || cdc == "CMV") && (lnClass == "MMXU" || lnClass == "MMXN"))
        result |= Measurement;
    if (operation && switching && (cdc == "DPC" || cdc == "SPC"))
        result |= ControlCandidate;
    if (setting && (cdc == "ASG" || cdc == "ING" || cdc == "SPG"))
        result |= Configuration;
    return result;
}
}

SignalCatalog SignalCatalog::Build(const ReferenceAnalysis& analysis, TaskControl* task)
{
    SignalCatalog result;
    const auto& doc = *analysis.document;
    QHash<NodeId, NodeId> targets;
    for (const auto& ref : analysis.references)
    {
        checkpoint(task);
        if ((ref.kind == LinkKind::LogicalNodeType || ref.kind == LinkKind::MemberType)
            && ref.state == LinkState::Resolved && ref.candidates.size() == 1)
            targets.insert(ref.source, ref.candidates.front());
    }
    QHash<NodeId, QPair<int, int>> layouts;
    QSet<NodeId> ancestry;
    const auto expand = [&](auto&& self, NodeId declaration, const QString& prefix,
                            const QString& lnClass, bool parentArray, int depth) -> void {
        checkpoint(task);
        if (depth >= 32 || result.definitions.size() >= DefinitionLimit)
        {
            result.limited = true;
            return;
        }
        const auto name = ReadAttribute(doc, declaration, "name");
        if (name.isEmpty() || prefix.size() + name.size() > 2048)
        {
            result.limited = true;
            return;
        }
        const auto path = prefix.isEmpty() ? name : prefix + '.' + name;
        const auto type = targets.value(declaration, NoNode);
        const bool array = parentArray || !ReadAttribute(doc, declaration, "count").isEmpty();
        bool composite = false;
        if (type != NoNode)
            for (auto child = doc.nodes[type].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
                composite |= doc.nodes[child].isScl() && doc.nodes[child].name == "SDO";
        const auto cdc = type == NoNode ? QString{} : ReadAttribute(doc, type, "cdc");
        result.definitions.push_back({path, cdc, declaration, type,
            type == NoNode ? 0u : Classify(doc, type, cdc, lnClass, path), array, composite});
        if (!composite)
            return;
        if (ancestry.contains(type))
        {
            result.limited = true;
            return;
        }
        ancestry.insert(type);
        for (auto child = doc.nodes[type].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
            if (doc.nodes[child].isScl() && doc.nodes[child].name == "SDO")
                self(self, child, path, lnClass, array, depth + 1);
        ancestry.remove(type);
    };
    for (const auto& ref : analysis.references)
    {
        checkpoint(task);
        if (ref.kind != LinkKind::LogicalNodeType)
            continue;
        if (ref.state != LinkState::Resolved || ref.candidates.size() != 1)
        {
            ++result.unresolvedLogicalNodes;
            continue;
        }
        const auto type = ref.candidates.front();
        if (!layouts.contains(type))
        {
            const int first = int(result.definitions.size());
            const auto lnClass = ReadAttribute(doc, type, "lnClass");
            for (auto child = doc.nodes[type].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
                if (doc.nodes[child].isScl() && doc.nodes[child].name == "DO")
                    expand(expand, child, {}, lnClass, false, 0);
            layouts.insert(type, {first, int(result.definitions.size())});
        }
        const auto range = layouts.value(type);
        for (int i = range.first; i < range.second; ++i)
        {
            checkpoint(task);
            if (result.instances.size() >= ObjectLimit)
            {
                result.limited = true;
                break;
            }
            result.instances.push_back({ref.source, i});
        }
    }
    return result;
}
}
