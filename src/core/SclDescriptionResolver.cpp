#include "SclDescriptionResolver.hpp"
#include <utility>

namespace iec61850sim
{
namespace
{
using namespace Scl;
using Source = SclDescriptionSource;
using State = SclDescriptionState;

struct Match
{
    State state = State::Missing;
    NodeId node = NoNode;
    QList<NodeId> candidates;
};

QString Attribute(const Document& doc, NodeId id, const char* name)
{
    return id == NoNode ? QString{} : doc.attribute(id, QString::fromLatin1(name));
}

Match Child(const Document& doc, Match parent, const char* element, const QString& name,
    int index, TaskControl* task, const QString& fc = {})
{
    if (parent.node == NoNode)
        return parent;
    Match match;
    for (auto id = doc.nodes[parent.node].firstChild; id != NoNode; id = doc.nodes[id].nextSibling)
    {
        checkpoint(task);
        const auto& node = doc.nodes[id];
        if (!node.isScl() || node.name != QLatin1StringView(element)
            || (!name.isNull() && Attribute(doc, id, "name") != name)
            || (!fc.isNull() && Attribute(doc, id, "fc") != fc))
            continue;
        const auto ix = Attribute(doc, id, "ix");
        bool valid = false;
        const auto number = ix.toInt(&valid);
        if (index >= 0 && (!valid || number != index))
            continue;
        const auto blocked = !Attribute(doc, id, "sGroup").isNull() ? State::SettingGroup
            : index < 0 && !ix.isNull() ? State::Array : State::Present;
        if (match.candidates.size() < 16)
            match.candidates.push_back(id);
        if (match.state == State::Missing)
        {
            match.state = blocked;
            match.node = blocked == State::Present ? id : NoNode;
        }
        else
        {
            match.state = State::Ambiguous;
            match.node = NoNode;
        }
    }
    if (match.node != NoNode)
        match.candidates.clear();
    return match;
}

Match Target(const ReferenceAnalysis& analysis, Match source, LinkKind kind)
{
    if (source.node == NoNode)
        return source;
    const auto refs = analysis.outgoing.equal_range(source.node);
    for (auto it = refs.first; it != refs.second; ++it)
    {
        const auto& ref = analysis.references[it.value()];
        if (ref.kind != kind)
            continue;
        if (ref.state == LinkState::Resolved && ref.candidates.size() == 1)
            return {State::Present, ref.candidates.front()};
        return {ref.state == LinkState::Ambiguous ? State::Ambiguous : State::Unresolved,
            NoNode, ref.candidates.mid(0, 16)};
    }
    return {State::Unresolved};
}

SclDescriptionField Field(const Document& doc, Source source, Match match)
{
    if (match.node == NoNode)
        return {source, match.state, NoNode, {}, std::move(match.candidates)};
    auto value = source == Source::InstanceDU ? doc.nodes[match.node].text
        : Attribute(doc, match.node, "desc");
    const auto state = source != Source::InstanceDU && value.isNull() ? State::Missing
        : value.trimmed().isEmpty() ? State::Empty : State::Present;
    return {source, state, match.node, std::move(value)};
}

Match InstanceChild(const Document& doc, Match instance, NodeId declaration, const char* element,
    int index, TaskControl* task)
{
    if (!Attribute(doc, declaration, "count").isEmpty() && index < 0)
        return {State::Array};
    return Child(doc, std::move(instance), element, Attribute(doc, declaration, "name"), index, task);
}

Match AttributeInstance(const Document& doc, Match instance, NodeId declaration, int index, TaskControl* task)
{
    const auto* element = Attribute(doc, declaration, "bType") == "Struct" ? "SDI" : "DAI";
    auto match = InstanceChild(doc, std::move(instance), declaration, element, index, task);
    if (match.node == NoNode)
        return match;
    const auto& node = doc.nodes[declaration];
    // Instance elements have no FC selector. A declaration with the same
    // name under another FC cannot be distinguished at the instance level.
    const auto templates = Child(doc, {State::Present, node.parent}, node.name == "DA" ? "DA" : "BDA",
        Attribute(doc, declaration, "name"), -1, task);
    if (templates.state == State::Ambiguous)
        return {State::Ambiguous, NoNode, {match.node}};
    return match;
}

void AddObject(SclDescription& result, const ReferenceAnalysis& analysis, Match instance,
    Match declaration, TaskControl* task)
{
    const auto& doc = *analysis.document;
    const auto type = Target(analysis, declaration, LinkKind::MemberType);
    const auto duType = Child(doc, type, "DA", QStringLiteral("dU"), -1, task, QStringLiteral("DC"));
    auto value = duType;
    if (duType.node != NoNode)
    {
        const auto type = Attribute(doc, duType.node, "bType");
        value = type == "Unicode255" || type == "VisString255"
            ? Child(doc, AttributeInstance(doc, instance, duType.node, -1, task), "Val", {}, -1, task)
            : Match{State::Unresolved};
    }
    result.objectInstance = instance.node;
    result.fields << Field(doc, Source::InstanceDU, std::move(value))
        << Field(doc, Source::InstanceObject, std::move(instance))
        << Field(doc, Source::TemplateObject, std::move(declaration))
        << Field(doc, Source::ObjectType, type);
}

}

const SclDescriptionField* SclDescription::ObjectTitle() const
{
    for (const auto& field : fields)
        if ((field.source == Source::InstanceDU || field.source == Source::InstanceObject
             || field.source == Source::TemplateObject) && field.state == State::Present)
            return &field;
    return nullptr;
}

SclDescription DescribeSclSignal(const Scl::ReferenceAnalysis& analysis, NodeId ln,
    const QString& objectPath, TaskControl* task)
{
    const auto& doc = *analysis.document;
    SclDescription result;
    result.logicalNode = ln;
    result.objectPath = objectPath;
    Match instance{State::Present, ln};
    Match owner = Target(analysis, instance, LinkKind::LogicalNodeType);
    Match declaration;
    const auto parts = objectPath.split('.');
    for (int i = 0; i < parts.size(); ++i)
    {
        declaration = Child(doc, owner, i == 0 ? "DO" : "SDO", parts[i], -1, task);
        if (declaration.node == NoNode)
        {
            instance = declaration;
            break;
        }
        instance = InstanceChild(doc, instance, declaration.node, i == 0 ? "DOI" : "SDI", -1, task);
        if (i + 1 < parts.size())
            owner = Target(analysis, declaration, LinkKind::MemberType);
    }
    AddObject(result, analysis, instance, declaration, task);
    return result;
}

SclDescription DescribeSclMember(const Scl::ReferenceAnalysis& analysis, NodeId member, TaskControl* task)
{
    const auto& doc = *analysis.document;
    SclDescription result;
    result.member = member;
    result.objectPath = Attribute(doc, member, "doName");
    result.attributePath = Attribute(doc, member, "daName");
    result.fc = Attribute(doc, member, "fc");
    const Reference* reference = nullptr;
    const auto refs = analysis.outgoing.equal_range(member);
    for (auto it = refs.first; it != refs.second; ++it)
        if (analysis.references[it.value()].kind == LinkKind::Fcda)
        {
            reference = &analysis.references[it.value()];
            break;
        }

    if (reference)
        result.memberOrdinal = reference->memberOrdinal;
    const auto trace = analysis.fcdaTraces.constFind(member);
    if (!reference || reference->state != LinkState::Resolved || reference->candidates.size() != 1
        || trace == analysis.fcdaTraces.cend())
    {
        const bool ambiguous = reference && reference->state == LinkState::Ambiguous;
        const Match unresolved{ambiguous ? State::Ambiguous : State::Unresolved, NoNode,
            reference ? reference->candidates.mid(0, 16) : QList<NodeId>{}};
        AddObject(result, analysis, unresolved, unresolved, task);
        if (!result.attributePath.isEmpty())
            result.fields << Field(doc, Source::InstanceAttribute, unresolved)
                << Field(doc, Source::TemplateAttribute, unresolved);
    }
    else
    {
        result.logicalNode = reference->logicalNode;
        result.arrayIndex = reference->arrayIndex;
        NodeId indexed = NoNode;
        for (const auto& step : trace->steps)
            if (step.field == FcdaField::Ix)
                indexed = step.node;
        Match instance{State::Present, result.logicalNode};
        NodeId object = NoNode;
        bool hasAttribute = false;
        for (const auto& step : trace->steps)
        {
            const int ix = step.node == indexed ? result.arrayIndex : -1;
            if (step.field == FcdaField::DoName)
            {
                instance = InstanceChild(doc, instance, step.node, object == NoNode ? "DOI" : "SDI", ix, task);
                object = step.node;
            }
            else if (step.field == FcdaField::DaName)
            {
                if (!hasAttribute)
                    AddObject(result, analysis, instance, {State::Present, object}, task);
                hasAttribute = true;
                instance = AttributeInstance(doc, instance, step.node, ix, task);
            }
        }
        if (hasAttribute)
            result.fields << Field(doc, Source::InstanceAttribute, instance)
                << Field(doc, Source::TemplateAttribute, {State::Present, reference->candidates.front()});
        else
            AddObject(result, analysis, instance, {State::Present, object}, task);
    }
    result.fields << Field(doc, Source::Member, {State::Present, member});
    return result;
}

SclDescription DescribeSclAttribute(const ReferenceAnalysis& analysis, const SclDescription& object,
    const QString& path, const QString& fc, const QList<NodeId>& declarations, TaskControl* task)
{
    const auto& doc = *analysis.document;
    SclDescription result;
    result.logicalNode = object.logicalNode;
    result.objectPath = object.objectPath;
    result.attributePath = path;
    result.fc = fc;
    result.objectInstance = object.objectInstance;
    Match instance{State::Unresolved};
    for (const auto& field : object.fields)
        if (field.source == Source::InstanceObject)
        {
            instance = {object.objectInstance == NoNode ? field.state : State::Present,
                object.objectInstance, field.candidates};
            break;
        }
    for (const auto declaration : declarations)
        instance = AttributeInstance(doc, instance, declaration, -1, task);
    result.fields << Field(doc, Source::InstanceAttribute, std::move(instance))
        << Field(doc, Source::TemplateAttribute, {State::Present, declarations.back()});
    return result;
}

QString SclDescriptionLabel(Source source)
{
    switch (source)
    {
    case Source::InstanceDU: return QStringLiteral("对象描述 · 实例 dU");
    case Source::InstanceObject: return QStringLiteral("对象描述 · 实例 desc");
    case Source::TemplateObject: return QStringLiteral("对象描述 · 模板 DO/SDO desc");
    case Source::ObjectType: return QStringLiteral("类型说明 · DOType desc");
    case Source::InstanceAttribute: return QStringLiteral("属性描述 · 实例 desc");
    case Source::TemplateAttribute: return QStringLiteral("属性描述 · 模板 DA/BDA desc");
    case Source::Member: return QStringLiteral("成员声明 · FCDA desc");
    }
    return {};
}

QString SclDescriptionText(const SclDescriptionField& field)
{
    switch (field.state)
    {
    case State::Present: return field.value;
    case State::Empty: return QStringLiteral("已声明空值");
    case State::Missing: return QStringLiteral("未声明");
    case State::Unresolved: return QStringLiteral("来源未定位");
    case State::Ambiguous: return QStringLiteral("来源有歧义");
    case State::Array: return QStringLiteral("数组元素未指定");
    case State::SettingGroup: return QStringLiteral("仅有定值组声明");
    }
    return {};
}
}
