#include "SclAnalysisService.hpp"
#include "SclMmsQuery.hpp"
#include <algorithm>

namespace iec61850sim {
using namespace Scl;
QString SclMmsStateText(LinkState state) {
    switch (state) {
    case LinkState::Resolved: return QStringLiteral("已解析（离线）");
    case LinkState::Missing: return QStringLiteral("缺失");
    case LinkState::Ambiguous: return QStringLiteral("候选歧义");
    case LinkState::Unbound: return QStringLiteral("未静态绑定");
    case LinkState::External: return QStringLiteral("工程外声明");
    case LinkState::Unsupported: return QStringLiteral("尚未解释");
    case LinkState::Blocked: return QStringLiteral("解释受阻");
    case LinkState::Partial: return QStringLiteral("部分解释");
    }
    return {};
}
namespace {
QString Attr(const SclSnapshot& s, NodeId node, const char* name) {
    return node >= 0 && node < int(s.analysis.document->nodes.size())
        ? s.analysis.document->attribute(node, QString::fromLatin1(name)) : QString();
}
QString Shape(SclMmsAddressShape shape) {
    switch (shape) {
    case SclMmsAddressShape::Scalar: return QStringLiteral("基本属性");
    case SclMmsAddressShape::Structure: return QStringLiteral("结构");
    case SclMmsAddressShape::ArrayTemplate: return QStringLiteral("数组模板（未选择元素）");
    case SclMmsAddressShape::ObjectMember: return QStringLiteral("DO 对象成员");
    case SclMmsAddressShape::AlternateAccess: return QStringLiteral("数组 alternate access");
    default: return QStringLiteral("未确认形态");
    }
}
QString ValueState(MmsValueState state) {
    switch (state) {
    case MmsValueState::Missing: return QStringLiteral("未声明");
    case MmsValueState::Empty: return QStringLiteral("已声明空值");
    case MmsValueState::Explicit: return QStringLiteral("显式声明");
    case MmsValueState::Invalid: return QStringLiteral("词法非法");
    case MmsValueState::Limited: return QStringLiteral("解释受限");
    case MmsValueState::Derived: return QStringLiteral("离线推导");
    }
    return {};
}
}
void PresentSclMms(const SclSnapshot& s, SclMmsResult& result, TaskControl* task) {
    for (auto& row : result.rows) {
        checkpoint(task);
        QString name, desc, type, parent;
        switch (row.key.kind) {
        case SclMmsRecordKind::Report: {
            const auto& r = s.mms.reports.at(row.key.record);
            name = r.fields.value("name").raw; desc = r.fields.value("desc").raw;
            type = r.fields.value("buffered").value.isValid() ? r.fields.value("buffered").value.toBool()
                ? QStringLiteral("缓冲报告 BR") : QStringLiteral("非缓冲报告 RP") : QStringLiteral("报告类型未确认");
            parent = r.fields.value("datSet").raw + QStringLiteral(" · %1 次成员出现").arg(r.memberCount);
            if (desc.isEmpty()) desc = r.instanceLimitation;
            break;
        }
        case SclMmsRecordKind::Instance:
            name = row.cells.value(0); desc = row.cells.value(2); type = QStringLiteral("预期实例（不是在线实例）"); parent = row.cells.value(1); break;
        case SclMmsRecordKind::Member: {
            if (!row.description) row.description = DescribeSclMember(s.analysis, row.key.node, task);
            name = QStringLiteral("%1 · %2").arg(row.key.ordinal).arg(Attr(s, row.key.node, "doName"));
            desc = Attr(s, row.key.node, "daName");
            if (row.description) for (const auto& f : row.description->fields)
                if (!f.value.isEmpty()) desc += '\n' + SclDescriptionLabel(f.source) + ": " + f.value;
            type = Shape(row.shape);
            if (row.key.record >= 0 && row.key.record < int(s.analysis.references.size()))
                parent = s.analysis.references[row.key.record].objectReference;
            break;
        }
        case SclMmsRecordKind::Client:
            name = row.cells.value(0); desc = row.cells.mid(1).join('\n');
            type = QStringLiteral("文件声明；不表示订阅"); parent = Attr(s, row.report, "name"); break;
        case SclMmsRecordKind::Signal:
            name = row.cells.value(2); desc = row.cells.value(4); type = row.cells.value(3);
            parent = row.cells.value(0) + '/' + row.cells.value(1); break;
        case SclMmsRecordKind::Address:
            name = row.key.field; desc = row.limitation; type = Shape(row.shape);
            if (row.description) for (const auto& f : row.description->fields)
                if (!f.value.isEmpty()) desc += '\n' + SclDescriptionLabel(f.source) + ": " + f.value;
            break;
        case SclMmsRecordKind::Endpoint:
            name = row.cells.value(0) + '/' + row.cells.value(1); desc = row.cells.mid(2).join('\n');
            type = QStringLiteral("ConnectedAP 文件端点"); break;
        case SclMmsRecordKind::Server:
            name = row.cells.value(0) + '/' + row.cells.value(1); desc = row.cells.mid(2).join('\n');
            type = QStringLiteral("AP / Server 访问路径"); break;
        case SclMmsRecordKind::Capability:
            name = row.cells.value(0) + " / Services"; desc = row.cells.mid(1).join('\n'); type = QStringLiteral("能力声明"); break;
        case SclMmsRecordKind::DataSet:
            name = row.cells.value(0); type = QStringLiteral("数据集 MMS 列表"); desc = row.cells.value(1); break;
        case SclMmsRecordKind::Reference:
            name = row.cells.value(0); desc = row.cells.value(1); type = Shape(row.shape); break;
        case SclMmsRecordKind::Finding: {
            const auto& f = row.key.ordinal ? s.mms.findings.at(row.key.record) : s.analysis.findings.at(row.key.record);
            name = f.rule; desc = f.detail; parent = f.objectPath;
            type = f.severity == Severity::Error ? QStringLiteral("错误") : f.severity == Severity::Warning ? QStringLiteral("警告") : QStringLiteral("提示");
            break;
        }
        }
        row.cells = {name, desc, type, parent, row.fc, row.domain, row.item,
            row.key.kind == SclMmsRecordKind::Finding ? (row.key.ordinal ? s.mms.ruleSetVersion : s.analysis.ruleSetVersion) : SclMmsStateText(row.state)};
    }
}

SclMmsResult DescribeSclMms(const SclSnapshot& s, const SclMmsRequest& request, TaskControl* task) {
    SclMmsResult out; out.request = request; out.sha256 = s.analysis.sha256;
    out.document = s.analysis.document; out.source = s.analysis.source;
    const auto& key = request.key; const auto& doc = *s.analysis.document;
    const auto valid = [&](NodeId n) { return n >= 0 && n < int(doc.nodes.size()); };
    const auto field = [&](const QString& id, const QString& label, const QString& value,
                            MmsEvidence evidence = {}, const QString& explanation = {}, bool present = true) {
        checkpoint(task);
        if (out.fields.size() >= 256) { out.limited = true; out.limitation = QStringLiteral("字段上限 256；更多声明请查看原文。"); return; }
        out.fields.push_back({id, label, value, explanation, present, evidence});
    };
    const auto rawFields = [&](const QMap<QString, MmsField>& fields, const QString& prefix) {
        for (auto it = fields.cbegin(); it != fields.cend(); ++it) {
            checkpoint(task);
            field(prefix + it.key(), prefix + it.key(), it.value().raw, it.value().evidence,
                ValueState(it.value().state) + (it.value().unit.isEmpty() ? QString() : " · " + it.value().unit)
                    + " · " + it.value().limitation, it.value().rawPresent);
        }
    };
    const auto mapping = [&](const QString& domain, const QString& item, NodeId evidence) {
        field("domain", "MMS Domain", domain, {evidence, {}, {}}, QStringLiteral("定义/类型映射；不是在线发现"));
        field("item", "MMS Item", item, {evidence, {}, {}}, QStringLiteral("离线推导；保留 FC 与数组语境"));
    };
    const auto description = [&](const SclDescription& d) {
        for (const auto& f : d.fields) {
            checkpoint(task);
            field("description." + QString::number(int(f.source)), SclDescriptionLabel(f.source), f.value,
                {f.node, {}, {}}, SclDescriptionText(f), f.state != SclDescriptionState::Missing);
        }
    };
    const auto rawNode = [&](NodeId node) {
        if (!valid(node)) return;
        const auto& n = doc.nodes[node];
        for (int i = n.firstAttribute; i < n.firstAttribute + n.attributeCount; ++i) {
            checkpoint(task); const auto& a = doc.attributes[i];
            field("@" + a.namespaceUri + ':' + a.name, '@' + a.name, a.value, {node, a.name, {}}, QStringLiteral("文件原始声明"));
        }
    };
    checkpoint(task);
    if (!valid(key.node)) { out.summary = QStringLiteral("没有可用的来源节点。"); return out; }
    out.title = doc.nodes[key.node].name + " · " + Attr(s, key.node, "name");
    out.summary = QStringLiteral("离线只读 · 文件配置不证明在线连接、订阅或工程完整性。字段原文定位到所在元素。");
    switch (key.kind) {
    case SclMmsRecordKind::Report: {
        const auto it = s.mmsIndex.reports.constFind(key.node); if (it == s.mmsIndex.reports.cend()) break;
        const auto& r = s.mms.reports[it.value()]; rawFields(r.fields, "@");
        field("definition", "RCB 定义引用", r.definitionReference, {r.node, "name", {}}, QStringLiteral("定义层派生引用"));
        mapping(r.mmsDomain, r.definitionMmsItem, r.node);
        field("dataset", "数据集引用", r.dataSetReference, {r.node, "datSet", {}}, SclMmsStateText(r.dataSet.state));
        field("list", "数据集 MMS 列表名", r.dataSetMmsName, {r.dataSet.target(), "name", {}}, QStringLiteral("命名变量列表，不是变量 Item"));
        field("instances", "预期实例解释", r.instanceLimitation, {r.node, "indexed", {}}, ValueState(r.instanceState));
        for (const auto& groups : {r.triggers, r.options, r.enabled}) for (const auto& group : groups) {
            checkpoint(task); rawFields(group.fields, doc.nodes[group.node].name + QString::number(group.node) + '.');
        }
        field("bufTime.help", "bufTime 含义", QStringLiteral("事件合并等待相关配置"));
        field("runtime", "实际 RptID / RptEna / Owner / SqNum / EntryID", {}, {}, QStringLiteral("离线未获取"), false);
        break;
    }
    case SclMmsRecordKind::Instance:
        if (key.record >= 0 && key.record < int(s.mms.instances.size())) {
            const auto& i = s.mms.instances[key.record]; if (i.definition != key.node || i.ordinal != key.ordinal) break;
            out.title = QStringLiteral("预期 RCB 实例 · ") + i.name;
            field("instance", "预期实例引用", i.reference, {i.definition, "name", {}}, QStringLiteral("按已确认版本推导；不是设备实际实例"));
            mapping(i.mmsDomain, i.mmsItem, i.definition);
            field("rptID", "原始 rptID", i.rptId.raw, i.rptId.evidence, ValueState(i.rptId.state), i.rptId.rawPresent);
        }
        break;
    case SclMmsRecordKind::Client:
        if (key.record >= 0 && key.record < int(s.mms.clients.size())) {
            const auto& c = s.mms.clients[key.record]; if (c.node != key.node) break;
            rawFields(c.fields, "@");
            field("resolution", "目标解释", c.target.detail, {c.node, {}, {}}, SclMmsStateText(c.target.state));
            field("ordinal", "声明原始位置", QString::number(c.ordinal), {c.node, {}, {}});
            field("allocation", "预期实例位置（0 为未确认）", QString::number(c.expectedInstanceOrdinal), {c.node, {}, {}}, c.allocationLimitation);
            for (auto n : c.target.candidates) { checkpoint(task); field("candidate." + QString::number(n), "目标候选", SclTaskPath(doc, n), {n, {}, {}}, SclMmsStateText(c.target.state)); }
        }
        break;
    case SclMmsRecordKind::Signal:
    case SclMmsRecordKind::Address:
        if (key.record >= 0 && key.record < int(s.tasks.catalog.instances.size())) {
            const auto& instance = s.tasks.catalog.instances[key.record]; if (instance.logicalNode != key.node) break;
            const auto& d = s.tasks.catalog.definitions[instance.definition];
            out.title = QStringLiteral("信号 · ") + SclTaskPath(doc, key.node) + '.' + d.path;
            if (key.kind == SclMmsRecordKind::Address) {
                auto query = request; query.kind = SclMmsQueryKind::Addresses; query.signal = key.record;
                query.text = key.field; query.offset = 0; query.pageSize = 200; query.presentation = false;
                const auto addresses = QuerySclMms(s, query, task);
                for (const auto& row : addresses.rows) if (row.key.field == key.field) {
                    mapping(row.domain, row.item, row.evidence.isEmpty() ? key.node : row.evidence.back().node);
                    field("fc", "FC", row.fc, row.evidence.isEmpty() ? MmsEvidence{} : row.evidence.back());
                    field("shape", "访问形态", Shape(row.shape), {}, row.limitation);
                    if (row.description) description(*row.description);
                    break;
                }
                out.limited = addresses.limited; out.limitation = addresses.limitation;
            }
            description(DescribeSclSignal(s.analysis, key.node, d.path, task));
        }
        break;
    case SclMmsRecordKind::Member:
    case SclMmsRecordKind::Reference:
        if (key.record >= 0 && key.record < int(s.analysis.references.size())) {
            const auto& ref = s.analysis.references[key.record];
            if (key.kind == SclMmsRecordKind::Member && ref.source != key.node) break;
            rawNode(ref.source); description(DescribeSclMember(s.analysis, ref.source, task));
            mapping(ref.mmsDomain, ref.mmsItem, ref.source);
            field("object", "对象引用", ref.objectReference, {ref.source, {}, {}}, SclMmsStateText(ref.state));
            field("resolution", "引用解析状态与原因", SclMmsStateText(ref.state) + " · " + ref.detail, {ref.source, {}, {}});
            for (int i = 0; i < std::min(64, int(ref.candidates.size())); ++i) {
                checkpoint(task); const auto node = ref.candidates[i];
                field("candidate." + QString::number(i), ref.state == LinkState::Resolved ? "解析目标" : "目标候选",
                    SclTaskPath(doc, node), {node, {}, {}}, SclMmsStateText(ref.state));
            }
            if (ref.candidates.size() > 64) { out.limited = true; out.limitation = QStringLiteral("目标候选最多展示 64 项；请查看原文与引用依据。"); }
            field("array", "数组下标（-1 为未指定）", QString::number(ref.arrayIndex), {ref.source, "ix", {}});
            field("component", "alternate access 组件", ref.mmsComponent, {ref.source, {}, {}});
            field("parent", "父报告定义", Attr(s, request.report, "name"), {request.report, {}, {}});
        } else if (key.kind == SclMmsRecordKind::Member) {
            rawNode(key.node);
            field("resolution", "引用解析状态与原因", QStringLiteral("成员引用索引未覆盖；仅显示文件声明。"), {key.node, {}, {}});
            out.limited = true; out.limitation = QStringLiteral("成员引用索引资源受限；未推导目标或地址。");
        }
        break;
    case SclMmsRecordKind::Finding: {
        const auto& findings = key.ordinal ? s.mms.findings : s.analysis.findings;
        if (key.record < 0 || key.record >= int(findings.size())) break;
        const auto& f = findings[key.record]; out.title = f.rule + " · " + f.detail;
        field("rule", "规则 / 版本", f.rule + " / " + (key.ordinal ? s.mms.ruleSetVersion : s.analysis.ruleSetVersion), {f.node, {}, f.rule});
        field("detail", "发现", f.detail, {f.node, {}, f.rule}); field("suggestion", "建议", f.suggestion, {f.node, {}, f.rule});
        field("scope", "检查范围", f.scope, {f.node, {}, f.rule}); break;
    }
    default: rawNode(key.node); break;
    }
    out.total = int(out.fields.size()); return out;
}
}
