#include "SclAnalysisService.hpp"
#include <algorithm>
#include <QSet>

namespace iec61850sim
{
using Scl::NodeId;
using Scl::NoNode;

namespace
{
NodeId UniqueChild(const Scl::Document& doc, NodeId parent, const QString& kind,
                   const QString& name, Scl::TaskControl* task)
{
    if (parent == NoNode)
        return NoNode;
    NodeId found = NoNode;
    for (auto child = doc.nodes[parent].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
    {
        Scl::checkpoint(task);
        const auto& element = doc.nodes[child];
        if (!element.isScl() || element.name != kind
            || (!name.isNull() && doc.attribute(child, QStringLiteral("name")) != name)
            || !doc.attribute(child, QStringLiteral("ix")).isNull()
            || !doc.attribute(child, QStringLiteral("sGroup")).isNull())
            continue;
        if (found != NoNode)
            return NoNode;
        found = child;
    }
    return found;
}

NodeId MemberType(const Scl::ReferenceAnalysis& analysis, NodeId source)
{
    const auto refs = analysis.outgoing.equal_range(source);
    for (auto it = refs.first; it != refs.second; ++it)
    {
        const auto& ref = analysis.references[it.value()];
        if (ref.kind == Scl::LinkKind::MemberType && ref.state == Scl::LinkState::Resolved
            && ref.candidates.size() == 1)
            return ref.candidates.front();
    }
    return NoNode;
}
}

SclTaskDetail DescribeSclTaskRow(const SclSnapshot& snapshot, SclTaskRow row, Scl::TaskControl* task)
{
    const auto& doc = *snapshot.analysis.document;
    const auto& index = snapshot.tasks;
    constexpr int displayLimit = 24;
    SclTaskDetail detail;
    QStringList lines;
    const auto attr = [&](NodeId node, const char* name) {
        return doc.attribute(node, QString::fromLatin1(name)).left(512);
    };
    const auto field = [&](const QString& name, const QString& value) {
        lines << name + QStringLiteral("  ") + (value.isNull() ? QStringLiteral("未声明") :
            value.isEmpty() ? QStringLiteral("已声明空值") : value);
    };
    const auto evidence = [&](const QString& label, NodeId node) {
        if (node != NoNode)
            detail.evidence.push_back({label, node});
    };
    const auto truncated = [&](size_t count) {
        if (count > displayLimit)
            lines << QStringLiteral("另有 %1 项，可在高级视图浏览").arg(count - displayLimit);
    };
    Scl::checkpoint(task);
    switch (row.kind)
    {
    case SclTaskKind::Signal:
    {
        const auto& instance = index.catalog.instances[row.index];
        const auto& definition = index.catalog.definitions[instance.definition];
        const auto cells = SclTaskCells(snapshot, row);
        lines << QStringLiteral("这是什么") << cells[2] << cells[1];
        field(QStringLiteral("CDC"), definition.cdc);
        field(QStringLiteral("模板描述"), attr(definition.declaration, "desc"));
        lines << QStringLiteral("分类规则 v0.1 · 根据 LN / DO 语义、CDC 与属性功能分组给出建议。");
        evidence(QStringLiteral("逻辑节点实例"), instance.logicalNode);
        auto object = definition.hasUnexpandedArray ? NoNode : instance.logicalNode;
        const auto segments = definition.path.split('.');
        for (int i = 0; i < segments.size() && object != NoNode; ++i)
            object = UniqueChild(doc, object, i == 0 ? QStringLiteral("DOI") : QStringLiteral("SDI"),
                segments[i], task);
        if (object != NoNode)
        {
            field(QStringLiteral("实例 desc"), attr(object, "desc"));
            evidence(QStringLiteral("对象实例声明"), object);
        }
        evidence(QStringLiteral("对象模板声明"), definition.declaration);
        evidence(QStringLiteral("CDC 类型定义"), definition.type);
        if (definition.capabilities & Scl::ControlCandidate)
        {
            const auto declaration = UniqueChild(doc, definition.type, QStringLiteral("DA"), QStringLiteral("ctlModel"), task);
            const auto model = UniqueChild(doc, object, QStringLiteral("DAI"), QStringLiteral("ctlModel"), task);
            const auto value = UniqueChild(doc, model, QStringLiteral("Val"), {}, task);
            if (declaration != NoNode && attr(declaration, "fc") == "CF" && value != NoNode)
            {
                const auto declared = doc.nodes[value].text.trimmed().left(512);
                field(QStringLiteral("实例 ctlModel"), declared);
                evidence(QStringLiteral("实例 ctlModel 原文"), value);
                if (declared == "status-only" || declared == "0")
                    lines << QStringLiteral("实例声明仅状态，当前不建议归为遥控。");
                else if (QStringList{"direct-with-normal-security", "sbo-with-normal-security",
                         "direct-with-enhanced-security", "sbo-with-enhanced-security"}.contains(declared))
                    lines << QStringLiteral("遥控建议 · 类型具备 CO/Oper，实例声明了控制模式。");
                else
                    lines << QStringLiteral("控制模式未识别，遥控分类证据不足。");
            }
            else
                lines << QStringLiteral("类型声明了 CO/Oper。实例 ctlModel 未确认，遥控分类证据不足。");
        }
        if (definition.capabilities & Scl::Measurement)
        {
            const auto declaration = UniqueChild(doc, definition.type, QStringLiteral("DA"), QStringLiteral("units"), task);
            const auto unitType = MemberType(snapshot.analysis, declaration);
            const auto unitDeclaration = UniqueChild(doc, unitType, QStringLiteral("BDA"), QStringLiteral("SIUnit"), task);
            const auto units = UniqueChild(doc, object, QStringLiteral("SDI"), QStringLiteral("units"), task);
            const auto unit = UniqueChild(doc, units, QStringLiteral("DAI"), QStringLiteral("SIUnit"), task);
            const auto value = UniqueChild(doc, unit, QStringLiteral("Val"), {}, task);
            if (unitDeclaration != NoNode && value != NoNode)
            {
                field(QStringLiteral("单位 SIUnit 原值"), doc.nodes[value].text.left(512));
                evidence(QStringLiteral("单位原文"), value);
            }
            else
                lines << QStringLiteral("单位没有可确认的实例声明。");
        }
        if (definition.hasUnexpandedArray)
            lines << QStringLiteral("数组元素未展开，本行代表模板路径。");
        if (definition.type != NoNode)
        {
            lines << QStringLiteral("属性定义（附属属性不计为独立信号）");
            int count = 0;
            QSet<NodeId> ancestry;
            bool limited = false;
            const auto expand = [&](auto&& self, NodeId type, const QString& prefix, const QString& fc, int depth) -> void {
                Scl::checkpoint(task);
                if (depth >= 16 || ancestry.contains(type))
                {
                    limited = true;
                    return;
                }
                ancestry.insert(type);
                for (auto child = doc.nodes[type].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
                {
                    Scl::checkpoint(task);
                    const auto& item = doc.nodes[child];
                    if (!item.isScl() || item.name != (depth ? "BDA" : "DA"))
                        continue;
                    if (count++ >= displayLimit)
                    {
                        limited = true;
                        break;
                    }
                    const auto name = prefix + attr(child, "name");
                    const auto grouping = depth ? fc : attr(child, "fc");
                    const auto dataType = attr(child, "bType");
                    lines << QStringLiteral("%1 · FC %2 · %3").arg(name, grouping, dataType);
                    const auto arrayCount = attr(child, "count");
                    if (!arrayCount.isNull())
                        field(QStringLiteral("数组声明 count"), arrayCount);
                    evidence(QStringLiteral("属性定义 %1").arg(name), child);
                    const auto nested = dataType == "Struct" ? MemberType(snapshot.analysis, child) : NoNode;
                    if (nested != NoNode)
                        self(self, nested, name + '.', grouping, depth + 1);
                }
                ancestry.remove(type);
            };
            expand(expand, definition.type, {}, {}, 0);
            if (limited)
                lines << QStringLiteral("属性展开达到预算或遇到类型环，可打开类型原文。");
        }
        lines << QString() << QStringLiteral("如何使用");
        const auto uses = index.usages.find({instance.logicalNode, definition.path});
        if (uses == index.usages.end())
            lines << QStringLiteral("未找到此对象路径的已解析数据集成员。");
        else
        {
            lines << QStringLiteral("%1 次已解析成员出现").arg(uses->second.size());
            for (size_t i = 0; i < qMin<size_t>(uses->second.size(), displayLimit); ++i)
            {
                Scl::checkpoint(task);
                const auto& use = uses->second[i];
                const auto& ref = snapshot.analysis.references[use.reference];
                const auto label = QStringLiteral("%1 · 成员 %2").arg(attr(use.dataSet, "name")).arg(ref.memberOrdinal);
                lines << label + QStringLiteral(" · ") + ref.declared.left(512);
                evidence(label, ref.source);
                if (ref.instanceDescription != NoNode && i == 0)
                {
                    field(QStringLiteral("实例描述 dU"), doc.nodes[ref.instanceDescription].text.left(512));
                    evidence(QStringLiteral("实例描述 dU"), ref.instanceDescription);
                }
                field(QStringLiteral("MMS Domain"), ref.mmsDomain.left(512));
                field(QStringLiteral("MMS Item"), ref.mmsItem.left(512));
                const auto controls = index.controlsByDataSet.constFind(use.dataSet);
                if (controls != index.controlsByDataSet.cend())
                    for (size_t n = 0; n < qMin<size_t>(controls->size(), 4); ++n)
                    {
                        const auto control = (*controls)[n];
                        const auto title = doc.nodes[control].name + QStringLiteral(" · ") + attr(control, "name");
                        lines << title;
                        evidence(title, control);
                        if (doc.nodes[control].name == "ReportControl")
                        {
                            const auto& reports = snapshot.analysis.reports;
                            const auto report = std::lower_bound(reports.begin(), reports.end(), control,
                                [](const auto& item, NodeId target) { return item.source < target; });
                            if (report != reports.end() && report->source == control)
                            {
                                field(QStringLiteral("原始 rptID"), report->rptId.left(512));
                                field(QStringLiteral("RCB 定义引用"), report->definitionReference.left(512));
                            }
                        }
                    }
            }
            truncated(uses->second.size());
        }
        lines << QString() << QStringLiteral("谁声明接收");
        const auto inputs = index.inputsBySignal.find({instance.logicalNode, definition.path});
        if (inputs == index.inputsBySignal.end())
            lines << QStringLiteral("当前文件没有已解析到此对象路径的接收声明。");
        else
        {
            lines << QStringLiteral("%1 条接收端对象声明").arg(inputs->second.size());
            for (size_t i = 0; i < qMin<size_t>(inputs->second.size(), displayLimit); ++i)
            {
                Scl::checkpoint(task);
                const auto& input = index.inputs[inputs->second[i]];
                const auto label = SclTaskPath(doc, input.node);
                lines << label + (input.matchedStream == NoNode ? QStringLiteral(" · 仅对象关联")
                    : QStringLiteral(" · 有发布流证据"));
                evidence(QStringLiteral("接收声明 %1").arg(label), input.node);
            }
            truncated(inputs->second.size());
        }
        break;
    }
    case SclTaskKind::Goose:
    case SclTaskKind::Sv:
    {
        const bool goose = row.kind == SclTaskKind::Goose;
        const auto& stream = goose ? index.goose[row.index] : index.sv[row.index];
        lines << SclTaskPath(doc, stream.control) << QStringLiteral("发送配置");
        evidence(QStringLiteral("发送控制块"), stream.control);
        field(goose ? QStringLiteral("appID") : QStringLiteral("smvID"), attr(stream.control, goose ? "appID" : "smvID"));
        field(QStringLiteral("confRev"), attr(stream.control, "confRev"));
        if (!goose)
        {
            field(QStringLiteral("smpRate"), attr(stream.control, "smpRate"));
            field(QStringLiteral("smpMod"), attr(stream.control, "smpMod"));
            field(QStringLiteral("nofASDU"), attr(stream.control, "nofASDU"));
            lines << QStringLiteral("保留原始采样声明，未换算为 Hz。");
        }
        lines << QStringLiteral("通信端点 %1 项").arg(stream.endpoints.size());
        for (size_t i = 0; i < qMin<size_t>(stream.endpoints.size(), displayLimit); ++i)
        {
            const auto endpoint = stream.endpoints[i];
            evidence(QStringLiteral("通信端点 %1").arg(i + 1), endpoint);
            for (auto child = doc.nodes[endpoint].firstChild; child != NoNode && lines.size() < 128;
                 child = doc.nodes[child].nextSibling)
            {
                Scl::checkpoint(task);
                if (!doc.nodes[child].isScl())
                    continue;
                const auto& name = doc.nodes[child].name;
                if (name == "MinTime" || name == "MaxTime")
                    field(name, doc.nodes[child].text.left(512) + ' ' + attr(child, "multiplier") + attr(child, "unit"));
                if (name == "Address")
                    for (auto item = doc.nodes[child].firstChild, n = 0; item != NoNode && n < displayLimit;
                         item = doc.nodes[item].nextSibling, ++n)
                        if (doc.nodes[item].isScl() && doc.nodes[item].name == "P")
                            field(attr(item, "type"), doc.nodes[item].text.left(512));
            }
        }
        truncated(stream.endpoints.size());
        const auto members = snapshot.browse.dataSetMembers.value(stream.dataSet);
        field(QStringLiteral("声明数据集"), attr(stream.control, "datSet"));
        evidence(QStringLiteral("解析到的数据集"), stream.dataSet);
        lines << (stream.dataSet == NoNode ? QStringLiteral("数据集未解析")
            : QStringLiteral("%1 条原始成员，按声明顺序").arg(members.count));
        for (int i = 0; i < qMin(members.count, displayLimit); ++i)
        {
            const auto member = snapshot.browse.memberRows[members.first + i];
            const auto label = QStringLiteral("成员 %1").arg(i + 1);
            lines << QStringLiteral("%1 · %2.%3 · FC %4").arg(label, attr(member, "doName"),
                attr(member, "daName"), attr(member, "fc"));
            evidence(label, member);
        }
        truncated(members.count);
        lines << QStringLiteral("有发布流证据的接收声明 %1 条").arg(stream.receivers.size());
        for (size_t i = 0; i < qMin<size_t>(stream.receivers.size(), displayLimit); ++i)
        {
            const auto receiver = stream.receivers[i];
            const auto label = SclTaskPath(doc, receiver);
            lines << label;
            evidence(QStringLiteral("接收声明 %1").arg(label), receiver);
        }
        truncated(stream.receivers.size());
        break;
    }
    case SclTaskKind::Input:
    case SclTaskKind::GooseInput:
    case SclTaskKind::SvInput:
    {
        const auto& input = index.inputs[row.index];
        const auto cells = SclTaskCells(snapshot, row);
        lines << QStringLiteral("接收声明") << cells[4] << cells[1];
        field(QStringLiteral("原始 serviceType"), attr(input.node, "serviceType"));
        field(QStringLiteral("声明的源控制块"), attr(input.node, "srcCBName"));
        field(QStringLiteral("声明的源对象"), cells[2]);
        evidence(QStringLiteral("ExtRef 声明"), input.node);
        evidence(QStringLiteral("已匹配的发布控制块"), input.matchedStream);
        break;
    }
    case SclTaskKind::Finding:
    {
        const auto& finding = snapshot.analysis.findings[row.index];
        lines << finding.rule << finding.detail << finding.suggestion << finding.scope;
        evidence(QStringLiteral("问题对应节点"), finding.node);
        break;
    }
    }
    detail.text = lines.join('\n');
    if (detail.text.size() > 24000)
        detail.text = detail.text.left(24000) + QStringLiteral("\n详情已截断，文件依据仍可定位原文。");
    return detail;
}
}
