#include "SclMmsQuery.hpp"
#include "SclAnalysisService.hpp"
#include <QtConcurrentRun>
#include <QSet>
#include <QRegularExpression>
#include <algorithm>

namespace iec61850sim {
using namespace Scl;

SclMmsIndex SclMmsIndex::Build(const MmsProjection& p, const ReferenceAnalysis& a, TaskControl* task)
{
    SclMmsIndex index;
    for (int i = 0; i < int(p.reports.size()); ++i) {
        checkpoint(task);
        index.reports.insert(p.reports[i].node, i);
        const auto ds = p.reports[i].dataSet.target();
        if (ds != NoNode) index.reportsByDataSet[ds].push_back(i);
    }
    for (int i = 0; i < int(p.instances.size()); ++i) {
        checkpoint(task);
        index.instancesByReport[p.instances[i].definition].push_back(i);
    }
    int reverseEntries = 0;
    for (int i = 0; i < int(p.clients.size()); ++i) {
        checkpoint(task);
        const auto& client = p.clients[i];
        if (client.report != NoNode) index.clientsByReport[client.report].push_back(i);
        // Keep all candidates and their original resolution state.
        QSet<NodeId> targets;
        for (auto target : client.target.candidates) {
            int depth = 0;
            while (target != NoNode && depth++ < 256) {
                checkpoint(task);
                if (!targets.contains(target)) {
                    targets.insert(target);
                    if (reverseEntries++ < 500000) index.clientsByTarget[target].push_back(i);
                    else index.limited = true;
                }
                target = a.document->nodes[target].parent;
            }
            if (target != NoNode) index.limited = true;
        }
        if (client.kind == MmsClientKind::ReportExtRef)
            for (const auto report : client.target.candidates) {
                checkpoint(task);
                index.clientsByReport[report].push_back(i);
            }
    }
    constexpr int memberLimit = 500000;
    for (int i = 0; i < int(a.references.size()); ++i) {
        checkpoint(task);
        if (a.references[i].kind != LinkKind::Fcda) continue;
        if (index.memberReferences.size() >= memberLimit) { index.limited = true; break; }
        index.memberReferences.insert(a.references[i].source, i);
    }
    return index;
}

namespace {
struct BudgetEnd {};
class Query {
public:
    Query(const SclSnapshot& s, SclMmsRequest r, TaskControl* t)
        : s(s), doc(*s.analysis.document), p(s.mms), r(std::move(r)), task(t) {
        this->r.pageSize = std::clamp(this->r.pageSize, 1, 200);
        this->r.offset = std::max(0, this->r.offset);
        this->r.workLimit = std::clamp(this->r.workLimit, 1, 100000);
        this->r.graphLimit = std::clamp(this->r.graphLimit, 1, 128);
        out.sha256 = s.analysis.sha256;
        out.document = s.analysis.document; out.source = s.analysis.source;
        out.request = this->r;
    }
    SclMmsResult Run();
private:
    const SclSnapshot& s;
    const Document& doc;
    const MmsProjection& p;
    SclMmsRequest r;
    TaskControl* task;
    SclMmsResult out;
    int work = 0;
    void Tick() { checkpoint(task); if (++work > r.workLimit) throw BudgetEnd{}; }
    bool Valid(NodeId n) const { return n >= 0 && n < int(doc.nodes.size()); }
    QString Attr(NodeId n, const char* key) const {
        return Valid(n) ? doc.attribute(n, QString::fromLatin1(key)) : QString{};
    }
    NodeId Ancestor(NodeId n, const char* kind) {
        while (Valid(n)) {
            Tick();
            if (doc.nodes[n].isScl() && doc.nodes[n].name == QLatin1String(kind)) return n;
            n = doc.nodes[n].parent;
        }
        return NoNode;
    }
    bool InScope(NodeId n);
    bool ReportAllowed(NodeId n) { return s.mmsIndex.reports.contains(n) && InScope(n); }
    bool Add(SclMmsRow row);
    SclMmsRow Report(int i);
    SclMmsRow Client(int i);
    SclMmsRow Instance(int i);
    SclMmsRow Signal(int i, bool describe = false);
    SclMmsAddressShape Shape(const Reference&);
    void Members(NodeId report);
    void Addresses(int signal);
    void ReverseSignal(int signal);
    void Graph(NodeId report);
    bool Matches(const SclMmsRow& row) const;
};

bool Query::InScope(NodeId n)
{
    if (!Valid(n)) return false;
    if (r.scope.ied != NoNode && s.browse.iedOwners[n] != r.scope.ied) return false;
    if (r.scope.accessPoint == NoNode && r.scope.server == NoNode) return true;
    const auto server = Ancestor(n, "Server");
    if (r.scope.server != NoNode && server != r.scope.server) return false;
    if (r.scope.accessPoint != NoNode) {
        if (Ancestor(n, "AccessPoint") == r.scope.accessPoint) return true;
        // ServerAt aliases are access paths, not another copy of the model.
        for (const auto& access : p.servers) {
            Tick();
            if (access.accessPoint == r.scope.accessPoint && access.model.target() == server && server != NoNode)
                return true;
        }
        return false;
    }
    return true;
}

bool Query::Matches(const SclMmsRow& row) const
{
    if (!r.fc.isEmpty() && row.fc != r.fc) return false;
    if (r.text.isEmpty()) return true;
    for (const auto& cell : row.cells)
        if (cell.contains(r.text, Qt::CaseInsensitive)) return true;
    return row.domain.contains(r.text, Qt::CaseInsensitive) || row.item.contains(r.text, Qt::CaseInsensitive)
        || (!row.domain.isEmpty() && (row.domain + '/' + row.item).contains(r.text, Qt::CaseInsensitive));
}

bool Query::Add(SclMmsRow row)
{
    Tick();
    if (!Matches(row)) return false;
    const auto pos = out.total++;
    if (pos >= r.offset && int(out.rows.size()) < r.pageSize) out.rows.push_back(std::move(row));
    return true;
}

SclMmsRow Query::Report(int i)
{
    const auto& report = p.reports[i];
    SclMmsRow row;
    row.key = {SclMmsRecordKind::Report, report.node, i};
    row.report = report.node; row.dataSet = report.dataSet.target(); row.state = report.dataSet.state;
    row.domain = report.mmsDomain; row.item = report.definitionMmsItem;
    row.cells = {report.fields.value("name").raw, report.fields.value("rptID").raw,
        report.definitionReference, report.dataSetReference, report.dataSetMmsName,
        report.instanceLimitation, Attr(report.ied, "name"), report.fields.value("desc").raw,
        report.fields.value("datSet").raw};
    row.limitation = report.instanceLimitation;
    row.evidence = {{report.node, {}, "mms-0.1"}};
    return row;
}

SclMmsRow Query::Instance(int i)
{
    const auto& instance = p.instances[i];
    SclMmsRow row;
    row.key = {SclMmsRecordKind::Instance, instance.definition, i, instance.ordinal};
    row.report = instance.definition;
    row.domain = instance.mmsDomain; row.item = instance.mmsItem;
    row.cells = {instance.name, instance.reference, instance.rptId.raw};
    row.evidence = instance.evidence; row.state = LinkState::Resolved;
    row.limitation = QStringLiteral("按文件推导的预期实例；不是设备在线实例。");
    return row;
}

SclMmsRow Query::Client(int i)
{
    const auto& client = p.clients[i];
    SclMmsRow row;
    row.key = {SclMmsRecordKind::Client, client.node, i, client.ordinal};
    row.report = client.report;
    if (client.kind == MmsClientKind::ReportExtRef && row.report == NoNode) row.report = client.target.target();
    row.state = client.target.state;
    row.cells = {client.kind == MmsClientKind::ClientLn ? QStringLiteral("ClientLN") :
        client.kind == MmsClientKind::Association ? QStringLiteral("Association") : QStringLiteral("Report ExtRef")};
    for (auto it = client.fields.cbegin(); it != client.fields.cend(); ++it) {
        Tick(); row.cells << it.key() + '=' + it.value().raw;
    }
    row.cells << client.target.detail << client.allocationLimitation;
    row.limitation = QStringLiteral("文件声明；不表示已订阅或在线接收。") + client.allocationLimitation;
    row.evidence = {{client.node, {}, "mms-0.1"}};
    for (auto node : client.target.candidates) {
        Tick(); row.evidence.push_back({node, {}, "mms-0.1"});
    }
    return row;
}

SclMmsRow Query::Signal(int i, bool describe)
{
    const auto& signal = s.tasks.catalog.instances[i];
    const auto& def = s.tasks.catalog.definitions[signal.definition];
    SclMmsRow row;
    row.key = {SclMmsRecordKind::Signal, signal.logicalNode, i, 0, def.path};
    row.cells = {Attr(Ancestor(signal.logicalNode, "IED"), "name"),
        Attr(signal.logicalNode, "prefix") + Attr(signal.logicalNode, "lnClass") + Attr(signal.logicalNode, "inst"),
        def.path, def.cdc};
    row.evidence = {{signal.logicalNode, {}, {}}, {def.declaration, {}, {}}};
    if (i < int(s.tasks.signalDescriptionNodes.size())) {
        const auto source = s.tasks.signalDescriptionNodes[i];
        if (Valid(source)) {
            row.cells << (doc.nodes[source].name == "Val" ? doc.nodes[source].text : Attr(source, "desc"));
            row.evidence.push_back({source, doc.nodes[source].name == "Val" ? QString() : QStringLiteral("desc"), {}});
        }
    }
    row.shape = def.hasUnexpandedArray ? SclMmsAddressShape::ArrayTemplate : SclMmsAddressShape::Structure;
    if (describe) row.description = DescribeSclSignal(s.analysis, signal.logicalNode, def.path, task);
    return row;
}

SclMmsAddressShape Query::Shape(const Reference& ref)
{
    if (ref.arrayIndex >= 0) return SclMmsAddressShape::AlternateAccess;
    if (Attr(ref.source, "daName").isEmpty()) return SclMmsAddressShape::ObjectMember;
    const auto trace = s.analysis.fcdaTraces.constFind(ref.source);
    if (trace != s.analysis.fcdaTraces.cend()) for (const auto& step : trace->steps) {
        Tick(); if (!Attr(step.node, "count").isNull()) return SclMmsAddressShape::ArrayTemplate;
    }
    if (ref.state != LinkState::Resolved || ref.candidates.size() != 1) return SclMmsAddressShape::None;
    return Attr(ref.candidates.front(), "bType") == "Struct" ? SclMmsAddressShape::Structure : SclMmsAddressShape::Scalar;
}

void Query::Members(NodeId report)
{
    if (!ReportAllowed(report)) return;
    const auto& definition = p.reports[s.mmsIndex.reports.value(report)];
    const auto ds = definition.dataSet.target();
    const auto members = s.browse.dataSetMembers.value(ds);
    for (int i = 0; i < members.count; ++i) {
        Tick();
        const auto member = s.browse.memberRows[members.first + i];
        SclMmsRow row;
        row.key = {SclMmsRecordKind::Member, member, -1, i + 1};
        row.report = report; row.dataSet = ds;
        row.fc = Attr(member, "fc");
        row.cells = {QString::number(i + 1), Attr(member, "doName"), Attr(member, "daName"), row.fc};
        const int refIndex = s.mmsIndex.memberReferences.value(member, -1);
        if (refIndex >= 0) {
            const auto& ref = s.analysis.references[refIndex];
            row.key.record = refIndex; row.state = ref.state;
            row.domain = ref.mmsDomain; row.item = ref.mmsItem;
            row.arrayIndex = ref.arrayIndex; row.component = ref.mmsComponent;
            row.limitation = ref.detail;
            row.shape = Shape(ref);
        }
        row.evidence = {{member, {}, {}}};
        row.cells << row.domain << row.item << row.component << row.limitation;
        // Resolve descriptions only for the page delivered, never every member.
        if (Matches(row) && out.total >= r.offset && int(out.rows.size()) < r.pageSize)
            row.description = DescribeSclMember(s.analysis, member, task);
        Add(std::move(row));
    }
}

void Query::Addresses(int i)
{
    if (i < 0 || i >= int(s.tasks.catalog.instances.size())) return;
    const auto& signal = s.tasks.catalog.instances[i];
    if (!InScope(signal.logicalNode)) return;
    const auto& def = s.tasks.catalog.definitions[signal.definition];
    auto base = Signal(i);
    const auto ld = Ancestor(signal.logicalNode, "LDevice");
    QString domain;
    for (const auto& server : p.servers) {
        Tick();
        for (const auto& d : server.domains) {
            Tick();
            if (d.device == ld && d.state == MmsValueState::Derived) domain = d.name;
        }
    }
    const auto ln = doc.nodes[signal.logicalNode].name == "LN0" ? QStringLiteral("LLN0") :
        Attr(signal.logicalNode, "prefix") + Attr(signal.logicalNode, "lnClass") + Attr(signal.logicalNode, "inst");
    const auto memberType = [&](NodeId declaration, LinkKind kind = LinkKind::MemberType) {
        const auto refs = s.analysis.outgoing.equal_range(declaration);
        NodeId target = NoNode;
        for (auto it = refs.first; it != refs.second; ++it) {
            Tick(); const auto& ref = s.analysis.references[it.value()];
            if (ref.kind != kind) continue;
            if (ref.state != LinkState::Resolved || ref.candidates.size() != 1 || target != NoNode) return NoNode;
            target = ref.candidates.front();
        }
        return target;
    };
    static const QRegularExpression addressPart(QStringLiteral("^[A-Za-z0-9_]+$"));
    const auto validPart = [&](const QString& part) { return addressPart.match(part).hasMatch(); };
    // Reuse the resolver's unique type links and walk only this selected
    // template path. Duplicated LN/DO names must not become scalar addresses.
    bool uniquePath = Valid(ld) && validPart(ln);
    int logicalNames = 0;
    if (Valid(ld)) for (auto n = doc.nodes[ld].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
        Tick();
        if (!doc.nodes[n].isScl() || (doc.nodes[n].name != "LN" && doc.nodes[n].name != "LN0")) continue;
        const auto name = doc.nodes[n].name == "LN0" ? QStringLiteral("LLN0") : Attr(n, "prefix") + Attr(n, "lnClass") + Attr(n, "inst");
        logicalNames += name == ln;
    }
    uniquePath &= logicalNames == 1 && (doc.nodes[signal.logicalNode].name == "LN0" ?
        Attr(signal.logicalNode, "lnClass") == "LLN0" && Attr(signal.logicalNode, "inst").isEmpty() :
        !Attr(signal.logicalNode, "lnClass").isEmpty() && !Attr(signal.logicalNode, "inst").isNull());
    auto objectType = memberType(signal.logicalNode, LinkKind::LogicalNodeType);
    const auto objectParts = def.path.split('.');
    QList<MmsEvidence> objectEvidence;
    for (int part = 0; part < objectParts.size() && objectType != NoNode; ++part) {
        Tick(); NodeId declaration = NoNode; int count = 0;
        for (auto n = doc.nodes[objectType].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
            Tick();
            if (doc.nodes[n].isScl() && doc.nodes[n].name == (part ? "SDO" : "DO") && Attr(n, "name") == objectParts[part]) {
                declaration = n; ++count;
            }
        }
        uniquePath &= count == 1 && validPart(objectParts[part]);
        objectType = count == 1 ? memberType(declaration) : NoNode;
        if (count == 1) objectEvidence.push_back({declaration, {}, {}});
    }
    uniquePath &= objectType == def.type && objectType != NoNode;
    base.evidence.append(objectEvidence);
    if (def.type == NoNode || !uniquePath) {
        auto row = base; row.key.kind = SclMmsRecordKind::Address;
        row.state = LinkState::Blocked; row.limitation = QStringLiteral("类型或逻辑节点地址未唯一解析。");
        Add(std::move(row)); return;
    }
    base.description = DescribeSclSignal(s.analysis, signal.logicalNode, def.path, task);
    QSet<NodeId> ancestry;
    QList<NodeId> declarations;
    const auto expand = [&](auto&& self, NodeId type, const QString& prefix, const QString& parentFc,
                            bool array, int depth) -> void {
        Tick();
        if (depth >= 32 || ancestry.contains(type)) {
            out.limited = true; out.limitation = QStringLiteral("属性类型深度/环预算受限。"); return;
        }
        ancestry.insert(type);
        QHash<QString, int> names;
        for (auto c = doc.nodes[type].firstChild; c != NoNode; c = doc.nodes[c].nextSibling) {
            Tick();
            if (doc.nodes[c].isScl() && doc.nodes[c].name == (depth ? "BDA" : "DA"))
                ++names[Attr(c, "name") + (depth ? QString() : '$' + Attr(c, "fc"))];
        }
        for (auto c = doc.nodes[type].firstChild; c != NoNode; c = doc.nodes[c].nextSibling) {
            Tick();
            if (!doc.nodes[c].isScl() || doc.nodes[c].name != (depth ? "BDA" : "DA")) continue;
            const auto name = Attr(c, "name");
            const auto path = prefix + name;
            const auto fc = depth ? parentFc : Attr(c, "fc");
            const auto bt = Attr(c, "bType");
            const auto nameKey = name + (depth ? QString() : '$' + fc);
            static const QSet<QString> basicTypes = {"BOOLEAN", "INT8", "INT16", "INT24", "INT32", "INT64", "INT128",
                "INT8U", "INT16U", "INT24U", "INT32U", "FLOAT32", "FLOAT64", "Enum", "Dbpos", "Tcmd", "Quality",
                "Timestamp", "VisString32", "VisString64", "VisString65", "VisString129", "VisString255", "Octet64",
                "Unicode255", "Struct", "EntryID", "Check", "Octet6", "Octet8", "Currency", "PhyComAddr", "ObjRef",
                "TrgOps", "OptFlds", "SvOptFlds", "LogOptFlds"};
            // Any count declaration remains a template until alternate access
            // is explicitly resolved. No implicit element 0 or scalar address.
            const bool isArray = array || !Attr(c, "count").isNull();
            auto row = base;
            row.key.kind = SclMmsRecordKind::Address; row.key.field = path;
            row.fc = fc; row.domain = domain;
            row.shape = isArray ? SclMmsAddressShape::ArrayTemplate :
                bt == "Struct" ? SclMmsAddressShape::Structure : SclMmsAddressShape::Scalar;
            static const QSet<QString> fcs = {"ST","MX","SP","SV","CF","DC","SG","SE","SR","OR","BL","EX","CO","US","MS","RP","BR","LG","GO","GS"};
            row.state = !domain.isEmpty() && validPart(name) && names.value(nameKey) == 1 && fcs.contains(fc)
                && basicTypes.contains(bt) ? LinkState::Resolved : LinkState::Blocked;
            if (!basicTypes.contains(bt)) row.shape = SclMmsAddressShape::None;
            if (names.value(nameKey) > 1) row.state = LinkState::Ambiguous;
            const auto nested = bt == "Struct" ? memberType(c) : NoNode;
            if (bt == "Struct" && nested == NoNode) row.state = LinkState::Blocked;
            if (bt == "Enum" && memberType(c) == NoNode) row.state = LinkState::Blocked;
            if (isArray) {
                row.state = LinkState::Partial;
                row.limitation = QStringLiteral("数组模板未选择元素；不生成标量 Item。");
            } else if (row.state == LinkState::Resolved) {
                auto object = def.path; object.replace('.', '$');
                auto attribute = path; attribute.replace('.', '$');
                row.item = ln + '$' + fc + '$' + object + '$' + attribute;
            } else row.limitation = QStringLiteral("名称、FC 或类型未唯一解析；不生成确定地址。");
            row.evidence.push_back({c, {}, {}});
            row.cells << path << fc << bt << Attr(c, "count") << row.domain << row.item << row.limitation;
            declarations.push_back(c);
            if (Matches(row) && out.total >= r.offset && int(out.rows.size()) < r.pageSize)
                row.description = DescribeSclAttribute(s.analysis, *base.description, path, fc, declarations, task);
            Add(std::move(row));
            if (nested != NoNode && names.value(nameKey) == 1 && validPart(name) && path.size() < 2048)
                self(self, nested, path + '.', fc, isArray, depth + 1);
            else if (nested != NoNode && path.size() >= 2048) {
                out.limited = true; out.limitation = QStringLiteral("属性路径长度受限。");
            }
            declarations.pop_back();
        }
        ancestry.remove(type);
    };
    expand(expand, def.type, {}, {}, def.hasUnexpandedArray, 0);
}

void Query::ReverseSignal(int i)
{
    if (i < 0 || i >= int(s.tasks.catalog.instances.size())) return;
    const auto& signal = s.tasks.catalog.instances[i];
    if (!InScope(signal.logicalNode)) return;
    const auto& definition = s.tasks.catalog.definitions[signal.definition];
    const auto uses = s.tasks.usages.find({signal.logicalNode, definition.path});
    if (uses == s.tasks.usages.end()) return;
    for (const auto& use : uses->second) {
        Tick();
        for (auto report : s.mmsIndex.reportsByDataSet.value(use.dataSet)) {
            Tick(); auto row = Report(report);
            const auto& ref = s.analysis.references[use.reference];
            row.key.kind = SclMmsRecordKind::Member; row.key.node = ref.source;
            row.key.record = use.reference; row.key.ordinal = ref.memberOrdinal;
            row.evidence.push_back({ref.source, {}, {}});
            row.shape = Shape(ref);
            row.arrayIndex = ref.arrayIndex; row.component = ref.mmsComponent;
            row.domain = ref.mmsDomain; row.item = ref.mmsItem; row.fc = Attr(ref.source, "fc");
            row.cells << Attr(ref.source, "daName") << Attr(ref.source, "fc") << QString::number(ref.memberOrdinal);
            row.limitation = QStringLiteral("已解析成员出现；不表示某客户端订阅或接收。");
            Add(std::move(row));
        }
    }
    if (s.tasks.relationsLimited) { out.limited = true; out.limitation = QStringLiteral("共享成员反向索引受限。"); }
}

void Query::Graph(NodeId report)
{
    if (!ReportAllowed(report)) return;
    const auto node = [&](SclMmsRow row) {
        Tick(); ++out.total;
        if (int(out.rows.size()) >= r.graphLimit) { out.limited = true; return -1; }
        out.rows.push_back(std::move(row)); return int(out.rows.size()) - 1;
    };
    const auto edge = [&](int from, int to, SclMmsEdgeKind kind, LinkState state) {
        if (from >= 0 && to >= 0 && out.edges.size() < 256) out.edges.push_back({from, to, kind, state});
    };
    node(Report(s.mmsIndex.reports.value(report)));
    const auto& definition = p.reports[s.mmsIndex.reports.value(report)];
    int dataSetNode = -1;
    if (definition.dataSet.target() != NoNode) {
        SclMmsRow ds;
        ds.key = {SclMmsRecordKind::DataSet, definition.dataSet.target()};
        ds.report = report; ds.dataSet = definition.dataSet.target();
        ds.cells = {definition.dataSetReference, definition.dataSetMmsName};
        ds.evidence = {{ds.dataSet, {}, {}}};
        dataSetNode = node(std::move(ds));
        edge(0, dataSetNode, SclMmsEdgeKind::UsesDataSet, definition.dataSet.state);
    }
    for (const auto i : s.mmsIndex.instancesByReport.value(report)) {
        Tick(); edge(0, node(Instance(i)), SclMmsEdgeKind::ExpectedInstance, LinkState::Resolved);
    }
    for (const auto i : s.mmsIndex.clientsByReport.value(report)) {
        Tick(); const auto& client = p.clients[i];
        const auto clientNode = node(Client(i));
        if (client.kind == MmsClientKind::ReportExtRef)
            edge(clientNode, 0, SclMmsEdgeKind::SourceReport, client.target.state);
        else edge(0, clientNode, SclMmsEdgeKind::DeclaredClient, client.target.state);
    }
    const auto members = s.browse.dataSetMembers.value(definition.dataSet.target());
    for (int i = 0; i < members.count; ++i) {
        Tick(); const auto member = s.browse.memberRows[members.first + i];
        const auto refIndex = s.mmsIndex.memberReferences.value(member, -1);
        SclMmsRow row; row.key = {SclMmsRecordKind::Member, member, refIndex, i + 1};
        row.report = report; row.dataSet = definition.dataSet.target();
        row.cells = {Attr(member, "doName"), Attr(member, "daName"), Attr(member, "fc")};
        row.evidence = {{member, {}, {}}};
        if (refIndex >= 0) { row.state = s.analysis.references[refIndex].state; row.shape = Shape(s.analysis.references[refIndex]); }
        const auto memberNode = node(std::move(row));
        edge(dataSetNode, memberNode, SclMmsEdgeKind::DataSetMember, LinkState::Resolved);
        if (refIndex >= 0) {
            const auto& ref = s.analysis.references[refIndex];
            SclMmsRow object; object.key = {SclMmsRecordKind::Reference, ref.logicalNode == NoNode ? ref.source : ref.logicalNode, refIndex, 0, ref.declared};
            object.state = ref.state; object.domain = ref.mmsDomain; object.item = ref.mmsItem;
            object.shape = Shape(ref);
            object.arrayIndex = ref.arrayIndex; object.component = ref.mmsComponent;
            object.cells = {ref.objectReference, ref.detail};
            object.evidence = {{member, {}, {}}};
            for (const auto candidate : ref.candidates) { Tick(); object.evidence.push_back({candidate, {}, {}}); }
            edge(memberNode, node(std::move(object)), SclMmsEdgeKind::MemberObject, ref.state);
        }
    }
    if (out.total > int(out.rows.size())) out.limitation += QStringLiteral(" 局部图节点上限 %1；总量为已观察下界。").arg(r.graphLimit);
}

SclMmsResult Query::Run()
{
    checkpoint(task);
    if (r.kind != SclMmsQueryKind::Findings && (!p.complete || p.document != s.analysis.document || p.sha256 != s.analysis.sha256)) {
        out.limited = true; out.limitation = p.limitation.isEmpty() ? QStringLiteral("MMS 投影不可用或快照不一致。") : p.limitation;
        return out;
    }
    if (p.resourceLimited) {
        out.limited = true;
        out.limitation = p.limitation.isEmpty() ? QStringLiteral("MMS 投影存在资源受限分支，详见该分支证据。") : p.limitation;
    }
    try {
        switch (r.kind) {
        case SclMmsQueryKind::Detail: return DescribeSclMms(s, r, task);
        case SclMmsQueryKind::Findings: {
            const auto append = [&](const std::vector<Finding>& findings, int source) {
                for (int i = 0; i < int(findings.size()); ++i) {
                    Tick(); const auto& finding = findings[i];
                    if (finding.node != NoNode && !InScope(finding.node)) continue;
                    SclMmsRow row; row.key = {SclMmsRecordKind::Finding, finding.node, i, source};
                    row.cells = {finding.rule, finding.detail, finding.objectPath, finding.suggestion};
                    row.evidence = {{finding.node, {}, finding.rule}}; Add(std::move(row));
                }
            };
            append(s.analysis.findings, 0); append(p.findings, 1); break;
        }
        case SclMmsQueryKind::Members: Members(r.report); break;
        case SclMmsQueryKind::Addresses: Addresses(r.signal); break;
        case SclMmsQueryKind::ReverseSignal: ReverseSignal(r.signal); break;
        case SclMmsQueryKind::Graph: Graph(r.report); break;
        case SclMmsQueryKind::Instances:
            if (ReportAllowed(r.report)) for (auto i : s.mmsIndex.instancesByReport.value(r.report)) { Tick(); Add(Instance(i)); }
            break;
        case SclMmsQueryKind::Reports:
        case SclMmsQueryKind::Search:
            for (int i = 0; i < int(p.reports.size()); ++i) { Tick(); if (InScope(p.reports[i].node)) Add(Report(i)); }
            if (r.kind != SclMmsQueryKind::Search) break;
            for (int i = 0; i < int(p.instances.size()); ++i) { Tick(); if (InScope(p.instances[i].definition)) Add(Instance(i)); }
            for (int i = 0; i < int(p.clients.size()); ++i) { Tick(); if (InScope(p.clients[i].node)) Add(Client(i)); }
            for (const auto member : s.browse.memberRows) {
                Tick(); if (!InScope(member)) continue;
                const auto i = s.mmsIndex.memberReferences.value(member, -1);
                if (i < 0) continue;
                const auto& ref = s.analysis.references[i];
                SclMmsRow row; row.key = {SclMmsRecordKind::Member, member, i, ref.memberOrdinal};
                row.dataSet = doc.nodes[member].parent; row.state = ref.state;
                row.domain = ref.mmsDomain; row.item = ref.mmsItem; row.fc = Attr(member, "fc");
                row.arrayIndex = ref.arrayIndex; row.component = ref.mmsComponent; row.shape = Shape(ref);
                row.cells = {ref.declared, ref.objectReference, ref.mmsComponent, Attr(member, "desc")};
                row.evidence = {{member, {}, {}}}; Add(std::move(row));
            }
            // Derived signal Items do not exist in XML. Search them by bounded
            // on-demand expansion; an exhausted scan is visibly incomplete.
            if (r.text.contains('$') || r.text.contains('/')) {
                if (r.scope.ied != NoNode) {
                    for (const auto i : s.tasks.signalsByIed.value(r.scope.ied)) { Tick(); Addresses(i); }
                } else for (int i = 0; i < int(s.tasks.catalog.instances.size()); ++i) { Tick(); Addresses(i); }
            }
            [[fallthrough]];
        case SclMmsQueryKind::Signals:
        {
            const auto member = s.mmsIndex.memberReferences.value(r.target, -1);
            const auto acceptSignal = [&](int i) {
                if (!InScope(s.tasks.catalog.instances[i].logicalNode)) return;
                if (r.kind == SclMmsQueryKind::Signals && r.target != NoNode) {
                    if (member < 0) return;
                    const auto& ref = s.analysis.references[member];
                    const auto& instance = s.tasks.catalog.instances[i];
                    if (instance.logicalNode != ref.logicalNode || s.tasks.catalog.definitions[instance.definition].path != Attr(ref.source, "doName")) return;
                }
                Add(Signal(i));
            };
            if (r.scope.ied != NoNode) {
                for (const auto i : s.tasks.signalsByIed.value(r.scope.ied)) {
                    Tick(); acceptSignal(i);
                }
            } else for (int i = 0; i < int(s.tasks.catalog.instances.size()); ++i) {
                Tick(); acceptSignal(i);
            }
            if (s.tasks.catalog.limited) { out.limited = true; out.limitation = QStringLiteral("共享信号目录受限。"); }
            break;
        }
        case SclMmsQueryKind::Clients:
        case SclMmsQueryKind::ReverseClients: {
            std::vector<int> candidates;
            if (r.kind == SclMmsQueryKind::ReverseClients) {
                if (InScope(r.target)) candidates = s.mmsIndex.clientsByTarget.value(r.target);
            } else if (r.report != NoNode) {
                if (ReportAllowed(r.report)) candidates = s.mmsIndex.clientsByReport.value(r.report);
            } else {
                for (int i = 0; i < int(p.clients.size()); ++i) { Tick(); if (InScope(p.clients[i].node)) candidates.push_back(i); }
            }
            for (const auto i : candidates) { Tick(); Add(Client(i)); }
            break;
        }
        case SclMmsQueryKind::Servers:
            for (int i = 0; i < int(p.servers.size()); ++i) {
                Tick(); const auto& server = p.servers[i];
                if (r.scope.ied != NoNode && r.scope.ied != server.ied) continue;
                if (r.scope.accessPoint != NoNode && r.scope.accessPoint != server.accessPoint) continue;
                if (r.scope.server != NoNode && r.scope.server != server.model.target()) continue;
                SclMmsRow row; row.key = {SclMmsRecordKind::Server, server.accessPoint, i};
                row.state = server.model.state; row.cells = {Attr(server.ied, "name"), Attr(server.accessPoint, "name"), server.model.detail};
                for (const auto d : server.domains) { Tick(); row.cells << d.name; }
                for (const auto n : server.path) { Tick(); row.evidence.push_back({n, {}, {}}); }
                Add(std::move(row));
            }
            break;
        case SclMmsQueryKind::Endpoints:
            for (int i = 0; i < int(p.endpoints.size()); ++i) {
                Tick(); const auto& endpoint = p.endpoints[i];
                if (r.scope.server != NoNode && r.scope.server != endpoint.server) continue;
                if (r.scope.accessPoint != NoNode && r.scope.accessPoint != endpoint.accessPoint.target()) continue;
                if (r.scope.ied != NoNode && (endpoint.accessPoint.target() == NoNode ||
                    Ancestor(endpoint.accessPoint.target(), "IED") != r.scope.ied)) continue;
                SclMmsRow row; row.key = {SclMmsRecordKind::Endpoint, endpoint.node, i}; row.state = endpoint.accessPoint.state;
                row.cells = {Attr(endpoint.node, "iedName"), Attr(endpoint.node, "apName")};
                row.evidence = {{endpoint.node, {}, {}}};
                for (const auto& address : endpoint.addresses) for (const auto& group : address.parameters)
                    for (auto it = group.fields.cbegin(); it != group.fields.cend(); ++it) {
                        Tick(); row.cells << it.key() + '=' + it.value().raw;
                        row.evidence.push_back(it.value().evidence);
                    }
                Add(std::move(row));
            }
            break;
        case SclMmsQueryKind::Capabilities:
            for (int i = 0; i < int(p.capabilities.size()); ++i) {
                Tick(); const auto& cap = p.capabilities[i];
                // IED-level Services remains visible in an AP/Server scope;
                // this is declaration visibility, never precedence inference.
                auto scope = r.scope; r.scope.accessPoint = NoNode; r.scope.server = NoNode;
                bool owner = InScope(cap.owner); r.scope = scope;
                if (owner && (r.scope.accessPoint != NoNode || r.scope.server != NoNode)) {
                    owner = false;
                    for (const auto& server : p.servers) {
                        Tick();
                        if (r.scope.ied != NoNode && server.ied != r.scope.ied) continue;
                        if (r.scope.accessPoint != NoNode && server.accessPoint != r.scope.accessPoint) continue;
                        if (r.scope.server != NoNode && server.model.target() != r.scope.server) continue;
                        for (const auto service : server.services) { Tick(); owner |= service == cap.services; }
                    }
                }
                if (!owner) continue;
                if (doc.nodes[cap.owner].name == "AccessPoint" && r.scope.accessPoint != NoNode && cap.owner != r.scope.accessPoint) continue;
                SclMmsRow row; row.key = {SclMmsRecordKind::Capability, cap.services, i};
                row.cells = {doc.nodes[cap.owner].name, cap.limitation}; row.limitation = cap.limitation;
                row.evidence = {{cap.services, {}, {}}};
                for (auto n : cap.declarations) { Tick(); row.cells << doc.nodes[n].name; row.evidence.push_back({n, {}, {}}); }
                Add(std::move(row));
            }
            break;
        }
    } catch (const BudgetEnd&) {
        out.limited = true; out.limitation = QStringLiteral("查询工作预算 %1 已用尽；总量为已观察下界。").arg(r.workLimit);
    }
    if (s.mmsIndex.limited) { out.limited = true; out.limitation += QStringLiteral(" MMS 引用索引资源受限。"); }
    out.hasMore = out.total > qint64(r.offset) + qint64(out.rows.size()) || out.limited;
    checkpoint(task);
    return std::move(out);
}
} // namespace

SclMmsResult QuerySclMms(const SclSnapshot& s, const SclMmsRequest& request, TaskControl* task)
{
    auto result = Query(s, request, task).Run();
    if (request.presentation && request.kind != SclMmsQueryKind::Detail) PresentSclMms(s, result, task);
    return result;
}

SclMmsQuerySession::SclMmsQuerySession(QObject* parent) : QObject(parent) {}
SclMmsQuerySession::~SclMmsQuerySession() { Cancel(); }

void SclMmsQuerySession::Cancel()
{
    ++m_generation;
    if (m_control) m_control->cancelled.store(true, std::memory_order_relaxed);
    m_pending.reset(); m_result.reset();
}
void SclMmsQuerySession::SetContext(std::shared_ptr<const SclSnapshot> snapshot, SclMmsScope scope)
{
    Cancel(); m_snapshot = std::move(snapshot); m_scope = scope;
}
quint64 SclMmsQuerySession::Query(SclMmsRequest request)
{
    Cancel();
    if (!m_snapshot) return m_generation;
    request.scope = m_scope;
    m_pending = std::move(request);
    Start(); return m_generation;
}
void SclMmsQuerySession::Start()
{
    if (m_control || !m_pending || !m_snapshot) return;
    auto snapshot = m_snapshot;
    const auto request = *m_pending;
    const auto generation = m_generation;
    m_pending.reset();
    m_control = std::make_shared<TaskControl>();
    auto control = m_control;
    auto* watcher = new QFutureWatcher<std::shared_ptr<const SclMmsResult>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, snapshot, request, generation, control] {
        m_control.reset();
        auto result = watcher->result(); watcher->deleteLater();
        const bool accepted = !control->cancelled.load(std::memory_order_relaxed) && generation == m_generation &&
            snapshot == m_snapshot && request.scope == m_scope && result;
        if (accepted) m_result = std::move(result);
        Start();
        if (accepted) emit Ready(); // Last operation: receiver may delete this.
    });
    watcher->setFuture(QtConcurrent::run([snapshot, request, generation, control]() -> std::shared_ptr<const SclMmsResult> {
        try {
            auto result = QuerySclMms(*snapshot, request, control.get());
            result.requestId = generation;
            return std::make_shared<const SclMmsResult>(std::move(result));
        } catch (const TaskCancelled&) { return {}; }
        catch (const std::exception&) {
            SclMmsResult result; result.sha256 = snapshot->analysis.sha256;
            result.document = snapshot->analysis.document; result.source = snapshot->analysis.source;
            result.request = request; result.requestId = generation; result.limited = true;
            result.limitation = QStringLiteral("无法完成 MMS 查询。");
            return std::make_shared<const SclMmsResult>(std::move(result));
        }
    }));
}
} // namespace iec61850sim
