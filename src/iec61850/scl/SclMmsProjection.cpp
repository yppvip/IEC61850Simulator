#include "SclMmsProjection.hpp"

#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <limits>

namespace Scl {
namespace {

struct BudgetExceeded {};

bool addressPart(const QString &value)
{
    // Conservative address subset. Preserve unfamiliar identifiers in raw
    // fields rather than presenting a delimiter-containing string as an address.
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_]+$"));
    return name.match(value).hasMatch();
}

bool limitedFields(const QMap<QString, MmsField> &fields)
{
    return std::any_of(fields.cbegin(), fields.cend(), [](const MmsField &f) {
        return f.state == MmsValueState::Missing || f.state == MmsValueState::Invalid || f.state == MmsValueState::Limited;
    });
}

MmsResolution resolution(const Reference &ref)
{
    return {ref.state, ref.candidates, ref.detail};
}

class Builder {
public:
    Builder(const ReferenceAnalysis &input, TaskControl *control, const MmsLimits &budget)
        : analysis(input), task(control), limits(budget), doc(*input.document)
    {
        out.sha256 = input.sha256;
        out.document = input.document;
        out.source = input.source;
        for (const auto *id : {"MMS001", "MMS002", "MMS003", "MMS004", "ENG004", "MMS005"})
            out.coverage.push_back({QString::fromLatin1(id)});
        out.coverage.push_back({QStringLiteral("MMS006"), Coverage::NotImplemented});
        out.coverage.push_back({QStringLiteral("XSD"), Coverage::NotImplemented});
        if (attr(0, "version") == "2007" && attr(0, "revision") == "B" && attr(0, "release") == "4")
            out.profile = MmsProfile::Explicit2007B4;
    }

    MmsProjection run();

private:
    const ReferenceAnalysis &analysis;
    TaskControl *task;
    MmsLimits limits;
    const Document &doc;
    MmsProjection out;
    int used = 0;
    std::vector<bool> active;
    QHash<NodeId, QHash<QString, QList<NodeId>>> keys;
    QHash<NodeId, MmsServer> paths;
    QHash<NodeId, QList<MmsDomain>> domains;
    QHash<NodeId, MmsDomain> deviceDomains;
    QHash<NodeId, int> memberCounts;
    QHash<NodeId, MmsResolution> dataSets;
    QHash<NodeId, const Reference *> connected, extControls, extData;
    QHash<NodeId, int> reportRows;
    QHash<NodeId, QList<NodeId>> serviceNodes;

    void take() { checkpoint(task); if (++used > limits.records) throw BudgetExceeded{}; }
    QString attr(NodeId id, const char *name) const { return id == NoNode ? QString() : doc.attribute(id, QLatin1String(name)); }
    bool kind(NodeId id, const char *name) const
    { return id != NoNode && active[id] && doc.nodes[id].name == QLatin1String(name); }
    NodeId parent(NodeId id) const { return id == NoNode ? NoNode : doc.nodes[id].parent; }
    NodeId ancestor(NodeId id, const char *name) const
    {
        for (; id != NoNode; id = parent(id))
            if (kind(id, name)) return id;
        return NoNode;
    }
    bool ln(NodeId id) const { return kind(id, "LN") || kind(id, "LN0"); }
    QString lnName(NodeId id) const
    { return attr(id, "prefix") + (kind(id, "LN0") ? QStringLiteral("LLN0") : attr(id, "lnClass")) + attr(id, "inst"); }
    void key(NodeId id, const QString &name, const QString &value)
    { take(); if (!value.isEmpty()) keys[parent(id)][name + QChar(0) + value].push_back(id); }
    QList<NodeId> lookup(NodeId scope, const QString &name, const QString &value) const
    { return value.isEmpty() ? QList<NodeId>() : keys.value(scope).value(name + QChar(0) + value); }
    MmsResolution match(QList<NodeId> candidates) const
    {
        const auto size = candidates.size();
        return {size == 0 ? LinkState::Missing : size == 1 ? LinkState::Resolved : LinkState::Ambiguous,
                std::move(candidates), {}};
    }
    bool unique(NodeId id, const QString &name, const QString &value) const
    { const auto candidates = lookup(parent(id), name, value); return candidates.size() == 1 && candidates[0] == id; }
    void evaluated(const char *id, bool limited = false)
    {
        for (auto &rule : out.coverage) if (rule.id == QLatin1String(id)) {
            rule.state = Coverage::Executed; ++rule.evaluated; rule.limited += limited; return;
        }
    }
    void finding(const char *rule, NodeId id, const QString &detail, Severity severity = Severity::Warning)
    {
        take();
        QStringList path;
        for (auto at = id; at != NoNode; at = parent(at)) {
            auto segment = doc.nodes[at].name;
            auto name = attr(at, "name");
            if (!name.isEmpty()) segment += '[' + name + ']';
            path.prepend(segment);
        }
        out.findings.push_back({QString::fromLatin1(rule), severity, id, doc.nodes[id].source,
                               path.join('/'), detail, QStringLiteral("核对同快照原文与适用版本；此结果不表示在线状态。"),
                               QStringLiteral("MMS 离线项目规则 / ") + out.ruleSetVersion});
    }
    MmsField field(NodeId id, const QString &name, int type = 0, const QString &unit = {});
    QMap<QString, MmsField> rawFields(NodeId id);
    QStringList unknown(NodeId id, const QStringList &known) const;
    MmsParameterGroup group(NodeId id, const QStringList &booleans, bool max = false);
    void index();
    MmsServer serverPath(NodeId ap);
    void servers();
    bool addressScope(NodeId report) const;
    void reports();
    MmsResolution clientTarget(NodeId id);
    void clients();
    void endpoints();
    void capabilities();
    void conflicts();
};

MmsField Builder::field(NodeId id, const QString &name, int type, const QString &unit)
{
    take();
    MmsField f;
    f.raw = doc.attribute(id, name);
    f.rawPresent = !f.raw.isNull();
    f.unit = unit;
    f.evidence = {id, name, QStringLiteral("MMS001")};
    if (!f.rawPresent) {
        f.limitation = QStringLiteral("未声明；未应用未经确认的缺省值。");
        return f;
    }
    f.state = f.raw.isEmpty() ? MmsValueState::Empty : MmsValueState::Explicit;
    if (type == 0) { f.value = f.raw; return f; }
    evaluated("MMS001");
    // XML Schema boolean/integer whitespace is collapsed for interpretation;
    // the original lexical spelling is retained verbatim in raw.
    const auto lexical = f.raw.trimmed();
    if (type == 1) {
        if (lexical == "true" || lexical == "1") f.value = true;
        else if (lexical == "false" || lexical == "0") f.value = false;
    } else {
        static const QRegularExpression integer(QStringLiteral("^[+-]?[0-9]+$"));
        bool ok = false;
        const auto number = lexical.startsWith('-') ? lexical.toLongLong(&ok) : lexical.toULongLong(&ok);
        // A negative lexical zero is an unsigned value of zero in XML Schema.
        const bool nonnegative = !lexical.startsWith('-') || lexical.toLongLong() == 0;
        if (ok && nonnegative && integer.match(lexical).hasMatch() && number <= std::numeric_limits<quint32>::max())
            f.value = QVariant::fromValue(number);
    }
    if (!f.value.isValid()) {
        f.state = MmsValueState::Invalid;
        finding("MMS001", id, QStringLiteral("非法显式字段：") + name + QStringLiteral("；预期 ")
                + (type == 1 ? QStringLiteral("XML boolean。") : QStringLiteral("0..4294967295 整数。")));
    }
    return f;
}

QMap<QString, MmsField> Builder::rawFields(NodeId id)
{
    QMap<QString, MmsField> fields;
    const auto &node = doc.nodes[id];
    for (int i = node.firstAttribute; i < node.firstAttribute + node.attributeCount; ++i) {
        checkpoint(task);
        const auto &a = doc.attributes[i];
        if (a.namespaceUri.isEmpty()) fields.insert(a.name, field(id, a.name));
    }
    return fields;
}

QStringList Builder::unknown(NodeId id, const QStringList &known) const
{
    QStringList result;
    const auto &node = doc.nodes[id];
    for (int i = node.firstAttribute; i < node.firstAttribute + node.attributeCount; ++i) {
        checkpoint(task);
        const auto &a = doc.attributes[i];
        if (!a.namespaceUri.isEmpty()) result.push_back('{' + a.namespaceUri + '}' + a.name);
        else if (!known.contains(a.name)) result.push_back(a.name);
    }
    return result;
}

MmsParameterGroup Builder::group(NodeId id, const QStringList &booleans, bool max)
{
    take();
    MmsParameterGroup g{id, rawFields(id), {}};
    for (const auto &name : booleans) g.fields.insert(name, field(id, name, 1));
    QStringList names = booleans;
    names << QStringLiteral("desc");
    if (max) {
        names << QStringLiteral("max");
        g.fields.insert(QStringLiteral("max"), field(id, QStringLiteral("max"), 2));
    }
    g.unknownAttributes = unknown(id, names);
    return g;
}

void Builder::index()
{
    active.resize(doc.nodes.size());
    for (NodeId id = 0; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        const auto p = parent(id);
        active[id] = doc.nodes[id].isScl() && (p == NoNode || (active[p] && !kind(p, "Private")));
        if (!active[id] || p == NoNode) continue;
        if (kind(id, "IED") && p == 0) key(id, QStringLiteral("IED"), attr(id, "name"));
        else if (kind(id, "AccessPoint") && kind(p, "IED") && parent(p) == 0)
            key(id, QStringLiteral("AP"), attr(id, "name"));
        else if (kind(id, "LDevice") && kind(p, "Server") && kind(parent(p), "AccessPoint"))
            key(id, QStringLiteral("LD"), attr(id, "inst"));
        else if (ln(id) && kind(p, "LDevice") && kind(parent(p), "Server"))
            key(id, QStringLiteral("LN"), lnName(id));
        else if (kind(id, "ReportControl") && ln(p))
            key(id, QStringLiteral("ReportControl"), attr(id, "name"));
        else if (kind(id, "FCDA") && kind(p, "DataSet") && ln(parent(p))) ++memberCounts[p];
        else if (kind(id, "Services") && (kind(p, "IED") || kind(p, "AccessPoint"))) {
            take(); serviceNodes[p].push_back(id);
        }
    }
    for (const auto &ref : analysis.references) {
        checkpoint(task);
        if (ref.kind == LinkKind::ControlDataSet) dataSets.insert(ref.source, resolution(ref));
        else if (ref.kind == LinkKind::ConnectedAp) connected.insert(ref.source, &ref);
        else if (ref.kind == LinkKind::ExtRefControl) extControls.insert(ref.source, &ref);
        else if (ref.kind == LinkKind::ExtRefData) extData.insert(ref.source, &ref);
    }
}

MmsServer Builder::serverPath(NodeId ap)
{
    MmsServer result;
    result.accessPoint = ap;
    result.ied = parent(ap);
    QSet<NodeId> seen;
    for (auto at = ap; at != NoNode;) {
        checkpoint(task);
        if (seen.contains(at) || seen.size() >= limits.serverPath) {
            const bool limited = !seen.contains(at);
            out.resourceLimited |= limited;
            result.model = {LinkState::Blocked, {}, limited ? QStringLiteral("ServerAt 路径超过预算；此分支未解释。") : QStringLiteral("ServerAt 路径存在环。")};
            return result;
        }
        seen.insert(at); take(); result.path.push_back(at);
        QList<NodeId> children;
        for (auto child = doc.nodes[at].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
            checkpoint(task);
            if (kind(child, "Server") || kind(child, "ServerAt")) children.push_back(child);
        }
        if (children.size() != 1) {
            result.model = children.empty() ? MmsResolution{LinkState::Unsupported, {}, QStringLiteral("访问点未声明服务器模型；可能是纯客户端。")}
                                            : MmsResolution{LinkState::Ambiguous, children, QStringLiteral("多个 Server/ServerAt。")};
            return result;
        }
        const auto child = children.front();
        take(); result.path.push_back(child);
        if (kind(child, "Server")) { result.model = {LinkState::Resolved, {child}, {}}; return result; }
        const auto next = match(lookup(parent(at), QStringLiteral("AP"), attr(child, "apName")));
        if (next.target() == NoNode) { result.model = next; result.model.detail = QStringLiteral("ServerAt 的目标 AP 不唯一或不存在。"); return result; }
        at = next.target();
    }
    return result;
}

void Builder::servers()
{
    // Domains are counted once per model, separately from network access paths.
    QHash<NodeId, QHash<QString, int>> counts;
    for (NodeId id = 1; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        if (!kind(id, "LDevice") || !kind(parent(id), "Server")) continue;
        const auto ied = ancestor(id, "IED");
        const auto explicitName = attr(id, "ldName");
        const auto name = !explicitName.isEmpty() ? explicitName :
            (!attr(ied, "name").isEmpty() && !attr(id, "inst").isEmpty() ? attr(ied, "name") + attr(id, "inst") : QString());
        take();
        domains[parent(id)].push_back({id, name, MmsValueState::Limited,
            {id, explicitName.isEmpty() ? QStringLiteral("inst") : QStringLiteral("ldName"), QStringLiteral("MMS002")}});
        if (!name.isEmpty()) ++counts[parent(id)][name];
    }
    for (auto it = domains.begin(); it != domains.end(); ++it) {
        for (auto &domain : it.value()) {
            checkpoint(task);
            if (addressPart(domain.name) && counts.value(it.key()).value(domain.name) == 1
                && unique(domain.device, QStringLiteral("LD"), attr(domain.device, "inst")))
                domain.state = MmsValueState::Derived;
            deviceDomains.insert(domain.device, domain);
        }
    }
    for (NodeId id = 1; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        if (!kind(id, "AccessPoint") || !kind(parent(id), "IED") || parent(parent(id)) != 0) continue;
        take();
        auto path = serverPath(id);
        if (!unique(path.ied, QStringLiteral("IED"), attr(path.ied, "name"))
            || !unique(id, QStringLiteral("AP"), attr(id, "name")))
            path.model = {LinkState::Ambiguous, path.model.candidates, QStringLiteral("IED/AP 标识缺失或重复。")};
        path.domains = domains.value(path.model.target());
        path.services = serviceNodes.value(path.ied) + serviceNodes.value(id);
        evaluated("MMS004", path.model.target() == NoNode);
        if (path.model.state == LinkState::Blocked || path.model.state == LinkState::Ambiguous
            || path.model.state == LinkState::Missing)
            finding("MMS004", id, path.model.detail);
        paths.insert(id, path);
        out.servers.push_back(std::move(path));
    }
}

bool Builder::addressScope(NodeId report) const
{
    const auto logical = parent(report), device = parent(logical), server = parent(device), ap = parent(server), ied = parent(ap);
    if (!ln(logical) || !kind(device, "LDevice") || !kind(server, "Server") || !kind(ap, "AccessPoint")
        || !kind(ied, "IED") || parent(ied) != 0 || paths.value(ap).model.target() != server)
        return false;
    if (!unique(logical, QStringLiteral("LN"), lnName(logical))
        || !unique(report, QStringLiteral("ReportControl"), attr(report, "name"))) return false;
    if (kind(logical, "LN") && (attr(logical, "lnClass").isEmpty() || attr(logical, "inst").isNull())) return false;
    if (kind(logical, "LN0") && (attr(logical, "lnClass") != "LLN0" || !attr(logical, "inst").isEmpty())) return false;
    if (!addressPart(lnName(logical)) || !addressPart(attr(report, "name"))) return false;
    return deviceDomains.value(device).state == MmsValueState::Derived;
}

void Builder::reports()
{
    for (const auto &mapping : analysis.reports) {
        take();
        const auto id = mapping.source;
        MmsReportDefinition r;
        r.node = id; r.logicalNode = parent(id); r.server = ancestor(id, "Server"); r.ied = ancestor(id, "IED");
        r.fields = rawFields(id);
        for (const auto *name : {"name", "desc", "datSet", "rptID"}) r.fields.insert(QLatin1String(name), field(id, QLatin1String(name)));
        for (const auto *name : {"buffered", "indexed"}) r.fields.insert(QLatin1String(name), field(id, QLatin1String(name), 1));
        for (const auto *name : {"confRev", "bufTime", "intgPd"})
            r.fields.insert(QLatin1String(name), field(id, QLatin1String(name), 2, QByteArray(name) == "confRev" ? QString() : QStringLiteral("ms")));
        r.unknownAttributes = unknown(id, {"name", "desc", "datSet", "rptID", "buffered", "indexed", "confRev", "bufTime", "intgPd"});
        r.dataSet = dataSets.value(id, {LinkState::Blocked, {}, QStringLiteral("缺少已有引用分析结果。")});
        r.memberCount = memberCounts.value(r.dataSet.target());
        r.dataSetReference = mapping.dataSetReference;
        r.dataSetMmsName = mapping.dataSetReference.section('/', 1);
        if (!addressScope(id)) { r.dataSetReference.clear(); r.dataSetMmsName.clear(); }
        for (auto child = doc.nodes[id].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
            checkpoint(task);
            if (kind(child, "TrgOps")) r.triggers.push_back(group(child, {"dchg", "qchg", "dupd", "period", "gi"}));
            else if (kind(child, "OptFields")) r.options.push_back(group(child, {"seqNum", "timeStamp", "dataSet", "reasonCode", "dataRef", "entryID", "configRef", "bufOvfl", "segmentation"}));
            else if (kind(child, "RptEnabled")) r.enabled.push_back(group(child, {}, true));
            else if (active[child] && !kind(child, "Private") && !kind(child, "Text")) { take(); r.unknownChildren.push_back(child); }
        }
        const auto buffered = r.fields.value(QStringLiteral("buffered"));
        if (addressScope(id) && buffered.state == MmsValueState::Explicit) {
            // The base ReportMapping deliberately remains compatible with its
            // original raw-boolean behaviour. This typed projection also accepts
            // the collapsed XML boolean lexical form, without applying defaults.
            r.mmsDomain = deviceDomains.value(parent(r.logicalNode)).name;
            const auto fc = buffered.value.toBool() ? QStringLiteral("BR") : QStringLiteral("RP");
            r.definitionMmsItem = lnName(r.logicalNode) + '$' + fc + '$' + attr(id, "name");
            r.definitionReference = r.mmsDomain + '/' + lnName(r.logicalNode) + '.' + fc + '.' + attr(id, "name");
            r.mappingState = MmsValueState::Derived;
        }
        const auto indexed = r.fields.value(QStringLiteral("indexed"));
        MmsField maximum;
        if (r.enabled.size() == 1) maximum = r.enabled[0].fields.value(QStringLiteral("max"));
        bool validMax = maximum.state == MmsValueState::Explicit && maximum.value.toULongLong() >= 1 && maximum.value.toULongLong() <= 99;
        if (maximum.state == MmsValueState::Explicit && !validMax) {
            // This is the supported indexed-profile range, not a blanket ban on
            // all non-indexed association-specific URCB counts.
            if (out.profile == MmsProfile::Explicit2007B4 && indexed.value.isValid() && indexed.value.toBool()) {
                finding("MMS002", r.enabled[0].node, QStringLiteral("显式索引配置的 max 不在已支持的 1..99 范围。"));
                maximum.state = MmsValueState::Invalid; maximum.value.clear();
                r.enabled[0].fields.insert(QStringLiteral("max"), maximum);
            }
        }
        const bool supported = out.profile == MmsProfile::Explicit2007B4 && r.mappingState == MmsValueState::Derived
            && indexed.state == MmsValueState::Explicit && indexed.value.toBool() && validMax;
        if (supported) {
            r.instanceState = MmsValueState::Derived;
            for (int ordinal = 1; ordinal <= int(maximum.value.toULongLong()); ++ordinal) {
                take();
                const auto suffix = QStringLiteral("%1").arg(ordinal, 2, 10, QLatin1Char('0'));
                MmsReportInstance instance;
                instance.definition = id; instance.ordinal = ordinal;
                instance.name = attr(id, "name") + suffix;
                instance.reference = r.definitionReference + suffix;
                instance.mmsDomain = r.mmsDomain; instance.mmsItem = r.definitionMmsItem + suffix;
                instance.rptId = r.fields.value(QStringLiteral("rptID"));
                if (!instance.rptId.rawPresent || instance.rptId.raw.isEmpty()) {
                    instance.rptId.state = MmsValueState::Limited; instance.rptId.value.clear();
                    instance.rptId.limitation = QStringLiteral("缺失/空值的默认 RptID 规则未在本配置档案启用；离线不提供实际 RptID。");
                }
                instance.evidence = {{id, "name", "MMS002"}, {id, "buffered", "MMS002"},
                    {id, "indexed", "MMS002"}, {r.enabled[0].node, "max", "MMS002"}};
                out.instances.push_back(std::move(instance));
            }
        } else {
            r.instanceLimitation = out.profile == MmsProfile::Unknown ? QStringLiteral("根版本不在已确认配置档案；不生成确定实例。") :
                QStringLiteral("仅支持唯一作用域、显式 buffered、indexed=true 和单个 RptEnabled/max=1..99；缺省与非索引关联语境未启用。");
            if (buffered.state == MmsValueState::Invalid || indexed.state == MmsValueState::Invalid
                || maximum.state == MmsValueState::Invalid) r.instanceState = MmsValueState::Invalid;
        }
        evaluated("MMS002", !supported);
        const auto limitedGroup = [](const MmsParameterGroup &g) { return !g.unknownAttributes.empty() || limitedFields(g.fields); };
        const bool unknownContent = limitedFields(r.fields) || !r.unknownAttributes.empty() || !r.unknownChildren.empty()
            || r.triggers.size() != 1 || r.options.size() != 1 || r.enabled.size() != 1
            || std::any_of(r.triggers.begin(), r.triggers.end(), limitedGroup)
            || std::any_of(r.options.begin(), r.options.end(), limitedGroup)
            || std::any_of(r.enabled.begin(), r.enabled.end(), limitedGroup);
        evaluated("MMS005", unknownContent);
        if (r.triggers.size() > 1 || r.options.size() > 1 || r.enabled.size() > 1)
            finding("MMS001", id, QStringLiteral("重复参数子元素；全部保留，不选择第一项或应用缺省值。"));
        reportRows.insert(id, int(out.reports.size()));
        out.reports.push_back(std::move(r));
    }
}

MmsResolution Builder::clientTarget(NodeId id)
{
    if (attr(id, "iedName").isEmpty() || attr(id, "ldInst").isEmpty() || attr(id, "lnClass").isEmpty()
        || attr(id, "lnInst").isNull())
        return {LinkState::Unbound, {}, QStringLiteral("客户端引用缺少 IED/LD/LN 字段；LLN0 的 lnInst 可显式为空。")};
    auto ied = match(lookup(0, QStringLiteral("IED"), attr(id, "iedName")));
    if (ied.state == LinkState::Missing) { ied.state = LinkState::External; ied.detail = QStringLiteral("客户端 IED 不在当前文件。"); }
    if (ied.target() == NoNode) return ied;
    QList<NodeId> aps;
    if (!attr(id, "apRef").isEmpty()) {
        auto ap = match(lookup(ied.target(), QStringLiteral("AP"), attr(id, "apRef")));
        if (ap.target() == NoNode) return ap;
        aps = ap.candidates;
    } else {
        for (auto child = doc.nodes[ied.target()].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
            checkpoint(task);
            if (kind(child, "AccessPoint")) aps.push_back(child);
        }
    }
    QSet<NodeId> seen;
    QList<NodeId> candidates;
    bool blocked = false;
    for (auto ap : aps) {
        take();
        const auto model = paths.value(ap).model;
        if (model.target() == NoNode) { blocked = true; continue; }
        if (seen.contains(model.target())) continue;
        seen.insert(model.target());
        const auto device = match(lookup(model.target(), QStringLiteral("LD"), attr(id, "ldInst")));
        if (device.state == LinkState::Ambiguous) return {LinkState::Ambiguous, device.candidates, QStringLiteral("客户端 LD 歧义；候选为 LD。")};
        if (device.target() == NoNode) continue;
        const auto logical = lookup(device.target(), QStringLiteral("LN"), attr(id, "prefix") + attr(id, "lnClass") + attr(id, "lnInst"));
        candidates += logical;
    }
    auto result = match(candidates);
    if (blocked && candidates.size() < 2) result.state = LinkState::Blocked;
    result.detail = blocked ? QStringLiteral("存在未解析/纯客户端 AP 路径；不以可见首项掩盖未覆盖范围。") : QStringLiteral("按 IED/AP/Server/LD/LN 作用域解析的文件客户端声明。");
    return result;
}

void Builder::clients()
{
    for (const auto &report : out.reports) {
        QSet<NodeId> targets;
        for (const auto &enabled : report.enabled) {
            int ordinal = 0;
            for (auto child = doc.nodes[enabled.node].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
                checkpoint(task);
                if (!kind(child, "ClientLN")) continue;
                take();
                MmsClientDeclaration client;
                client.node = child; client.report = report.node; client.ownerServer = report.server;
                client.ordinal = ++ordinal; client.fields = rawFields(child); client.target = clientTarget(child);
                if (report.instanceState == MmsValueState::Derived
                    && ordinal <= int(enabled.fields.value(QStringLiteral("max")).value.toULongLong()))
                    client.expectedInstanceOrdinal = ordinal; // Unresolved earlier entries still consume their position.
                else client.allocationLimitation = QStringLiteral("未确认实例或 ClientLN 位置超出显式 max；不分配实例。");
                evaluated("MMS003", client.target.target() == NoNode || client.expectedInstanceOrdinal == 0);
                if (client.target.state == LinkState::Missing || client.target.state == LinkState::Ambiguous || client.target.state == LinkState::Unbound)
                    finding("MMS003", child, QStringLiteral("客户端声明不能唯一定位：") + client.target.detail);
                if (client.target.target() != NoNode) {
                    if (targets.contains(client.target.target())) finding("MMS003", child, QStringLiteral("同一报告重复声明相同客户端 LN；保留原位置。"));
                    targets.insert(client.target.target());
                }
                if (report.instanceState == MmsValueState::Derived && client.expectedInstanceOrdinal == 0)
                    finding("MMS003", child, QStringLiteral("ClientLN 的 XML 位置超过已确认的实例 max。"));
                out.clients.push_back(std::move(client));
            }
        }
    }
    for (NodeId id = 1; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        const bool association = kind(id, "Association") && kind(parent(id), "Server") && kind(parent(parent(id)), "AccessPoint");
        const bool reportExt = kind(id, "ExtRef") && extData.contains(id) && attr(id, "serviceType") == "Report";
        if (!association && !reportExt) continue;
        take();
        MmsClientDeclaration c;
        c.node = id; c.ownerServer = ancestor(id, "Server"); c.fields = rawFields(id);
        c.kind = association ? MmsClientKind::Association : MmsClientKind::ReportExtRef;
        if (association) {
            c.target = clientTarget(id);
            evaluated("MMS003", c.target.target() == NoNode);
            if (c.target.state == LinkState::Missing || c.target.state == LinkState::Ambiguous || c.target.state == LinkState::Unbound)
                finding("MMS003", id, QStringLiteral("Association 的客户端引用不能唯一定位：") + c.target.detail);
        } else {
            c.dataTarget = resolution(*extData.value(id));
            c.target = extControls.contains(id) ? resolution(*extControls.value(id)) :
                MmsResolution{LinkState::Unbound, {}, QStringLiteral("未提供明确的 Report 控制块证据；不从对象匹配猜测报告。")};
            if (c.target.target() != NoNode && reportRows.contains(c.target.target())) c.report = c.target.target();
            evaluated("MMS003", c.report == NoNode);
        }
        c.allocationLimitation = QStringLiteral("独立文件声明；不自动分配报告实例，不表示在线连接或订阅。");
        out.clients.push_back(std::move(c));
    }
}

bool ipv4(const QString &text, quint32 *number = nullptr)
{
    const auto parts = text.split('.');
    if (parts.size() != 4) return false;
    quint32 value = 0;
    for (const auto &part : parts) {
        if (part.isEmpty() || part.size() > 3) return false;
        for (const auto c : part) if (c < '0' || c > '9') return false;
        bool ok = false; const auto octet = part.toUInt(&ok);
        if (!ok || octet > 255) return false;
        value = (value << 8) | octet;
    }
    if (number) *number = value;
    return true;
}

void Builder::endpoints()
{
    // XML order rather than QHash iteration keeps identities deterministic.
    for (NodeId id = 1; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        if (!connected.contains(id)) continue;
        take();
        MmsEndpoint endpoint;
        endpoint.node = id; endpoint.subNetwork = parent(id); endpoint.accessPoint = resolution(*connected.value(id));
        endpoint.server = paths.value(endpoint.accessPoint.target()).model.target();
        for (auto address = doc.nodes[id].firstChild; address != NoNode; address = doc.nodes[address].nextSibling) {
            checkpoint(task);
            if (!kind(address, "Address")) continue;
            MmsNetworkAddress g; g.node = address; take();
            // P type is not a unique key. Preserve duplicate P elements as
            // separate groups so invalid/ambiguous declarations cannot overwrite.
            for (auto p = doc.nodes[address].firstChild; p != NoNode; p = doc.nodes[p].nextSibling) {
                checkpoint(task);
                if (!kind(p, "P")) continue;
                take();
                MmsParameterGroup parameter; parameter.node = p; parameter.fields = rawFields(p);
                MmsField value;
                value.rawPresent = true; value.raw = doc.nodes[p].text; value.value = value.raw;
                value.state = value.raw.isEmpty() ? MmsValueState::Empty : MmsValueState::Explicit;
                value.evidence = {p, {}, QStringLiteral("MMS004")};
                const auto type = attr(p, "type");
                const auto lexical = value.raw.trimmed();
                bool known = type == "IP" || type == "IP-SUBNET" || type == "IP-GATEWAY";
                if (known) {
                    quint32 number = 0;
                    if (!ipv4(lexical, &number)) {
                        if (lexical.contains(':')) { value.state = MmsValueState::Limited; value.limitation = QStringLiteral("IPv6 地址值域检查未实现。"); }
                        else { value.state = MmsValueState::Invalid; value.value.clear(); finding("MMS004", p, QStringLiteral("端点 ") + type + QStringLiteral(" 不是本检查支持的 IPv4 地址。")); }
                    } else if (type == "IP-SUBNET" && ((~number) & ((~number) + 1U)) != 0) {
                        value.state = MmsValueState::Invalid; value.value.clear(); finding("MMS004", p, QStringLiteral("IPv4 子网掩码不是连续位。"));
                    }
                } else {
                    value.state = MmsValueState::Limited;
                    value.limitation = QStringLiteral("保留已声明参数；OSI/selector/端口及未知类型的值域未校验，不填默认值。");
                }
                evaluated("MMS004", !known || value.state == MmsValueState::Limited);
                parameter.fields.insert(QStringLiteral("#text"), value);
                g.parameters.push_back(std::move(parameter));
            }
            endpoint.addresses.push_back(std::move(g));
        }
        out.endpoints.push_back(std::move(endpoint));
    }
}

void Builder::capabilities()
{
    for (NodeId id = 1; id < int(doc.nodes.size()); ++id) {
        checkpoint(task);
        if (!kind(id, "Services") || !serviceNodes.value(parent(id)).contains(id)) continue;
        take();
        MmsCapability capability;
        capability.services = id; capability.owner = parent(id);
        capability.limitation = QStringLiteral("能力原始声明；IED/AP 版本适用性、覆盖优先级和能力/配置一致性规则未启用，不能当成当前报告或连接数。");
        for (auto child = doc.nodes[id].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
            checkpoint(task);
            if (!active[child] || kind(child, "Private")) continue;
            take(); capability.declarations.push_back(child);
            MmsParameterGroup g{child, rawFields(child), {}};
            if (kind(child, "ConfReportControl") || kind(child, "DynAssociation"))
                g.fields.insert(QStringLiteral("max"), field(child, QStringLiteral("max"), 2));
            capability.parameters.push_back(std::move(g));
        }
        evaluated("MMS005", true);
        out.capabilities.push_back(std::move(capability));
    }
}

void Builder::conflicts()
{
    QHash<NodeId, QHash<QString, NodeId>> addresses, reportIds;
    for (const auto &instance : out.instances) {
        checkpoint(task);
        const auto &report = out.reports[reportRows.value(instance.definition)];
        auto &scope = addresses[report.server];
        const auto key = instance.mmsDomain + '/' + instance.mmsItem;
        evaluated("ENG004");
        if (scope.contains(key)) finding("ENG004", instance.definition, QStringLiteral("同一服务器内的已推导索引实例地址疑似冲突：") + key);
        scope.insert(key, instance.definition);
    }
    // One custom rptID across the instances of a single definition is legal
    // evidence here, not N duplicate declarations. Cross-server reuse is legal.
    for (const auto &report : out.reports) {
        checkpoint(task);
        const auto id = report.fields.value(QStringLiteral("rptID"));
        if (report.mappingState != MmsValueState::Derived || !id.rawPresent || id.raw.isEmpty()) continue;
        auto &scope = reportIds[report.server];
        evaluated("ENG004");
        if (scope.contains(id.raw)) finding("ENG004", report.node, QStringLiteral("同一服务器不同报告定义重复声明非空 rptID；仅作工程提示，不能据此认定订阅错误。"));
        scope.insert(id.raw, report.node);
    }
}

MmsProjection Builder::run()
{
    index(); servers(); reports(); clients(); endpoints(); capabilities(); conflicts();
    checkpoint(task);
    out.complete = true;
    return std::move(out);
}

} // namespace

MmsProjection MmsProjector::Build(const ReferenceAnalysis &analysis, TaskControl *task, const MmsLimits &limits)
{
    enterStage(task, TaskStage::Validation);
    if (analysis.document && analysis.source && !analysis.sha256.isEmpty()) {
        try { return Builder(analysis, task, limits).run(); }
        catch (const BudgetExceeded &) { /* Never publish a truncated usable index. */ }
    }
    checkpoint(task);
    MmsProjection result;
    result.sha256 = analysis.sha256; result.document = analysis.document; result.source = analysis.source;
    result.resourceLimited = analysis.document && analysis.source && !analysis.sha256.isEmpty();
    result.limitation = result.resourceLimited ? QStringLiteral("MMS 投影记录预算不足；未发布部分模型。") : QStringLiteral("缺少完整的引用分析快照；未执行 MMS 投影。");
    for (const auto *id : {"MMS001", "MMS002", "MMS003", "MMS004", "ENG004", "MMS005"})
        result.coverage.push_back({QString::fromLatin1(id), Coverage::Blocked, 0, 1});
    result.coverage.push_back({QStringLiteral("MMS006"), Coverage::NotImplemented});
    result.coverage.push_back({QStringLiteral("XSD"), Coverage::NotImplemented});
    return result;
}

} // namespace Scl
