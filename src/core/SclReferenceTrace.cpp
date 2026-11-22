#include "SclReferenceTrace.hpp"

#include <algorithm>

namespace iec61850sim
{
namespace
{
using namespace Scl;
using Kind = SclReferenceValueKind;
using Branch = SclTraceBranch;

QString FcMeaning(const QString& fc)
{
    if (fc == "MX") return QStringLiteral("测量量");
    if (fc == "ST") return QStringLiteral("状态信息");
    if (fc == "SP") return QStringLiteral("设定值");
    if (fc == "CO") return QStringLiteral("控制");
    if (fc == "CF") return QStringLiteral("配置");
    if (fc == "SG" || fc == "SE") return QStringLiteral("定值组");
    return QStringLiteral("IEC 61850 功能约束");
}

class Projection
{
public:
    Projection(const SclSnapshot& snapshot, const SclReferenceValue& value, TaskControl* task)
        : snap(snapshot), doc(*snap.analysis.document), root(value), task(task) {}
    SclReferenceTrace Run(const SclTraceNode&);

private:
    bool Valid(NodeId id) const { return id >= 0 && id < int(doc.nodes.size()); }
    QString Attr(NodeId id, const QString& key) const { return Valid(id) ? doc.attribute(id, key) : QString(); }
    bool Is(NodeId id, const char* tag) const { return Valid(id) && doc.nodes[id].isScl() && doc.nodes[id].name == QLatin1String(tag); }
    NodeId Parent(NodeId id) const { return Valid(id) ? doc.nodes[id].parent : NoNode; }
    NodeId Ancestor(NodeId, const char*) const;
    QString Path(NodeId) const;
    bool Tick();
    const Reference* TypeLink(NodeId);
    SclTraceNode Node(QString label, QString value, QString note, QString relation = {}, NodeId source = NoNode,
        const QString& attribute = {}, Branch branch = Branch::None) const;
    void Attribute(NodeId, const QString&, const QString& label, const QString& relation);
    void Add(SclTraceNode);
    void Context();
    void Root();
    void Address();
    void LogicalDevice();
    void LogicalNode();
    void FunctionalConstraint();
    void Type(NodeId);
    void TypeDefinition(NodeId);
    void Scope();
    void Member();
    void Array();
    void ReportName();
    QString Domain() const;
    QString LnName() const;
    QString EffectiveFc() const;
    QString ScopeName() const;

    const SclSnapshot& snap;
    const Document& doc;
    const SclReferenceValue& root;
    TaskControl* task;
    SclReferenceTrace out;
    SclMmsScope scope;
    NodeId ln = NoNode, ld = NoNode, report = NoNode, selector = NoNode, axis = NoNode, fcda = NoNode, input = NoNode;
    NodeId da = NoNode;
    int index = -1, instance = -1;
    QString component;
    QList<NodeId> path;
    const Reference* reference = nullptr;
    const FcdaTrace* trace = nullptr;
    int work = 0;
};

NodeId Projection::Ancestor(NodeId id, const char* tag) const
{
    for (int depth = 0; Valid(id) && depth < 64; ++depth, id = Parent(id))
        if (Is(id, tag)) return id;
    return NoNode;
}

QString Projection::Path(NodeId id) const
{
    QStringList parts;
    for (; Valid(id) && parts.size() < 48; id = Parent(id)) {
        auto key = Is(id, "LN") ? Attr(id, "prefix") + Attr(id, "lnClass") + Attr(id, "inst")
            : Is(id, "LN0") ? QStringLiteral("LLN0") : Attr(id, "name");
        if (key.isEmpty()) key = Attr(id, "id");
        if (key.isEmpty()) key = Attr(id, "inst");
        parts.prepend(key.isEmpty() ? doc.nodes[id].name : doc.nodes[id].name + '[' + key + ']');
    }
    if (Valid(id)) parts.prepend(QStringLiteral("…"));
    return parts.join(QStringLiteral(" / "));
}

bool Projection::Tick()
{
    checkpoint(task);
    if (++work <= 10000) return true;
    out.limited = true;
    return false;
}

const Reference* Projection::TypeLink(NodeId id)
{
    const auto links = snap.analysis.outgoing.equal_range(id);
    for (auto it = links.first; it != links.second && Tick(); ++it) {
        const auto& link = snap.analysis.references[*it];
        if (link.kind == LinkKind::LogicalNodeType || link.kind == LinkKind::MemberType) return &link;
    }
    return nullptr;
}

SclTraceNode Projection::Node(QString label, QString value, QString note, QString relation,
    NodeId source, const QString& attribute, Branch branch) const
{
    SclTraceNode node{std::move(label), std::move(value), std::move(note), std::move(relation), source, attribute};
    node.branch = branch;
    if (Valid(source)) {
        node.location = doc.nodes[source].source;
        node.path = Path(source);
        if (snap.analysis.source && node.location.line > 0) {
            const auto line = snap.analysis.source->line(node.location.line);
            node.excerpt = line.text;
            while (node.excerpt.endsWith('\n') || node.excerpt.endsWith('\r')) node.excerpt.chop(1);
            if (line.truncated) node.excerpt += QStringLiteral("…");
        }
    }
    return node;
}

void Projection::Add(SclTraceNode node)
{
    if (out.children.size() < SclReferenceTrace::VisibleLimit - 1)
        out.children.push_back(std::move(node));
    else
        out.limited = true;
}

void Projection::Attribute(NodeId node, const QString& attribute, const QString& label, const QString& relation)
{
    if (!Valid(node)) return;
    const auto value = Attr(node, attribute);
    Add(Node(label, value, value.isNull() ? QStringLiteral("文件未声明此属性。")
        : value.isEmpty() ? QStringLiteral("文件显式声明了空值。") : QStringLiteral("取自文件属性。"),
        relation, node, attribute));
}

void Projection::Context()
{
    int record = -1;
    if (root.basis) {
        const auto& basis = *root.basis;
        scope = basis.scope;
        ln = basis.logicalNode;
        report = basis.report;
        instance = basis.instance;
        path = basis.path;
        da = basis.fc.node;
        axis = basis.arrayDeclaration;
        selector = basis.selector;
        index = basis.arrayIndex;
        component = basis.component;
        record = basis.reference;
    }
    if (Is(root.evidence.node, "FCDA")) {
        fcda = root.evidence.node;
        if (record < 0) record = snap.mmsIndex.memberReferences.value(fcda, -1);
    }
    if (record >= 0 && record < int(snap.analysis.references.size())) {
        reference = &snap.analysis.references[record];
        ln = reference->logicalNode;
        if (reference->kind == LinkKind::Fcda || reference->kind == LinkKind::ExtRefData) {
            const auto source = reference->source;
            if (reference->kind == LinkKind::Fcda) fcda = source;
            else input = source;
            index = reference->arrayIndex;
            component = reference->mmsComponent;
            selector = source;
            const auto& traces = fcda == NoNode ? snap.analysis.extRefTraces : snap.analysis.fcdaTraces;
            const auto evidence = traces.constFind(source);
            if (evidence != traces.cend()) {
                trace = &*evidence;
                for (const auto& step : trace->steps) {
                    if (!Tick()) break;
                    switch (step.field) {
                    case FcdaField::LogicalNode: ln = step.node; break;
                    case FcdaField::LdInst: ld = step.node; break;
                    case FcdaField::DoName:
                    case FcdaField::DaName: path.push_back(step.node); break;
                    case FcdaField::Fc:
                        if (!Attr(source, "daName").isEmpty() && Is(step.node, "DA")) da = step.node;
                        break;
                    case FcdaField::Ix: axis = step.node; break;
                    default: break;
                    }
                }
            }
        }
    }
    if (ln == NoNode && !reference)
        ln = Ancestor(report == NoNode ? root.evidence.node : report, "LN");
    if (ln == NoNode && !reference)
        ln = Ancestor(report == NoNode ? root.evidence.node : report, "LN0");
    if (ld == NoNode) ld = Ancestor(ln, "LDevice");
    if (const auto server = Ancestor(ld == NoNode ? fcda : ld, "Server"); server != NoNode) {
        const auto ied = Ancestor(server, "IED");
        if ((scope.server != NoNode && scope.server != server)
            || (scope.accessPoint != NoNode && Ancestor(scope.accessPoint, "IED") != ied))
            scope.accessPoint = NoNode;
        scope.server = server;
        scope.ied = ied;
    }
    if (scope.accessPoint == NoNode) scope.accessPoint = Ancestor(scope.server, "AccessPoint");
    if (scope.ied == NoNode) scope.ied = Ancestor(scope.server, "IED");
}

QString Projection::Domain() const
{
    const auto explicitName = Attr(ld, "ldName");
    return explicitName.isEmpty() ? Attr(Ancestor(ld, "IED"), "name") + Attr(ld, "inst") : explicitName;
}

QString Projection::LnName() const
{
    return Is(ln, "LN0") ? QStringLiteral("LLN0") : Attr(ln, "prefix") + Attr(ln, "lnClass") + Attr(ln, "inst");
}

QString Projection::EffectiveFc() const
{
    const auto definition = Attr(da, "fc");
    return definition.isEmpty() ? Attr(fcda, "fc") : definition;
}

QString Projection::ScopeName() const
{
    QStringList parts;
    if (Valid(scope.ied)) parts << QStringLiteral("IED %1").arg(Attr(scope.ied, "name"));
    if (Valid(scope.accessPoint)) parts << QStringLiteral("AP %1").arg(Attr(scope.accessPoint, "name"));
    if (Valid(scope.server)) parts << QStringLiteral("Server");
    if (Valid(ld)) parts << QStringLiteral("LD %1").arg(Attr(ld, "inst"));
    return parts.join(QStringLiteral(" / "));
}

void Projection::Root()
{
    auto note = ScopeName();
    if (root.kind == Kind::MmsDomain || root.kind == Kind::MmsItem)
        note += QStringLiteral("\n文件模型的离线映射。在线未验证。");
    if (root.basis && !reference && !root.basis->limitation.isEmpty())
        note += QStringLiteral("\n%1 · %2").arg(SclMmsStateText(root.basis->state), root.basis->limitation);
    out.node = Node(SclReferenceLabel(root.kind), root.value, note.trimmed(), {},
        root.evidence.node, root.evidence.attribute, Branch::Root);
    switch (root.kind) {
    case Kind::ObjectReference:
    case Kind::MmsItem: Address(); break;
    case Kind::MmsDomain:
        if (Valid(ld) && Domain() == root.value)
            Add(Node(QStringLiteral("LDName"), root.value, QStringLiteral("此映射的 MMS Domain 使用逻辑设备名称。"),
                QStringLiteral("映射"), ld, {}, Branch::LogicalDevice));
        break;
    case Kind::FunctionalConstraint: FunctionalConstraint(); break;
    case Kind::TypeReference: Type(root.evidence.node); break;
    default: break;
    }
    if (out.children.empty())
        Add(Node(QStringLiteral("依据受限"), {}, QStringLiteral("当前投影未提供完整组成证据。"), QStringLiteral("说明")));
}

void Projection::Address()
{
    if (!Valid(ln)) return;
    const bool item = root.kind == Kind::MmsItem;
    const bool reportItem = item && Valid(report) && !reference;
    const auto name = LnName();
    const auto fc = reportItem ? (Attr(report, "buffered") == "true" || Attr(report, "buffered") == "1"
        ? QStringLiteral("BR") : QStringLiteral("RP")) : EffectiveFc();
    QString controlName = Attr(report, "name");
    if (instance >= 0 && instance < int(snap.mms.instances.size()))
        controlName = snap.mms.instances[instance].name;
    QStringList parts;
    for (const auto declaration : path) {
        auto part = Attr(declaration, "name");
        if (declaration == axis && index >= 0 && !item) part += '(' + QString::number(index) + ')';
        parts << part;
        if (item && declaration == axis && index >= 0) break;
    }
    const auto composed = item ? name + '$' + fc + '$' + (reportItem ? controlName : parts.join('$'))
        : Domain() + '/' + name + '.' + parts.join('.');
    if (composed != root.value) return;
    const bool object = path.isEmpty() || Is(path.back(), "DO") || Is(path.back(), "SDO");
    if (reportItem)
        out.node.explanation += instance >= 0 ? QStringLiteral("\n层级 · 预期报告实例") : QStringLiteral("\n层级 · 报告定义");
    else if (index >= 0)
        out.node.explanation += QStringLiteral("\n层级 · 数组元素或其成员");
    else
        out.node.explanation += object ? QStringLiteral("\n层级 · 数据对象")
            : Attr(path.back(), "bType") == "Struct" ? QStringLiteral("\n层级 · 结构属性") : QStringLiteral("\n层级 · 属性");
    out.node.explanation += item
        ? QStringLiteral("\n名称片段按顺序用 $ 连接。")
        : QStringLiteral("\n先写 LDName 和 /，再用 . 连接 LNName 和对象／属性片段。");
    if (!item) {
        Add(Node(QStringLiteral("LDName"), Domain(), QStringLiteral("逻辑设备名称。"), QStringLiteral("组成"),
            ld, {}, Branch::LogicalDevice));
    }
    Add(Node(QStringLiteral("LNName"), name, QStringLiteral("当前作用域内的逻辑节点名称。"), QStringLiteral("组成"),
        ln, {}, Branch::LogicalNode));
    if (item) {
        if (reportItem) {
            Add(Node(QStringLiteral("报告映射类别"), fc, QStringLiteral("按当前报告定义的 buffered 声明映射。"),
                QStringLiteral("组成"), report, QStringLiteral("buffered")));
            Add(Node(QStringLiteral("报告名称"), controlName,
                instance >= 0 ? QStringLiteral("离线预期报告实例名称。") : QStringLiteral("报告定义名称。"),
                QStringLiteral("组成"), report, QStringLiteral("name"), Branch::ReportName));
        } else {
            Add(Node(QStringLiteral("功能约束 FC"), fc, FcMeaning(fc), QStringLiteral("组成"),
                Valid(da) ? da : fcda, QStringLiteral("fc"), Branch::FunctionalConstraint));
        }
    }
    for (int i = 0; i < parts.size(); ++i) {
        if (!Tick()) break;
        const auto id = path[i];
        const auto tag = doc.nodes[id].name;
        const auto label = (tag == "DO" || tag == "SDO" ? QStringLiteral("对象 ") : QStringLiteral("属性 ")) + tag;
        Add(Node(label, parts[i], QStringLiteral("名称来自解析到的模板成员。"), QStringLiteral("组成"),
            id, QStringLiteral("name"), Attr(id, "type").isEmpty() ? Branch::None : Branch::Type));
    }
    if (index >= 0)
        Add(Node(QStringLiteral("数组访问"), QString::number(index), QStringLiteral("展开有效下标、声明范围及后续访问组件。"),
            QStringLiteral("访问依据"), axis, QStringLiteral("count"), Branch::Array));
    if (Valid(fcda))
        Add(Node(QStringLiteral("FCDA 成员声明"), QString::number(reference ? reference->memberOrdinal : 0),
            QStringLiteral("声明的路径和解析到的目标分别展示。"), QStringLiteral("依据"),
            fcda, {}, Branch::Member));
    else if (Valid(input))
        Add(Node(QStringLiteral("ExtRef 输入声明"), {}, QStringLiteral("文件中的外部输入声明。"),
            QStringLiteral("依据"), input, {}, Branch::Member));
}

void Projection::LogicalDevice()
{
    const auto name = Attr(ld, "ldName");
    if (!name.isEmpty()) {
        Attribute(ld, "ldName", QStringLiteral("显式 LD 名"), QStringLiteral("声明"));
    } else {
        Attribute(Ancestor(ld, "IED"), "name", QStringLiteral("IED 名称"), QStringLiteral("先取"));
        Attribute(ld, "inst", QStringLiteral("LD 实例名"), QStringLiteral("再拼接"));
        Add(Node(QStringLiteral("命名规则"), QStringLiteral("IED 名称 + LD 实例名"),
            QStringLiteral("未提供非空 ldName 时按当前映射拼接，两部分之间无分隔符。"), QStringLiteral("规则")));
    }
    Add(Node(QStringLiteral("所属范围"), ScopeName(), {}, QStringLiteral("位于"), ld,
        QStringLiteral("inst"), Branch::Scope));
}

void Projection::LogicalNode()
{
    if (Is(ln, "LN0"))
        Add(Node(QStringLiteral("LN0 专用名称"), QStringLiteral("LLN0"), QStringLiteral("当前映射使用 LN0 专用名称。"),
            QStringLiteral("规则"), ln));
    Attribute(ln, "prefix", QStringLiteral("前缀 prefix"), QStringLiteral("组成"));
    Attribute(ln, "lnClass", QStringLiteral("逻辑节点类 lnClass"), QStringLiteral("组成"));
    Attribute(ln, "inst", QStringLiteral("LN 实例号 inst"), QStringLiteral("组成"));
    if (!Attr(ln, "lnType").isEmpty())
        Add(Node(QStringLiteral("LN 类型声明"), Attr(ln, "lnType"), {}, QStringLiteral("声明"), ln, "lnType", Branch::Type));
}

void Projection::FunctionalConstraint()
{
    if (!root.value.isEmpty() && root.kind == Kind::FunctionalConstraint)
        out.node.explanation += QStringLiteral("\n%1").arg(FcMeaning(root.value));
    if (Valid(fcda)) {
        Attribute(fcda, "fc", QStringLiteral("FCDA 声明的 FC"), QStringLiteral("声明"));
        if (reference && reference->state != LinkState::Resolved)
            Add(Node(QStringLiteral("成员解析状态"), SclMmsStateText(reference->state), reference->detail,
                QStringLiteral("停止"), fcda));
        else if (!reference)
            Add(Node(QStringLiteral("成员解析状态"), QStringLiteral("依据受限"),
                QStringLiteral("当前成员没有可用的解析记录。"), QStringLiteral("停止"), fcda));
        if (Attr(fcda, "daName").isEmpty() && reference && reference->state == LinkState::Resolved)
            Add(Node(QStringLiteral("DO 级成员"), Attr(fcda, "doName"), QStringLiteral("该 FC 约束对象成员的范围。"),
                QStringLiteral("作用范围"), fcda, "doName"));
    }
    if (Valid(da)) {
        Attribute(da, "fc", QStringLiteral("DA 定义的有效 FC"),
            !path.isEmpty() && path.back() != da ? QStringLiteral("继承 FC") : QStringLiteral("定义"));
    } else if (Valid(fcda) && !Attr(fcda, "daName").isEmpty() && !path.isEmpty() && reference
        && reference->state != LinkState::Resolved) {
        const auto* type = TypeLink(path.back());
        if (!type || type->state != LinkState::Resolved || type->candidates.size() != 1) return;
        const auto name = Attr(fcda, "daName").section('.', 0, 0).section('(', 0, 0);
        const auto owner = type->candidates.front();
        for (auto child = doc.nodes[owner].firstChild; Valid(child) && Tick(); child = doc.nodes[child].nextSibling) {
            if (Is(child, "DA") && Attr(child, "name") == name)
                Attribute(child, "fc", QStringLiteral("同名 DA 的 FC"), QStringLiteral("候选依据"));
            if (out.limited) break;
        }
    } else if (!Valid(fcda) && root.evidence.attribute == "fc") {
        Attribute(root.evidence.node, "fc", QStringLiteral("文件中的 FC"), QStringLiteral("声明"));
    }
}

void Projection::Type(NodeId declaration)
{
    const auto attribute = Is(declaration, "LN") || Is(declaration, "LN0") ? QStringLiteral("lnType") : QStringLiteral("type");
    Attribute(declaration, attribute, QStringLiteral("类型声明"), QStringLiteral("声明"));
    const auto* link = TypeLink(declaration);
    if (!link) {
        Add(Node(QStringLiteral("类型定位受限"), {}, QStringLiteral("当前引用索引没有该声明的解析结果。"), QStringLiteral("停止")));
        return;
    }
    const bool resolved = link->state == LinkState::Resolved && link->candidates.size() == 1;
    if (!resolved)
        Add(Node(QStringLiteral("类型解析状态"), SclMmsStateText(link->state), link->detail, QStringLiteral("停止"), declaration, attribute));
    for (const auto target : link->candidates) {
        if (!Tick() || out.limited) break;
        if (Valid(target)) {
            const bool hasPath = std::any_of(path.cbegin(), path.cend(),
                [&](NodeId member) { return Parent(member) == target; });
            Add(Node(doc.nodes[target].name, Attr(target, "id"), SclMmsStateText(link->state),
                resolved ? QStringLiteral("引用定义") : QStringLiteral("候选"), target, "id",
                resolved && (hasPath || Is(target, "LNodeType") || Is(target, "DOType"))
                    ? Branch::TypeDefinition : Branch::None));
        }
    }
}

void Projection::TypeDefinition(NodeId definition)
{
    if (Is(definition, "LNodeType"))
        Attribute(definition, "lnClass", QStringLiteral("逻辑节点类"), QStringLiteral("定义"));
    else if (Is(definition, "DOType"))
        Attribute(definition, "cdc", QStringLiteral("公共数据类 CDC"), QStringLiteral("定义"));
    for (const auto member : path) {
        if (!Tick()) break;
        if (Parent(member) != definition) continue;
        Add(Node(doc.nodes[member].name, Attr(member, "name"), QStringLiteral("当前引用经过的模板成员。"),
            QStringLiteral("包含"), member, "name", Attr(member, "type").isEmpty() ? Branch::None : Branch::Type));
    }
}

void Projection::Scope()
{
    Attribute(scope.ied, "name", QStringLiteral("IED"), QStringLiteral("位于"));
    Attribute(scope.accessPoint, "name", QStringLiteral("访问点 AP"), QStringLiteral("位于"));
    if (Valid(scope.server) && scope.accessPoint != Parent(scope.server)) {
        for (const auto& server : snap.mms.servers) {
            if (!Tick()) break;
            if (server.accessPoint != scope.accessPoint || server.model.target() != scope.server) continue;
            for (const auto node : server.path) {
                if (!Tick() || out.limited) break;
                if (Is(node, "ServerAt")) Attribute(node, "apName", QStringLiteral("ServerAt 指向访问点"), QStringLiteral("解析别名"));
            }
            break;
        }
    }
    if (Valid(scope.server)) Add(Node(QStringLiteral("实际 Server"), {}, {}, QStringLiteral("位于"), scope.server));
    Attribute(ld, "inst", QStringLiteral("LDevice 实例"), QStringLiteral("位于"));
}

void Projection::Member()
{
    const auto source = Valid(fcda) ? fcda : input;
    if (Valid(input)) Attribute(input, "iedName", QStringLiteral("源 IED"), QStringLiteral("声明"));
    for (const auto* key : {"ldInst", "prefix", "lnClass", "lnInst", "doName", "daName", "fc", "ix"})
        Attribute(source, QLatin1String(key), QLatin1String(key), QStringLiteral("声明"));
    if (reference)
        Add(Node(QStringLiteral("解析状态"), SclMmsStateText(reference->state), reference->detail, QStringLiteral("结果"), source));
}

void Projection::Array()
{
    Attribute(axis, "count", QStringLiteral("数组声明数量"), QStringLiteral("定义"));
    if (Valid(selector)) {
        Attribute(selector, "ix", QStringLiteral("原始 ix"), QStringLiteral("声明"));
        if (Is(selector, "FCDA") && Attr(selector, "ix").isNull()) {
            const auto key = Attr(selector, "doName").contains('(') ? QStringLiteral("doName") : QStringLiteral("daName");
            Attribute(selector, key, QStringLiteral("路径内联选择"), QStringLiteral("声明"));
        }
    }
    Add(Node(QStringLiteral("有效下标"), QString::number(index), {}, QStringLiteral("解析结果"), axis, "count"));
    if (!component.isEmpty())
        Add(Node(QStringLiteral("alternate access 组件"), component,
            QStringLiteral("选择数组元素后继续访问的成员。"), QStringLiteral("访问依据")));
}

void Projection::ReportName()
{
    Attribute(report, "name", QStringLiteral("报告定义名称"), QStringLiteral("声明"));
    if (instance < 0 || instance >= int(snap.mms.instances.size())) return;
    const auto& entry = snap.mms.instances[instance];
    Add(Node(QStringLiteral("预期实例序号"), QString::number(entry.ordinal),
        QStringLiteral("按当前离线报告实例投影生成。"), QStringLiteral("推导"), report));
    for (const auto& evidence : entry.evidence) {
        if (!Tick() || out.limited) break;
        if (!evidence.attribute.isEmpty() && evidence.attribute != "name")
            Attribute(evidence.node, evidence.attribute, evidence.attribute, QStringLiteral("依据"));
    }
}

SclReferenceTrace Projection::Run(const SclTraceNode& branch)
{
    checkpoint(task);
    Context();
    out.node = branch;
    switch (branch.branch) {
    case Branch::Root: Root(); break;
    case Branch::LogicalDevice: LogicalDevice(); break;
    case Branch::LogicalNode: LogicalNode(); break;
    case Branch::FunctionalConstraint: FunctionalConstraint(); break;
    case Branch::Type: Type(branch.source); break;
    case Branch::TypeDefinition: TypeDefinition(branch.source); break;
    case Branch::Scope: Scope(); break;
    case Branch::Member: Member(); break;
    case Branch::Array: Array(); break;
    case Branch::ReportName: ReportName(); break;
    case Branch::None: break;
    }
    checkpoint(task);
    return std::move(out);
}
}

SclReferenceTrace TraceSclReference(const SclSnapshot& snapshot, const SclReferenceValue& value,
    const SclTraceNode& branch, Scl::TaskControl* task)
{
    return Projection(snapshot, value, task).Run(branch);
}
}
