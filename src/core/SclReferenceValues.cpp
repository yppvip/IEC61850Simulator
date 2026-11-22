#include "SclReferenceValues.hpp"

#include <QRegularExpression>
#include <algorithm>

namespace iec61850sim
{
using namespace Scl;
using Kind = SclReferenceValueKind;

QString SclReferenceLabel(Kind kind)
{
    switch (kind) {
    case Kind::ObjectReference: return QStringLiteral("对象引用");
    case Kind::FunctionalConstraint: return QStringLiteral("功能约束");
    case Kind::MmsDomain: return QStringLiteral("MMS Domain");
    case Kind::MmsItem: return QStringLiteral("MMS Item");
    case Kind::ArrayIndex: return QStringLiteral("有效数组下标");
    case Kind::AlternateAccessComponent: return QStringLiteral("alternate access 组件");
    case Kind::RawFcdaDeclaration: return QStringLiteral("原始 FCDA 声明");
    case Kind::RawArrayIndex: return QStringLiteral("原始 ix");
    case Kind::TypeReference: return QStringLiteral("类型引用");
    case Kind::DataSetReference: return QStringLiteral("数据集引用");
    case Kind::ReportDefinition: return QStringLiteral("报告定义引用");
    case Kind::ExpectedReportInstance: return QStringLiteral("预期报告实例引用");
    case Kind::RawReportId: return QStringLiteral("原始 rptID");
    case Kind::MmsDataSetList: return QStringLiteral("MMS 数据集列表名");
    case Kind::ObjectPath: return QStringLiteral("工程对象路径");
    case Kind::TypeIdentifier: return QStringLiteral("类型标识");
    case Kind::DataSetDeclaration: return QStringLiteral("数据集原始名称");
    case Kind::ControlPath: return QStringLiteral("控制块路径");
    case Kind::SourceControlPath: return QStringLiteral("源控制块路径");
    case Kind::CandidateControlPath: return QStringLiteral("候选控制块路径");
    case Kind::RawExtRef: return QStringLiteral("原始 ExtRef 声明");
    case Kind::RawReference: return QStringLiteral("原始引用声明");
    case Kind::CandidatePath: return QStringLiteral("候选路径");
    case Kind::InternalAddress: return QStringLiteral("原始内部地址");
    case Kind::AppId: return QStringLiteral("原始 appID");
    case Kind::SmvId: return QStringLiteral("原始 smvID");
    case Kind::NumericAppId: return QStringLiteral("数值 APPID");
    case Kind::MacAddress: return QStringLiteral("MAC 地址");
    case Kind::VlanId: return QStringLiteral("VLAN ID");
    case Kind::VlanPriority: return QStringLiteral("VLAN 优先级");
    case Kind::NetworkAddress: return QStringLiteral("网络地址参数");
    case Kind::EndpointPath: return QStringLiteral("端点路径");
    case Kind::IedName: return QStringLiteral("IED 名称");
    case Kind::AccessPointName: return QStringLiteral("访问点名称");
    case Kind::HitValue: return QStringLiteral("命中属性原值");
    }
    return {};
}

bool CanTraceSclReference(Kind kind)
{
    return kind == Kind::ObjectReference || kind == Kind::FunctionalConstraint || kind == Kind::MmsDomain
        || kind == Kind::MmsItem || kind == Kind::TypeReference;
}

namespace
{
std::optional<Kind> ParameterKind(const QString& type)
{
    if (type == "APPID") return Kind::NumericAppId;
    if (type == "MAC-Address") return Kind::MacAddress;
    if (type == "VLAN-ID") return Kind::VlanId;
    if (type == "VLAN-PRIORITY") return Kind::VlanPriority;
    return type.isEmpty() ? std::nullopt : std::optional(Kind::NetworkAddress);
}

std::optional<Kind> AttributeKind(const QString& name, const QString& element)
{
    if (name == "fc") return Kind::FunctionalConstraint;
    if (name == "ix") return Kind::RawArrayIndex;
    if (name == "lnType" || (name == "type" && (element == "DO" || element == "SDO" || element == "DA" || element == "BDA")))
        return Kind::TypeReference;
    if (name == "datSet" || (name == "name" && element == "DataSet")) return Kind::DataSetDeclaration;
    if (name == "iedName" || (name == "name" && element == "IED")) return Kind::IedName;
    if (name == "apName" || name == "apRef" || (name == "name" && element == "AccessPoint")) return Kind::AccessPointName;
    if (name == "rptID") return Kind::RawReportId;
    if (name == "appID" && element == "GSEControl") return Kind::AppId;
    if (name == "smvID" && element == "SampledValueControl") return Kind::SmvId;
    if (name == "intAddr" && element == "ExtRef") return Kind::InternalAddress;
    return std::nullopt;
}

class Projection
{
public:
    Projection(const SclSnapshot& snapshot, TaskControl* task, bool includeTrace)
        : snapshot(snapshot), doc(*snapshot.analysis.document), task(task), includeTrace(includeTrace) {}
    SclReferenceValues Run(const SclReferenceRequest& request);

private:
    const SclSnapshot& snapshot;
    const Document& doc;
    TaskControl* task;
    bool includeTrace;
    SclReferenceValues out;
    bool Valid(NodeId node) const { return node >= 0 && node < int(doc.nodes.size()); }
    QString Attr(NodeId node, const char* attr) const { return Valid(node) ? doc.attribute(node, QLatin1String(attr)) : QString{}; }
    QString Ln(NodeId node) const {
        return doc.nodes[node].name == "LN0" ? QStringLiteral("LLN0") : Attr(node, "prefix") + Attr(node, "lnClass") + Attr(node, "inst");
    }
    void Add(SclReferenceValue value, bool allowTrace = true);
    void Derived(Kind kind, const QString& value, NodeId node);
    void Raw(Kind kind, const QString& value, NodeId node, const QString& attr = {});
    void Tag(Kind kind, NodeId node);
    void Path(Kind kind, NodeId node);
    void DataSet(NodeId node);
    void Node(NodeId node);
    void Link(int record, bool showCandidates);
    void Signal(int record);
    void Field(const SclReferenceRequest& request);
    void Hit(const SclHit& hit);
    void Parameters(NodeId node);
    void Mms(const SclReferenceRequest& request);
};

void Projection::Add(SclReferenceValue value, bool allowTrace)
{
    checkpoint(task);
    if (value.value.isEmpty()) return;
    if (includeTrace && allowTrace && CanTraceSclReference(value.kind)) {
        const auto source = std::find_if(out.traceEntries.begin(), out.traceEntries.end(), [&](const auto& entry) {
            if (entry.kind != value.kind || entry.value != value.value || entry.evidence.node != value.evidence.node
                || entry.evidence.attribute != value.evidence.attribute) return false;
            if (!entry.basis || !value.basis) return true;
            const auto& a = *entry.basis;
            const auto& b = *value.basis;
            // Menu providers may supplement a prepared member with its raw link.
            // The same declaration can only add an alternate view when both contexts differ.
            if (a.reference >= 0 && a.reference == b.reference
                && (a.report == b.report || a.report == NoNode || b.report == NoNode)
                && (a.scope == b.scope || a.scope == SclMmsScope{} || b.scope == SclMmsScope{}))
                return true;
            return a.scope == b.scope && a.logicalNode == b.logicalNode && a.path == b.path
                && a.reference == b.reference && a.report == b.report && a.instance == b.instance
                && a.arrayIndex == b.arrayIndex && a.selector == b.selector;
        });
        if (source != out.traceEntries.end()) {
            if (!source->basis && value.basis) *source = value;
        } else if (out.traceEntries.size() < 64) {
            out.traceEntries.push_back(value);
        } else {
            out.traceLimited = true;
        }
    }
    if (std::any_of(out.entries.cbegin(), out.entries.cend(), [&](const auto& entry) {
        return entry.kind == value.kind && entry.value == value.value;
    })) return;
    if (out.entries.size() >= 64) { out.limited = true; return; }
    out.entries.push_back(std::move(value));
}

void Projection::Derived(Kind kind, const QString& value, NodeId node)
{
    Add({kind, value, {node, {}, {}}, MmsValueState::Derived});
}

void Projection::Raw(Kind kind, const QString& value, NodeId node, const QString& attr)
{
    Add({kind, value, {node, attr, {}}, MmsValueState::Explicit});
}

void Projection::Tag(Kind kind, NodeId node)
{
    Raw(kind, snapshot.analysis.source->openingTag(doc.nodes[node].source, task), node);
}

void Projection::Path(Kind kind, NodeId node)
{
    if (!Valid(node)) return;
    auto path = SclTaskPath(doc, node, true);
    const auto id = Attr(node, "id");
    if (path.isEmpty() && !id.isEmpty()) path = doc.nodes[node].name + '/' + id;
    Derived(kind, path, node);
}

void Projection::DataSet(NodeId node)
{
    if (!Valid(node) || doc.nodes[node].name != "DataSet") return;
    const auto ln = doc.nodes[node].parent;
    const auto name = Attr(node, "name");
    Raw(Kind::DataSetDeclaration, name, node, QStringLiteral("name"));
    if (!Valid(ln) || (doc.nodes[ln].name != "LN" && doc.nodes[ln].name != "LN0")) return;
    const auto ld = doc.nodes[ln].parent;
    const auto domain = snapshot.mmsIndex.domainsByDevice.constFind(ld);
    if (domain == snapshot.mmsIndex.domainsByDevice.cend() || domain->state != MmsValueState::Derived) return;
    const auto logicalName = Ln(ln);
    static const QRegularExpression segment(QStringLiteral("^[A-Za-z0-9_]+$"));
    if (!segment.match(logicalName).hasMatch() || !segment.match(name).hasMatch()) return;
    int sameLn = 0, sameSet = 0;
    for (auto child = doc.nodes[ld].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
        checkpoint(task);
        if (doc.nodes[child].isScl() && (doc.nodes[child].name == "LN" || doc.nodes[child].name == "LN0") && Ln(child) == logicalName)
            ++sameLn;
    }
    for (auto child = doc.nodes[ln].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
        checkpoint(task);
        if (doc.nodes[child].isScl() && doc.nodes[child].name == "DataSet" && Attr(child, "name") == name)
            ++sameSet;
    }
    if (sameLn != 1 || sameSet != 1) return;
    const auto item = logicalName + '$' + name;
    Derived(Kind::DataSetReference, domain->name + '/' + item, node);
    Derived(Kind::MmsDataSetList, item, node);
}

void Projection::Parameters(NodeId node)
{
    const auto& element = doc.nodes[node];
    if (element.name == "P") {
        if (const auto kind = ParameterKind(Attr(node, "type"))) Raw(*kind, element.text, node);
        return;
    }
    for (auto child = element.firstChild; child != NoNode && !out.limited; child = doc.nodes[child].nextSibling) {
        checkpoint(task);
        if (doc.nodes[child].isScl() && (doc.nodes[child].name == "Address" || doc.nodes[child].name == "P"))
            Parameters(child);
    }
}

void Projection::Link(int record, bool showCandidates)
{
    if (record < 0 || record >= int(snapshot.analysis.references.size())) return;
    const auto& ref = snapshot.analysis.references[record];
    const auto& source = doc.nodes[ref.source];
    const auto candidate = ref.state == LinkState::Resolved && ref.candidates.size() == 1 ? ref.candidates.front() : NoNode;
    SclMmsRow mms;
    mms.key = {SclMmsRecordKind::Reference, ref.source, record};
    for (auto& value : ProjectSclMmsReferences(snapshot, mms, task, true)) Add(std::move(value));

    switch (ref.kind) {
    case LinkKind::Fcda: DataSet(source.parent); break;
    case LinkKind::LogicalNodeType: Raw(Kind::TypeReference, Attr(ref.source, "lnType"), ref.source, "lnType"); break;
    case LinkKind::MemberType: Raw(Kind::TypeReference, Attr(ref.source, "type"), ref.source, "type"); break;
    case LinkKind::ControlDataSet:
        Raw(Kind::DataSetDeclaration, Attr(ref.source, "datSet"), ref.source, "datSet");
        DataSet(candidate);
        break;
    case LinkKind::ExtRefData:
    case LinkKind::ExtRefControl:
        Tag(Kind::RawExtRef, ref.source);
        Raw(Kind::InternalAddress, Attr(ref.source, "intAddr"), ref.source, "intAddr");
        if (ref.kind == LinkKind::ExtRefControl) Path(Kind::SourceControlPath, candidate);
        break;
    case LinkKind::ConnectedAp:
        Raw(Kind::IedName, Attr(ref.source, "iedName"), ref.source, "iedName");
        Raw(Kind::AccessPointName, Attr(ref.source, "apName"), ref.source, "apName");
        Path(Kind::EndpointPath, candidate);
        break;
    case LinkKind::NetworkControl:
        Tag(Kind::RawReference, ref.source);
        Path(Kind::ControlPath, candidate);
        break;
    }
    if (showCandidates && candidate == NoNode) {
        const auto kind = ref.kind == LinkKind::ExtRefControl || ref.kind == LinkKind::NetworkControl
            ? Kind::CandidateControlPath : Kind::CandidatePath;
        for (const auto node : ref.candidates) {
            checkpoint(task);
            Path(kind, node);
            if (out.limited) break;
        }
    }
}

void Projection::Node(NodeId id)
{
    if (!Valid(id) || !doc.nodes[id].isScl()) return;
    const auto& node = doc.nodes[id];
    const auto& name = node.name;
    for (int i = node.firstAttribute; i < node.firstAttribute + node.attributeCount; ++i) {
        checkpoint(task);
        const auto& attr = doc.attributes[i];
        if (attr.namespaceUri.isEmpty())
            if (const auto kind = AttributeKind(attr.name, name)) Raw(*kind, attr.value, id, attr.name);
    }
    if (name == "LNodeType" || name == "DOType" || name == "DAType" || name == "EnumType")
        Raw(Kind::TypeIdentifier, Attr(id, "id"), id, "id");
    else if (name == "ClientLN" || name == "Association" || name == "ServerAt")
        Tag(Kind::RawReference, id);
    else if (name == "ReportControl") {
        SclMmsRow row;
        row.key = {SclMmsRecordKind::Report, id};
        for (auto& value : ProjectSclMmsReferences(snapshot, row, task)) Add(std::move(value));
    } else if (name == "GSEControl" || name == "SampledValueControl") {
        Path(Kind::ControlPath, id);
        const auto endpoints = snapshot.analysis.incoming.equal_range(id);
        for (auto it = endpoints.first; it != endpoints.second && !out.limited; ++it) {
            checkpoint(task);
            const auto& ref = snapshot.analysis.references[it.value()];
            if (ref.kind == LinkKind::NetworkControl) Parameters(ref.source);
        }
    } else if (name == "DataSet") {
        DataSet(id);
    } else if (name == "ConnectedAP" || name == "GSE" || name == "SMV" || name == "Address" || name == "P") {
        Parameters(id);
        if (name != "P" && name != "Address") {
            const auto ap = name == "ConnectedAP" ? id : node.parent;
            const auto& parent = doc.nodes[ap];
            if (parent.name == "ConnectedAP") {
                QStringList parts{Attr(parent.parent, "name"), Attr(ap, "iedName"), Attr(ap, "apName")};
                if (ap != id) parts << Attr(id, "ldInst") << Attr(id, "cbName");
                if (!parts.contains(QString())) Derived(Kind::EndpointPath, parts.join('/'), id);
            }
        }
    } else if (name == "LN" || name == "LN0" || name == "DOI" || name == "SDI" || name == "DAI") {
        if (snapshot.browse.iedOwners[id] != NoNode) Path(Kind::ObjectPath, id);
        if (name == "DAI") {
            SclMmsRequest query;
            query.kind = SclMmsQueryKind::Attributes;
            query.scope.ied = snapshot.browse.iedOwners[id];
            query.target = id;
            query.pageSize = 16;
            auto result = QuerySclMms(snapshot, query, task);
            for (const auto& row : result.rows)
                for (const auto& value : row.references) Add(value);
            out.limited |= result.limited || result.hasMore;
        }
    }
    const auto links = snapshot.analysis.outgoing.equal_range(id);
    bool declaration = false;
    for (auto it = links.first; it != links.second && !out.limited; ++it) {
        checkpoint(task);
        declaration = true;
        Link(it.value(), true);
    }
    if (!declaration && name == "FCDA") { Tag(Kind::RawFcdaDeclaration, id); DataSet(node.parent); }
    if (!declaration && name == "ExtRef") Tag(Kind::RawExtRef, id);
}

void Projection::Signal(int record)
{
    if (record < 0 || record >= int(snapshot.tasks.catalog.instances.size())) return;
    const auto& instance = snapshot.tasks.catalog.instances[record];
    const auto& definition = snapshot.tasks.catalog.definitions[instance.definition];
    const auto path = SclTaskPath(doc, instance.logicalNode, true) + '.' + definition.path;
    Derived(Kind::ObjectPath, path, instance.logicalNode);
    Raw(Kind::TypeReference, Attr(instance.logicalNode, "lnType"), instance.logicalNode, "lnType");
    Raw(Kind::TypeReference, Attr(definition.declaration, "type"), definition.declaration, "type");
    SclMmsRequest query;
    query.kind = SclMmsQueryKind::Addresses;
    query.signal = record;
    query.pageSize = 16;
    const auto addresses = QuerySclMms(snapshot, query, task);
    const auto mapped = std::find_if(addresses.rows.cbegin(), addresses.rows.cend(), [](const auto& row) {
        return !row.objectReference.isEmpty();
    });
    if (mapped != addresses.rows.cend()) {
        auto basis = std::make_shared<SclReferenceBasis>();
        basis->scope = mapped->addressScope;
        basis->logicalNode = instance.logicalNode;
        for (const auto node : mapped->templatePath) {
            if (doc.nodes[node].name != "DO" && doc.nodes[node].name != "SDO") break;
            basis->path.push_back(node);
        }
        Add({Kind::ObjectReference, mapped->objectReference.chopped(mapped->key.field.size() + 1),
            {definition.declaration, {}, {}}, MmsValueState::Derived, basis});
        Add({Kind::MmsDomain, mapped->domain, mapped->domainEvidence, MmsValueState::Derived, std::move(basis)});
    }
    out.limited |= addresses.limited;
}

void Projection::Mms(const SclReferenceRequest& request)
{
    for (const auto& value : request.prepared) Add(value);
    const auto& key = request.mms;
    switch (key.kind) {
    case SclMmsRecordKind::Signal: Signal(key.record); break;
    case SclMmsRecordKind::Member:
    case SclMmsRecordKind::Reference:
        if (key.record >= 0) Link(key.record, true);
        else Node(key.node);
        break;
    case SclMmsRecordKind::Address:
    case SclMmsRecordKind::Instance: break;
    case SclMmsRecordKind::Client:
        Node(key.node);
        if (key.record >= 0 && key.record < int(snapshot.mms.clients.size())) {
            const auto& target = snapshot.mms.clients[key.record].target;
            if (const auto id = target.target(); id != NoNode) {
                if (doc.nodes[id].name == "ReportControl") Path(Kind::SourceControlPath, id);
                else Path(Kind::ObjectPath, id);
            } else for (const auto id : target.candidates) {
                Path(Kind::CandidatePath, id);
                if (out.limited) break;
            }
        }
        break;
    default: Node(key.node); break;
    }
}

void Projection::Field(const SclReferenceRequest& request)
{
    const auto& field = request.field;
    const auto node = field.evidence.node;
    if (!field.present || field.value.isEmpty() || !Valid(node)) return;
    if (field.key.startsWith('@')) {
        if (const auto kind = AttributeKind(field.evidence.attribute, doc.nodes[node].name)) {
            const auto unqualified = field.key.lastIndexOf(':') <= 1;
            Add({*kind, field.value, field.evidence, MmsValueState::Explicit}, unqualified);
        }
    } else if (field.key.startsWith("candidate.")) {
        const auto index = request.mms.record;
        if (index >= 0 && index < int(snapshot.analysis.references.size())
            && (request.mms.kind == SclMmsRecordKind::Member || request.mms.kind == SclMmsRecordKind::Reference)
            && snapshot.analysis.references[index].state == LinkState::Resolved) Path(Kind::ObjectPath, node);
        else Path(Kind::CandidatePath, node);
    } else if (field.key == "report.dataset") {
        DataSet(node);
    } else if (field.key == "ln" || field.key.startsWith("path.")) {
        Node(node);
    } else {
        static const QHash<QString, Kind> fieldKinds{{"domain", Kind::MmsDomain}, {"item", Kind::MmsItem},
            {"object", Kind::ObjectReference}, {"fc", Kind::FunctionalConstraint}, {"array", Kind::ArrayIndex},
            {"component", Kind::AlternateAccessComponent}, {"definition", Kind::ReportDefinition},
            {"dataset", Kind::DataSetReference}, {"list", Kind::MmsDataSetList}, {"instance", Kind::ExpectedReportInstance},
            {"rptID", Kind::RawReportId}};
        const auto kind = fieldKinds.constFind(field.key);
        if (kind != fieldKinds.cend())
            for (const auto& value : request.prepared)
                if (value.kind == *kind && value.value == field.value) Add(value);
    }
}

void Projection::Hit(const SclHit& hit)
{
    if (!Valid(hit.node)) return;
    switch (hit.field) {
    case MatchField::Reference: Link(hit.detail, true); break;
    case MatchField::Address: Node(hit.detail); break;
    case MatchField::Owner: {
        const auto owner = snapshot.browse.iedOwners[hit.node];
        Raw(Kind::IedName, Attr(owner, "name"), owner, "name");
        break;
    }
    case MatchField::Attribute: {
        const auto& attr = doc.attributes[hit.detail];
        const auto kind = attr.namespaceUri.isEmpty() ? AttributeKind(attr.name, doc.nodes[hit.node].name) : std::nullopt;
        Raw(kind.value_or(Kind::HitValue), attr.value, hit.node, attr.name);
        Node(hit.node);
        break;
    }
    default: Node(hit.node); break;
    }
}

SclReferenceValues Projection::Run(const SclReferenceRequest& request)
{
    checkpoint(task);
    switch (request.anchor) {
    case SclReferenceAnchor::Node: Node(request.node); break;
    case SclReferenceAnchor::Link: Link(request.record, true); break;
    case SclReferenceAnchor::Candidate: Path(Kind::CandidatePath, request.node); break;
    case SclReferenceAnchor::Signal: Signal(request.record); break;
    case SclReferenceAnchor::Mms: Mms(request); break;
    case SclReferenceAnchor::Field: Field(request); break;
    case SclReferenceAnchor::Hit: Hit(request.hit); break;
    }
    if (request.input != NoNode) Node(request.input);
    if (request.control != NoNode)
        Path(request.candidateInput ? Kind::CandidateControlPath : Kind::SourceControlPath, request.control);
    out.limited |= request.limited;
    return std::move(out);
}
}

SclReferenceValues CollectSclReferences(const SclSnapshot& snapshot, const SclReferenceRequest& request, TaskControl* task,
    bool includeTrace)
{
    return Projection(snapshot, task, includeTrace).Run(request);
}
} // namespace iec61850sim
