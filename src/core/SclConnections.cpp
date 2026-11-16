#include "SclConnections.hpp"

#include <QSet>
#include <QMap>
#include <map>

namespace iec61850sim
{
using Scl::NodeId;
using Scl::NoNode;

namespace
{
const SclTaskIndex::Stream* Stream(const SclSnapshot& snapshot, NodeId control)
{
    const auto& list = snapshot.analysis.document->nodes[control].name == "GSEControl"
        ? snapshot.tasks.goose : snapshot.tasks.sv;
    const auto found = std::lower_bound(list.begin(), list.end(), control,
        [](const auto& stream, NodeId id) { return stream.control < id; });
    return found != list.end() && found->control == control ? &*found : nullptr;
}

QString Name(const SclSnapshot& snapshot, NodeId node)
{
    return node == NoNode ? QStringLiteral("未确认")
        : snapshot.analysis.document->attribute(node, QStringLiteral("name")).left(256);
}

QString Object(const Scl::Document& doc, NodeId member)
{
    const auto attr = [&](const char* key) { return doc.attribute(member, QLatin1String(key)); };
    const auto da = attr("daName");
    return attr("ldInst") + '/' + attr("prefix") + attr("lnClass") + attr("lnInst")
        + '.' + attr("doName") + (da.isEmpty() ? QString{} : '.' + da);
}
}

QString SclStreamAddresses(const SclSnapshot& snapshot, NodeId control)
{
    const auto* stream = Stream(snapshot, control);
    if (!stream)
        return QStringLiteral("端点未解析");
    const auto& doc = *snapshot.analysis.document;
    QStringList result;
    for (const auto endpoint : stream->endpoints)
    {
        if (result.size() >= 16)
        {
            result << QStringLiteral("其余端点见文件依据");
            break;
        }
        QStringList fields;
        QMap<QString, QStringList> address;
        const auto ap = doc.nodes[endpoint].parent;
        fields << QStringLiteral("AP %1").arg(doc.attribute(ap, QStringLiteral("apName")).left(128));
        int budget = 128;
        for (auto child = doc.nodes[endpoint].firstChild; child != NoNode && budget-- > 0; child = doc.nodes[child].nextSibling)
        {
            if (!doc.nodes[child].isScl() || doc.nodes[child].name != "Address")
                continue;
            for (auto p = doc.nodes[child].firstChild; p != NoNode && budget-- > 0; p = doc.nodes[p].nextSibling)
            {
                if (!doc.nodes[p].isScl() || doc.nodes[p].name != "P")
                    continue;
                const auto type = doc.attribute(p, QStringLiteral("type"));
                if (type == "APPID" || type == "MAC-Address" || type == "VLAN-ID" || type == "VLAN-PRIORITY")
                    address[type].push_back(doc.nodes[p].text.left(128));
            }
        }
        for (const auto& type : {QStringLiteral("APPID"), QStringLiteral("MAC-Address"),
             QStringLiteral("VLAN-ID"), QStringLiteral("VLAN-PRIORITY")})
            if (address.contains(type)) fields << type + ' ' + address[type].join(QStringLiteral(" / "));
        result << fields.join(QStringLiteral(" · "));
        if (budget <= 0) result << QStringLiteral("端点字段预览受限，完整内容见原文");
    }
    return result.isEmpty() ? QStringLiteral("端点未解析，不能确认 APPID") : result.join('\n');
}

SclConnectionList QuerySclConnections(const SclSnapshot& snapshot, NodeId scope,
    const QString& service, const QString& text, Scl::TaskControl* task)
{
    SclConnectionList result;
    const auto& index = snapshot.tasks;
    const auto& doc = *snapshot.analysis.document;
    int totalStreams = 0, declarations = 0, bound = 0, candidates = 0;
    QSet<NodeId> peers;
    const auto add = [&](SclConnection row) {
        Scl::checkpoint(task);
        if (scope != NoNode && row.sender != scope && row.receiver != scope)
            return;
        if (!service.isEmpty() && row.service != service)
            return;
        if (!text.isEmpty() && !(row.sourceName + ' ' + row.targetName + ' ' + row.controlName + ' '
            + row.identifier + ' ' + row.addresses).contains(text, Qt::CaseInsensitive))
            return;
        if (result.rows.size() >= 512)
        {
            result.limited = true;
            return;
        }
        if (row.receiver != NoNode)
            peers.insert(row.receiver);
        bound += row.bound;
        candidates += row.candidates;
        result.rows.push_back(std::move(row));
    };
    const auto streams = [&](const auto& list, const QString& type) {
        for (const auto& stream : list)
        {
            Scl::checkpoint(task);
            std::map<NodeId, std::vector<int>> targets;
            for (const auto link : index.linksByControl.value(stream.control))
            {
                Scl::checkpoint(task);
                const auto input = index.inputs[index.memberLinks[link].input].node;
                targets[snapshot.browse.iedOwners[input]].push_back(link);
            }
            if (targets.empty())
                targets[NoNode] = {};
            bool counted = false;
            for (auto& [receiver, links] : targets)
            {
                SclConnection row;
                row.control = stream.control;
                row.sender = snapshot.browse.iedOwners[stream.control];
                row.receiver = receiver;
                row.service = type;
                row.sourceName = Name(snapshot, row.sender);
                row.targetName = receiver == NoNode ? QStringLiteral("本文件未见接收声明") : Name(snapshot, receiver);
                row.controlName = SclTaskPath(doc, stream.control);
                row.identifier = doc.attribute(stream.control, type == "GOOSE" ? QStringLiteral("appID") : QStringLiteral("smvID")).left(512);
                row.addresses = SclStreamAddresses(snapshot, stream.control);
                row.links = std::move(links);
                for (const auto link : row.links)
                    index.memberLinks[link].bound ? ++row.bound : ++row.candidates;
                const auto before = result.rows.size();
                add(std::move(row));
                if (result.rows.size() != before)
                    counted = true;
            }
            totalStreams += counted;
        }
    };
    streams(index.goose, QStringLiteral("GOOSE"));
    streams(index.sv, QStringLiteral("SV"));
    for (const auto& input : index.inputs)
    {
        Scl::checkpoint(task);
        if (!index.linksByInput.value(input.node).empty())
            continue;
        SclConnection row;
        row.unresolvedInput = input.node;
        if (input.dataReference >= 0)
        {
            const auto& reference = snapshot.analysis.references[input.dataReference];
            if (reference.state == Scl::LinkState::Resolved && reference.logicalNode != NoNode)
                row.sender = snapshot.browse.iedOwners[reference.logicalNode];
        }
        row.receiver = snapshot.browse.iedOwners[input.node];
        row.sourceName = doc.attribute(input.node, QStringLiteral("iedName")).left(256);
        row.targetName = Name(snapshot, row.receiver);
        row.controlName = Object(doc, input.node);
        const auto declared = doc.attribute(input.node, QStringLiteral("serviceType"));
        row.service = declared == "SMV" ? QStringLiteral("SV") : declared.isEmpty() ? QStringLiteral("服务未确认") : declared;
        row.addresses = QStringLiteral("发布流未确认，APPID 未知");
        const auto before = result.rows.size();
        add(std::move(row));
        declarations += result.rows.size() != before;
    }
    result.limited |= index.relationsLimited;
    result.statistics = QStringLiteral("%1 个 IED · %2 条发布流 · %3 个接收 IED · %4 条明确成员映射 · %5 条候选映射 · %6 条未定位发布流的输入")
        .arg(scope == NoNode ? snapshot.browse.ieds.size() : 1).arg(totalStreams).arg(peers.size())
        .arg(bound).arg(candidates).arg(declarations);
    if (result.limited)
        result.statistics += QStringLiteral(" · 覆盖受限，请缩小 IED/筛选范围");
    return result;
}

SclConnectionDetail DescribeSclConnection(const SclSnapshot& snapshot, const SclConnection& connection,
    int offset, bool mappedOnly, Scl::TaskControl* task, bool publication)
{
    SclConnectionDetail result;
    const auto& doc = *snapshot.analysis.document;
    result.title = connection.sourceName + QStringLiteral(" → ") + connection.targetName;
    result.description = connection.service + " · " + connection.controlName + '\n'
        + QStringLiteral("声明标识：") + connection.identifier + '\n' + connection.addresses + '\n'
        + QStringLiteral("明确映射 %1 · 成员匹配候选 %2。虚线仅表示候选；离线配置不能证明现场已收到。")
            .arg(connection.bound).arg(connection.candidates);
    const auto evidence = [&](const QString& label, NodeId node) {
        if (node != NoNode)
            result.evidence.push_back({label, node});
    };
    if (connection.control == NoNode)
    {
        evidence(QStringLiteral("未定位流的 ExtRef"), connection.unresolvedInput);
        result.description += QStringLiteral("\n源控制块/数据成员证据不足或冲突；请查阅原文，不能按名称猜测接线。");
        result.members.push_back({{QStringLiteral("—"), connection.controlName, {}, connection.targetName,
            doc.attribute(connection.unresolvedInput, QStringLiteral("intAddr")), QStringLiteral("发布流未确认")},
            {}, NoNode, connection.unresolvedInput, true});
        result.total = 1;
        return result;
    }
    const auto* stream = Stream(snapshot, connection.control);
    if (!stream)
        return result;
    evidence(QStringLiteral("发送控制块"), stream->control);
    evidence(QStringLiteral("发布数据集"), stream->dataSet);
    if (publication)
    {
        const auto attr = [&](NodeId node, const char* key) { return doc.attribute(node, QLatin1String(key)); };
        const auto display = [](const QString& value) {
            return value.isNull() ? QStringLiteral("未声明") : value.isEmpty() ? QStringLiteral("已声明空值") : value;
        };
        result.description = SclTaskPath(doc, stream->control)
            + QStringLiteral("\n数据集 %1 · %2 %3 · confRev %4").arg(display(attr(stream->control, "datSet")),
                connection.service == "SV" ? QStringLiteral("smvID") : QStringLiteral("appID"),
                display(connection.identifier), display(attr(stream->control, "confRev")));
        if (connection.service == "SV")
        {
            result.description += QStringLiteral("\n采样配置 · smpRate %1 · smpMod %2 · nofASDU %3\n单位/换算依据未确认")
                .arg(display(attr(stream->control, "smpRate")), display(attr(stream->control, "smpMod")),
                     display(attr(stream->control, "nofASDU")));
            QStringList options;
            for (auto child = doc.nodes[stream->control].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
            {
                Scl::checkpoint(task);
                const auto& node = doc.nodes[child];
                if (!node.isScl() || node.name != "SmvOpts")
                    continue;
                evidence(QStringLiteral("SmvOpts 声明"), child);
                QStringList values;
                for (int i = 0; i < node.attributeCount; ++i)
                {
                    const auto& item = doc.attributes[node.firstAttribute + i];
                    if (item.namespaceUri.isEmpty())
                        values << item.name + '=' + item.value;
                }
                options << (values.isEmpty() ? QStringLiteral("已声明，无属性") : values.join(QStringLiteral(" · ")));
                if (options.size() == 16)
                    break;
            }
            result.description += QStringLiteral("\nSmvOpts · ") + (options.isEmpty()
                ? QStringLiteral("未声明") : options.join(QStringLiteral(" / ")));
        }
        for (const auto endpoint : stream->endpoints)
        {
            Scl::checkpoint(task);
            if (result.endpoints.size() == 512)
            {
                result.endpointsLimited = true;
                break;
            }
            const auto ap = doc.nodes[endpoint].parent;
            const auto subnet = doc.nodes[ap].parent;
            QMap<QString, QStringList> address;
            for (auto child = doc.nodes[endpoint].firstChild; child != NoNode; child = doc.nodes[child].nextSibling)
            {
                Scl::checkpoint(task);
                if (!doc.nodes[child].isScl() || doc.nodes[child].name != "Address")
                    continue;
                for (auto p = doc.nodes[child].firstChild; p != NoNode; p = doc.nodes[p].nextSibling)
                {
                    Scl::checkpoint(task);
                    if (!doc.nodes[p].isScl() || doc.nodes[p].name != "P")
                        continue;
                    const auto type = attr(p, "type");
                    if (type == "APPID" || type == "MAC-Address" || type == "VLAN-ID" || type == "VLAN-PRIORITY")
                        address[type] << doc.nodes[p].text;
                }
            }
            QStringList cells{attr(subnet, "name"), attr(ap, "apName")};
            for (const auto* field : {"APPID", "MAC-Address", "VLAN-ID", "VLAN-PRIORITY"})
                cells << address.value(QLatin1String(field)).join('\n');
            result.endpoints.push_back({std::move(cells), endpoint});
        }
    }
    for (size_t i = 0; i < stream->endpoints.size() && i < 16; ++i)
        evidence(QStringLiteral("通信端点 %1").arg(i + 1), stream->endpoints[i]);
    const auto members = snapshot.browse.dataSetMembers.value(stream->dataSet);
    std::map<NodeId, std::vector<int>> mapped;
    for (const auto link : connection.links)
    {
        Scl::checkpoint(task);
        mapped[snapshot.analysis.references[snapshot.tasks.memberLinks[link].memberReference].source].push_back(link);
    }
    const auto append = [&](NodeId member, int ordinal, int link) {
        if (result.total++ < offset || result.members.size() >= SclConnectionDetail::PageSize)
            return;
        SclConnectionMember row;
        row.member = member;
        row.cells = {QString::number(ordinal), Object(doc, member), {}, connection.targetName, {},
            QStringLiteral("本文件未见此成员的接收声明")};
        const auto references = snapshot.analysis.outgoing.equal_range(member);
        for (auto it = references.first; it != references.second; ++it)
        {
            const auto& reference = snapshot.analysis.references[it.value()];
            if (reference.kind != Scl::LinkKind::Fcda) continue;
            if (reference.state != Scl::LinkState::Resolved)
                row.cells[5] = QStringLiteral("发送成员未唯一解析，接收关系未知");
            else
            {
                row.mmsDomain = reference.mmsDomain;
                row.mmsItem = reference.mmsItem;
                row.arrayIndex = reference.arrayIndex;
            }
            break;
        }
        row.description = DescribeSclMember(snapshot.analysis, member, task);
        if (const auto* title = row.description.ObjectTitle())
            row.cells[2] = title->value;
        if (publication)
        {
            row.cells[4] = doc.attribute(member, QStringLiteral("fc"));
            for (const auto& field : row.description.fields)
                if ((field.source == SclDescriptionSource::InstanceAttribute || field.source == SclDescriptionSource::TemplateAttribute)
                    && field.state == SclDescriptionState::Present)
                {
                    row.cells[3] = field.value;
                    break;
                }
            const auto found = mapped.find(member);
            int bound = 0, candidates = 0;
            if (found != mapped.end())
                for (const int index : found->second)
                {
                    Scl::checkpoint(task);
                    const auto& link = snapshot.tasks.memberLinks[index];
                    link.bound ? ++bound : ++candidates;
                    if (row.reception.size() < 8)
                    {
                        const auto input = snapshot.tasks.inputs[link.input].node;
                        row.reception.push_back({(link.bound ? QStringLiteral("明确接收 · ")
                            : QStringLiteral("候选接收 · ")) + Name(snapshot, snapshot.browse.iedOwners[input]), input});
                    }
                }
            if (bound || candidates)
                row.cells[5] = QStringLiteral("明确映射 %1 · 候选 %2").arg(bound).arg(candidates);
        }
        if (link >= 0)
        {
            const auto& mapping = snapshot.tasks.memberLinks[link];
            row.input = snapshot.tasks.inputs[mapping.input].node;
            row.candidate = !mapping.bound;
            row.cells[4] = doc.attribute(row.input, QStringLiteral("intAddr"));
            row.cells[5] = mapping.bound ? QStringLiteral("源控制块与成员绑定")
                : mapping.aggregate ? QStringLiteral("对象包含候选 · 未确认绑定") : QStringLiteral("成员匹配候选 · 未确认绑定");
        }
        if (!publication)
            row.cells[1] += QStringLiteral(" [FC %1]").arg(doc.attribute(member, QStringLiteral("fc")));
        result.members.push_back(std::move(row));
    };
    for (int ordinal = 0; ordinal < members.count; ++ordinal)
    {
        Scl::checkpoint(task);
        const auto member = snapshot.browse.memberRows[members.first + ordinal];
        const auto found = mapped.find(member);
        if (publication || found == mapped.end())
        {
            if (!mappedOnly)
                append(member, ordinal + 1, -1);
        }
        else
            for (const auto link : found->second)
                append(member, ordinal + 1, link);
    }
    if (stream->dataSet == NoNode)
        result.description += QStringLiteral("\n数据集未解析，成员覆盖不足。");
    return result;
}

SclConnectionDetail DescribeSclPublication(const SclSnapshot& snapshot, NodeId control, int offset,
    Scl::TaskControl* task)
{
    const auto* stream = Stream(snapshot, control);
    if (!stream)
        return {};
    const auto& doc = *snapshot.analysis.document;
    SclConnection publication;
    publication.control = control;
    const bool sv = doc.nodes[control].name == "SampledValueControl";
    publication.service = sv ? QStringLiteral("SV") : QStringLiteral("GOOSE");
    publication.identifier = doc.attribute(control, sv ? QStringLiteral("smvID") : QStringLiteral("appID"));
    publication.controlName = doc.attribute(control, QStringLiteral("name"));
    publication.sourceName = Name(snapshot, snapshot.browse.iedOwners[control]);
    const auto links = snapshot.tasks.linksByControl.constFind(control);
    if (links != snapshot.tasks.linksByControl.cend())
        publication.links = *links;
    return DescribeSclConnection(snapshot, publication, offset, false, task, true);
}
}
