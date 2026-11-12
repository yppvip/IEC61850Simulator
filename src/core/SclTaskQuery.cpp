#include "SclAnalysisService.hpp"

namespace iec61850sim
{
using Scl::NodeId;
using Scl::NoNode;

QString SclTaskPath(const Scl::Document& doc, NodeId node)
{
    QStringList path;
    for (int depth = 0; node != NoNode && depth < 32; ++depth)
    {
        const auto& element = doc.nodes[node];
        QString label;
        if (element.name == "LN" || element.name == "LN0")
            label = doc.attribute(node, QStringLiteral("prefix")) + doc.attribute(node, QStringLiteral("lnClass"))
                + doc.attribute(node, QStringLiteral("inst"));
        else if (element.name == "LDevice")
            label = doc.attribute(node, QStringLiteral("inst"));
        else if (element.name == "Server")
            label = QStringLiteral("Server");
        else if (element.name != "SCL")
            label = doc.attribute(node, QStringLiteral("name"));
        if (!label.isEmpty())
            path.prepend(label.left(512));
        node = element.parent;
    }
    return path.join('/');
}

QString SclSignalCategory(const Scl::SignalDefinition& definition)
{
    QStringList labels;
    if (definition.capabilities & Scl::Indication)
        labels << QStringLiteral("遥信建议");
    if (definition.capabilities & Scl::Measurement)
        labels << QStringLiteral("遥测建议");
    if (definition.capabilities & Scl::ControlCandidate)
        labels << QStringLiteral("有控制结构");
    if (definition.capabilities & Scl::Configuration)
        labels << QStringLiteral("定值 / 配置");
    if (labels.empty())
        labels << (definition.composite ? QStringLiteral("复合结构") : QStringLiteral("其他 / 未分类"));
    return labels.join(QStringLiteral(" · "));
}

QStringList SclTaskCells(const SclSnapshot& snapshot, SclTaskRow row)
{
    const auto& doc = *snapshot.analysis.document;
    const auto& task = snapshot.tasks;
    const auto attr = [&](NodeId id, const char* name) {
        return doc.attribute(id, QString::fromLatin1(name)).left(2048);
    };
    switch (row.kind)
    {
    case SclTaskKind::Signal:
    {
        const auto& instance = task.catalog.instances[row.index];
        const auto& definition = task.catalog.definitions[instance.definition];
        auto description = attr(definition.declaration, "desc");
        const auto uses = task.usages.find({instance.logicalNode, definition.path});
        if (uses != task.usages.end() && !uses->second.empty())
        {
            const auto source = snapshot.analysis.references[uses->second.front().reference].instanceDescription;
            if (source != NoNode && !doc.nodes[source].text.isEmpty())
                description = doc.nodes[source].text.left(512);
        }
        const auto path = SclTaskPath(doc, instance.logicalNode) + '.' + definition.path;
        return {description.isEmpty() ? definition.path : description, SclSignalCategory(definition),
                path, definition.cdc, definition.hasUnexpandedArray ? QStringLiteral("数组未展开")
                    : definition.type == NoNode ? QStringLiteral("类型未解析") : QString{}};
    }
    case SclTaskKind::Goose:
    case SclTaskKind::Sv:
    {
        const bool goose = row.kind == SclTaskKind::Goose;
        const auto& stream = goose ? task.goose[row.index] : task.sv[row.index];
        return {attr(stream.control, "name"), SclTaskPath(doc, stream.control),
                attr(stream.control, goose ? "appID" : "smvID"), attr(stream.control, "datSet"),
                QString::number(snapshot.browse.dataSetMembers.value(stream.dataSet).count),
                QString::number(stream.endpoints.size()), QString::number(stream.receivers.size())};
    }
    case SclTaskKind::Input:
    case SclTaskKind::GooseInput:
    case SclTaskKind::SvInput:
    {
        const auto& input = task.inputs[row.index];
        const auto* reference = input.dataReference < 0 ? nullptr : &snapshot.analysis.references[input.dataReference];
        QString state = QStringLiteral("来源未确认");
        if (input.matchedStream != NoNode)
            state = input.inferred ? QStringLiteral("发布流由源控制块字段推导") : QStringLiteral("发布流已关联");
        else if (input.ambiguousControl)
            state = QStringLiteral("发布控制块有歧义");
        else if (reference && reference->state == Scl::LinkState::Resolved)
            state = QStringLiteral("仅数据对象已关联");
        else if (reference && reference->state == Scl::LinkState::External)
            state = QStringLiteral("工程外来源");
        else if (reference && reference->state == Scl::LinkState::Ambiguous)
            state = QStringLiteral("来源有歧义");
        return {attr(input.node, "intAddr"), state, reference ? reference->declared : QString{},
                attr(input.node, "serviceType"), SclTaskPath(doc, input.node)};
    }
    case SclTaskKind::Finding:
    {
        const auto& finding = snapshot.analysis.findings[row.index];
        return {finding.rule, finding.detail, finding.objectPath, QString::number(finding.location.line)};
    }
    }
    return {};
}

std::vector<SclTaskRow> FilterSclTaskRows(const SclSnapshot& snapshot, SclTaskKind kind, NodeId ied,
                                         const QString& text, Scl::TaskControl* task)
{
    const auto& index = snapshot.tasks;
    const auto& doc = *snapshot.analysis.document;
    std::vector<SclTaskRow> matches;
    const auto add = [&](int row, NodeId node) {
        Scl::checkpoint(task);
        if (ied != NoNode && !snapshot.browse.InScope(node, ied))
            return;
        const SclTaskRow item{kind, row};
        if (!text.isEmpty())
        {
            bool matched = false;
            for (const auto& field : SclTaskCells(snapshot, item))
                matched |= field.contains(text, Qt::CaseInsensitive);
            if (!matched && (kind == SclTaskKind::Goose || kind == SclTaskKind::Sv))
            {
                const auto& stream = kind == SclTaskKind::Goose ? index.goose[row] : index.sv[row];
                for (auto endpoint : stream.endpoints)
                {
                    Scl::checkpoint(task);
                    for (auto address = doc.nodes[endpoint].firstChild; address != NoNode;
                         address = doc.nodes[address].nextSibling)
                    {
                        if (!doc.nodes[address].isScl() || doc.nodes[address].name != "Address")
                            continue;
                        for (auto field = doc.nodes[address].firstChild; field != NoNode;
                             field = doc.nodes[field].nextSibling)
                            if (doc.nodes[field].isScl() && doc.nodes[field].name == "P"
                                && doc.nodes[field].text.contains(text, Qt::CaseInsensitive))
                                matched = true;
                    }
                }
            }
            if (!matched)
                return;
        }
        matches.push_back(item);
    };
    switch (kind)
    {
    case SclTaskKind::Signal:
        if (ied != NoNode)
        {
            const auto found = index.signalsByIed.constFind(ied);
            if (found != index.signalsByIed.cend())
                for (auto row : *found)
                    add(row, index.catalog.instances[row].logicalNode);
        }
        else
            for (int i = 0; i < int(index.catalog.instances.size()); ++i)
                add(i, index.catalog.instances[i].logicalNode);
        break;
    case SclTaskKind::Input:
    case SclTaskKind::GooseInput:
    case SclTaskKind::SvInput:
        for (int i = 0; i < int(index.inputs.size()); ++i)
        {
            Scl::checkpoint(task);
            const auto& input = index.inputs[i];
            const auto declared = doc.attribute(input.node, QStringLiteral("serviceType"));
            if (kind == SclTaskKind::Input
                || (kind == SclTaskKind::GooseInput && (declared == "GOOSE"
                    || (input.matchedStream != NoNode && doc.nodes[input.matchedStream].name == "GSEControl")))
                || (kind == SclTaskKind::SvInput && (declared == "SMV"
                    || (input.matchedStream != NoNode && doc.nodes[input.matchedStream].name == "SampledValueControl"))))
                add(i, input.node);
        }
        break;
    case SclTaskKind::Goose:
    case SclTaskKind::Sv:
    {
        const auto& list = kind == SclTaskKind::Goose ? index.goose : index.sv;
        for (int i = 0; i < int(list.size()); ++i)
            add(i, list[i].control);
        break;
    }
    case SclTaskKind::Finding:
        for (int i = 0; i < int(snapshot.analysis.findings.size()); ++i)
            add(i, snapshot.analysis.findings[i].node);
        break;
    }
    return matches;
}
}
