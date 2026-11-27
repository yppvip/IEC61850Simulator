#include "SclMmsQuery.hpp"
#include "SclAnalysisService.hpp"
#include <QtConcurrentRun>
#include <QSet>
#include <QRegularExpression>
#include <QElapsedTimer>
#include <algorithm>
#include <tuple>

namespace iec61850sim {
using namespace Scl;

bool IsSclMmsAttributeTarget(const SclSnapshot& s, NodeId id)
{
    const auto& doc = *s.analysis.document;
    if (id < 0 || id >= int(doc.nodes.size()) || !doc.nodes[id].isScl()) return false;
    const auto& node = doc.nodes[id];
    if (node.name == "FCDA") return s.browse.dataSetMembers.contains(node.parent);
    if (node.name == "DO" || node.name == "SDO" || node.name == "DA" || node.name == "BDA") return true;
    return s.browse.iedOwners[id] != NoNode && (node.name == "LN" || node.name == "LN0"
        || node.name == "DOI" || node.name == "SDI" || node.name == "DAI");
}

SclMmsIndex SclMmsIndex::Build(const MmsProjection& p, const ReferenceAnalysis& a, TaskControl* task)
{
    SclMmsIndex index;
    for (const auto& server : p.servers)
        for (const auto& domain : server.domains) {
            checkpoint(task);
            index.domainsByDevice.insert(domain.device, domain);
        }
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
    int dataSetUserEntries = 0;
    for (int i = 0; i < int(a.references.size()); ++i) {
        checkpoint(task);
        const auto& ref = a.references[i];
        if (ref.kind == LinkKind::ControlDataSet) {
            for (const auto target : ref.candidates) {
                checkpoint(task);
                if (dataSetUserEntries++ >= memberLimit) { index.dataSetIndexLimited = true; break; }
                index.dataSetUsers[target].push_back(i);
            }
        }
        if (a.references[i].kind != LinkKind::Fcda) continue;
        if (index.memberReferences.size() >= memberLimit) {
            index.limited = true; index.dataSetIndexLimited = true; continue;
        }
        index.memberReferences.insert(ref.source, i);
        if (ref.state == LinkState::Resolved && ref.candidates.size() == 1 && ref.logicalNode != NoNode)
            index.membersByLogicalNode[ref.logicalNode].push_back(i);
        NodeId logicalNode = ref.logicalNode;
        const auto trace = a.fcdaTraces.constFind(ref.source);
        if (logicalNode == NoNode && trace != a.fcdaTraces.cend()) for (const auto& step : trace->steps) {
            checkpoint(task);
            if (step.field == FcdaField::LogicalNode) logicalNode = step.node;
        }
        if (logicalNode != NoNode) index.dataSetMembersByLogicalNode[logicalNode].push_back(i);
        else {
            auto server = ref.source;
            while (server != NoNode && (!a.document->nodes[server].isScl() || a.document->nodes[server].name != "Server")) {
                checkpoint(task); server = a.document->nodes[server].parent;
            }
            index.uncertainMembersByServer[server].push_back(i);
        }
    }
    return index;
}

namespace {
struct BudgetEnd { QString reason; };
struct ArraySelection {
    NodeId declaration = NoNode;
    int index = -1;
    QList<NodeId> path;
};

struct AttributeTarget {
    NodeId logicalNode = NoNode, declaration = NoNode;
    QList<NodeId> path;
    QStringList instancePath;
    QString fc;
    int indexPart = -1, index = -1;
    bool exact = false;
};

int UnsignedIndex(const QString& value)
{
    static const QRegularExpression digits(QStringLiteral("^[0-9]+$"));
    bool ok = false;
    const int number = value.toInt(&ok);
    return ok && digits.match(value).hasMatch() ? number : -1;
}

SclMmsDirectState DirectState(const SclMmsRow& row)
{
    if (row.state == LinkState::Ambiguous) return SclMmsDirectState::Ambiguous;
    if (row.state == LinkState::Missing) return SclMmsDirectState::Missing;
    if (row.state == LinkState::Partial && row.shape == SclMmsAddressShape::ArrayTemplate)
        return SclMmsDirectState::ArraySelectionRequired;
    if (row.state != LinkState::Resolved) return SclMmsDirectState::Blocked;
    if (row.shape == SclMmsAddressShape::AlternateAccess) return SclMmsDirectState::AlternateAccess;
    return row.shape == SclMmsAddressShape::Structure ? SclMmsDirectState::Structure : SclMmsDirectState::Mapped;
}

void ComposePlainAddress(SclMmsRow& row, const QString& ln, const QStringList& parts)
{
    row.item = ln + '$' + row.fc + '$' + parts.join('$');
    row.objectReference = row.domain + '/' + ln + '.' + parts.join('.');
}

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
    std::optional<SclMmsRow> reportAttribute;
    AttributeTarget attributeTarget;
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
    void Attributes();
    bool PrepareAttributeTarget();
    bool MatchesAttributeTarget(const SclMmsRow& row) const;
    void AttributeReports();
    void ObjectEvidence();
    void DataSetRelations();
    void OfflineCheck();
    bool SelectAttribute();
    ArraySelection RequestedArray(const SclMmsKey& signal);
    SclMmsReportMatch MatchReport(const SclMmsRow& attribute, const Reference& member);
    SclMmsAttributeMatch MatchAttribute(const SclMmsRow& row, const QString& ln, const QString& cdc) const;
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
    if (r.kind == SclMmsQueryKind::Attributes)
        return r.text.trimmed().isEmpty() || row.attributeMatch != SclMmsAttributeMatch::None;
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
    if (r.locate) {
        if (!out.locateFound && pos % r.pageSize == 0) out.rows.clear();
        if (!out.locateFound || pos < out.request.offset + r.pageSize) out.rows.push_back(row);
        if (row.key == *r.locate) {
            out.locateFound = true;
            out.request.offset = (pos / r.pageSize) * r.pageSize;
        }
        return true;
    }
    if (pos >= r.offset && int(out.rows.size()) < r.pageSize) out.rows.push_back(std::move(row));
    return true;
}

SclMmsRow Query::Report(int i)
{
    const auto& report = p.reports[i];
    SclMmsRow row;
    row.key = {SclMmsRecordKind::Report, report.node, i};
    row.report = report.node; row.dataSet = report.dataSet.target(); row.state = report.dataSet.state;
    row.addressScope = {report.ied, r.scope.accessPoint, report.server};
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
    if (const auto report = s.mmsIndex.reports.constFind(instance.definition); report != s.mmsIndex.reports.cend())
        row.addressScope = {p.reports[*report].ied, r.scope.accessPoint, p.reports[*report].server};
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
    NodeId ds = NoNode;
    if (report != NoNode) {
        if (!ReportAllowed(report)) return;
        ds = p.reports[s.mmsIndex.reports.value(report)].dataSet.target();
    } else if (Valid(r.target) && doc.nodes[r.target].isScl()
        && doc.nodes[r.target].name == "DataSet" && InScope(r.target)) ds = r.target;
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
        } else {
            row.state = LinkState::Blocked;
            row.limitation = QStringLiteral("成员引用索引未覆盖；请查看成员声明原文。");
        }
        row.evidence = {{member, {}, {}}};
        row.cells << row.domain << row.item << row.component << row.limitation;
        // Resolve descriptions only for the page delivered, never every member.
        if (!r.locate && Matches(row) && out.total >= r.offset && int(out.rows.size()) < r.pageSize)
            row.description = DescribeSclMember(s.analysis, member, task);
        Add(std::move(row));
    }
}

ArraySelection Query::RequestedArray(const SclMmsKey& signal)
{
    const auto& key = r.kind == SclMmsQueryKind::AttributeReports || r.kind == SclMmsQueryKind::ObjectEvidence
        || r.kind == SclMmsQueryKind::DataSetRelations || r.kind == SclMmsQueryKind::OfflineCheck ? r.key : r.locate.value_or(SclMmsKey{});
    if (key.kind == SclMmsRecordKind::Address && key.node == signal.node && key.record == signal.record
        && key.arrayDeclaration != NoNode)
        return {key.arrayDeclaration, key.arrayIndex};
    const auto member = s.mmsIndex.memberReferences.constFind(r.arrayMember);
    if (member == s.mmsIndex.memberReferences.cend()) return {};
    const auto& ref = s.analysis.references[*member];
    if (ref.state != LinkState::Resolved || ref.logicalNode != signal.node || ref.arrayIndex < 0) return {};
    const auto trace = s.analysis.fcdaTraces.constFind(ref.source);
    if (trace != s.analysis.fcdaTraces.cend()) {
        QList<NodeId> path;
        for (const auto& step : trace->steps) {
            Tick();
            if (step.field == FcdaField::DoName || step.field == FcdaField::DaName) path.push_back(step.node);
            if (step.field == FcdaField::Ix)
                return {step.node, ref.arrayIndex, path.mid(0, path.indexOf(step.node) + 1)};
        }
    }
    return {};
}

SclMmsAttributeMatch Query::MatchAttribute(const SclMmsRow& row, const QString& ln, const QString& cdc) const
{
    const auto text = r.text.trimmed();
    if (text.isEmpty()) return SclMmsAttributeMatch::None;
    const auto contains = [&](const QString& value) { return value.contains(text, Qt::CaseInsensitive); };
    auto fullPath = row.objectPath;
    if (row.key.declaration != NoNode) fullPath += '.' + row.key.field;
    if (contains(fullPath) || contains(ln + '.' + fullPath) || contains(row.objectReference)
        || contains(row.domain) || contains(row.item) || contains(row.domain + '/' + row.item)
        || contains(Attr(row.logicalDevice, "inst")))
        return SclMmsAttributeMatch::PathOrAddress;

    if (row.fc == "MX" && cdc == "CMV"
        && (row.objectPath == "PhV.phsA" || row.objectPath == "PhV.phsB" || row.objectPath == "PhV.phsC")) {
        const bool magnitude = row.key.field.startsWith("cVal.mag.");
        const bool angle = row.key.field.startsWith("cVal.ang.");
        if (magnitude || angle) {
            auto compact = text.simplified(); compact.remove(' ');
            const auto phase = row.objectPath.back() + QStringLiteral("相电压");
            const auto quantity = magnitude ? QStringLiteral("幅值") : QStringLiteral("相位");
            if (compact.compare(phase, Qt::CaseInsensitive) == 0 || compact == quantity
                || compact == QStringLiteral("电压") + quantity
                || compact.compare(phase + quantity, Qt::CaseInsensitive) == 0)
                return SclMmsAttributeMatch::BusinessTerm;
        }
    }
    return SclMmsAttributeMatch::None;
}

void Query::Addresses(int i)
{
    if (i < 0 || i >= int(s.tasks.catalog.instances.size())) return;
    const auto& signal = s.tasks.catalog.instances[i];
    if (attributeTarget.logicalNode != NoNode && signal.logicalNode != attributeTarget.logicalNode) return;
    if (!InScope(signal.logicalNode)) return;
    const auto& def = s.tasks.catalog.definitions[signal.definition];
    auto base = Signal(i);
    const auto ld = Ancestor(signal.logicalNode, "LDevice");
    const auto server = Ancestor(ld, "Server");
    const auto ap = r.scope.accessPoint == NoNode ? Ancestor(server, "AccessPoint") : r.scope.accessPoint;
    base.addressScope = {s.browse.iedOwners[signal.logicalNode], ap, server};
    base.logicalDevice = ld;
    base.objectPath = def.path;
    if (const auto domain = s.mmsIndex.domainsByDevice.constFind(ld);
        domain != s.mmsIndex.domainsByDevice.cend() && domain->state == MmsValueState::Derived) {
        base.domain = domain->name;
        base.domainEvidence = domain->evidence;
        base.evidence.push_back(domain->evidence);
        if (domain->evidence.attribute == "inst") base.evidence.push_back({base.addressScope.ied, "name", {}});
    }
    const auto ln = doc.nodes[signal.logicalNode].name == "LN0" ? QStringLiteral("LLN0") :
        Attr(signal.logicalNode, "prefix") + Attr(signal.logicalNode, "lnClass") + Attr(signal.logicalNode, "inst");
    const auto memberType = [&](NodeId declaration, LinkKind kind = LinkKind::MemberType) {
        const auto refs = s.analysis.outgoing.equal_range(declaration);
        std::pair<NodeId, LinkState> result{NoNode, LinkState::Missing};
        bool found = false;
        for (auto it = refs.first; it != refs.second; ++it) {
            Tick(); const auto& ref = s.analysis.references[it.value()];
            if (ref.kind != kind) continue;
            if (found) return std::pair{NoNode, LinkState::Ambiguous};
            found = true;
            result = {ref.state == LinkState::Resolved && ref.candidates.size() == 1 ? ref.candidates.front() : NoNode,
                ref.state};
        }
        return result;
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
    auto [objectType, objectState] = memberType(signal.logicalNode, LinkKind::LogicalNodeType);
    const auto objectParts = def.path.split('.');
    for (int part = 0; part < objectParts.size() && objectType != NoNode; ++part) {
        Tick(); NodeId declaration = NoNode; int count = 0;
        for (auto n = doc.nodes[objectType].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
            Tick();
            if (doc.nodes[n].isScl() && doc.nodes[n].name == (part ? "SDO" : "DO") && Attr(n, "name") == objectParts[part]) {
                declaration = n; ++count;
            }
        }
        uniquePath &= count == 1 && validPart(objectParts[part]);
        if (count == 1) {
            std::tie(objectType, objectState) = memberType(declaration);
            base.templatePath.push_back(declaration);
            base.evidence.push_back({declaration, {}, {}});
        } else {
            objectType = NoNode;
            objectState = count > 1 ? LinkState::Ambiguous : LinkState::Missing;
        }
    }
    uniquePath &= objectType == def.type && objectType != NoNode;
    const bool reportQuery = r.kind == SclMmsQueryKind::AttributeReports
        || r.kind == SclMmsQueryKind::ObjectEvidence || r.kind == SclMmsQueryKind::DataSetRelations
        || r.kind == SclMmsQueryKind::OfflineCheck;
    const bool search = r.kind == SclMmsQueryKind::Attributes;
    const auto publish = [&](SclMmsRow row) {
        if (reportQuery) {
            if (row.key == r.key) reportAttribute = std::move(row);
        } else if (MatchesAttributeTarget(row)) {
            Add(std::move(row));
        }
    };
    if (def.type == NoNode || !uniquePath) {
        auto row = base; row.key.kind = SclMmsRecordKind::Address;
        row.state = logicalNames > 1 ? LinkState::Ambiguous : objectState;
        if (row.state == LinkState::Resolved) row.state = LinkState::Blocked;
        row.direct = DirectState(row);
        row.limitation = QStringLiteral("类型或逻辑节点地址未唯一解析。");
        if (search) row.attributeMatch = MatchAttribute(row, ln, def.cdc);
        if (!search || r.arrayMember == NoNode) publish(std::move(row));
        return;
    }
    const auto selection = RequestedArray(base.key);
    QSet<NodeId> ancestry;
    QList<NodeId> declarations;
    const auto describe = [&](const SclMmsRow& row) {
        if (!base.description) base.description = DescribeSclSignal(s.analysis, signal.logicalNode, def.path, task);
        const auto object = row.arrayIndex < 0 ? *base.description : DescribeSclSignal(s.analysis,
            signal.logicalNode, def.path, task, row.key.arrayDeclaration, row.arrayIndex);
        return DescribeSclAttribute(s.analysis, object, row.key.field, row.fc, declarations, task,
            row.key.arrayDeclaration, row.arrayIndex);
    };
    const auto expand = [&](auto&& self, NodeId type, const QString& prefix, const QString& parentFc, int depth) -> void {
        Tick(); if (reportAttribute) return;
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
            auto row = base;
            row.key.kind = SclMmsRecordKind::Address; row.key.field = path;
            row.key.fc = fc; row.key.declaration = c;
            row.fc = fc;
            row.templatePath.append(declarations);
            row.templatePath.push_back(c);
            row.fcEvidence = {depth ? declarations.front() : c, "fc", {}};
            const auto selectedFromInstance = attributeTarget.indexPart >= 0 && attributeTarget.indexPart < row.templatePath.size();
            const int axis = selectedFromInstance ? attributeTarget.indexPart : int(row.templatePath.indexOf(selection.declaration));
            const int index = selectedFromInstance ? attributeTarget.index : selection.index;
            const auto axisNode = axis >= 0 ? row.templatePath[axis] : NoNode;
            const bool selected = axis >= 0 && index >= 0 && index < UnsignedIndex(Attr(axisNode, "count"))
                && (selection.path.isEmpty() || selection.path == row.templatePath.mid(0, axis + 1));
            const bool isArray = std::any_of(row.templatePath.cbegin(), row.templatePath.cend(), [&](NodeId declaration) {
                return !Attr(declaration, "count").isNull() && (!selected || declaration != axisNode);
            });
            row.shape = isArray ? SclMmsAddressShape::ArrayTemplate :
                bt == "Struct" ? SclMmsAddressShape::Structure : SclMmsAddressShape::Scalar;
            static const QSet<QString> fcs = {"ST","MX","SP","SV","CF","DC","SG","SE","SR","OR","BL","EX","CO","US","MS","RP","BR","LG","GO","GS"};
            row.state = !row.domain.isEmpty() && validPart(name) && names.value(nameKey) == 1 && fcs.contains(fc)
                && basicTypes.contains(bt) ? LinkState::Resolved : LinkState::Blocked;
            if (!basicTypes.contains(bt)) row.shape = SclMmsAddressShape::None;
            const auto [target, typeState] = (bt == "Struct" || bt == "Enum") ? memberType(c) :
                std::pair{NoNode, LinkState::Resolved};
            const auto nested = bt == "Struct" ? target : NoNode;
            if (typeState != LinkState::Resolved) row.state = typeState;
            if (names.value(nameKey) > 1) row.state = LinkState::Ambiguous;
            if (selected) {
                row.arrayIndex = row.key.arrayIndex = index;
                row.key.arrayDeclaration = axisNode;
                row.arraySelector = selectedFromInstance ? r.target : r.arrayMember;
            }
            if (isArray && row.state == LinkState::Resolved) {
                row.state = LinkState::Partial;
                row.limitation = QStringLiteral("数组模板未选择元素；不生成标量 Item。");
            } else if (row.state == LinkState::Resolved) {
                auto parts = objectParts + path.split('.');
                if (selected) {
                    row.shape = SclMmsAddressShape::AlternateAccess;
                    row.item = ln + '$' + fc + '$' + parts.mid(0, axis + 1).join('$');
                    row.component = parts.mid(axis + 1).join('$');
                    parts[axis] += '(' + QString::number(index) + ')';
                } else {
                    ComposePlainAddress(row,ln,parts);
                }
                row.objectReference = row.domain + '/' + ln + '.' + parts.join('.');
            } else row.limitation = QStringLiteral("名称、FC 或类型未唯一解析；不生成确定地址。");
            row.direct = DirectState(row);
            row.evidence.push_back({c, {}, {}});
            row.cells << path << fc << bt << Attr(c, "count") << row.domain << row.item << row.limitation;
            declarations.push_back(c);
            const bool targetMatches = !search || MatchesAttributeTarget(row);
            if (search && targetMatches && (r.fc.isEmpty() || row.fc == r.fc)) {
                row.attributeMatch = MatchAttribute(row, ln, def.cdc);
                if (row.attributeMatch == SclMmsAttributeMatch::None && !r.text.trimmed().isEmpty()) {
                    row.description = describe(row);
                    const auto matchesDescription = [&](const SclDescriptionField& field) {
                        return field.state == SclDescriptionState::Present && field.value.contains(r.text.trimmed(), Qt::CaseInsensitive);
                    };
                    const auto attribute = std::find_if(row.description->fields.cbegin(), row.description->fields.cend(), matchesDescription);
                    const auto object = std::find_if(base.description->fields.cbegin(), base.description->fields.cend(), matchesDescription);
                    if (attribute != row.description->fields.cend())
                        row.matchEvidence = {attribute->node, "desc", {}};
                    else if (object != base.description->fields.cend())
                        row.matchEvidence = {object->node, object->source == SclDescriptionSource::InstanceDU ? QString() : QStringLiteral("desc"), {}};
                    else if (Attr(signal.logicalNode, "desc").contains(r.text.trimmed(), Qt::CaseInsensitive))
                        row.matchEvidence = {signal.logicalNode, "desc", {}};
                    if (row.matchEvidence.node != NoNode)
                        row.attributeMatch = SclMmsAttributeMatch::Description;
                }
            }
            const bool onPage = r.locate ? row.key == *r.locate || out.locateFound :
                out.total >= r.offset && int(out.rows.size()) < r.pageSize;
            if (!reportQuery && targetMatches && onPage && Matches(row) && !row.description) row.description = describe(row);
            if (targetMatches && (!search || r.arrayMember == NoNode || selected)) publish(std::move(row));
            if (reportAttribute) {
                declarations.pop_back();
                break;
            }
            if (nested != NoNode && names.value(nameKey) == 1 && validPart(name) && path.size() < 2048)
                self(self, nested, path + '.', fc, depth + 1);
            else if (nested != NoNode && path.size() >= 2048) {
                out.limited = true; out.limitation = QStringLiteral("属性路径长度受限。");
            }
            declarations.pop_back();
            if (reportAttribute) break;
        }
        ancestry.remove(type);
    };
    expand(expand, def.type, {}, {}, 0);
}

void Query::Attributes()
{
    if (r.target != NoNode && !PrepareAttributeTarget()) return;
    if (r.signal >= 0) {
        Addresses(r.signal);
    } else if (r.scope.ied != NoNode) {
        const auto candidates = s.tasks.signalsByIed.constFind(r.scope.ied);
        if (candidates != s.tasks.signalsByIed.cend())
            for (const auto signal : *candidates) { Tick(); Addresses(signal); }
    } else {
        for (int i = 0; i < int(s.tasks.catalog.instances.size()); ++i) { Tick(); Addresses(i); }
    }
    if (s.tasks.catalog.limited) {
        out.limited = true;
        out.limitation += QStringLiteral(" 共享信号目录受限。");
    }
}

bool Query::PrepareAttributeTarget()
{
    if (!IsSclMmsAttributeTarget(s, r.target)) {
        out.summary = QStringLiteral("所选记录没有可查阅的属性依据。");
        return false;
    }
    const auto& element = doc.nodes[r.target];
    if (element.name == "FCDA") {
        const auto entry = s.mmsIndex.memberReferences.constFind(r.target);
        if (entry == s.mmsIndex.memberReferences.cend()) {
            out.limited = s.mmsIndex.limited;
            out.summary = QStringLiteral("成员引用索引未覆盖。可查看来源 FCDA 原文。");
            return false;
        }
        const auto& ref = s.analysis.references[*entry];
        const auto trace = s.analysis.fcdaTraces.constFind(r.target);
        if (ref.state != LinkState::Resolved || trace == s.analysis.fcdaTraces.cend()) {
            out.summary = QStringLiteral("FCDA 引用未解析 · %1").arg(SclMmsStateText(ref.state));
            if (!ref.detail.isEmpty()) out.summary += QStringLiteral(" · ") + ref.detail;
            return false;
        }
        attributeTarget.logicalNode = ref.logicalNode;
        attributeTarget.fc = Attr(r.target, "fc");
        for (const auto& step : trace->steps) {
            Tick();
            if (step.field == FcdaField::DoName || step.field == FcdaField::DaName)
                attributeTarget.path.push_back(step.node);
            attributeTarget.exact |= step.field == FcdaField::DaName;
        }
        if (ref.arrayIndex >= 0) r.arrayMember = r.target;
    } else if (element.name == "DO" || element.name == "SDO" || element.name == "DA" || element.name == "BDA") {
        attributeTarget.declaration = r.target;
        attributeTarget.exact = element.name == "DA" || element.name == "BDA";
    } else {
        attributeTarget.exact = element.name == "DAI";
        auto node = r.target;
        int reversedIndexPart = -1;
        while (Valid(node) && (doc.nodes[node].name == "DOI" || doc.nodes[node].name == "SDI" || doc.nodes[node].name == "DAI")) {
            Tick();
            const auto name = Attr(node, "name");
            const auto index = Attr(node, "ix");
            if (!index.isNull()) {
                if (reversedIndexPart >= 0 || UnsignedIndex(index) < 0) {
                    out.summary = QStringLiteral("实例索引无法唯一解释。可查看来源原文。");
                    return false;
                }
                reversedIndexPart = int(attributeTarget.instancePath.size());
                attributeTarget.index = UnsignedIndex(index);
            }
            attributeTarget.instancePath.prepend(name);
            node = doc.nodes[node].parent;
        }
        if (reversedIndexPart >= 0)
            attributeTarget.indexPart = int(attributeTarget.instancePath.size()) - 1 - reversedIndexPart;
        if (!Valid(node) || !doc.nodes[node].isScl() || (doc.nodes[node].name != "LN" && doc.nodes[node].name != "LN0")) {
            out.summary = QStringLiteral("来源对象没有可定位的 LN 实例。可查看来源原文。");
            return false;
        }
        attributeTarget.logicalNode = node;
    }
    if (attributeTarget.logicalNode != NoNode && !InScope(attributeTarget.logicalNode)) {
        out.summary = QStringLiteral("来源对象不在当前 IED / AP / Server 范围。");
        return false;
    }
    return true;
}

bool Query::MatchesAttributeTarget(const SclMmsRow& row) const
{
    const auto& target = attributeTarget;
    if (!target.fc.isEmpty() && target.fc != row.fc) return false;
    if (target.declaration != NoNode)
        return target.exact ? row.key.declaration == target.declaration : row.templatePath.contains(target.declaration);
    if (!target.path.isEmpty())
        return (!target.exact || row.templatePath.size() == target.path.size())
            && row.templatePath.size() >= target.path.size()
            && std::equal(target.path.cbegin(), target.path.cend(), row.templatePath.cbegin());
    if (row.templatePath.size() < target.instancePath.size()
        || (target.indexPart >= 0 && row.key.arrayIndex != target.index)
        || (target.exact && row.templatePath.size() != target.instancePath.size())) return false;
    for (int i = 0; i < target.instancePath.size(); ++i)
        if (Attr(row.templatePath[i], "name") != target.instancePath[i]) return false;
    return true;
}

SclMmsReportMatch Query::MatchReport(const SclMmsRow& attribute, const Reference& member)
{
    return MatchSclDataSetMember(s, attribute, member, [this] { Tick(); });
}

bool Query::SelectAttribute()
{
    if (r.key.kind != SclMmsRecordKind::Address) return false;
    Addresses(r.key.record);
    if (!reportAttribute) {
        out.limitation += QStringLiteral("当前快照/范围未定位到完整属性身份。");
        return false;
    }
    out.attributeDirect = reportAttribute->direct;
    return true;
}

void Query::ObjectEvidence()
{
    SclMmsRow row;
    if (r.key.kind == SclMmsRecordKind::Address) {
        if (!SelectAttribute()) return;
        row = *reportAttribute;
    } else if (r.key.kind == SclMmsRecordKind::Signal) {
        if (r.key.record < 0 || r.key.record >= int(s.tasks.catalog.instances.size())) return;
        row = Signal(r.key.record);
        if (row.key != r.key || !InScope(row.key.node)) return;
    } else if (r.key.kind == SclMmsRecordKind::Member) {
        if (r.key.record < 0 || r.key.record >= int(s.analysis.references.size())) return;
        const auto& ref = s.analysis.references[r.key.record];
        if (ref.kind != LinkKind::Fcda || ref.source != r.key.node || ref.memberOrdinal != r.key.ordinal || !InScope(ref.source)) return;
        row.key = r.key; row.state = ref.state; row.fc = Attr(ref.source,"fc");
        row.domain = ref.mmsDomain; row.item = ref.mmsItem; row.objectReference = ref.objectReference;
        row.arrayIndex = ref.arrayIndex; row.component = ref.mmsComponent; row.limitation = ref.detail;
    } else return;
    if (r.key.kind != SclMmsRecordKind::Address) row.addressScope = r.scope;
    auto evidence = ProjectSclObjectEvidence(s,row,[this] { Tick(); });
    out.objectEvidence = std::make_shared<const SclObjectEvidence>(std::move(evidence));
}

void Query::DataSetRelations()
{
    if (s.mmsIndex.dataSetIndexLimited) {
        out.limited = true; out.limitation = QStringLiteral("数据集反向索引资源受限，不能给出未引用结论。");
    }
    if (r.target != NoNode && (!Valid(r.target) || !doc.nodes[r.target].isScl()
        || doc.nodes[r.target].name != "DataSet" || !InScope(r.target))) {
        out.limitation = QStringLiteral("当前范围没有对应的数据集身份。"); return;
    }
    if (!SelectAttribute()) return;
    const auto& attribute = *reportAttribute;
    if (attribute.state != LinkState::Resolved || attribute.direct == SclMmsDirectState::ArraySelectionRequired) {
        out.limitation += QStringLiteral("属性身份尚未唯一解析，不能确定数据集关系。"); return;
    }
    QHash<NodeId,std::shared_ptr<const SclDataSetUsage>> usages;
    const auto useOf = [&](NodeId ds) -> std::shared_ptr<const SclDataSetUsage> {
        if (const auto it = usages.constFind(ds); it != usages.cend()) return *it;
        auto usage = std::make_shared<SclDataSetUsage>();
        usage->complete = !s.mmsIndex.dataSetIndexLimited;
        const auto found = s.mmsIndex.dataSetUsers.constFind(ds);
        if (found != s.mmsIndex.dataSetUsers.cend()) for (const auto index : *found) {
            Tick(); const auto& ref = s.analysis.references[index];
            const auto tag = doc.nodes[ref.source].name;
            if (tag != "ReportControl" && tag != "GSEControl" && tag != "SampledValueControl") continue;
            ++usage->observed;
            if (usage->users.size() >= 512) { usage->complete = false; continue; }
            usage->users << SclDataSetUser{tag == "ReportControl" ? SclDataSetUserKind::Report
                : tag == "GSEControl" ? SclDataSetUserKind::Goose : SclDataSetUserKind::Sv,
                ref.state, SclEvidenceAt(s,ref.source,"datSet")};
        }
        if (!usage->complete) { out.limited = true; out.limitation += QStringLiteral("数据集使用方受限；最多交付512项。"); }
        if (usages.size() < 200) usages.insert(ds,usage);
        return usage;
    };
    const auto visit = [&](int index) {
        Tick(); const auto& ref = s.analysis.references[index];
        const auto ds = doc.nodes[ref.source].parent;
        if (!InScope(ref.source) || (r.target != NoNode && r.target != ds)) return;
        if (ref.state != LinkState::Resolved || ref.candidates.size() != 1) {
            if (!CouldSclDataSetMemberAffect(s,attribute,ref,[this] { Tick(); })) return;
            auto& coverage = out.dataSetCoverage;
            ++coverage.uncertainOccurrences;
            if (coverage.uncertainSources.size() < 64) coverage.uncertainSources << SclEvidenceAt(s,ref.source);
            return;
        }
        const auto match = MatchReport(attribute,ref);
        if (match == SclMmsReportMatch::None) return;
        ++out.dataSetCoverage.observedMatches;
        out.dataSetCoverage.conclusion = SclDataSetConclusion::Referenced;
        SclMmsRow row; row.key = {SclMmsRecordKind::Member,ref.source,index,ref.memberOrdinal};
        row.dataSet = ds; row.state = ref.state; row.reportMatch = match;
        row.shape = Shape(ref); row.fc = Attr(ref.source,"fc");
        row.domain = ref.mmsDomain; row.item = ref.mmsItem;
        row.objectReference = ref.objectReference; row.addressScope = attribute.addressScope;
        row.arrayIndex = ref.arrayIndex; row.component = ref.mmsComponent;
        row.cells = {Attr(ds,"name"),Attr(ref.source,"doName"),Attr(ref.source,"daName"),SclMmsReportMatchText(match)};
        // Projection is page-bounded. Locate uses the generic rolling page buffer.
        const bool onPage = r.locate || (out.total >= r.offset && int(out.rows.size()) < r.pageSize);
        if (onPage && Matches(row)) {
            auto relation = std::make_shared<SclDataSetRelation>();
            relation->sha256 = s.analysis.sha256; relation->document = s.analysis.document;
            relation->member = SclEvidenceAt(s,ref.source); relation->dataSet = SclEvidenceAt(s,ds,"name");
            relation->ordinal = ref.memberOrdinal; relation->match = match;
            relation->rawDoName = Attr(ref.source,"doName"); relation->rawDaName = Attr(ref.source,"daName");
            relation->rawFc = row.fc; relation->rawIx = Attr(ref.source,"ix");
            relation->daNamePresent = !relation->rawDaName.isNull();
            relation->sourceIed = Ancestor(ref.source,"IED"); relation->sourceAccessPoint = Ancestor(ref.source,"AccessPoint");
            relation->sourceServer = Ancestor(ref.source,"Server"); relation->targetLogicalNode = attribute.key.node;
            relation->usage = useOf(ds); row.dataSetRelation = std::move(relation);
        }
        Add(std::move(row));
    };
    const auto known = s.mmsIndex.dataSetMembersByLogicalNode.constFind(attribute.key.node);
    if (known != s.mmsIndex.dataSetMembersByLogicalNode.cend()) for (const auto index : *known) visit(index);
    const auto unknown = s.mmsIndex.uncertainMembersByServer.constFind(attribute.addressScope.server);
    if (unknown != s.mmsIndex.uncertainMembersByServer.cend()) for (const auto index : *unknown) visit(index);
    auto& coverage = out.dataSetCoverage;
    coverage.scanComplete = !s.mmsIndex.dataSetIndexLimited && !s.mmsIndex.limited;
    if (coverage.observedMatches == 0 && coverage.scanComplete && coverage.uncertainOccurrences == 0)
        coverage.conclusion = SclDataSetConclusion::NotReferenced;
}

void Query::AttributeReports()
{
    if (r.key.kind != SclMmsRecordKind::Address) return;
    Addresses(r.key.record);
    if (!reportAttribute) {
        out.summary = QStringLiteral("当前范围内未定位到属性身份。");
        return;
    }
    const auto& attribute = *reportAttribute;
    out.attributeDirect = attribute.direct;
    if (attribute.state != LinkState::Resolved) {
        out.summary = QStringLiteral("属性尚无确定的访问身份，无法确认报告成员匹配。");
        return;
    }
    const auto members = s.mmsIndex.membersByLogicalNode.constFind(attribute.key.node);
    if (members == s.mmsIndex.membersByLogicalNode.cend()) return;
    for (const auto index : *members) {
        Tick();
        const auto& member = s.analysis.references[index];
        const auto match = MatchReport(attribute, member);
        if (match == SclMmsReportMatch::None) continue;
        const auto ds = doc.nodes[member.source].parent;
        const auto reports = s.mmsIndex.reportsByDataSet.constFind(ds);
        if (reports == s.mmsIndex.reportsByDataSet.cend()) continue;
        for (const auto report : *reports) {
            Tick();
            if (!InScope(p.reports[report].node)) continue;
            auto row = Report(report);
            row.key = {SclMmsRecordKind::Member, member.source, index, member.memberOrdinal};
            row.key.parentReport = row.report;
            row.reportMatch = match;
            row.state = member.state;
            row.shape = Shape(member);
            row.domain = member.mmsDomain; row.item = member.mmsItem;
            row.fc = attribute.fc; row.fcEvidence = {member.source, "fc", {}};
            row.arrayIndex = member.arrayIndex; row.component = member.mmsComponent;
            row.evidence.push_back({ds, "name", {}});
            row.evidence.push_back({member.source, {}, {}});
            row.cells << Attr(member.source, "doName") << Attr(member.source, "daName")
                << row.fc << QString::number(member.memberOrdinal);
            Add(std::move(row));
        }
    }
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

void Query::OfflineCheck()
{
    auto check = std::make_shared<SclOfflineCheck>();
    out.check = check; // Publish partial evidence on budget exhaustion, never a successful target.
    const auto stop = [&](SclCheckState state, const QString& reason) {
        check->state = state; check->model = reason;
    };
    auto input = r.check.original.trimmed();
    check->domain = r.check.domain.trimmed(); check->fc = r.check.fc.trimmed();
    if (input.size() > 4096 || check->domain.size() > 256) {
        stop(SclCheckState::Unsupported, QStringLiteral("输入长度超出支持范围。")); return;
    }
    const bool object = r.check.kind == SclCheckInputKind::ObjectReference;
    const auto slash = input.split('/');
    if (slash.size() == 2) {
        if (!check->domain.isEmpty() && check->domain != slash[0]) {
            check->format = QStringLiteral("Domain 栏与粘贴原文冲突。"); check->state = SclCheckState::Rejected; return;
        }
        check->domain = slash[0]; input = slash[1];
    } else if (slash.size() != 1 || object) {
        check->format = QStringLiteral("对象引用需要一个 LDName/路径分隔符；MMS 输入最多一个 Domain/Item 分隔符。");
        check->state = SclCheckState::Rejected; return;
    }
    QStringList parts;
    if (object) {
        parts = input.split('.');
        if (parts.size() < 2) { check->format = QStringLiteral("对象引用需要 LN.DO 路径。"); check->state = SclCheckState::Rejected; return; }
    } else {
        parts = input.split('$');
        if (parts.size() >= 2 && QSet<QString>{"RP","BR","GO","GS","MS","US"}.contains(parts[1])) {
            stop(SclCheckState::Unsupported, QStringLiteral("控制块或数据集类别不在普通信号 Item 校核范围；请使用报告/数据集/收发任务入口。")); return;
        }
        if (parts.size() < 3) { stop(SclCheckState::Unsupported, QStringLiteral("未识别为普通 LN$FC$对象 Item；数据集列表名与 rptID 请使用既有任务入口。")); return; }
        if (!check->fc.isEmpty() && check->fc != parts[1]) {
            check->format = QStringLiteral("FC 栏与 Item 中的 FC 冲突。"); check->state = SclCheckState::Rejected; return;
        }
        check->fc = parts.takeAt(1);
        if (check->fc.isEmpty()) { check->format = QStringLiteral("MMS Item 的 FC 段不能为空。"); check->state = SclCheckState::Rejected; return; }
    }
    if (parts.contains(QString()) || (slash.size() == 2 && check->domain.isEmpty())) {
        check->format = QStringLiteral("支持的引用语法不允许空段。"); check->state = SclCheckState::Rejected; return;
    }
    static const QRegularExpression identifier(QStringLiteral("^[A-Za-z0-9_]+$"));
    static const QRegularExpression indexed(QStringLiteral("^([A-Za-z0-9_]+)\\(([0-9]+)\\)$"));
    int arrayPart = -1, arrayIndex = -1;
    for (int i = 0; i < parts.size(); ++i) {
        Tick();
        const auto match = indexed.match(parts[i]);
        if (i > 0 && match.hasMatch()) {
            if (arrayPart >= 0) { stop(SclCheckState::Unsupported, QStringLiteral("多维数组选择尚未支持。")); return; }
            arrayPart = i - 1; arrayIndex = UnsignedIndex(match.captured(2)); parts[i] = match.captured(1);
            if (arrayIndex < 0) { stop(SclCheckState::Unsupported, QStringLiteral("数组下标超出支持整数范围。")); return; }
        } else if (!identifier.match(parts[i]).hasMatch()) {
            stop(SclCheckState::Unsupported, QStringLiteral("含未知标识或扩展语法；未将其判为非法。")); return;
        }
    }
    if ((!check->domain.isEmpty() && !identifier.match(check->domain).hasMatch())
        || (!check->fc.isEmpty() && !identifier.match(check->fc).hasMatch())) {
        stop(SclCheckState::Unsupported, QStringLiteral("Domain/FC 含尚未支持的语法。")); return;
    }
    static const QSet<QString> signalFcs = {"ST","MX","SP","SV","CF","DC","SG","SE","SR","OR","BL","EX","CO"};
    if (!check->fc.isEmpty() && !signalFcs.contains(check->fc)) {
        stop(SclCheckState::Unsupported, QStringLiteral("此 FC 的普通信号映射尚未解释。")); return;
    }
    check->format = QStringLiteral("支持的普通对象语法；数组以 name(index) 表示，索引从 0 起。");
    const auto version = Attr(0,"version");
    if (!version.isEmpty() && version != "2003" && version != "2007") {
        stop(SclCheckState::Uncertain,QStringLiteral("当前文件版本尚未解释；不宣告映射通过。")); return;
    }
    check->logicalNode = parts.takeFirst(); check->path = parts.join('.');
    QList<NodeId> devices; bool unknownDomain = false;
    for (const auto& access : p.servers) {
        Tick();
        if ((r.scope.ied == NoNode || access.ied == r.scope.ied)
            && (r.scope.accessPoint == NoNode || access.accessPoint == r.scope.accessPoint)
            && (r.scope.server == NoNode || access.model.candidates.contains(r.scope.server))
            && r.check.logicalDevice == NoNode
            && access.model.state != LinkState::Resolved) unknownDomain = true;
    }
    for (auto d = s.mmsIndex.domainsByDevice.cbegin(); d != s.mmsIndex.domainsByDevice.cend(); ++d) {
        Tick(); const auto n = d.key(); if (!InScope(n)) continue;
        if (r.check.logicalDevice != NoNode && n != r.check.logicalDevice) continue;
        if (d->state != MmsValueState::Derived) { unknownDomain = true; continue; }
        if ((!check->domain.isEmpty() && d->name != check->domain) || (r.check.logicalDevice != NoNode && n != r.check.logicalDevice)) continue;
        // With no Domain, narrow only to devices that actually declare this LN.
        bool hasLn = !check->domain.isEmpty();
        for (auto c = doc.nodes[n].firstChild; c != NoNode && !hasLn; c = doc.nodes[c].nextSibling) {
            Tick(); if (!doc.nodes[c].isScl() || (doc.nodes[c].name != "LN" && doc.nodes[c].name != "LN0")) continue;
            hasLn = (doc.nodes[c].name == "LN0" ? QStringLiteral("LLN0") : Attr(c,"prefix") + Attr(c,"lnClass") + Attr(c,"inst")) == check->logicalNode;
        }
        if (hasLn) devices << n;
        if (devices.size() > 64) { out.limited = true; break; }
    }
    std::sort(devices.begin(),devices.end());
    if (devices.size() != 1 || check->domain.isEmpty()) {
        for (auto ld : devices.mid(0,64)) {
            Tick(); const auto domain = s.mmsIndex.domainsByDevice.value(ld).name;
            check->candidates << SclCheckCandidate{ld, domain + " · " + SclTaskPath(doc,ld)};
        }
        stop(devices.size() > 1 || unknownDomain || out.limited ? SclCheckState::Uncertain
            : check->domain.isEmpty() ? SclCheckState::ContextRequired : SclCheckState::Rejected,
            check->domain.isEmpty() ? QStringLiteral("需补充 Domain；请显式选择范围候选后重新校核。")
            : devices.isEmpty() ? QStringLiteral("当前可检查范围没有此 Domain；未知命名分支另保留不确定。")
            : QStringLiteral("Domain 在多个 Server/LD 中同名；请显式选择候选。")); return;
    }
    if (unknownDomain) { stop(SclCheckState::Uncertain,QStringLiteral("范围中存在命名未确定的 LD，不能证明 Domain 唯一。")); return; }
    const auto ld = devices.front();
    check->lastConfirmed = SclEvidenceAt(s,ld);
    check->steps << SclCheckStep{check->domain,QStringLiteral("唯一 LD；命名依据见声明"),SclEvidenceAt(s,ld,s.mmsIndex.domainsByDevice.value(ld).evidence.attribute)};
    QList<NodeId> lns; bool unknownLn = false;
    for (auto n = doc.nodes[ld].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
        Tick(); if (!doc.nodes[n].isScl() || (doc.nodes[n].name != "LN" && doc.nodes[n].name != "LN0")) continue;
        if (doc.nodes[n].name == "LN" && (Attr(n,"lnClass").isEmpty() || Attr(n,"inst").isNull())) unknownLn = true;
        const auto name = doc.nodes[n].name == "LN0" ? QStringLiteral("LLN0") : Attr(n,"prefix") + Attr(n,"lnClass") + Attr(n,"inst");
        if (name == check->logicalNode) lns << n;
    }
    if (lns.size() != 1 || unknownLn) { stop(lns.isEmpty() && !unknownLn ? SclCheckState::Rejected : SclCheckState::Uncertain,
        QStringLiteral("LN 不存在或未唯一声明。")); check->stoppedAt = check->logicalNode; return; }
    const auto ln = lns.front();
    if (doc.nodes[ln].name == "LN0" ? Attr(ln,"lnClass") != "LLN0" || !Attr(ln,"inst").isEmpty()
        : Attr(ln,"lnClass").isEmpty() || Attr(ln,"inst").isNull()) {
        stop(SclCheckState::Uncertain,QStringLiteral("LN 实例声明不满足现有映射前提。")); return;
    }
    check->lastConfirmed = SclEvidenceAt(s,ln);
    check->steps << SclCheckStep{check->logicalNode,QStringLiteral("唯一 LN 实例"),check->lastConfirmed};
    for (auto n = doc.nodes[ln].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
        Tick(); const auto& node = doc.nodes[n];
        if (node.isScl() && (node.name == "ReportControl" || node.name == "DataSet" || node.name == "GSEControl"
            || node.name == "SampledValueControl" || node.name == "LogControl") && Attr(n,"name") == parts.front()) {
            check->lastConfirmed = SclEvidenceAt(s,n);
            stop(SclCheckState::Unsupported,QStringLiteral("这是控制块或数据集声明；请使用相应任务入口，不按普通信号路径判错。")); return;
        }
    }
    const auto typeOf = [&](NodeId declaration, LinkKind kind) {
        NodeId target = NoNode; int links = 0;
        const auto refs = s.analysis.outgoing.equal_range(declaration);
        for (auto it = refs.first; it != refs.second; ++it) {
            Tick(); const auto& ref = s.analysis.references[it.value()]; if (ref.kind != kind) continue;
            ++links;
            if (ref.state == LinkState::Resolved && ref.candidates.size() == 1) target = ref.candidates.front();
            else for (auto candidate : ref.candidates) {
                Tick(); if (check->typeCandidates.size() < 64) check->typeCandidates << SclEvidenceAt(s,candidate);
            }
        }
        return links == 1 ? target : NoNode;
    };
    auto type = typeOf(ln,LinkKind::LogicalNodeType);
    const auto addFc = [&](QStringList& values,const QString& fc) {
        if (values.contains(fc)) return;
        if (values.size() >= 64) { out.limited = true; throw BudgetEnd{QStringLiteral("FC 候选预览预算 64 项已用尽；不能交付完整候选或成功目标。")}; }
        values << fc;
    };
    NodeId declaration = NoNode, arrayDeclaration = NoNode;
    QString objectPath, field, effectiveFc;
    QList<NodeId> path;
    bool arrayMissing = false;
    for (int i = 0; i < parts.size(); ++i) {
        Tick(); check->stoppedAt = parts[i];
        if (type == NoNode) { stop(SclCheckState::Uncertain,QStringLiteral("必要类型缺失、歧义或未支持；不能判路径不存在。")); return; }
        check->lastConfirmed = SclEvidenceAt(s,type);
        QList<NodeId> found; QStringList possibleFc;
        for (auto n = doc.nodes[type].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
            Tick(); const auto& node = doc.nodes[n];
            if (!node.isScl() || (node.name != "DO" && node.name != "SDO" && node.name != "DA" && node.name != "BDA") || Attr(n,"name") != parts[i]) continue;
            if (node.name == "DA") {
                addFc(possibleFc,Attr(n,"fc"));
                if (!check->fc.isEmpty() && Attr(n,"fc") != check->fc) continue;
            }
            found << n;
        }
        possibleFc.removeDuplicates();
        if (found.size() != 1) {
            if (found.size() > 1) for (auto candidate : found) {
                Tick(); if (check->typeCandidates.size() < 64) check->typeCandidates << SclEvidenceAt(s,candidate);
            }
            check->fcCandidates = possibleFc;
            stop(found.size() > 1 ? (check->fc.isEmpty() && possibleFc.size() > 1 ? SclCheckState::ContextRequired : SclCheckState::Uncertain)
                : SclCheckState::Rejected, found.isEmpty() ? (possibleFc.isEmpty() ? QStringLiteral("实际类型声明中不存在此路径段。") : QStringLiteral("该属性的实际 FC 与输入不符。"))
                : QStringLiteral("路径候选不唯一；需选择 FC 或修复歧义，不能取首项。")); return;
        }
        declaration = found.front(); path << declaration;
        const auto& tag = doc.nodes[declaration].name;
        const bool isObject = tag == "DO" || tag == "SDO";
        auto& name = isObject ? objectPath : field; name += (name.isEmpty() ? QString() : QStringLiteral(".")) + parts[i];
        if (tag == "DA") effectiveFc = Attr(declaration,"fc");
        const auto count = Attr(declaration,"count");
        if (!count.isNull()) {
            const auto size = UnsignedIndex(count);
            if (size <= 0) { stop(SclCheckState::Uncertain,QStringLiteral("数组边界未确定。")); return; }
            if (arrayDeclaration != NoNode) { stop(SclCheckState::Unsupported,QStringLiteral("多维数组尚未支持。")); return; }
            arrayDeclaration = declaration;
            if (arrayPart != i) arrayMissing = true;
            else if (arrayIndex >= size) { stop(SclCheckState::Rejected,QStringLiteral("下标超出实际 count 边界。")); return; }
        } else if (arrayPart == i) { stop(SclCheckState::Rejected,QStringLiteral("此段没有数组声明，不能使用下标。")); return; }
        check->lastConfirmed = SclEvidenceAt(s,declaration);
        check->steps << SclCheckStep{parts[i],isObject ? QStringLiteral("对象声明；CDC 属于其 DOType") : QStringLiteral("属性声明；有效 FC：") + effectiveFc,check->lastConfirmed};
        const auto bt = Attr(declaration,"bType");
        type = isObject || bt == "Struct" || bt == "Enum" ? typeOf(declaration,LinkKind::MemberType) : NoNode;
        if (isObject && type == NoNode) { stop(SclCheckState::Uncertain,QStringLiteral("对象类型未唯一解析。")); return; }
        if (i + 1 < parts.size() && !isObject && bt != "Struct") {
            if (bt.isEmpty()) stop(SclCheckState::Uncertain,QStringLiteral("属性基本类型未确定。"));
            else {
                static const QSet<QString> terminalTypes = {"BOOLEAN","INT8","INT16","INT24","INT32","INT64","INT128",
                    "INT8U","INT16U","INT24U","INT32U","FLOAT32","FLOAT64","Enum","Dbpos","Tcmd","Quality","Timestamp",
                    "VisString32","VisString64","VisString65","VisString129","VisString255","Octet64","Unicode255",
                    "EntryID","Check","Octet6","Octet8","Currency","PhyComAddr","ObjRef","TrgOps","OptFlds","SvOptFlds","LogOptFlds"};
                stop(terminalTypes.contains(bt) ? SclCheckState::Rejected : SclCheckState::Uncertain,
                    terminalTypes.contains(bt) ? QStringLiteral("实际属性不是结构，不能继续展开。") : QStringLiteral("基本类型未支持，不能断言下级路径不存在。"));
            }
            check->stoppedAt = parts[i+1]; return;
        }
    }
    check->stoppedAt.clear();
    bool fcScanComplete = true;
    if (field.isEmpty()) {
        QSet<NodeId> ancestors;
        const auto collect = [&](auto&& self,NodeId objectType,int depth) -> void {
            Tick();
            if (objectType == NoNode) { fcScanComplete = false; return; }
            if (depth >= 32 || ancestors.contains(objectType)) { fcScanComplete = false; out.limited = true; return; }
            ancestors.insert(objectType);
            for (auto n = doc.nodes[objectType].firstChild; n != NoNode; n = doc.nodes[n].nextSibling) {
                Tick(); if (!doc.nodes[n].isScl()) continue;
                if (doc.nodes[n].name == "DA") addFc(check->fcCandidates,Attr(n,"fc"));
                else if (doc.nodes[n].name == "SDO") self(self,typeOf(n,LinkKind::MemberType),depth+1);
            }
            ancestors.remove(objectType);
        };
        collect(collect,type,0);
    } else check->fcCandidates << effectiveFc;
    check->fcCandidates.removeDuplicates();
    if (!fcScanComplete) { stop(SclCheckState::Uncertain,QStringLiteral("对象下级类型/FC 未完整解析，不能宣告完整映射通过。")); return; }
    if (check->fc.isEmpty() || arrayMissing) {
        stop(SclCheckState::ContextRequired, arrayMissing ? QStringLiteral("路径存在；需补充数组选择。") : QStringLiteral("路径存在；需补充实际 FC 后核对映射。")); return;
    }
    if (field.isEmpty() && !check->fcCandidates.contains(check->fc)) {
        stop(fcScanComplete ? SclCheckState::Rejected : SclCheckState::Uncertain,
            fcScanComplete ? QStringLiteral("对象类型没有声明该 FC 的属性。") : QStringLiteral("对象下级类型未完整解析，不能否定此 FC。")); return;
    }
    // Resolve the catalog identity; failure under its resource budget is uncertainty, not absence.
    int signal = -1;
    const auto signalEntries = s.tasks.signalsByIed.constFind(s.browse.iedOwners[ln]);
    if (signalEntries != s.tasks.signalsByIed.cend()) for (auto i : *signalEntries) {
        Tick(); const auto& entry = s.tasks.catalog.instances[i];
        if (entry.logicalNode == ln && s.tasks.catalog.definitions[entry.definition].path == objectPath) {
            if (signal >= 0) { stop(SclCheckState::Uncertain,QStringLiteral("对象目录身份歧义。")); return; }
            signal = i;
        }
    }
    if (signal < 0) { stop(SclCheckState::Uncertain,QStringLiteral("有界对象目录未交付身份；不能据此否定路径。")); return; }
    const SclMmsScope scope{s.browse.iedOwners[ln],r.scope.accessPoint == NoNode ? Ancestor(ln,"AccessPoint") : r.scope.accessPoint,Ancestor(ln,"Server")};
    if (field.isEmpty()) {
        if (arrayDeclaration != NoNode) { stop(SclCheckState::Unsupported,QStringLiteral("对象级数组映射请使用数组查阅入口。")); return; }
        auto row = Signal(signal); row.addressScope = scope; row.logicalDevice = ld; row.objectPath = objectPath;
        row.domain = check->domain; row.fc = check->fc; row.state = LinkState::Resolved;
        row.shape = SclMmsAddressShape::ObjectMember; row.templatePath = path;
        ComposePlainAddress(row,check->logicalNode,objectPath.split('.'));
        check->target = row;
    } else {
        r.key = {SclMmsRecordKind::Address,ln,signal,0,field,effectiveFc,declaration,arrayDeclaration,arrayIndex};
        if (arrayDeclaration == NoNode) r.key.arrayIndex = -1;
        Addresses(signal);
        if (!reportAttribute || reportAttribute->state != LinkState::Resolved || out.limited) {
            stop(SclCheckState::Uncertain,QStringLiteral("类型路径存在，现有映射未交付唯一完整地址。")); return;
        }
        check->target = *reportAttribute;
    }
    const auto& target = *check->target;
    const auto canonical = object ? target.objectReference : target.item;
    const auto supplied = object ? check->domain + '/' + input : input;
    if (canonical != supplied || (!object && target.shape == SclMmsAddressShape::AlternateAccess)) {
        check->target.reset(); stop(SclCheckState::Unsupported,QStringLiteral("路径已解析，输入尚不能与现有映射完整重组一致；alternate access 组件需独立表示。")); return;
    }
    check->mapping = target.domain + '/' + target.item;
    check->model = QStringLiteral("唯一类型链支持此路径；结构/对象不代表标量读取。");
    check->state = SclCheckState::Supported; check->mappingPassed = true;
    out.objectEvidence = std::make_shared<const SclObjectEvidence>(ProjectSclObjectEvidence(s,target,[this] { Tick(); }));
    if (target.key.kind == SclMmsRecordKind::Address) {
        // All work, including reverse evidence, shares this Query's Tick budget.
        DataSetRelations(); check->dataSets = out.dataSetCoverage;
        for (const auto& row : out.rows) if (!check->coverageKinds.contains(row.reportMatch)) check->coverageKinds << row.reportMatch;
        out.rows.clear(); out.total = 0;
    } else {
        // Object-level coverage uses exactly the same FCDA trace matcher; no virtual leaves.
        const auto visit = [&](int index) {
            Tick(); const auto& ref = s.analysis.references[index];
            if (ref.state != LinkState::Resolved || ref.candidates.size() != 1) {
                if (CouldSclDataSetMemberAffect(s,target,ref,[this] { Tick(); })) ++check->dataSets.uncertainOccurrences;
                return;
            }
            const auto match = MatchReport(target,ref);
            if (match == SclMmsReportMatch::None) return;
            ++check->dataSets.observedMatches;
            if (!check->coverageKinds.contains(match)) check->coverageKinds << match;
        };
        const auto known = s.mmsIndex.dataSetMembersByLogicalNode.constFind(ln);
        if (known != s.mmsIndex.dataSetMembersByLogicalNode.cend()) for (auto index : *known) visit(index);
        const auto unknown = s.mmsIndex.uncertainMembersByServer.constFind(scope.server);
        if (unknown != s.mmsIndex.uncertainMembersByServer.cend()) for (auto index : *unknown) visit(index);
        check->dataSets.scanComplete = !s.mmsIndex.dataSetIndexLimited && !s.mmsIndex.limited;
        check->dataSets.conclusion = check->dataSets.observedMatches ? SclDataSetConclusion::Referenced
            : check->dataSets.scanComplete && !check->dataSets.uncertainOccurrences ? SclDataSetConclusion::NotReferenced : SclDataSetConclusion::Uncertain;
    }
}

SclMmsResult Query::Run()
{
    const bool evidenceQuery = r.kind == SclMmsQueryKind::ObjectEvidence || r.kind == SclMmsQueryKind::DataSetRelations
        || r.kind == SclMmsQueryKind::NodeEvidence || r.kind == SclMmsQueryKind::OfflineCheck;
    if (evidenceQuery && r.identityDocument != s.analysis.document) {
        out.limited = true; out.limitation = QStringLiteral("证据查询缺少对应快照身份，或身份已过期。"); return std::move(out);
    }
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
        case SclMmsQueryKind::OfflineCheck: OfflineCheck(); break;
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
        case SclMmsQueryKind::Attributes: Attributes(); break;
        case SclMmsQueryKind::AttributeReports: AttributeReports(); break;
        case SclMmsQueryKind::ObjectEvidence: ObjectEvidence(); break;
        case SclMmsQueryKind::NodeEvidence:
            if (Valid(r.target) && s.browse.InScope(r.target,r.scope.ied)
                && (s.browse.iedOwners[r.target] == NoNode || InScope(r.target)))
                out.objectEvidence = std::make_shared<const SclObjectEvidence>(ProjectSclNodeEvidence(s,r.target,[this] { Tick(); }));
            break;
        case SclMmsQueryKind::DataSetRelations: DataSetRelations(); break;
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
    } catch (const BudgetEnd& end) {
        out.limited = true; out.limitation = end.reason.isEmpty()
            ? QStringLiteral("查询工作预算 %1 已用尽；总量为已观察下界。").arg(r.workLimit) : end.reason;
        if (out.check) {
            auto check = std::make_shared<SclOfflineCheck>(*out.check);
            check->state = SclCheckState::Uncertain; check->mappingPassed = false; check->target.reset();
            out.check = std::move(check);
        }
    }
    if (s.mmsIndex.limited) { out.limited = true; out.limitation += QStringLiteral(" MMS 引用索引资源受限。"); }
    if (out.check && out.limited) {
        auto check = std::make_shared<SclOfflineCheck>(*out.check);
        check->state = SclCheckState::Uncertain; check->mappingPassed = false; check->target.reset();
        out.check = std::move(check);
    }
    if (r.locate && !out.locateFound) {
        out.rows.clear();
        out.limitation += QStringLiteral(" 未定位到所选身份；可能被范围或筛选遮挡，或查询已受限。");
    }
    // A check publishes layered evidence only. In particular, a budget failure
    // during reverse lookup must not trigger unrelated row/reference projections.
    if (r.kind == SclMmsQueryKind::OfflineCheck) { out.rows.clear(); out.total = 0; }
    out.hasMore = out.total > qint64(out.request.offset) + qint64(out.rows.size()) || out.limited;
    if (out.check) {
        auto check = std::make_shared<SclOfflineCheck>(*out.check); check->workUsed = work; out.check = std::move(check);
    }
    checkpoint(task);
    return std::move(out);
}
} // namespace

SclMmsResult QuerySclMms(const SclSnapshot& s, const SclMmsRequest& request, TaskControl* task)
{
    QElapsedTimer timer; if (request.kind == SclMmsQueryKind::OfflineCheck) timer.start();
    auto result = Query(s, request, task).Run();
    for (auto& row : result.rows)
        row.references = ProjectSclMmsReferences(s, row, task);
    if (request.presentation && request.kind != SclMmsQueryKind::Detail) PresentSclMms(s, result, task);
    if (result.check) {
        auto check = std::make_shared<SclOfflineCheck>(*result.check); check->elapsedUs = timer.nsecsElapsed()/1000;
        result.check = std::move(check);
    }
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
