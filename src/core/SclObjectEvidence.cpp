#include "SclObjectEvidence.hpp"
#include "SclAnalysisService.hpp"
#include <QRegularExpression>

namespace iec61850sim {
using namespace Scl;

SclObjectEvidence ProjectSclNodeEvidence(const SclSnapshot& s, NodeId id, const std::function<void()>& tick)
{
    const auto& doc = *s.analysis.document;
    const auto& tag = doc.nodes[id].name;
    if (tag == "FCDA") {
        const auto entry = s.mmsIndex.memberReferences.constFind(id);
        if (entry != s.mmsIndex.memberReferences.cend()) {
            const auto& ref = s.analysis.references[*entry];
            SclMmsRow row; row.key = {SclMmsRecordKind::Member,id,*entry,ref.memberOrdinal};
            row.state = ref.state; row.fc = doc.attribute(id,"fc"); row.arrayIndex = ref.arrayIndex;
            row.component = ref.mmsComponent; row.domain = ref.mmsDomain; row.item = ref.mmsItem;
            row.objectReference = ref.objectReference; row.limitation = ref.detail;
            return ProjectSclObjectEvidence(s,row,tick);
        }
    }
    SclObjectEvidence out; out.document = s.analysis.document; out.sha256 = s.analysis.sha256;
    const auto meta = s.browse.objects.constFind(id);
    if (meta == s.browse.objects.cend()) {
        out.limitation = s.browse.objectsLimited ? QStringLiteral("结构索引受限；尚未确定。") : QStringLiteral("该节点不是对象或模板结构。");
        return out;
    }
    out.state = meta->state;
    if (s.browse.iedOwners[id] != NoNode && tag != "LN" && tag != "LN0") {
        // Use the same declaration chain and instance locator as task details.
        SclMmsRow row; row.key.kind = SclMmsRecordKind::Address; row.state = meta->state;
        auto n = id;
        while (n != NoNode && doc.nodes[n].name != "LN" && doc.nodes[n].name != "LN0") {
            tick();
            const auto part = s.browse.objects.constFind(n);
            if (part == s.browse.objects.cend() || part->declaration == NoNode) break;
            row.templatePath.prepend(part->declaration);
            const auto ix = doc.attribute(n,"ix");
            if (!ix.isNull()) {
                bool ok = false; const auto value = ix.toInt(&ok);
                if (row.key.arrayDeclaration != NoNode || !ok || value < 0) row.state = LinkState::Unsupported;
                else { row.key.arrayDeclaration = part->declaration; row.arrayIndex = value; row.arraySelector = n; }
            }
            n = doc.nodes[n].parent;
        }
        if (n != NoNode && (doc.nodes[n].name == "LN" || doc.nodes[n].name == "LN0")) {
            row.key.node = n;
            if (meta->fcDeclaration != NoNode) row.fc = doc.attribute(meta->fcDeclaration,"fc");
            row.fcEvidence = {meta->fcDeclaration,"fc",{}};
            return ProjectSclObjectEvidence(s,row,tick);
        }
    }
    // A template has no selected LN, Domain or Item. Do not bind a first user.
    out.objectPath = meta->objectPath; out.attributePath = meta->attributePath;
    out.objectType = meta->cdcType;
    if (meta->cdcType != NoNode) {
        out.cdc = doc.attribute(meta->cdcType,"cdc"); out.cdcEvidence = SclEvidenceAt(s,meta->cdcType,"cdc");
    }
    if (meta->declaration != NoNode) {
        out.declarations << SclEvidenceAt(s,meta->declaration,"name");
        out.basicType = doc.attribute(meta->declaration,"bType");
        out.basicTypeEvidence = SclEvidenceAt(s,meta->declaration,"bType");
    }
    if (meta->type != NoNode) out.typeLinks << SclEvidenceAt(s,meta->type,"id");
    if (meta->fcDeclaration != NoNode) {
        out.fc = doc.attribute(meta->fcDeclaration,"fc"); out.fcEvidence = SclEvidenceAt(s,meta->fcDeclaration,"fc");
    }
    out.instance.reason = s.browse.iedOwners[id] != NoNode
        ? QStringLiteral("LN / 实例尚无唯一对象类型链；不生成地址。请查看声明与引用依据。")
        : QStringLiteral("模板结构未绑定 LN 实例；路径相对所属定义，不生成实例地址。复用候选请查看引用与使用方。");
    out.limitation = out.instance.reason;
    return out;
}

SclEvidenceSource SclEvidenceAt(const SclSnapshot& s, NodeId id, const QString& attribute)
{
    SclEvidenceSource out;
    if (id < 0 || id >= int(s.analysis.document->nodes.size())) return out;
    out.node = id; out.attribute = attribute;
    out.location = s.analysis.document->nodes[id].source;
    if (s.analysis.source && out.location.byteOffset >= 0 && out.location.line > 0)
        out.precision = SclEvidencePrecision::OpeningTag;
    out.raw = attribute.isEmpty() ? QString{} : s.analysis.document->attribute(id, attribute);
    out.present = attribute.isEmpty() || !out.raw.isNull();
    return out;
}

namespace {
NodeId Ancestor(const Document& doc, NodeId id, const char* tag, const std::function<void()>& tick)
{
    while (id != NoNode) {
        tick();
        if (doc.nodes[id].isScl() && doc.nodes[id].name == QLatin1String(tag)) return id;
        id = doc.nodes[id].parent;
    }
    return NoNode;
}

SclInstanceLocation LocateInstance(const SclSnapshot& s, NodeId ln, const QList<NodeId>& path,
                                 NodeId array, int index, const std::function<void()>& tick)
{
    SclInstanceLocation out;
    const auto& doc = *s.analysis.document;
    if (ln == NoNode) { out.reason = QStringLiteral("没有唯一 LN 实例。"); return out; }
    NodeId parent = ln;
    out.target = SclEvidenceAt(s, ln);
    out.matched << out.target;
    for (const auto declaration : path) {
        tick(); const auto& node = doc.nodes[declaration];
        const auto name = doc.attribute(declaration, "name");
        out.stoppedAt = name;
        const auto count = doc.attribute(declaration, "count");
        if (!count.isNull() && (declaration != array || index < 0)) {
            out.state = SclInstanceLocationState::ArraySelectionRequired;
            out.reason = QStringLiteral("数组未选择唯一实例元素；保留已匹配祖先。"); return out;
        }
        // DOI/SDI/DAI cannot distinguish identically named DA declarations by FC.
        if (node.name == "DA" || node.name == "BDA") {
            int names = 0;
            for (auto c = doc.nodes[node.parent].firstChild; c != NoNode; c = doc.nodes[c].nextSibling) {
                tick();
                if (doc.nodes[c].isScl() && doc.nodes[c].name == node.name && doc.attribute(c,"name") == name) ++names;
            }
            if (names != 1) {
                out.state = SclInstanceLocationState::Ambiguous;
                out.reason = QStringLiteral("实例节点不能区分同名属性声明/FC。"); return out;
            }
        }
        const auto tag = node.name == "DO" ? QStringLiteral("DOI")
            : node.name == "SDO" || doc.attribute(declaration,"bType") == "Struct" ? QStringLiteral("SDI") : QStringLiteral("DAI");
        QList<NodeId> candidates;
        bool blockedArray = false, group = false;
        for (auto c = doc.nodes[parent].firstChild; c != NoNode; c = doc.nodes[c].nextSibling) {
            tick();
            if (!doc.nodes[c].isScl() || doc.nodes[c].name != tag || doc.attribute(c,"name") != name) continue;
            const auto ix = doc.attribute(c,"ix");
            if (declaration == array && index >= 0) {
                bool ok = false;
                const auto n = ix.toInt(&ok);
                static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
                if (!ok || !digits.match(ix).hasMatch() || n != index) continue;
            } else if (!ix.isNull()) blockedArray = true;
            group |= !doc.attribute(c,"sGroup").isNull();
            if (candidates.size() < 65) candidates << c;
        }
        if (candidates.size() != 1 || blockedArray || group) {
            for (const auto c : candidates.mid(0,64)) out.candidates << SclEvidenceAt(s,c);
            out.candidatesLimited = candidates.size() > 64;
            out.state = candidates.size() > 1 ? SclInstanceLocationState::Ambiguous
                : group ? SclInstanceLocationState::SettingGroup
                : blockedArray ? SclInstanceLocationState::ArraySelectionRequired : SclInstanceLocationState::Ancestor;
            out.reason = candidates.isEmpty() ? QStringLiteral("实例未声明该段；定位到最深已匹配祖先。")
                : QStringLiteral("实例候选或数组/定值组语境不能唯一确定；保留祖先。");
            if (out.candidatesLimited) out.reason += QStringLiteral("候选最多预览64项。");
            return out;
        }
        parent = candidates.front(); out.target = SclEvidenceAt(s,parent); out.matched << out.target;
    }
    out.state = SclInstanceLocationState::Exact; out.stoppedAt.clear();
    return out;
}
}

SclObjectEvidence ProjectSclObjectEvidence(const SclSnapshot& s, const SclMmsRow& row,
                                         const std::function<void()>& tick)
{
    SclObjectEvidence out;
    out.sha256 = s.analysis.sha256; out.document = s.analysis.document;
    out.baseRuleVersion = s.analysis.ruleSetVersion; out.mmsRuleVersion = s.mms.ruleSetVersion;
    const auto& doc = *s.analysis.document;
    const auto attr = [&](NodeId n, const char* name) { return n == NoNode ? QString{} : doc.attribute(n,QString::fromLatin1(name)); };
    out.state = row.state; out.fc = row.fc; out.limitation = row.limitation;
    auto path = row.templatePath;
    auto ln = row.key.node;
    NodeId array = row.key.arrayDeclaration;
    int arrayIndex = row.arrayIndex;
    if (row.key.kind == SclMmsRecordKind::Member) {
        const auto& ref = s.analysis.references.at(row.key.record);
        ln = ref.logicalNode; arrayIndex = ref.arrayIndex;
        out.member = SclEvidenceAt(s,ref.source); out.dataSet = SclEvidenceAt(s,doc.nodes[ref.source].parent,"name");
        out.rawDoName = attr(ref.source,"doName"); out.rawDaName = attr(ref.source,"daName");
        out.rawFc = attr(ref.source,"fc"); out.rawIx = attr(ref.source,"ix");
        out.daNamePresent = !out.rawDaName.isNull();
        out.fcEvidence = SclEvidenceAt(s,ref.source,"fc");
        out.targetCandidatesLimited = ref.candidates.size() > 64;
        for (const auto candidate : ref.candidates.mid(0,64)) {
            tick(); out.targetCandidates << SclEvidenceAt(s,candidate);
        }
        const auto trace = s.analysis.fcdaTraces.constFind(ref.source);
        if (trace != s.analysis.fcdaTraces.cend()) for (const auto& step : trace->steps) {
            tick();
            if (step.field == FcdaField::DoName || step.field == FcdaField::DaName) path << step.node;
            if (step.field == FcdaField::Ix) array = step.node;
        }
        if (ref.state != LinkState::Resolved) ln = NoNode;
    }
    out.logicalNode = ln;
    out.arrayDeclaration = array; out.arrayIndex = arrayIndex;
    out.arraySelector = out.member.node != NoNode ? out.member.node : row.arraySelector;
    out.component = row.component;
    if (ln == NoNode) {
        out.instance.reason = QStringLiteral("成员未唯一解析；保留原始声明与失败状态。");
        return out;
    }
    QStringList objects, attributes;
    QHash<NodeId,NodeId> linkedTypes;
    const auto typeLink = [&](NodeId declaration, LinkKind kind) {
        if (const auto cached = linkedTypes.constFind(declaration); cached != linkedTypes.cend()) return *cached;
        const auto links = s.analysis.outgoing.equal_range(declaration);
        NodeId target = NoNode;
        int matches = 0;
        SclEvidenceResolution resolution;
        resolution.source = SclEvidenceAt(s,declaration,kind == LinkKind::LogicalNodeType ? "lnType" : "type");
        for (auto it = links.first; it != links.second; ++it) {
            tick(); const auto& link = s.analysis.references[it.value()];
            if (link.kind != kind) continue;
            ++matches;
            resolution.state = link.state;
            for (const auto candidate : link.candidates) {
                tick();
                if (resolution.candidates.size() < 64) resolution.candidates << SclEvidenceAt(s,candidate,"id");
                else resolution.candidatesLimited = true;
            }
            if (link.state == LinkState::Resolved && link.candidates.size() == 1) target = link.candidates.front();
        }
        if (matches != 1) {
            target = NoNode;
            if (matches > 1) resolution.state = LinkState::Ambiguous;
        }
        if (resolution.source.present || kind == LinkKind::LogicalNodeType) out.typeResolutions << resolution;
        linkedTypes.insert(declaration,target);
        if (target != NoNode) {
            const auto source = SclEvidenceAt(s,declaration,kind == LinkKind::LogicalNodeType ? "lnType" : "type");
            const auto definition = SclEvidenceAt(s,target,"id");
            out.typeLinks << source << definition;
            out.resolutions << SclEvidencePart{"type",definition.raw,"scl-" + out.baseRuleVersion + "/type-reference",
                SclEvidenceOrigin::ReferenceResolution,{source,definition}};
        }
        return target;
    };
    auto owner = typeLink(ln, LinkKind::LogicalNodeType);
    if (row.key.kind == SclMmsRecordKind::Signal) {
        out.state = LinkState::Resolved;
        const auto parts = row.key.field.split('.');
        for (int i = 0; i < parts.size(); ++i) {
            tick();
            if (owner == NoNode) { out.state = LinkState::Blocked; break; }
            QList<NodeId> found;
            for (auto c = doc.nodes[owner].firstChild; c != NoNode; c = doc.nodes[c].nextSibling) {
                tick();
                if (doc.nodes[c].isScl() && doc.nodes[c].name == (i ? "SDO" : "DO") && attr(c,"name") == parts[i]) found << c;
            }
            if (found.size() != 1) { out.state = found.isEmpty() ? LinkState::Missing : LinkState::Ambiguous; break; }
            path << found.front();
            owner = typeLink(found.front(), LinkKind::MemberType);
            if (owner == NoNode) { out.state = LinkState::Blocked; break; }
        }
    }
    for (const auto declaration : path) {
        tick(); const auto& node = doc.nodes[declaration];
        out.declarations << SclEvidenceAt(s,declaration,"name");
        const auto type = typeLink(declaration, LinkKind::MemberType);
        if (node.name == "DO" || node.name == "SDO") {
            objects << attr(declaration,"name"); out.objectType = type;
            out.cdc = attr(type,"cdc");
            out.cdcEvidence = SclEvidenceAt(s,type,"cdc");
            out.objectCdcs << out.cdcEvidence;
        } else {
            attributes << attr(declaration,"name"); out.basicType = attr(declaration,"bType");
            out.basicTypeEvidence = SclEvidenceAt(s,declaration,"bType");
        }
    }
    out.objectPath = objects.join('.'); out.attributePath = attributes.join('.');
    out.fcEvidence = row.key.kind == SclMmsRecordKind::Member ? SclEvidenceAt(s,row.key.node,"fc")
        : SclEvidenceAt(s,row.fcEvidence.node,row.fcEvidence.attribute);
    if (out.state == LinkState::Resolved || out.state == LinkState::Partial)
        out.instance = LocateInstance(s,ln,path,array,arrayIndex,tick);
    else out.instance.reason = QStringLiteral("类型路径未唯一解析，不能确认实例位置。");
    const auto ld = Ancestor(doc,ln,"LDevice",tick);
    const auto ied = Ancestor(doc,ln,"IED",tick);
    out.logicalDevice = ld; out.ied = ied; out.server = Ancestor(doc,ln,"Server",tick);
    out.accessPoint = row.addressScope.accessPoint == NoNode ? Ancestor(doc,ln,"AccessPoint",tick) : row.addressScope.accessPoint;
    const auto sourcePart = [&](const QString& role, NodeId n, const char* a, SclEvidenceOrigin origin) {
        return SclEvidencePart{role,attr(n,a),{},origin,{SclEvidenceAt(s,n,QString::fromLatin1(a))}};
    };
    const auto lnParts = [&] {
        QList<SclEvidencePart> parts;
        if (doc.nodes[ln].name == "LN0") {
            auto p = sourcePart("LNName",ln,"lnClass",SclEvidenceOrigin::RuleCombination);
            p.value = "LLN0"; p.rule = "mms-0.1/LN0"; parts << p;
        } else for (const auto* a : {"prefix","lnClass","inst"}) {
            auto part = sourcePart("LNName",ln,a,SclEvidenceOrigin::Declaration);
            if (!part.sources.front().present) {
                part.origin = SclEvidenceOrigin::RuleCombination; part.rule = "mms-0.1/absent-LN-part";
            }
            parts << part;
        }
        return parts;
    };
    for (const auto& value : ProjectSclMmsReferences(s,row,nullptr)) {
        tick();
        if (value.kind != SclReferenceValueKind::MmsItem && value.kind != SclReferenceValueKind::MmsDomain
            && value.kind != SclReferenceValueKind::ObjectReference && value.kind != SclReferenceValueKind::FunctionalConstraint) continue;
        SclEvidenceValue v; v.kind = value.kind; v.value = value.value;
        v.rule = out.mmsRuleVersion;
        if (value.kind == SclReferenceValueKind::MmsDomain) {
            if (!attr(ld,"ldName").isEmpty()) {
                v.origin = SclEvidenceOrigin::Declaration; v.parts << sourcePart("LDName",ld,"ldName",v.origin);
                v.formula = "LDevice/@ldName";
            } else {
                v.parts << sourcePart("IEDName",ied,"name",SclEvidenceOrigin::Declaration)
                        << sourcePart("LDInst",ld,"inst",SclEvidenceOrigin::Declaration);
                v.formula = "IED/@name + LDevice/@inst";
            }
        } else if (value.kind == SclReferenceValueKind::FunctionalConstraint) {
            const auto e = row.key.kind == SclMmsRecordKind::Member ? SclEvidenceAt(s,row.key.node,"fc")
                : SclEvidenceAt(s,row.fcEvidence.node,row.fcEvidence.attribute);
            v.origin = SclEvidenceOrigin::Declaration;
            v.formula = row.key.kind == SclMmsRecordKind::Member ? "FCDA/@fc" : "DA/@fc (BDA inherits FC)";
            v.parts << SclEvidencePart{"FC",row.fc,{},v.origin,{e}};
        } else {
            v.formula = value.kind == SclReferenceValueKind::MmsItem ? "LNName$FC$DO[$SDO...][$DA$BDA...]"
                : "LDName/LNName.DO[.SDO...][.DA.BDA...]";
            if (array != NoNode) v.formula += " (array selection / alternate access retained separately)";
            if (value.kind == SclReferenceValueKind::ObjectReference) {
                if (!attr(ld,"ldName").isEmpty()) v.parts << sourcePart("LDName",ld,"ldName",SclEvidenceOrigin::Declaration);
                else v.parts << sourcePart("IEDName",ied,"name",SclEvidenceOrigin::Declaration)
                             << sourcePart("LDInst",ld,"inst",SclEvidenceOrigin::Declaration);
            }
            v.parts << lnParts();
            if (value.kind == SclReferenceValueKind::MmsItem)
                v.parts << SclEvidencePart{"FC",row.fc,"mms-0.1/FC-placement",SclEvidenceOrigin::RuleCombination,
                    {row.key.kind == SclMmsRecordKind::Member ? SclEvidenceAt(s,row.key.node,"fc")
                     : SclEvidenceAt(s,row.fcEvidence.node,row.fcEvidence.attribute)}};
            bool afterArray = false;
            for (const auto declaration : path) {
                auto part = sourcePart(doc.nodes[declaration].name,declaration,"name",SclEvidenceOrigin::TypeExpansion);
                // The component following an indexed array belongs to alternate
                // access; it is not a suffix of the MMS Item copied by Task8.
                part.contributes = (out.member.node == NoNode || array != NoNode)
                    && !(value.kind == SclReferenceValueKind::MmsItem && afterArray);
                v.parts << part;
                afterArray |= declaration == array;
            }
            if (out.member.node != NoNode) {
                v.parts << sourcePart("FCDA doName",out.member.node,"doName",SclEvidenceOrigin::Declaration)
                        << sourcePart("FCDA daName",out.member.node,"daName",SclEvidenceOrigin::Declaration);
                v.parts[v.parts.size()-1].rule = "mms-0.1/path-separators";
                v.parts[v.parts.size()-2].rule = "mms-0.1/path-separators";
                if (array != NoNode) {
                    v.parts[v.parts.size()-1].contributes = false;
                    v.parts[v.parts.size()-2].contributes = false;
                }
            }
            if (array != NoNode) v.parts << SclEvidencePart{"array",QString::number(arrayIndex),"mms-0.1/alternate-access",
                SclEvidenceOrigin::RuleCombination,{SclEvidenceAt(s,array,"count"),SclEvidenceAt(s,out.arraySelector,"ix"),
                    SclEvidenceAt(s,out.arraySelector,"doName"),SclEvidenceAt(s,out.arraySelector,"daName")},false};
        }
        out.values << v;
    }
    return out;
}

SclMmsReportMatch MatchSclDataSetMember(const SclSnapshot& s, const SclMmsRow& attribute,
                                     const Reference& member, const std::function<void()>& tick)
{
    const auto& doc = *s.analysis.document;
    if (member.state != LinkState::Resolved || member.candidates.size() != 1 || member.logicalNode != attribute.key.node
        || doc.attribute(member.source,"fc") != attribute.fc) return SclMmsReportMatch::None;
    const auto trace = s.analysis.fcdaTraces.constFind(member.source);
    if (trace == s.analysis.fcdaTraces.cend()) return SclMmsReportMatch::None;
    int length = 0; bool hasAttribute = false; NodeId axis = NoNode;
    for (const auto& step : trace->steps) {
        tick();
        if (step.field == FcdaField::Ix) axis = step.node;
        if (step.field != FcdaField::DoName && step.field != FcdaField::DaName) continue;
        if (length >= attribute.templatePath.size() || step.node != attribute.templatePath[length++]) return SclMmsReportMatch::None;
        hasAttribute |= step.field == FcdaField::DaName;
    }
    if (length == 0 || (member.arrayIndex >= 0 && (axis == NoNode || axis != attribute.key.arrayDeclaration
        || member.arrayIndex != attribute.key.arrayIndex))) return SclMmsReportMatch::None;
    if (!hasAttribute) return SclMmsReportMatch::ObjectCoverage;
    return length == attribute.templatePath.size() && member.arrayIndex == attribute.key.arrayIndex
        ? SclMmsReportMatch::ExactAttribute : SclMmsReportMatch::AttributeCoverage;
}

bool CouldSclDataSetMemberAffect(const SclSnapshot& s, const SclMmsRow& attribute,
                               const Reference& member, const std::function<void()>& tick)
{
    const auto& doc = *s.analysis.document;
    const auto attr = [&](NodeId n, const char* a) { return doc.attribute(n,QString::fromLatin1(a)); };
    const auto fc = attr(member.source,"fc");
    if (!fc.isEmpty() && fc != attribute.fc) return false;
    const auto ln = attribute.key.node;
    const auto ld = Ancestor(doc,ln,"LDevice",tick);
    if (Ancestor(doc,member.source,"Server",tick) != attribute.addressScope.server) return false;
    auto ldInst = attr(member.source,"ldInst");
    if (ldInst.isEmpty()) ldInst = attr(Ancestor(doc,member.source,"LDevice",tick),"inst");
    if (!ldInst.isEmpty() && ldInst != attr(ld,"inst")) return false;
    for (const auto names : {std::pair{"lnClass","lnClass"}, std::pair{"lnInst","inst"}}) {
        const auto raw = attr(member.source,names.first);
        if (!raw.isEmpty() && raw != attr(ln,names.second)) return false;
    }
    if (attr(member.source,"prefix") != attr(ln,"prefix")) return false;
    const auto trace = s.analysis.fcdaTraces.constFind(member.source);
    int length = 0;
    if (trace != s.analysis.fcdaTraces.cend()) for (const auto& step : trace->steps) {
        tick();
        if (step.field == FcdaField::LogicalNode && step.node != ln) return false;
        if (step.field != FcdaField::DoName && step.field != FcdaField::DaName) continue;
        if (length >= attribute.templatePath.size() || step.node != attribute.templatePath[length++]) return false;
    }
    // A well-formed, different first DO name proves a disjoint branch even when
    // resolution stopped before recording a member. Other malformed forms stay uncertain.
    static const QRegularExpression name(QStringLiteral("^([A-Za-z0-9_]+)(?:\\([0-9]+\\))?$"));
    const auto first = name.match(attr(member.source,"doName").section('.',0,0));
    if (first.hasMatch() && !attribute.templatePath.isEmpty()
        && first.captured(1) != attr(attribute.templatePath.front(),"name")) return false;
    return true;
}
}
