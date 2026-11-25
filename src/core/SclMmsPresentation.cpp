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

QString SclMmsReportMatchText(SclMmsReportMatch match) {
    switch (match) {
    case SclMmsReportMatch::ExactAttribute: return QStringLiteral("精确属性匹配");
    case SclMmsReportMatch::AttributeCoverage: return QStringLiteral("上级属性覆盖");
    case SclMmsReportMatch::ObjectCoverage: return QStringLiteral("DO 级覆盖");
    default: return {};
    }
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

QString DirectStateText(SclMmsDirectState state) {
    switch (state) {
    case SclMmsDirectState::Mapped: return QStringLiteral("已推导离线地址");
    case SclMmsDirectState::Structure: return QStringLiteral("结构地址");
    case SclMmsDirectState::ArraySelectionRequired: return QStringLiteral("数组元素待选");
    case SclMmsDirectState::AlternateAccess: return QStringLiteral("数组 alternate access");
    case SclMmsDirectState::Ambiguous: return QStringLiteral("地址候选歧义");
    case SclMmsDirectState::Missing: return QStringLiteral("缺少定义");
    case SclMmsDirectState::Blocked: return QStringLiteral("映射受阻");
    case SclMmsDirectState::Unavailable: return QStringLiteral("无直接映射证据");
    }
    return {};
}

QString MatchText(SclMmsAttributeMatch match) {
    switch (match) {
    case SclMmsAttributeMatch::PathOrAddress: return QStringLiteral("路径或地址匹配");
    case SclMmsAttributeMatch::Description: return QStringLiteral("文件描述匹配");
    case SclMmsAttributeMatch::BusinessTerm: return QStringLiteral("模型结构匹配");
    default: return {};
    }
}

QString AddressLocation(const SclSnapshot& s, const SclMmsRow& row) {
    const auto ln = Attr(s, row.key.node, "prefix") + Attr(s, row.key.node, "lnClass") + Attr(s, row.key.node, "inst");
    return QStringList{Attr(s, row.addressScope.ied, "name"), Attr(s, row.addressScope.accessPoint, "name"),
        QStringLiteral("Server"), Attr(s, row.logicalDevice, "inst"), ln}.join(" / ");
}
}

std::vector<SclReferenceValue> ProjectSclMmsReferences(const SclSnapshot& s, const SclMmsRow& row,
    TaskControl* task, bool includeDeclaration)
{
    using Kind = SclReferenceValueKind;
    std::vector<SclReferenceValue> values;
    std::shared_ptr<const SclReferenceBasis> basis;
    checkpoint(task);
    const auto add = [&](Kind kind, const QString& value, MmsEvidence source,
                         MmsValueState origin = MmsValueState::Derived) {
        if (!value.isEmpty()) values.push_back({kind, value, std::move(source), origin, basis});
    };
    const auto mapping = [&](const QString& domain, const QString& item, const MmsEvidence& domainSource,
                             const MmsEvidence& itemSource) {
        add(Kind::MmsDomain, domain, domainSource,
            domainSource.attribute == "ldName" ? MmsValueState::Explicit : MmsValueState::Derived);
        add(Kind::MmsItem, item, itemSource);
    };
    const auto reportValues = [&](NodeId reportNode) {
        const auto it = s.mmsIndex.reports.constFind(reportNode);
        if (it == s.mmsIndex.reports.cend()) return;
        const auto& report = s.mms.reports[*it];
        add(Kind::ReportDefinition, report.definitionReference, {report.node, "name", {}});
        const auto& rptId = report.fields.value("rptID");
        add(Kind::RawReportId, rptId.raw, rptId.evidence, MmsValueState::Explicit);
        add(Kind::DataSetReference, report.dataSetReference, {report.node, "datSet", {}});
        add(Kind::MmsDataSetList, report.dataSetMmsName, {report.dataSet.target(), "name", {}});
        if (row.key.kind == SclMmsRecordKind::Report) {
            auto anchor = std::make_shared<SclReferenceBasis>();
            anchor->scope = row.addressScope;
            anchor->report = report.node;
            anchor->logicalNode = report.logicalNode;
            basis = std::move(anchor);
            mapping(report.mmsDomain, report.definitionMmsItem, {report.node, {}, {}}, {report.node, "name", {}});
        }
    };
    switch (row.key.kind) {
    case SclMmsRecordKind::Address: {
        auto anchor = std::make_shared<SclReferenceBasis>();
        anchor->scope = row.addressScope;
        anchor->logicalNode = row.key.node;
        anchor->path = row.templatePath;
        anchor->fc = row.fcEvidence;
        anchor->arrayDeclaration = row.key.arrayDeclaration;
        anchor->arrayIndex = row.arrayIndex;
        anchor->selector = row.arraySelector;
        anchor->component = row.component;
        anchor->state = row.state;
        anchor->limitation = row.limitation;
        basis = std::move(anchor);
        const MmsEvidence leaf{row.key.declaration, {}, {}};
        add(Kind::ObjectReference, row.objectReference, leaf);
        add(Kind::FunctionalConstraint, row.fc, row.fcEvidence, MmsValueState::Explicit);
        mapping(row.domain, row.item, row.domainEvidence, leaf);
        if (row.arrayIndex >= 0) {
            add(Kind::ArrayIndex, QString::number(row.arrayIndex), {row.key.arrayDeclaration, "count", {}});
            add(Kind::AlternateAccessComponent, row.component, leaf);
        }
        for (const auto node : row.templatePath) {
            checkpoint(task);
            add(Kind::TypeReference, Attr(s, node, "type"), {node, "type", {}}, MmsValueState::Explicit);
        }
        break;
    }
    case SclMmsRecordKind::Signal:
        add(Kind::TypeReference, Attr(s, row.key.node, "lnType"), {row.key.node, "lnType", {}}, MmsValueState::Explicit);
        break;
    case SclMmsRecordKind::Report:
        reportValues(row.key.node);
        break;
    case SclMmsRecordKind::Instance:
        if (row.key.record >= 0 && row.key.record < int(s.mms.instances.size())) {
            const auto& instance = s.mms.instances[row.key.record];
            if (instance.definition != row.key.node || instance.ordinal != row.key.ordinal) break;
            const MmsEvidence source{instance.definition, "name", {}};
            auto anchor = std::make_shared<SclReferenceBasis>();
            anchor->scope = row.addressScope;
            anchor->report = instance.definition;
            anchor->instance = row.key.record;
            basis = std::move(anchor);
            add(Kind::ExpectedReportInstance, instance.reference, source);
            mapping(instance.mmsDomain, instance.mmsItem, source, source);
            add(Kind::RawReportId, instance.rptId.raw, instance.rptId.evidence, MmsValueState::Explicit);
            if (const auto report = s.mmsIndex.reports.constFind(instance.definition); report != s.mmsIndex.reports.cend())
                add(Kind::ReportDefinition, s.mms.reports[*report].definitionReference, source);
        }
        break;
    case SclMmsRecordKind::Member:
    case SclMmsRecordKind::Reference: {
        const Reference* ref = nullptr;
        if (row.key.record >= 0 && row.key.record < int(s.analysis.references.size()))
            ref = &s.analysis.references[row.key.record];
        if (ref && row.key.kind == SclMmsRecordKind::Member && ref->source != row.key.node) break;
        if (ref) {
            auto anchor = std::make_shared<SclReferenceBasis>();
            anchor->reference = row.key.record;
            anchor->scope = row.addressScope;
            anchor->report = row.report;
            basis = std::move(anchor);
        }
        const auto source = ref ? ref->source : row.key.node;
        const auto& doc = *s.analysis.document;
        const MmsEvidence member{source, {}, {}};
        if (ref && ref->state == LinkState::Resolved && ref->candidates.size() == 1) {
            add(Kind::ObjectReference, ref->objectReference, member);
            mapping(ref->mmsDomain, ref->mmsItem, member, member);
            if (ref->arrayIndex >= 0) {
                const auto indexAttribute = !Attr(s, source, "ix").isNull() ? QStringLiteral("ix")
                    : Attr(s, source, "doName").contains('(') ? QStringLiteral("doName") : QStringLiteral("daName");
                add(Kind::ArrayIndex, QString::number(ref->arrayIndex), {source, indexAttribute, {}});
                add(Kind::AlternateAccessComponent, ref->mmsComponent, member);
            }
        }
        if (source >= 0 && source < int(doc.nodes.size()) && doc.nodes[source].name == "FCDA") {
            add(Kind::FunctionalConstraint, Attr(s, source, "fc"), {source, "fc", {}}, MmsValueState::Explicit);
            add(Kind::RawArrayIndex, Attr(s, source, "ix"), {source, "ix", {}}, MmsValueState::Explicit);
            if (includeDeclaration && s.analysis.source)
                add(Kind::RawFcdaDeclaration, s.analysis.source->openingTag(doc.nodes[source].source, task), member, MmsValueState::Explicit);
        }
        const auto report = s.mmsIndex.reports.constFind(row.report);
        if (report != s.mmsIndex.reports.cend() && source >= 0 && source < int(doc.nodes.size())
            && s.mms.reports[*report].dataSet.target() == doc.nodes[source].parent)
            reportValues(row.report);
        break;
    }
    default: break;
    }
    return values;
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
            if (result.request.kind == SclMmsQueryKind::AttributeReports) {
                name = Attr(s, row.report, "name");
                const auto it = s.mmsIndex.reports.constFind(row.report);
                if (it != s.mmsIndex.reports.cend() && !s.mms.reports[*it].definitionReference.isEmpty())
                    name = s.mms.reports[*it].definitionReference;
                type = SclMmsReportMatchText(row.reportMatch);
                const auto memberPath = Attr(s, row.key.node, "doName") +
                    (Attr(s, row.key.node, "daName").isEmpty() ? QString() : '.' + Attr(s, row.key.node, "daName"));
                desc = QStringLiteral("#%1 · %2").arg(row.key.ordinal).arg(memberPath);
                parent = Attr(s, row.dataSet, "name");
            }
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
            if (result.request.kind == SclMmsQueryKind::Attributes) {
                if (row.key.declaration != NoNode) name = row.objectPath + '.' + name;
                if (!row.objectReference.isEmpty() && row.arrayIndex >= 0) name = row.objectReference.section('/', 1);
                parent = AddressLocation(s, row);
                type = MatchText(row.attributeMatch);
                const auto evidence = row.matchEvidence;
                if (evidence.node != NoNode) {
                    const auto value = evidence.attribute.isEmpty() ? s.analysis.document->nodes[evidence.node].text
                        : s.analysis.document->attribute(evidence.node, evidence.attribute);
                    if (!desc.contains(value)) desc = value + (desc.isEmpty() ? QString() : '\n' + desc);
                }
            }
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
        const auto state = row.key.kind == SclMmsRecordKind::Finding ? (row.key.ordinal ? s.mms.ruleSetVersion : s.analysis.ruleSetVersion)
            : result.request.kind == SclMmsQueryKind::Attributes ? DirectStateText(row.direct) : SclMmsStateText(row.state);
        row.cells = {name, desc, type, parent, row.fc, row.domain, row.item, state};
        if (row.key.kind != SclMmsRecordKind::Signal && row.key.kind != SclMmsRecordKind::Address
            && row.key.kind != SclMmsRecordKind::Member) continue;
        // Page-bounded path metadata. Dataset scans and instance lookup are only
        // requested for the selected identity, never for every catalogue leaf.
        auto path = row.templatePath;
        const bool member = row.key.kind == SclMmsRecordKind::Member;
        if (member) {
            const auto trace = s.analysis.fcdaTraces.constFind(row.key.node);
            if (trace != s.analysis.fcdaTraces.cend()) for (const auto& step : trace->steps) {
                checkpoint(task);
                if (step.field == FcdaField::DoName || step.field == FcdaField::DaName) path << step.node;
            }
        }
        QStringList objects, attributes;
        QString cdc, basicType;
        for (const auto node : path) {
            checkpoint(task);
            const auto& n = s.analysis.document->nodes[node];
            if (n.name == "DO" || n.name == "SDO") {
                objects << Attr(s,node,"name"); cdc.clear();
                const auto range = s.analysis.outgoing.equal_range(node);
                for (auto it = range.first; it != range.second; ++it) {
                    checkpoint(task); const auto& link = s.analysis.references[it.value()];
                    if (link.kind == LinkKind::MemberType && link.state == LinkState::Resolved && link.candidates.size() == 1)
                        cdc = Attr(s,link.candidates.front(),"cdc");
                }
            } else { attributes << Attr(s,node,"name"); basicType = Attr(s,node,"bType"); }
        }
        const auto rawDa = member ? Attr(s,row.key.node,"daName") : QString();
        const auto source = SclEvidenceAt(s,row.key.node);
        row.cells << (row.key.kind == SclMmsRecordKind::Signal ? name : objects.join('.'))
            << attributes.join('.') << (row.key.kind == SclMmsRecordKind::Signal ? type : cdc) << basicType
            << (member ? QStringLiteral("FCDA 文件声明 + 引用解析；地址按规则组合") : QStringLiteral("LN 类型展开 + MMS 规则组合"))
            << (member ? Attr(s,row.key.node,"doName") : QString())
            << (member ? rawDa.isNull() ? QStringLiteral("未声明") : rawDa.isEmpty() ? QStringLiteral("已声明空值") : rawDa : QString())
            << (member ? QString::number(row.key.ordinal) : QString())
            << (source.precision == SclEvidencePrecision::OpeningTag ? QStringLiteral("标签起始行 %1").arg(source.location.line) : QStringLiteral("无可信位置"));
        QStringList users;
        if (row.dataSetRelation) {
            const auto& relation = *row.dataSetRelation;
            for (const auto& user : relation.usage->users) {
                checkpoint(task);
                const auto kind = user.kind == SclDataSetUserKind::Report ? QStringLiteral("报告")
                    : user.kind == SclDataSetUserKind::Goose ? QStringLiteral("GOOSE") : QStringLiteral("SV");
                users << kind + " · " + SclTaskPath(*s.analysis.document,user.declaration.node) + " · " + SclMmsStateText(user.state);
            }
            if (users.isEmpty()) users << (relation.usage->complete ? QStringLiteral("未发现已解析使用方") : QStringLiteral("使用方受限，尚未确定"));
            if (!relation.usage->complete) users << QStringLiteral("使用方受限；以上为已观察项");
            row.cells[0] = SclTaskPath(*s.analysis.document,relation.dataSet.node);
            row.cells[3] = SclMmsReportMatchText(relation.match);
            row.cells[16] = QStringLiteral("DataSet 标签起始行 %1；FCDA 标签起始行 %2")
                .arg(relation.dataSet.location.line).arg(relation.member.location.line);
        }
        row.cells << users.join('\n');
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
    SclMmsRow referenceRow;
    referenceRow.key = key;
    referenceRow.report = key.parentReport == NoNode ? request.report : key.parentReport;
    referenceRow.addressScope = request.scope;
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
                query.text.clear(); query.fc.clear(); query.offset = 0; query.pageSize = 1; query.presentation = false;
                query.locate = key; query.arrayMember = NoNode;
                const auto addresses = QuerySclMms(s, query, task);
                for (const auto& row : addresses.rows) if (row.key == key) {
                    referenceRow = row;
                    out.title += '.' + (key.declaration != NoNode ? key.field : QStringLiteral("属性依据"));
                    field("domain", "MMS Domain", row.domain, row.domainEvidence, QStringLiteral("离线 LD 命名依据"), !row.domain.isEmpty());
                    field("item", "MMS Item", row.item, {key.declaration, {}, {}}, QStringLiteral("离线属性映射"), !row.item.isEmpty());
                    field("object", "对象引用", row.objectReference, {key.declaration, {}, {}}, {}, !row.objectReference.isEmpty());
                    field("fc", "FC", row.fc, row.fcEvidence, {}, !row.fc.isEmpty());
                    field("shape", "访问形态", Shape(row.shape), {}, row.limitation);
                    field("direct", "直接映射状态", DirectStateText(row.direct), {key.declaration, {}, {}}, row.limitation);
                    field("online", "在线读取", QStringLiteral("未验证"));
                    field("scope", "IED / AP / Server / LD / LN", AddressLocation(s, row), {key.node, {}, {}});
                    field("ln", "LN 实例", SclTaskPath(doc, key.node), {key.node, "lnType", {}});
                    const auto lnDesc = Attr(s, key.node, "desc");
                    if (!lnDesc.isEmpty()) field("ln.desc", "LN 描述", lnDesc, {key.node, "desc", {}});
                    for (const auto declaration : row.templatePath) {
                        const auto& node = doc.nodes[declaration];
                        field("path." + QString::number(declaration), node.name + QStringLiteral(" 定义"),
                            Attr(s, declaration, "name"), {declaration, "name", {}}, Attr(s, declaration, "type"));
                    }
                    if (key.declaration != NoNode)
                        field("bType", "属性基本类型", Attr(s, key.declaration, "bType"), {key.declaration, "bType", {}});
                    if (row.arrayIndex >= 0) {
                        field("array", "数组下标", QString::number(row.arrayIndex), {key.arrayDeclaration, "count", {}});
                        if (!row.component.isEmpty()) field("component", "alternate access 组件", row.component, {key.declaration, {}, {}});
                    }
                    if (row.description) {
                        description(*row.description);
                        for (const auto& info : row.description->fields) {
                            if (info.source != SclDescriptionSource::InstanceAttribute || info.node == NoNode) continue;
                            for (auto child = doc.nodes[info.node].firstChild; child != NoNode; child = doc.nodes[child].nextSibling) {
                                checkpoint(task);
                                const auto& value = doc.nodes[child];
                                if (!value.isScl() || value.name != "Val") continue;
                                const auto group = Attr(s, child, "sGroup");
                                field("val." + QString::number(child), "文件 Val", value.text, {child, {}, {}}, group.isNull()
                                    ? QStringLiteral("文件原值 · 在线未验证") : QStringLiteral("定值组 %1 · 文件原值").arg(group));
                                if (out.limited) break;
                            }
                        }
                    }
                    break;
                }
                out.limited = addresses.limited; out.limitation = addresses.limitation;
            }
            description(DescribeSclSignal(s.analysis, key.node, d.path, task, key.arrayDeclaration, key.arrayIndex));
        }
        break;
    case SclMmsRecordKind::Member:
    case SclMmsRecordKind::Reference:
        if (key.record >= 0 && key.record < int(s.analysis.references.size())) {
            const auto& ref = s.analysis.references[key.record];
            if (key.kind == SclMmsRecordKind::Member && ref.source != key.node) break;
            if (request.reportMatch != SclMmsReportMatch::None) {
                field("report.match", "报告匹配", SclMmsReportMatchText(request.reportMatch), {ref.source, {}, {}});
                const auto ds = doc.nodes[ref.source].parent;
                field("report.dataset", "数据集", Attr(s, ds, "name"), {ds, "name", {}});
                field("report.member", "FCDA 原始序号", QString::number(ref.memberOrdinal), {ref.source, {}, {}});
                field("online", "在线报告", QStringLiteral("未验证"));
            }
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
            field("parent", "父报告定义", Attr(s, referenceRow.report, "name"), {referenceRow.report, {}, {}});
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
    if (key.kind == SclMmsRecordKind::Address || key.kind == SclMmsRecordKind::Signal || key.kind == SclMmsRecordKind::Member) {
        auto query = request; query.kind = SclMmsQueryKind::ObjectEvidence;
        query.presentation = false; query.identityDocument = s.analysis.document;
        const auto evidence = QuerySclMms(s,query,task);
        out.objectEvidence = evidence.objectEvidence;
        if (evidence.limited) { out.limited = true; out.limitation += evidence.limitation; }
        if (out.objectEvidence) {
            const auto& e = *out.objectEvidence;
            field("evidence.do", "DO / SDO 路径", e.objectPath);
            field("evidence.da", "DA / BDA 属性路径", e.attributePath);
            field("evidence.cdc", "所属对象 CDC", e.cdc, {e.cdcEvidence.node,"cdc",{}}, QStringLiteral("CDC 属于 DO / SDO，不属于叶 BDA"));
            field("evidence.type", "叶基本类型 / 结构类型", e.basicType, {e.basicTypeEvidence.node,"bType",{}});
            field("evidence.instance", "实例配置位置", e.instance.state == SclInstanceLocationState::Exact ? QStringLiteral("精确实例配置") : e.instance.reason,
                {e.instance.target.node,{},{}}, e.instance.state == SclInstanceLocationState::Exact
                    ? QStringLiteral("精确匹配实例；文件配置不是在线值") : QStringLiteral("回退到同一已证实实例链；未声明叶 DAI 不代表类型不存在"));
            for (int v = 0; v < e.values.size(); ++v) {
                const auto& value = e.values[v];
                field("evidence.value." + QString::number(v), QStringLiteral("完整引用 / 映射规则"), value.value, {}, value.rule + " · " + value.formula);
                for (int p = 0; p < value.parts.size(); ++p) {
                    const auto& part = value.parts[p];
                    const auto method = part.origin == SclEvidenceOrigin::Declaration ? QStringLiteral("文件直接声明")
                        : part.origin == SclEvidenceOrigin::ReferenceResolution ? QStringLiteral("引用解析")
                        : part.origin == SclEvidenceOrigin::TypeExpansion ? QStringLiteral("类型展开") : QStringLiteral("规则组合");
                    if (part.sources.isEmpty()) field(QStringLiteral("evidence.rule.%1.%2").arg(v).arg(p),part.role,part.value,{},method + " · " + part.rule);
                    for (int k = 0; k < part.sources.size(); ++k) {
                        const auto& source = part.sources[k];
                        field(QStringLiteral("evidence.part.%1.%2.%3").arg(v).arg(p).arg(k),part.role,part.value,
                            {source.node,source.attribute,{}},method + " · " + part.rule,source.present);
                    }
                }
            }
        }
    }
    for (auto& f : out.fields) {
        checkpoint(task);
        const auto source = SclEvidenceAt(s,f.evidence.node,f.evidence.attribute);
        if (source.precision == SclEvidencePrecision::OpeningTag)
            f.explanation += QStringLiteral(" · 标签起始行 %1").arg(source.location.line);
    }
    if (key.kind != SclMmsRecordKind::Address || referenceRow.key.declaration != NoNode)
        out.references = ProjectSclMmsReferences(s, referenceRow, task, true);
    out.total = int(out.fields.size()); return out;
}
}
