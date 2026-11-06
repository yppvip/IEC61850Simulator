#include "SclReferenceResolver.hpp"

#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <utility>

namespace Scl {
namespace {

enum Rule { XmlFile, XmlSyntax, XmlRoot, XmlSafety, Structure, Duplicate, LnType, MemberType,
            TypeGraph, ControlSet, FcdaRule, Network, ExtRefRule, EmptySet, Unused, Address,
            ReportConflict, Version, Extension, Schema };

struct RuleSpec { const char *id; const char *suggestion; const char *scope; };
constexpr RuleSpec Rules[] = {
    {"XML001", "Check file access and content.", "Input file"},
    {"XML002", "Check XML syntax and encoding at the indicated line.", "XML reader"},
    {"XML003", "Check the root element and SCL namespace.", "XML reader"},
    {"XML004", "Remove the DTD or review the import limits.", "XML reader"},
    {"STR001", "Check the identifying attribute on this element.", "Indexed SCL definitions"},
    {"KEY001", "Use unique keys within this scope.", "Indexed SCL definitions"},
    {"TYP001", "Check lnType against the declared LNodeType IDs.", "Document templates"},
    {"TYP002", "Check the referenced template ID and its kind.", "Document templates"},
    {"TYP003", "Remove recursive template links or shorten the expansion chain.", "Document templates"},
    {"REF001", "Check the data set name within the owning LN.", "Owning LN"},
    {"REF002", "Check the member path, FC and array selector against the templates.", "Owning server"},
    {"REF003", "Check the IED/AP and the published control block.", "Communication and IED model"},
    {"REF004", "Check the binding and whether the source is in this document.", "ExtRef source"},
    {"ENG001", "Check whether the empty data set is intentional.", "Document data sets"},
    {"ENG002", "", "Document definitions"},
    {"ENG003", "", "Communication address and redundancy"},
    {"ENG004", "", "Report instances"},
    {"VER001", "Confirm the standard edition before edition-specific checks.", "Root SCL attributes"},
    {"EXT001", "Consult the vendor format for extension semantics.", "Foreign namespaces"},
    {"XSD", "", "Full schema validation"}
};

constexpr int MaxTypeDepth = 64;

struct Match {
    LinkState state;
    QList<NodeId> nodes;

    NodeId target() const { return state == LinkState::Resolved ? nodes.front() : NoNode; }
};

Match matched(QList<NodeId> nodes)
{
    const auto state = nodes.empty() ? LinkState::Missing
        : nodes.size() == 1 ? LinkState::Resolved : LinkState::Ambiguous;
    return {state, std::move(nodes)};
}

bool isLn(const QString &kind) { return kind == "LN" || kind == "LN0"; }

bool isControl(const QString &kind)
{
    return kind == "ReportControl" || kind == "GSEControl" || kind == "SampledValueControl"
        || kind == "LogControl";
}

bool isTemplate(const QString &kind)
{
    return kind == "LNodeType" || kind == "DOType" || kind == "DAType" || kind == "EnumType";
}

QString key(const QString &kind, const QString &name) { return kind + QChar(0) + name; }

bool unsignedNumber(const QString &value, int &number)
{
    if (value.isEmpty())
        return false;
    for (const auto c : value) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
    }
    bool ok = false;
    number = value.toInt(&ok);
    return ok;
}

QString stateDetail(LinkState state)
{
    switch (state) {
    case LinkState::Missing: return QStringLiteral("No matching definition in the applicable scope.");
    case LinkState::Ambiguous: return QStringLiteral("More than one definition matches in the applicable scope.");
    case LinkState::Unbound: return QStringLiteral("No static binding is declared.");
    case LinkState::External: return QStringLiteral("The source IED is outside the loaded document.");
    case LinkState::Unsupported: return QStringLiteral("The declared form is outside this rule's coverage.");
    case LinkState::Blocked: return QStringLiteral("A prerequisite reference or scope could not be resolved.");
    case LinkState::Partial: return QStringLiteral("Only part of the source binding is declared.");
    case LinkState::Resolved: return {};
    }
    return {};
}

class Analysis {
public:
    Analysis(const Result &input, TaskControl *task);
    ReferenceAnalysis run();

private:
    const Node &node(NodeId id) const { return result.document->nodes[id]; }
    QString attr(NodeId id, const char *name) const { return result.document->attribute(id, QString::fromLatin1(name)); }
    bool kind(NodeId id, const char *name) const { return id != NoNode && active[id] && node(id).name == QLatin1String(name); }
    QString lnName(NodeId id) const;
    QString path(NodeId id) const;
    NodeId ancestor(NodeId id, const char *name) const;
    void evaluate(Rule rule, bool limited = false);
    void finding(Rule rule, NodeId id, const QString &detail, Severity severity = Severity::Error,
                 SourceLocation location = {});
    void addKey(NodeId id, NodeId scope, const QString &type, const QString &value);
    Match lookup(NodeId scope, const QString &type, const QString &value) const;
    void index();
    void types();
    void checkTypeGraph(const QHash<NodeId, std::vector<NodeId>> &edges);
    void record(Rule rule, Reference ref, Match match, const QString &detail = {});
    void record(Rule rule, LinkKind kind, NodeId id, const QString &declared, Match match);
    QString domain(NodeId ld) const;
    Match locateLn(NodeId server, const QString &ld, const QString &ln, FcdaTrace *trace = nullptr) const;
    Match serverFor(NodeId ap) const;
    Match sourceLn(NodeId ied, const QString &ld, const QString &ln) const;
    Match hasFc(NodeId type, const QString &fc, FcdaTrace *trace) const;
    Match object(NodeId ln, const QString &doName, const QString &daName, const QString &fc,
                 const QString &ix, Reference &ref, QString &detail, FcdaTrace *trace = nullptr) const;
    void fcda(NodeId id, int ordinal);
    void control(NodeId id);
    void communication(NodeId id);
    void extRef(NodeId id);

    ReferenceAnalysis result;
    const Diagnostic &inputError;
    TaskControl *m_task;
    std::vector<bool> active;
    QHash<NodeId, QHash<QString, QList<NodeId>>> keys;
    QHash<NodeId, Match> typeTargets;
    QSet<NodeId> cyclicTypes;
};

Analysis::Analysis(const Result &input, TaskControl *task) : inputError(input.diagnostic), m_task(task)
{
    result.document = input.document;
    result.source = input.source;
    if (input.source) {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        const auto &bytes = input.source->bytes();
        constexpr int ChunkBytes = 256 * 1024;
        for (int i = 0; i < bytes.size(); i += ChunkBytes) {
            checkpoint(m_task);
            hash.addData(QByteArray::fromRawData(bytes.constData() + i, qMin(ChunkBytes, int(bytes.size()) - i)));
        }
        result.sha256 = hash.result().toHex();
    }
    for (const auto &rule : Rules)
        result.coverage.push_back({QString::fromLatin1(rule.id)});
    for (const auto rule : {Unused, Address, ReportConflict, Schema})
        result.coverage[rule].state = Coverage::NotImplemented;
}

void Analysis::evaluate(Rule rule, bool limited)
{
    auto &coverage = result.coverage[rule];
    coverage.state = Coverage::Executed;
    ++(limited ? coverage.limited : coverage.evaluated);
}

QString Analysis::lnName(NodeId id) const
{
    return kind(id, "LN0") ? QStringLiteral("LLN0") : attr(id, "prefix") + attr(id, "lnClass") + attr(id, "inst");
}

NodeId Analysis::ancestor(NodeId id, const char *name) const
{
    for (; id != NoNode; id = node(id).parent) {
        if (kind(id, name))
            return id;
    }
    return NoNode;
}

QString Analysis::path(NodeId id) const
{
    QStringList parts;
    for (; id != NoNode; id = node(id).parent) {
        QString name = attr(id, "name");
        if (name.isEmpty())
            name = attr(id, "id");
        if (name.isEmpty())
            name = isLn(node(id).name) ? lnName(id) : attr(id, "inst");
        parts.prepend(name.isEmpty() ? node(id).name : node(id).name + '[' + name + ']');
    }
    return '/' + parts.join('/');
}

void Analysis::finding(Rule rule, NodeId id, const QString &detail, Severity severity, SourceLocation location)
{
    const auto &spec = Rules[rule];
    result.findings.push_back({QString::fromLatin1(spec.id), severity, id,
        id == NoNode ? location : node(id).source, id == NoNode ? QString() : path(id), detail,
        QString::fromLatin1(spec.suggestion), QString::fromLatin1(spec.scope)});
}

void Analysis::addKey(NodeId id, NodeId scope, const QString &type, const QString &value)
{
    evaluate(Structure);
    if (value.isEmpty()) {
        finding(Structure, id, QStringLiteral("An identifying attribute is empty or absent."));
        return;
    }
    evaluate(Duplicate);
    auto &ids = keys[scope][key(type, value)];
    if (!ids.empty())
        finding(Duplicate, id, QStringLiteral("Duplicate %1 key: %2").arg(type, value));
    ids.push_back(id);
}

Match Analysis::lookup(NodeId scope, const QString &type, const QString &value) const
{
    if (scope == NoNode)
        return {LinkState::Blocked};
    const auto group = keys.constFind(scope);
    if (group != keys.cend()) {
        const auto ids = group->constFind(key(type, value));
        if (ids != group->cend())
            return matched(ids.value());
    }
    return {LinkState::Missing};
}

void Analysis::index()
{
    active.resize(result.document->nodes.size());
    bool hasExtension = false;
    for (NodeId id = 0; id < int(active.size()); ++id) {
        checkpoint(m_task);
        const auto &n = node(id);
        const auto parent = n.parent;
        hasExtension |= !n.isScl();
        active[id] = n.isScl() && (parent == NoNode || (active[parent] && node(parent).name != "Private"));
        if (!active[id] || parent == NoNode)
            continue;
        const auto &type = n.name;
        if (type == "IED" && parent == 0)
            addKey(id, 0, type, attr(id, "name"));
        else if (type == "AccessPoint" && kind(parent, "IED"))
            addKey(id, parent, type, attr(id, "name"));
        else if (type == "LDevice" && kind(parent, "Server"))
            addKey(id, parent, type, attr(id, "inst"));
        else if (isLn(type) && kind(parent, "LDevice"))
            addKey(id, parent, QStringLiteral("LN"), type == "LN" && attr(id, "lnClass").isEmpty() ? QString() : lnName(id));
        else if ((type == "DataSet" || isControl(type)) && isLn(node(parent).name))
            addKey(id, parent, type, attr(id, "name"));
        else if (isTemplate(type) && kind(parent, "DataTypeTemplates") && node(parent).parent == 0)
            addKey(id, 0, type, attr(id, "id"));
        else if ((type == "DO" && kind(parent, "LNodeType"))
                 || ((type == "SDO" || type == "DA") && kind(parent, "DOType"))
                 || (type == "BDA" && kind(parent, "DAType")))
            addKey(id, parent, type == "DA" ? QStringLiteral("DA:") + attr(id, "fc") : type, attr(id, "name"));
    }
    if (hasExtension) {
        evaluate(Extension);
        finding(Extension, 0, QStringLiteral("Foreign namespace elements are preserved for inspection."), Severity::Info);
    }
}

void Analysis::record(Rule rule, Reference ref, Match match, const QString &detail)
{
    ref.state = match.state;
    ref.detail = detail.isEmpty() ? stateDetail(match.state) : detail;
    evaluate(rule, match.state == LinkState::Unsupported || match.state == LinkState::Blocked
                  || match.state == LinkState::External || match.state == LinkState::Partial);
    if (match.state == LinkState::Missing || match.state == LinkState::Ambiguous)
        finding(rule, ref.source, ref.detail);
    else if (match.state == LinkState::Unsupported || match.state == LinkState::External
             || match.state == LinkState::Partial)
        finding(rule, ref.source, ref.detail, Severity::Info);
    const int index = int(result.references.size());
    if (match.target() != NoNode)
        result.incoming.insert(match.target(), index);
    result.outgoing.insert(ref.source, index);
    ref.candidates = std::move(match.nodes);
    result.references.push_back(std::move(ref));
}

void Analysis::record(Rule rule, LinkKind type, NodeId id, const QString &declared, Match match)
{
    record(rule, {type, match.state, id, declared}, std::move(match));
}

void Analysis::types()
{
    QHash<NodeId, std::vector<NodeId>> edges;
    for (NodeId id = 1; id < int(active.size()); ++id) {
        checkpoint(m_task);
        if (!active[id])
            continue;
        const auto &type = node(id).name;
        const auto parent = node(id).parent;
        QString expected;
        if (isLn(type) && kind(parent, "LDevice"))
            expected = QStringLiteral("LNodeType");
        else if ((type == "DO" && kind(parent, "LNodeType")) || (type == "SDO" && kind(parent, "DOType")))
            expected = QStringLiteral("DOType");
        else if ((type == "DA" && kind(parent, "DOType")) || (type == "BDA" && kind(parent, "DAType"))) {
            const auto basic = attr(id, "bType");
            if (basic == "Struct")
                expected = QStringLiteral("DAType");
            else if (basic == "Enum")
                expected = QStringLiteral("EnumType");
        }
        if (expected.isEmpty())
            continue;

        const bool ln = isLn(type);
        const auto declared = attr(id, ln ? "lnType" : "type");
        auto match = lookup(0, expected, declared);
        if (ln && match.target() != NoNode && attr(match.target(), "lnClass") !=
                (type == "LN0" ? QStringLiteral("LLN0") : attr(id, "lnClass"))) {
            record(LnType, {LinkKind::LogicalNodeType, LinkState::Missing, id, declared},
                   {LinkState::Missing, match.nodes}, QStringLiteral("LNodeType exists but lnClass differs."));
            match.state = LinkState::Missing;
        } else {
            record(ln ? LnType : MemberType, ln ? LinkKind::LogicalNodeType : LinkKind::MemberType,
                   id, declared, match);
        }
        if (!ln && match.target() != NoNode)
            edges[parent].push_back(match.target());
        typeTargets.insert(id, std::move(match));
    }
    checkTypeGraph(edges);
}

void Analysis::checkTypeGraph(const QHash<NodeId, std::vector<NodeId>> &edges)
{
    struct Frame { NodeId id; size_t next = 0; int height = 1; };
    QHash<NodeId, int> heights; // Zero denotes an active DFS frame.
    for (NodeId id = 1; id < int(active.size()); ++id) {
        if (!active[id] || !isTemplate(node(id).name) || heights.contains(id))
            continue;
        std::vector<Frame> stack{{id}};
        heights.insert(id, 0);
        while (!stack.empty()) {
            checkpoint(m_task);
            auto &frame = stack.back();
            const auto children = edges.constFind(frame.id);
            if (children == edges.cend() || frame.next == children->size()) {
                evaluate(TypeGraph);
                if (frame.height == MaxTypeDepth + 1)
                    finding(TypeGraph, frame.id, QStringLiteral("Template path exceeds the expansion limit."));
                heights[frame.id] = frame.height;
                const int height = frame.height;
                stack.pop_back();
                if (!stack.empty())
                    stack.back().height = std::max(stack.back().height, height + 1);
                continue;
            }
            const auto child = (*children)[frame.next++];
            const auto known = heights.constFind(child);
            if (known == heights.cend()) {
                heights.insert(child, 0);
                stack.push_back({child});
            } else if (*known == 0) {
                cyclicTypes.insert(child);
                finding(TypeGraph, frame.id, QStringLiteral("Recursive template link to %1.").arg(attr(child, "id")));
            } else {
                frame.height = std::max(frame.height, *known + 1);
            }
        }
    }
}

QString Analysis::domain(NodeId ld) const
{
    const auto explicitName = attr(ld, "ldName");
    if (!explicitName.isEmpty())
        return explicitName;
    const auto ied = ancestor(ld, "IED");
    const auto instance = attr(ld, "inst");
    if (ied == NoNode || attr(ied, "name").isEmpty() || instance.isEmpty())
        return {};
    return attr(ied, "name") + instance;
}

Match Analysis::locateLn(NodeId server, const QString &ld, const QString &ln, FcdaTrace *trace) const
{
    auto device = lookup(server, QStringLiteral("LDevice"), ld);
    if (device.target() == NoNode)
        return device;
    if (trace) {
        trace->steps.push_back({FcdaField::LdInst, device.target()});
        trace->stoppedAt = FcdaField::LogicalNode;
    }
    auto logical = lookup(device.target(), QStringLiteral("LN"), ln);
    if (trace && logical.target() != NoNode)
        trace->steps.push_back({FcdaField::LogicalNode, logical.target()});
    return logical;
}

Match Analysis::serverFor(NodeId ap) const
{
    QSet<NodeId> visited;
    while (ap != NoNode && !visited.contains(ap)) {
        checkpoint(m_task);
        visited.insert(ap);
        QList<NodeId> servers;
        QList<NodeId> aliases;
        for (auto id = node(ap).firstChild; id != NoNode; id = node(id).nextSibling) {
            checkpoint(m_task);
            if (kind(id, "Server"))
                servers.push_back(id);
            else if (kind(id, "ServerAt"))
                aliases.push_back(id);
        }
        if (servers.size() + aliases.size() > 1) {
            servers += aliases;
            return {LinkState::Ambiguous, std::move(servers)};
        }
        if (!servers.empty())
            return matched(std::move(servers));
        if (aliases.empty())
            return {LinkState::Unsupported};
        auto next = lookup(node(ap).parent, QStringLiteral("AccessPoint"), attr(aliases.front(), "apName"));
        if (next.target() == NoNode)
            return next;
        ap = next.target();
    }
    return {LinkState::Blocked};
}

Match Analysis::sourceLn(NodeId ied, const QString &ld, const QString &ln) const
{
    QSet<NodeId> seenServers;
    QList<NodeId> candidates;
    bool incomplete = false;
    for (auto ap = node(ied).firstChild; ap != NoNode; ap = node(ap).nextSibling) {
        checkpoint(m_task);
        if (!kind(ap, "AccessPoint"))
            continue;
        auto server = serverFor(ap);
        if (server.target() == NoNode) {
            incomplete |= server.state != LinkState::Unsupported;
            continue;
        }
        if (seenServers.contains(server.target()))
            continue;
        seenServers.insert(server.target());
        auto found = locateLn(server.target(), ld, ln);
        if (found.state == LinkState::Ambiguous)
            return found;
        if (found.target() != NoNode)
            candidates.push_back(found.target());
    }
    if (incomplete && candidates.size() < 2)
        return {LinkState::Blocked, std::move(candidates)};
    if (seenServers.isEmpty())
        return {LinkState::Unsupported};
    return matched(std::move(candidates));
}

Match Analysis::hasFc(NodeId type, const QString &fc, FcdaTrace *trace) const
{
    QSet<NodeId> seen;
    std::vector<NodeId> pending{type};
    bool blocked = false;
    while (!pending.empty()) {
        checkpoint(m_task);
        const auto definition = pending.back();
        pending.pop_back();
        if (seen.contains(definition))
            continue;
        if (seen.size() >= 4096)
            return {LinkState::Unsupported};
        seen.insert(definition);
        blocked |= cyclicTypes.contains(definition);
        for (auto child = node(definition).firstChild; child != NoNode; child = node(child).nextSibling) {
            checkpoint(m_task);
            if (kind(child, "DA") && attr(child, "fc") == fc) {
                if (trace)
                    trace->steps.push_back({FcdaField::Fc, child});
                return matched({type});
            }
            if (kind(child, "SDO")) {
                const auto target = typeTargets.value(child, {LinkState::Blocked}).target();
                if (target == NoNode)
                    blocked = true;
                else
                    pending.push_back(target);
            }
        }
    }
    return {blocked ? LinkState::Blocked : LinkState::Missing};
}

Match Analysis::object(NodeId ln, const QString &doName, const QString &daName, const QString &fc,
                       const QString &ix, Reference &ref, QString &detail, FcdaTrace *trace) const
{
    const auto stage = [trace](FcdaField field) { if (trace) trace->stoppedAt = field; };
    stage(FcdaField::DoName);
    if (doName.isEmpty())
        return {LinkState::Unsupported};
    const auto doParts = doName.split('.');
    const auto daParts = daName.isEmpty() ? QStringList{} : daName.split('.');
    auto parts = doParts + daParts;
    if (parts.size() > MaxTypeDepth)
        return {LinkState::Unsupported};

    // An inline selector identifies the array dimension explicitly. A separate ix
    // can be used alone when exactly one dimension occurs in the resolved path.
    int inlineAt = -1;
    int inlineIndex = -1;
    int ixIndex = -1;
    stage(FcdaField::Ix);
    if (!ix.isNull() && !unsignedNumber(ix, ixIndex)) {
        detail = QStringLiteral("Invalid FCDA ix.");
        return {LinkState::Missing};
    }
    static const QRegularExpression selector(QStringLiteral(R"(^([^()]+)\(([0-9]+)\)$)"));
    for (int i = 0; i < parts.size(); ++i) {
        if (!parts[i].contains('(') && !parts[i].contains(')'))
            continue;
        const auto m = selector.match(parts[i]);
        if (!m.hasMatch() || inlineAt >= 0 || !unsignedNumber(m.captured(2), inlineIndex))
            return {LinkState::Unsupported};
        inlineAt = i;
        parts[i] = m.captured(1);
    }
    if (inlineAt >= 0 && ixIndex >= 0 && inlineIndex != ixIndex) {
        detail = QStringLiteral("Inline index and ix differ.");
        return {LinkState::Missing};
    }

    stage(FcdaField::DoName);
    auto owner = typeTargets.value(ln, {LinkState::Blocked}).target();
    if (owner == NoNode)
        return {LinkState::Blocked};
    NodeId member = NoNode;
    std::vector<std::pair<int, int>> arrays;
    QList<NodeId> path;
    QSet<NodeId> visitedTypes;
    QString actualFc = fc;
    for (int i = 0; i < parts.size(); ++i) {
        const auto field = i < doParts.size() ? FcdaField::DoName : FcdaField::DaName;
        stage(field);
        if (cyclicTypes.contains(owner)) {
            if (visitedTypes.contains(owner))
                return {LinkState::Blocked};
            visitedTypes.insert(owner);
        }
        Match found{LinkState::Missing};
        if (i == doParts.size()) {
            if (!fc.isEmpty()) {
                found = lookup(owner, QStringLiteral("DA:") + fc, parts[i]);
            } else {
                QList<NodeId> candidates;
                for (auto child = node(owner).firstChild; child != NoNode; child = node(child).nextSibling) {
                    checkpoint(m_task);
                    if (kind(child, "DA") && attr(child, "name") == parts[i])
                        candidates.push_back(child);
                }
                found = matched(std::move(candidates));
            }
            if (found.target() != NoNode)
                actualFc = attr(found.target(), "fc");
        } else {
            const auto type = i == 0 ? "DO" : i < doParts.size() ? "SDO" : "BDA";
            found = lookup(owner, QString::fromLatin1(type), parts[i]);
        }
        if (found.target() == NoNode) {
            if (found.state == LinkState::Missing)
                detail = QStringLiteral("No matching template member for %1 with the declared path and FC.").arg(parts[i]);
            return found;
        }
        member = found.target();
        if (trace) {
            path.push_back(member);
            trace->steps.push_back({field, member});
            if (i == doParts.size())
                trace->steps.push_back({FcdaField::Fc, member});
        }
        int count = 0;
        const auto countValue = attr(member, "count");
        if (!countValue.isNull() && !unsignedNumber(countValue, count))
            return {LinkState::Blocked};
        if (count > 0)
            arrays.emplace_back(i, count);
        if (i + 1 < parts.size()) {
            if (!typeTargets.contains(member)) {
                detail = QStringLiteral("The path continues through a non-structured member.");
                return {LinkState::Missing};
            }
            owner = typeTargets.value(member).target();
            if (owner == NoNode)
                return {LinkState::Blocked};
        }
    }
    if (daParts.isEmpty() && !fc.isEmpty()) {
        stage(FcdaField::Fc);
        const auto type = typeTargets.value(member, {LinkState::Blocked}).target();
        if (type == NoNode)
            return {LinkState::Blocked};
        const auto fcMatch = hasFc(type, fc, trace);
        if (fcMatch.state != LinkState::Resolved) {
            if (fcMatch.state == LinkState::Missing)
                detail = QStringLiteral("The referenced DO contains no member with the declared FC.");
            return fcMatch;
        }
    }

    int selected = inlineAt;
    int index = inlineAt >= 0 ? inlineIndex : ixIndex;
    stage(FcdaField::Ix);
    if (selected < 0 && index >= 0) {
        if (arrays.size() > 1)
            return {LinkState::Unsupported};
        if (!arrays.empty())
            selected = arrays.front().first;
    }
    if (index >= 0) {
        const auto array = std::find_if(arrays.begin(), arrays.end(),
                                       [selected](const auto &entry) { return entry.first == selected; });
        if (array == arrays.end() || index >= array->second) {
            detail = QStringLiteral("Array selector is outside the declared count or selects a scalar.");
            return {LinkState::Missing};
        }
    }
    for (const auto &array : arrays) {
        if (array.first != selected && array.first + 1 < parts.size())
            return {LinkState::Unsupported};
    }
    if (trace && selected >= 0)
        trace->steps.push_back({FcdaField::Ix, path[selected]});

    ref.logicalNode = ln;
    ref.arrayIndex = index;
    const auto ldDomain = domain(node(ln).parent);
    if (!ldDomain.isEmpty()) {
        auto objectParts = parts;
        if (selected >= 0)
            objectParts[selected] += '(' + QString::number(index) + ')';
        ref.objectReference = ldDomain + '/' + lnName(ln) + '.' + objectParts.join('.');
        if (!actualFc.isEmpty()) {
            ref.mmsDomain = ldDomain;
            const auto itemCount = selected < 0 ? int(parts.size()) : selected + 1;
            ref.mmsItem = lnName(ln) + '$' + actualFc + '$' + parts.mid(0, itemCount).join('$');
            if (selected >= 0)
                ref.mmsComponent = parts.mid(itemCount).join('$');
        }
    }
    return matched({member});
}

void Analysis::fcda(NodeId id, int ordinal)
{
    const auto ln = node(node(id).parent).parent;
    const auto ld = node(ln).parent;
    const auto server = ancestor(ld, "Server");
    const auto ldInst = attr(id, "ldInst");
    const auto lnClass = attr(id, "lnClass");
    const auto prefix = attr(id, "prefix");
    const auto lnInst = attr(id, "lnInst");
    const auto doName = attr(id, "doName");
    const auto daName = attr(id, "daName");
    const auto fc = attr(id, "fc");
    Reference ref{LinkKind::Fcda, LinkState::Blocked, id,
                  ldInst + '/' + prefix + lnClass + lnInst + '.' + doName + '.' + daName + " [" + fc + ']'};
    ref.memberOrdinal = ordinal;
    FcdaTrace trace;
    const auto ied = ancestor(ld, "IED");
    if (ied != NoNode)
        trace.steps.push_back({FcdaField::Ied, ied});
    auto target = locateLn(server, ldInst.isEmpty() ? attr(ld, "inst") : ldInst, prefix + lnClass + lnInst, &trace);
    QString detail;
    if (lnClass.isEmpty() || fc.isEmpty()) {
        trace.stoppedAt = lnClass.isEmpty() ? FcdaField::LogicalNode : FcdaField::Fc;
        target = {LinkState::Unsupported};
    } else if (target.target() != NoNode) {
        target = object(target.target(), doName, daName, fc, attr(id, "ix"), ref, detail, &trace);
    }
    result.fcdaTraces.insert(id, std::move(trace));
    record(FcdaRule, std::move(ref), std::move(target), detail);
}

void Analysis::control(NodeId id)
{
    const auto ln = node(id).parent;
    const auto dataSet = attr(id, "datSet");
    const auto dataSetMatch = dataSet.isEmpty() ? Match{LinkState::Unbound}
                                               : lookup(ln, QStringLiteral("DataSet"), dataSet);
    record(ControlSet, LinkKind::ControlDataSet, id, dataSet, dataSetMatch);
    if (!kind(id, "ReportControl"))
        return;

    ReportMapping report{id, attr(id, "rptID"), dataSet};
    const auto ldDomain = domain(node(ln).parent);
    const auto name = attr(id, "name");
    const auto flag = attr(id, "buffered");
    if (!ldDomain.isEmpty() && dataSetMatch.target() != NoNode)
        report.dataSetReference = ldDomain + '/' + lnName(ln) + '$' + dataSet;
    if (!ldDomain.isEmpty() && !name.isEmpty()
        && (flag.isNull() || flag == "false" || flag == "0" || flag == "true" || flag == "1")) {
        const auto fc = flag == "true" || flag == "1" ? QStringLiteral("BR") : QStringLiteral("RP");
        report.mmsDomain = ldDomain;
        report.mmsItem = lnName(ln) + '$' + fc + '$' + name;
        report.definitionReference = ldDomain + '/' + lnName(ln) + '.' + fc + '.' + name;
    }
    result.reports.push_back(std::move(report));
}

void Analysis::communication(NodeId id)
{
    const auto iedName = attr(id, "iedName");
    const auto apName = attr(id, "apName");
    const auto ied = lookup(0, QStringLiteral("IED"), iedName);
    auto ap = ied.target() != NoNode ? lookup(ied.target(), QStringLiteral("AccessPoint"), apName) : ied;
    if (ied.state == LinkState::Missing && !iedName.isEmpty())
        ap.state = LinkState::External;
    record(Network, LinkKind::ConnectedAp, id, iedName + '/' + apName, ap);
    const auto server = ap.target() == NoNode ? Match{LinkState::Blocked} : serverFor(ap.target());
    for (auto child = node(id).firstChild; child != NoNode; child = node(child).nextSibling) {
        checkpoint(m_task);
        if (!kind(child, "GSE") && !kind(child, "SMV"))
            continue;
        const auto ld = attr(child, "ldInst");
        const auto cb = attr(child, "cbName");
        auto match = server.target() == NoNode ? server : locateLn(server.target(), ld, QStringLiteral("LLN0"));
        if (match.target() != NoNode)
            match = lookup(match.target(), kind(child, "GSE") ? QStringLiteral("GSEControl")
                                                              : QStringLiteral("SampledValueControl"), cb);
        record(Network, LinkKind::NetworkControl, child, ld + '/' + cb, std::move(match));
    }
}

void Analysis::extRef(NodeId id)
{
    const auto ied = attr(id, "iedName");
    const auto ld = attr(id, "ldInst");
    const auto lnClass = attr(id, "lnClass");
    const auto ln = attr(id, "prefix") + lnClass + attr(id, "lnInst");
    const auto doName = attr(id, "doName");
    const auto daName = attr(id, "daName");
    const auto service = attr(id, "serviceType");
    const auto cb = attr(id, "srcCBName");
    Reference ref{LinkKind::ExtRefData, LinkState::Blocked, id,
                  ied + '/' + ld + '/' + ln + '.' + doName + '.' + daName};
    auto source = lookup(0, QStringLiteral("IED"), ied);
    if (source.state == LinkState::Missing && !ied.isEmpty())
        source.state = LinkState::External;
    bool hasSourceAttribute = false;
    for (const auto *name : {"iedName", "ldInst", "prefix", "lnClass", "lnInst", "doName", "daName",
                             "srcLDInst", "srcPrefix", "srcLNClass", "srcLNInst", "srcCBName"})
        hasSourceAttribute |= !attr(id, name).isEmpty();
    auto match = source;
    QString detail;
    if (!hasSourceAttribute) {
        match = {LinkState::Unbound};
    } else if (ied.isEmpty() || ld.isEmpty() || lnClass.isEmpty() || doName.isEmpty()) {
        match = {LinkState::Partial};
    } else if (source.target() != NoNode) {
        match = sourceLn(source.target(), ld, ln);
        if (match.target() != NoNode)
            match = object(match.target(), doName, daName, {}, {}, ref, detail);
    }
    record(ExtRefRule, std::move(ref), std::move(match), detail);

    if (cb.isEmpty() && attr(id, "srcLDInst").isEmpty() && attr(id, "srcLNClass").isEmpty()
        && attr(id, "srcPrefix").isEmpty() && attr(id, "srcLNInst").isEmpty())
        return;
    auto ctrl = source;
    const auto sourceLd = attr(id, "srcLDInst");
    const auto sourceClass = attr(id, "srcLNClass");
    const auto sourceLnName = attr(id, "srcPrefix") + sourceClass + attr(id, "srcLNInst");
    QString controlKind;
    if (service == "GOOSE")
        controlKind = QStringLiteral("GSEControl");
    else if (service == "SMV")
        controlKind = QStringLiteral("SampledValueControl");
    else if (service == "Report")
        controlKind = QStringLiteral("ReportControl");
    if (ied.isEmpty() || cb.isEmpty() || sourceLd.isEmpty() || sourceClass.isEmpty()) {
        ctrl = {LinkState::Partial};
    } else if (controlKind.isEmpty()) {
        ctrl = {LinkState::Unsupported};
    } else if (source.target() != NoNode) {
        ctrl = sourceLn(source.target(), sourceLd, sourceLnName);
        if (ctrl.target() != NoNode)
            ctrl = lookup(ctrl.target(), controlKind, cb);
    }
    record(ExtRefRule, LinkKind::ExtRefControl, id, sourceLd + '/' + sourceLnName + '.' + cb, std::move(ctrl));
}

ReferenceAnalysis Analysis::run()
{
    if (!result.document || result.document->nodes.empty()) {
        for (auto &coverage : result.coverage) {
            if (coverage.state != Coverage::NotImplemented)
                coverage.state = Coverage::Blocked;
        }
        if (inputError.code != Error::None) {
            Rule rule = XmlSyntax;
            switch (inputError.code) {
            case Error::File: case Error::Empty: rule = XmlFile; break;
            case Error::UnsupportedRoot: rule = XmlRoot; break;
            case Error::Dtd: case Error::Limit: rule = XmlSafety; break;
            default: break;
            }
            evaluate(rule);
            finding(rule, NoNode, inputError.detail, Severity::Error, {-1, int(inputError.line)});
        }
        return std::move(result);
    }

    for (const auto rule : {XmlFile, XmlSyntax, XmlRoot, XmlSafety})
        evaluate(rule);
    index();
    types();
    enterStage(m_task, TaskStage::Validation);
    evaluate(Version);
    if (attr(0, "version").isEmpty() || attr(0, "revision").isEmpty() || attr(0, "release").isEmpty())
        finding(Version, 0, QStringLiteral("Root attributes do not identify a complete SCL version."), Severity::Info);

    for (NodeId id = 1; id < int(active.size()); ++id) {
        checkpoint(m_task);
        if (!active[id])
            continue;
        const auto parent = node(id).parent;
        const bool inLn = isLn(node(parent).name) && kind(node(parent).parent, "LDevice");
        if (kind(id, "DataSet") && inLn) {
            int ordinal = 0;
            for (auto child = node(id).firstChild; child != NoNode; child = node(child).nextSibling) {
                checkpoint(m_task);
                if (kind(child, "FCDA"))
                    fcda(child, ++ordinal);
            }
            evaluate(EmptySet);
            if (ordinal == 0)
                finding(EmptySet, id, QStringLiteral("The data set has no FCDA members."), Severity::Warning);
        } else if (isControl(node(id).name) && inLn) {
            control(id);
        } else if (kind(id, "ConnectedAP") && kind(parent, "SubNetwork")
                   && kind(node(parent).parent, "Communication")) {
            communication(id);
        } else if (kind(id, "ExtRef") && kind(parent, "Inputs") && isLn(node(node(parent).parent).name)) {
            extRef(id);
        }
    }
    return std::move(result);
}

} // namespace

ReferenceAnalysis ReferenceResolver::analyze(const Result &input, TaskControl *task)
{
    enterStage(task, TaskStage::ReferenceIndex);
    return Analysis(input, task).run();
}

} // namespace Scl
