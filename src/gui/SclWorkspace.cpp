#include "SclWorkspace.hpp"
#include "SclTableInteraction.hpp"
#include "SclReferenceMenu.hpp"
#include "SclTiming.hpp"
#include "SclTopology.hpp"
#include <QStackedWidget>
#include "SclSourcePreview.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSplitter>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QScrollArea>
#include <QTextBrowser>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <DockManager.h>
#include <DockWidget.h>
#include <QTreeWidget>
#include <DockAreaWidget.h>

namespace iec61850sim
{
namespace
{
using Scl::NodeId;
using Scl::NoNode;
using Snapshot = std::shared_ptr<const SclSnapshot>;

QString Attribute(const SclSnapshot& snapshot, NodeId id, const char* key)
{
    return snapshot.analysis.document->attribute(id, QString::fromLatin1(key));
}

QString Title(const SclSnapshot& snapshot, NodeId id)
{
    if (id == NoNode)
        return QStringLiteral("—");
    const auto& node = snapshot.analysis.document->nodes[id];
    QString name;
    if (node.name == "LN0")
        name = QStringLiteral("LLN0");
    else if (node.name == "LN")
        name = Attribute(snapshot, id, "prefix") + Attribute(snapshot, id, "lnClass") + Attribute(snapshot, id, "inst");
    else
    {
        for (const auto* key : {"name", "id", "inst", "cbName", "type", "intAddr", "apName"})
        {
            name = Attribute(snapshot, id, key);
            if (!name.isEmpty())
                break;
        }
    }
    return name.isEmpty() ? node.name : node.name + QStringLiteral(" · ") + name;
}

QString Path(const SclSnapshot& snapshot, NodeId id, bool ownerOnly = false)
{
    QStringList parts;
    if (ownerOnly)
        id = snapshot.analysis.document->nodes[id].parent;
    for (; id > 0; id = snapshot.analysis.document->nodes[id].parent)
        parts.prepend(Title(snapshot, id));
    return parts.isEmpty() ? QStringLiteral("SCL") : parts.join(QStringLiteral(" / "));
}

QColor TypeColor(const Scl::Node& node, const QPalette& palette)
{
    if (!node.isScl())
        return {};
    const bool dark = palette.base().color().lightness() < 128;
    if (node.name == "LDevice") return QColor(dark ? "#8ab4f8" : "#2355a4");
    if (node.name == "LN" || node.name == "LN0") return QColor(dark ? "#8de0c5" : "#13675e");
    if (node.name == "DO" || node.name == "DOI" || node.name == "SDO" || node.name == "SDI")
        return QColor(dark ? "#d6a9f5" : "#693c85");
    if (node.name == "DA" || node.name == "DAI" || node.name == "BDA") return QColor(dark ? "#bcc9d8" : "#526378");
    return {};
}

QString NodeLink(const SclSnapshot& snapshot, NodeId id, const QPalette& palette, int maxLength = 80)
{
    auto title = Title(snapshot, id);
    if (title.size() > maxLength)
        title = title.left(maxLength - title.at(maxLength - 1).isHighSurrogate()) + QStringLiteral("…");
    const auto color = TypeColor(snapshot.analysis.document->nodes[id], palette);
    return QStringLiteral("<a href='%1'><span style='color:%2'>%3</span></a>").arg(id)
        .arg(color.isValid() ? color.name() : palette.link().color().name(), title.toHtmlEscaped());
}

QString LinkState(Scl::LinkState state)
{
    switch (state)
    {
    case Scl::LinkState::Resolved: return QStringLiteral("已解析");
    case Scl::LinkState::Missing: return QStringLiteral("找不到目标");
    case Scl::LinkState::Ambiguous: return QStringLiteral("有歧义");
    case Scl::LinkState::Unbound: return QStringLiteral("未绑定");
    case Scl::LinkState::External: return QStringLiteral("文件外");
    case Scl::LinkState::Unsupported: return QStringLiteral("未覆盖");
    case Scl::LinkState::Blocked: return QStringLiteral("前置条件失败");
    case Scl::LinkState::Partial: return QStringLiteral("部分绑定");
    }
    return {};
}

QString RelationshipName(const SclSnapshot& snapshot, const Scl::Reference& ref, bool incoming)
{
    const auto& node = snapshot.analysis.document->nodes[ref.source];
    switch (ref.kind)
    {
    case Scl::LinkKind::ControlDataSet:
        if (!incoming) return QStringLiteral("使用的数据集");
        if (node.name == "ReportControl") return QStringLiteral("被报告使用");
        if (node.name == "SampledValueControl") return QStringLiteral("被 SV 使用");
        if (node.name == "GSEControl")
            return Attribute(snapshot, ref.source, "type") == "GSSE" ? QStringLiteral("被 GSSE 使用")
                : QStringLiteral("被 GOOSE 使用");
        return QStringLiteral("被控制块使用");
    case Scl::LinkKind::Fcda:
        return incoming ? QStringLiteral("引用同一模板定义的 FCDA") : QStringLiteral("成员引用目标");
    case Scl::LinkKind::LogicalNodeType:
    case Scl::LinkKind::MemberType:
        return incoming ? QStringLiteral("类型模板复用") : QStringLiteral("引用的类型模板");
    default:
        return incoming ? QStringLiteral("使用方") : QStringLiteral("引用");
    }
}

QString Severity(Scl::Severity severity)
{
    switch (severity)
    {
    case Scl::Severity::Error: return QStringLiteral("错误");
    case Scl::Severity::Warning: return QStringLiteral("警告");
    case Scl::Severity::Info: return QStringLiteral("提示");
    }
    return {};
}

enum class Table { Reports, Objects, Communication, Templates, Findings, Members, Links, Candidates };

QStringList Headers(Table table)
{
    switch (table)
    {
    case Table::Reports: return {QStringLiteral("报告"), QStringLiteral("所属位置"), QStringLiteral("原始 rptID"),
        QStringLiteral("datSet"), QStringLiteral("BRCB / URCB"), QStringLiteral("定义引用"), QStringLiteral("描述")};
    case Table::Objects: return {QStringLiteral("对象"), QStringLiteral("所属位置"), QStringLiteral("描述"),
        QStringLiteral("lnType / type"), QStringLiteral("bType"), QStringLiteral("FC"), QStringLiteral("count / ix")};
    case Table::Communication: return {QStringLiteral("通信对象"), QStringLiteral("所属位置"), QStringLiteral("IED / AP"),
        QStringLiteral("控制块 / 来源"), QStringLiteral("地址参数"), QStringLiteral("描述")};
    case Table::Templates: return {QStringLiteral("模板"), QStringLiteral("LN 类 / CDC"), QStringLiteral("使用方"), QStringLiteral("描述")};
    case Table::Findings: return {QStringLiteral("级别"), QStringLiteral("规则"), QStringLiteral("行"), QStringLiteral("对象"), QStringLiteral("发现"), QStringLiteral("建议"), QStringLiteral("对象描述")};
    case Table::Members: return {QStringLiteral("序号"), QStringLiteral("FCDA 声明"), QStringLiteral("FC"), QStringLiteral("状态"), QStringLiteral("对象引用"),
        QStringLiteral("成员描述"), QStringLiteral("解析目标描述"), QStringLiteral("MMS Item"), QStringLiteral("实例描述（dU）")};
    case Table::Links: return {QStringLiteral("方向"), QStringLiteral("声明"), QStringLiteral("状态"), QStringLiteral("相关对象"), QStringLiteral("说明"), QStringLiteral("相关对象描述")};
    case Table::Candidates: return {QStringLiteral("候选目标"), QStringLiteral("所属位置"), QStringLiteral("行"), QStringLiteral("描述")};
    }
    return {};
}
} // namespace

class SclTreeModel final : public QAbstractItemModel
{
public:
    using QAbstractItemModel::QAbstractItemModel;

    void Reset(Snapshot snapshot)
    {
        beginResetModel();
        m_snapshot = std::move(snapshot);
        endResetModel();
    }

    QModelIndex ForNode(NodeId id) const
    {
        if (!m_snapshot || id == NoNode)
            return {};
        return createIndex(m_snapshot->browse.branches[id].row, 0, quintptr(id) + 1);
    }

    static NodeId Node(const QModelIndex& index)
    {
        return index.isValid() ? NodeId(index.internalId() - 1) : NoNode;
    }

    QModelIndex index(int row, int column, const QModelIndex& parent) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        if (!parent.isValid())
            return ForNode(0);
        const auto& branch = m_snapshot->browse.branches[Node(parent)];
        return ForNode(m_snapshot->browse.children[branch.first + row]);
    }

    QModelIndex parent(const QModelIndex& index) const override
    {
        return index.isValid() ? ForNode(m_snapshot->analysis.document->nodes[Node(index)].parent) : QModelIndex{};
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        if (!m_snapshot || parent.column() > 0)
            return 0;
        return parent.isValid() ? m_snapshot->browse.branches[Node(parent)].count : 1;
    }

    int columnCount(const QModelIndex& = {}) const override { return 1; }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid())
            return {};
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
            return Title(*m_snapshot, Node(index));
        if (role == Qt::UserRole)
            return Node(index);
        if (role == Qt::ForegroundRole)
        {
            const auto color = TypeColor(m_snapshot->analysis.document->nodes[Node(index)], QApplication::palette());
            if (color.isValid())
                return QBrush(color);
        }
        return {};
    }

private:
    Snapshot m_snapshot;
};

class SclTableModel final : public QAbstractTableModel
{
public:
    SclTableModel(Table table, QObject* parent)
        : QAbstractTableModel(parent), m_table(table), m_headers(Headers(table)) {}

    void Reset(Snapshot snapshot, NodeId context = NoNode, QList<NodeId> candidates = {})
    {
        beginResetModel();
        m_snapshot = std::move(snapshot);
        m_context = context;
        m_filter.reset();
        m_scopeRows = nullptr;
        m_links.clear();
        m_linksLimited = false;
        m_candidates = std::move(candidates);
        m_displayRow = -1;
        m_displayCells.clear();
        if (m_snapshot && context != NoNode && m_table == Table::Links)
        {
            const auto& analysis = m_snapshot->analysis;
            const auto add = [&](const auto& index, bool incoming) {
                const auto range = index.equal_range(context);
                for (auto it = range.first; it != range.second; ++it)
                {
                    if (m_links.size() == 512)
                    {
                        m_linksLimited = true;
                        break;
                    }
                    m_links.push_back({it.value(), incoming});
                }
            };
            add(analysis.outgoing, false);
            add(analysis.incoming, true);
            std::sort(m_links.begin(), m_links.end(), [](const auto& a, const auto& b) {
                return a.incoming == b.incoming ? a.index < b.index : !a.incoming;
            });
        }
        endResetModel();
    }

    void SetMembers(Snapshot snapshot, NodeId dataSet)
    {
        if (m_snapshot == snapshot && m_context == dataSet)
            return;
        beginResetModel();
        m_snapshot = std::move(snapshot);
        m_context = dataSet;
        m_filter.reset();
        m_displayRow = -1;
        m_displayCells.clear();
        endResetModel();
    }

    void SetIed(NodeId ied)
    {
        const std::vector<int>* rows = nullptr;
        if (m_snapshot && ied != NoNode)
        {
            const auto scope = m_snapshot->browse.scopedRows.constFind(ied);
            if (scope != m_snapshot->browse.scopedRows.cend())
            {
                int view = -1;
                switch (m_table)
                {
                case Table::Reports: view = int(SclView::Reports); break;
                case Table::Objects: view = int(SclView::Objects); break;
                case Table::Communication: view = int(SclView::Communication); break;
                case Table::Templates: view = int(SclView::Templates); break;
                case Table::Findings: view = int(SclView::Findings); break;
                default: break;
                }
                if (view >= 0)
                    rows = &(*scope)[size_t(view)];
            }
        }
        if (rows == m_scopeRows)
            return;
        beginResetModel();
        m_scopeRows = rows;
        m_filter.reset();
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        if (parent.isValid() || !m_snapshot)
            return 0;
        if (m_filter)
            return int(m_filter->size());
        return ContentRowCount();
    }

    int ContentRowCount() const
    {
        if (!m_snapshot)
            return 0;
        if (m_scopeRows)
            return int(m_scopeRows->size());
        switch (m_table)
        {
        case Table::Reports: return int(m_snapshot->analysis.reports.size());
        case Table::Findings: return int(m_snapshot->analysis.findings.size());
        case Table::Links: return int(m_links.size());
        case Table::Candidates: return int(m_candidates.size());
        case Table::Members: return m_snapshot->browse.dataSetMembers.value(m_context).count;
        default: return int(Category().size());
        }
    }

    int columnCount(const QModelIndex& = {}) const override { return m_headers.size(); }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        return orientation == Qt::Horizontal && role == Qt::DisplayRole ? m_headers.value(section) : QVariant{};
    }

    QList<NodeId> Targets(int row) const
    {
        if (row < 0 || row >= rowCount())
            return {};
        if (m_table != Table::Links)
            return {Node(row)};
        const auto link = m_links[row];
        const auto& ref = m_snapshot->analysis.references[link.index];
        return link.incoming ? QList<NodeId>{ref.source} : ref.candidates;
    }

    const Scl::Reference& ReferenceAt(int row) const { return m_snapshot->analysis.references[m_links[row].index]; }
    int ReferenceIndex(int row) const { return m_links[row].index; }
    int RowForReference(int index) const
    {
        const auto found = std::find_if(m_links.begin(), m_links.end(),
            [index](const auto& link) { return link.index == index; });
        return found == m_links.end() ? -1 : int(found - m_links.begin());
    }
    bool LinksLimited() const { return m_linksLimited; }
    bool IsIncoming(int row) const { return m_links[row].incoming; }

    NodeId Node(int row) const
    {
        row = OriginalRow(row);
        switch (m_table)
        {
        case Table::Findings: return m_snapshot->analysis.findings[row].node;
        case Table::Candidates: return m_candidates[row];
        case Table::Links:
        {
            if (!IsIncoming(row) && ReferenceAt(row).state != Scl::LinkState::Resolved)
                return NoNode;
            const auto targets = Targets(row);
            return targets.size() == 1 ? targets.front() : NoNode;
        }
        default: return UnfilteredNode(row);
        }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::UserRole
            && role != SclTableInteraction::FullValueRole))
            return {};
        const auto id = Node(index.row());
        if (role == Qt::UserRole)
            return id;
        const int row = OriginalRow(index.row());
        // The view requests cells separately. Reuse the formatted row across its columns.
        if (m_displayRow != row)
        {
            m_displayCells = Cells(row, id);
            m_displayRow = row;
        }
        const auto value = m_displayCells.value(index.column());
        if (m_table == Table::Members && role == Qt::DisplayRole && value.size() > SclTableInteraction::PreviewLimit)
            return value.left(SclTableInteraction::PreviewLimit) + QStringLiteral("…（查看完整值）");
        if (role == Qt::ToolTipRole && !value.isEmpty())
            return QStringLiteral("<qt>%1</qt>").arg(value.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
        return value;
    }

    void Filter(std::optional<std::vector<int>> rows)
    {
        if (!rows && !m_filter)
            return;
        beginResetModel();
        m_filter = std::move(rows);
        endResetModel();
    }

    int VisibleRow(int row) const
    {
        const auto* rows = m_filter ? &*m_filter : m_scopeRows;
        if (row < 0 || !rows)
            return row;
        const auto found = std::lower_bound(rows->begin(), rows->end(), row);
        return found != rows->end() && *found == row ? int(found - rows->begin()) : -1;
    }

    int OriginalRow(int row) const { return m_filter ? (*m_filter)[row] : m_scopeRows ? (*m_scopeRows)[row] : row; }

    // Browse lists and report mappings are emitted in document order.
    int FirstAtOrAfter(NodeId id) const
    {
        if (m_table == Table::Reports)
        {
            const auto& reports = m_snapshot->analysis.reports;
            return int(std::lower_bound(reports.begin(), reports.end(), id,
                [](const auto& report, NodeId needle) { return report.source < needle; }) - reports.begin());
        }
        if (m_table == Table::Members)
        {
            const auto range = m_snapshot->browse.dataSetMembers.value(m_context);
            const auto first = m_snapshot->browse.memberRows.begin() + range.first;
            return int(std::lower_bound(first, first + range.count, id) - first);
        }
        const auto& rows = Category();
        return int(std::lower_bound(rows.begin(), rows.end(), id) - rows.begin());
    }

    NodeId UnfilteredNode(int row) const
    {
        if (row < 0)
            return NoNode;
        if (m_table == Table::Reports)
        {
            const auto& reports = m_snapshot->analysis.reports;
            return row < int(reports.size()) ? reports[row].source : NoNode;
        }
        if (m_table == Table::Members)
        {
            const auto range = m_snapshot->browse.dataSetMembers.value(m_context);
            return row < range.count ? m_snapshot->browse.memberRows[range.first + row] : NoNode;
        }
        const auto& rows = Category();
        return row < int(rows.size()) ? rows[row] : NoNode;
    }

    int RowForNode(NodeId id) const
    {
        const int row = FirstAtOrAfter(id);
        return UnfilteredNode(row) == id ? row : -1;
    }

private:
    struct Link { int index; bool incoming; };
    bool m_linksLimited = false;

    const std::vector<NodeId>& Category() const
    {
        switch (m_table)
        {
        case Table::Objects: return m_snapshot->browse.categories[size_t(SclCategory::Objects)];
        case Table::Communication: return m_snapshot->browse.categories[size_t(SclCategory::Communication)];
        default: return m_snapshot->browse.categories[size_t(SclCategory::Templates)];
        }
    }

    QStringList Cells(int row, NodeId id) const
    {
        const auto& snapshot = *m_snapshot;
        const auto& analysis = snapshot.analysis;
        const auto description = [&](NodeId node) {
            return node == NoNode ? QString() : Attribute(snapshot, node, "desc");
        };
        if (m_table == Table::Links)
        {
            const auto link = m_links[row];
            const auto& ref = analysis.references[link.index];
            auto target = id == NoNode ? QString::number(ref.candidates.size()) + QStringLiteral(" 个候选") : Title(snapshot, id);
            if (ref.candidates.isEmpty() && !link.incoming)
                target = QStringLiteral("—");
            if (id != NoNode)
            {
                const auto owner = snapshot.browse.iedOwners[id];
                const auto sourceOwner = snapshot.browse.iedOwners[m_context];
                if (owner != NoNode)
                    target = (sourceOwner != NoNode && owner != sourceOwner ? QStringLiteral("跨 IED · ") : QString())
                        + Attribute(snapshot, owner, "name") + QStringLiteral(" · ") + target;
            }
            return {link.incoming ? QStringLiteral("使用方") : QStringLiteral("引用"), ref.declared,
                    LinkState(ref.state), target, ref.detail,
                    link.incoming || ref.state == Scl::LinkState::Resolved ? description(id) : QString()};
        }
        if (m_table == Table::Findings)
        {
            const auto& finding = analysis.findings[row];
            return {Severity(finding.severity), finding.rule, QString::number(finding.location.line),
                    finding.objectPath, finding.detail, finding.suggestion, description(id)};
        }

        const auto attr = [&](const char* key) { return Attribute(snapshot, id, key); };
        const auto& node = analysis.document->nodes[id];
        switch (m_table)
        {
        case Table::Reports: {
            const auto& report = analysis.reports[row];
            const auto flag = attr("buffered");
            const auto mode = flag == "true" || flag == "1" ? QStringLiteral("BRCB")
                : flag.isNull() || flag == "false" || flag == "0" ? QStringLiteral("URCB") : QStringLiteral("未识别");
            return {Title(snapshot, id), Path(snapshot, id, true), report.rptId, report.declaredDataSet,
                    mode, report.definitionReference, description(id)};
        }
        case Table::Objects:
            return {Title(snapshot, id), Path(snapshot, id, true), attr("desc"), attr("lnType") + attr("type"),
                    attr("bType"), attr("fc"), attr("count") + attr("ix")};
        case Table::Communication:
        {
            QStringList address;
            const auto& branch = snapshot.browse.branches[id];
            for (int i = 0; i < qMin(branch.count, 24); ++i)
            {
                const auto child = snapshot.browse.children[branch.first + i];
                const auto& element = analysis.document->nodes[child];
                if (element.name != "Address" || !element.isScl())
                    continue;
                const auto& fields = snapshot.browse.branches[child];
                for (int j = 0; j < qMin(fields.count, 16); ++j)
                {
                    const auto field = snapshot.browse.children[fields.first + j];
                    const auto& parameter = analysis.document->nodes[field];
                    if (parameter.isScl() && parameter.name == "P")
                        address << Attribute(snapshot, field, "type") + '=' + parameter.text.left(128);
                }
            }
            return {Title(snapshot, id), Path(snapshot, id, true), attr("iedName") + ' ' + attr("apName"),
                    attr("ldInst") + ' ' + attr("cbName") + ' ' + attr("doName"), address.join(QStringLiteral(" · ")),
                    description(id)};
        }
        case Table::Templates:
            return {Title(snapshot, id), attr("lnClass") + attr("cdc"), QString::number(analysis.incoming.count(id)), description(id)};
        case Table::Members: {
            const auto found = analysis.outgoing.constFind(id);
            if (found == analysis.outgoing.cend())
                return {{}, Title(snapshot, id), {}, {}, {}, description(id)};
            const auto& ref = analysis.references[*found];
            const auto targetDescription = ref.state == Scl::LinkState::Resolved && ref.candidates.size() == 1
                ? description(ref.candidates.front()) : QString();
            return {QString::number(ref.memberOrdinal), ref.declared, attr("fc"), LinkState(ref.state), ref.objectReference,
                    description(id), targetDescription, ref.mmsItem,
                    ref.instanceDescription == NoNode ? QString() : analysis.document->nodes[ref.instanceDescription].text};
        }
        case Table::Candidates:
            return {snapshot.browse.iedOwners[id] == NoNode ? Title(snapshot, id)
                    : Attribute(snapshot, snapshot.browse.iedOwners[id], "name") + QStringLiteral(" · ") + Title(snapshot, id),
                    Path(snapshot, id, true), QString::number(node.source.line), description(id)};
        default: return {};
        }
    }

    Table m_table;
    QStringList m_headers;
    Snapshot m_snapshot;
    NodeId m_context = NoNode;
    std::vector<Link> m_links;
    QList<NodeId> m_candidates;
    std::optional<std::vector<int>> m_filter;
    const std::vector<int>* m_scopeRows = nullptr; // Owned by immutable m_snapshot.
    mutable int m_displayRow = -1;
    mutable QStringList m_displayCells;
};

void SclWorkspace::ApplyTableFilter(QTableView* table, std::optional<std::vector<int>> rows)
{
    if (table)
    {
        static_cast<SclTableModel*>(table->model())->Filter(std::move(rows));
        if (table == m_tables->currentWidget() && !m_restoring)
            LocateTableRow();
    }
}

SclWorkspace::SclWorkspace(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclWorkspace");
    // Keep the native item-view paint and size-hint paths for large SCL models.
    auto workspacePalette = palette();
    for (const auto group : {QPalette::Active, QPalette::Inactive})
    {
        workspacePalette.setColor(group, QPalette::Highlight, QColor(QStringLiteral("#2355a4")));
        workspacePalette.setColor(group, QPalette::HighlightedText, Qt::white);
    }
    setPalette(workspacePalette);
    auto* outer = new QVBoxLayout(this);
    auto* navigation = new QHBoxLayout;
    m_back = new QPushButton(QStringLiteral("返回"), this);
    m_back->setObjectName("SclBack");
    m_back->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_back->setEnabled(false);
    m_forward = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), QStringLiteral("前进"), this);
    m_forward->setObjectName("SclForward");
    m_forward->setEnabled(false);
    m_breadcrumb = new QLabel(QStringLiteral("打开 SCL 文件以开始分析"), this);
    m_breadcrumb->setObjectName("SclBreadcrumb");
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_pathMenu = new QToolButton(this);
    m_pathMenu->setText(QStringLiteral("完整路径"));
    m_pathMenu->setPopupMode(QToolButton::InstantPopup);
    m_pathMenu->setMenu(new QMenu(m_pathMenu));
    m_pathMenu->setEnabled(false);
    const auto jumpLink = [this](const QString& link) {
        bool ok = false;
        const auto id = link.toInt(&ok);
        if (ok)
            Select(id, true);
    };
    connect(m_breadcrumb, &QLabel::linkActivated, this, jumpLink);
    connect(m_pathMenu->menu(), &QMenu::aboutToShow, this, [this] {
        auto* menu = m_pathMenu->menu();
        menu->clear();
        if (!m_snapshot || m_selected == NoNode)
            return;
        const std::weak_ptr<const SclSnapshot> snapshot = m_snapshot;
        const auto path = Path(*m_snapshot, m_selected);
        menu->addAction(QStringLiteral("复制完整路径"), this, [this, snapshot, path] {
            if (snapshot.lock() == m_snapshot && m_snapshot)
                QApplication::clipboard()->setText(path);
        });
        menu->addSeparator();
        std::vector<NodeId> nodes;
        for (auto id = m_selected; id != NoNode; id = m_snapshot->analysis.document->nodes[id].parent)
            nodes.push_back(id);
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        {
            const auto id = *it;
            auto title = Title(*m_snapshot, id).left(120);
            title.replace('&', QStringLiteral("&&"));
            menu->addAction(title, this, [this, snapshot, id] {
                if (snapshot.lock() == m_snapshot && m_snapshot)
                    Select(id, true);
            });
        }
    });
    m_failedSource = new QPushButton(QStringLiteral("查看失败文件原文"), this);
    m_failedSource->setObjectName("SclFailedSource");
    m_failedSource->hide();
    navigation->addWidget(m_back);
    navigation->addWidget(m_forward);
    navigation->addWidget(m_breadcrumb, 1);
    navigation->addWidget(m_pathMenu);
    navigation->addWidget(m_failedSource);
    outer->addLayout(navigation);
    CreateSearchBar(outer);
    m_advancedScope = new QWidget(this);
    auto* browseRow = new QHBoxLayout(m_advancedScope);
    browseRow->setContentsMargins(0, 0, 0, 0);
    browseRow->addWidget(new QLabel(QStringLiteral("浏览范围"), this));
    m_browseScope = new QComboBox(this);
    m_browseScope->setObjectName("SclBrowseScope");
    m_browseScope->setMinimumContentsLength(20);
    m_browseScope->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_browseScope->addItem(QStringLiteral("整个工程"), NoNode);
    browseRow->addWidget(m_browseScope);
    m_allTemplates = new QCheckBox(QStringLiteral("显示全部模板"), this);
    m_allTemplates->setObjectName("SclAllTemplates");
    m_allTemplates->setToolTip(QStringLiteral("默认仅显示当前 IED 经唯一解析引用使用的模板；工程范围显示全部。"));
    browseRow->addWidget(m_allTemplates);
    browseRow->addStretch();
    outer->addWidget(m_advancedScope);
    connect(m_browseScope, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        if (!m_snapshot || m_restoring)
            return;
        const auto ied = m_browseScope->currentData().toInt();
        Select(ied == NoNode ? 0 : ied, true);
    });
    connect(m_allTemplates, &QCheckBox::toggled, this, [this] {
        if (!m_snapshot)
            return;
        const QScopedValueRollback<bool> guard(m_restoring, true);
        if (m_searchMode->currentIndex() == 1)
            ResetSearch();
        static_cast<SclTableModel*>(m_routeTables.back()->model())->SetIed(m_allTemplates->isChecked() ? NoNode : m_ied);
        UpdateOverview();
        LocateTableRow();
        if (m_searchMode->currentIndex() == 1)
            ScheduleSearch();
    });
    m_clearNavigationFilter = new QPushButton(QStringLiteral("目标被当前筛选隐藏 · 清除筛选"), this);
    m_clearNavigationFilter->setObjectName("SclClearNavigationFilter");
    m_clearNavigationFilter->hide();
    outer->addWidget(m_clearNavigationFilter, 0, Qt::AlignLeft);
    connect(m_clearNavigationFilter, &QPushButton::clicked, this, [this] {
        const bool outside = m_clearNavigationFilter->property("OutsideIed").toBool();
        const auto target = m_selected;
        ResetSearch(true);
        if (outside)
        {
            m_browseScope->setCurrentIndex(0);
            Select(target, true);
        }
    });

    m_docks = new ads::CDockManager(this);
    m_docks->setObjectName("SclDockManager");
    outer->addWidget(m_docks, 1);
    m_tree = new QTreeView(this);
    m_tree->setObjectName("SclTree");
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setAccessibleName(QStringLiteral("SCL 对象树"));
    m_treeModel = new SclTreeModel(m_tree);
    m_tree->setModel(m_treeModel);
    m_tableModes = new QStackedWidget(this);
    m_tableModes->setMinimumSize(0, 0); m_tableModes->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_taskBrowser = new SclTaskBrowser(m_tableModes);
    m_taskBrowser->View()->SetExternalNavigation(true);
    auto* tablePane = new QWidget(m_tableModes);
    m_tableModes->addWidget(tablePane);
    m_tableModes->addWidget(m_taskBrowser->View());
    m_taskSwitch = new QPushButton(QStringLiteral("任务视图"), this);
    m_taskSwitch->setObjectName("SclTaskViewSwitch");
    navigation->addWidget(m_taskSwitch);
    connect(m_taskSwitch, &QPushButton::clicked, this, [this] {
        ShowTaskView(m_tableModes->currentIndex() == 0, true);
    });
    connect(m_taskBrowser->View(), &SclTaskPages::ExportIssues, this, &SclWorkspace::SaveIssues);
    connect(m_taskBrowser, &SclTaskBrowser::ScopeChanged, this, [this](NodeId ied) {
        if (!m_snapshot)
            return;
        Select(ied == NoNode ? 0 : ied, true);
    });
    connect(m_taskBrowser, &SclTaskBrowser::OpenEvidence, this, [this](NodeId id, int finding) {
        if (!m_snapshot)
            return;
        RememberView();
        Select(id, false, finding, true);
        m_inspectorDock->toggleView(true);
        RememberView();
    });
    auto* tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    m_tables = new QTabWidget(tablePane);
    tableLayout->addWidget(m_tables, 1);
    auto* empty = new QLabel(tablePane);
    empty->setObjectName("SclTableEmpty");
    empty->setBackgroundRole(QPalette::AlternateBase);
    empty->setAutoFillBackground(true);
    empty->setMargin(8);
    empty->setWordWrap(true);
    tableLayout->addWidget(empty);
    empty->hide();
    m_tables->setObjectName("SclTables");
    m_tables->setUsesScrollButtons(true);
    m_inspector = new QTabWidget(this);
    m_inspector->setMinimumSize(0, 0); m_inspector->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_inspector->setObjectName("SclInspector");
    const auto dock = [this](const QString& title, const char* name, QWidget* widget) {
        auto* result = new ads::CDockWidget(m_docks, title);
        result->setObjectName(QString::fromLatin1(name));
        result->setWidget(widget);
        return result;
    };
    m_tableDock = dock(QStringLiteral("概览与数据"), "SclTableDock", m_tableModes);
    m_tableDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* tableArea = m_docks->addDockWidget(ads::CenterDockWidgetArea, m_tableDock);
    m_inspectorDock = dock(QStringLiteral("属性与依据"), "SclInspectorDock", m_inspector);
    m_docks->addDockWidget(ads::BottomDockWidgetArea, m_inspectorDock, tableArea);
    auto* navigationPane = new QWidget(this);
    auto* navigationLayout = new QVBoxLayout(navigationPane);
    navigationLayout->setContentsMargins(0, 0, 0, 0);
    m_navigationKind = new QComboBox(navigationPane); m_navigationKind->setObjectName("SclNavigationKind");
    m_navigationKind->addItems({QStringLiteral("任务导航"), QStringLiteral("XML 结构")});
    navigationLayout->addWidget(m_navigationKind);
    m_navigationModes = new QStackedWidget(navigationPane);
    m_tasks = new QTreeWidget(m_navigationModes); m_tasks->setObjectName("SclTaskTree");
    m_tasks->setHeaderHidden(true); m_tasks->setMinimumSize(0, 0);
    m_navigationModes->addWidget(m_tasks); m_navigationModes->addWidget(m_tree);
    navigationLayout->addWidget(m_navigationModes, 1);
    const auto task = [&](QTreeWidgetItem* parent, const QString& title, int page, int route = -1) {
        auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tasks);
        item->setText(0, title); item->setData(0, Qt::UserRole, page); item->setData(0, Qt::UserRole + 1, route);
        item->setIcon(0, style()->standardIcon(page == SclTaskPages::Mms ? QStyle::SP_DriveNetIcon : QStyle::SP_FileDialogListView)); return item;
    };
    task(nullptr, QStringLiteral("工程概览"), SclTaskPages::Overview);
    task(nullptr, QStringLiteral("信号查阅"), SclTaskPages::Signals);
    auto* mmsTask = task(nullptr, QStringLiteral("MMS 查阅"), SclTaskPages::Mms, int(SclMmsQueryKind::Reports));
    task(mmsTask, QStringLiteral("报告上送"), SclTaskPages::Mms, int(SclMmsQueryKind::Reports));
    auto* signalAddresses = task(mmsTask, QStringLiteral("信号地址"), SclTaskPages::Mms, int(SclMmsQueryKind::Signals));
    m_mmsAttributeTask = task(signalAddresses, QStringLiteral("查找属性"), SclTaskPages::Mms, int(SclMmsQueryKind::Attributes));
    task(mmsTask, QStringLiteral("连接配置"), SclTaskPages::Mms, int(SclMmsQueryKind::Endpoints));
    task(mmsTask, QStringLiteral("作为客户端的文件声明"), SclTaskPages::Mms, int(SclMmsQueryKind::ReverseClients));
    task(nullptr, QStringLiteral("GOOSE"), SclTaskPages::Goose);
    task(nullptr, QStringLiteral("SV"), SclTaskPages::SampledValues);
    task(nullptr, QStringLiteral("双向收发关系"), SclTaskPages::Connections);
    task(nullptr, QStringLiteral("文件检查"), SclTaskPages::Mms, int(SclMmsQueryKind::Findings));
    mmsTask->setExpanded(true);
    signalAddresses->setExpanded(true);
    connect(m_navigationKind, &QComboBox::currentIndexChanged, m_navigationModes, &QStackedWidget::setCurrentIndex);
    connect(m_tasks, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (!item || m_restoring || !m_snapshot) return;
        RememberView();
        {
            QScopedValueRollback<bool> guard(m_restoring, true);
            ShowTaskView(true);
            const auto page = static_cast<SclTaskPages::Page>(item->data(0, Qt::UserRole).toInt());
            m_taskBrowser->View()->SetPage(page);
            if (page == SclTaskPages::Mms) m_taskBrowser->View()->MmsView()->Open(static_cast<SclMmsQueryKind>(item->data(0, Qt::UserRole + 1).toInt()));
        }
        PushTaskHistory();
    });
    m_treeDock = dock(QStringLiteral("工程导航"), "SclTreeDock", navigationPane);
    m_treeDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_docks->addDockWidget(ads::LeftDockWidgetArea, m_treeDock);
    m_docks->setSplitterSizes(m_tableDock->dockAreaWidget(), {360, 230});
    m_docks->setSplitterSizes(m_treeDock->dockAreaWidget(), {250, 900});
    m_defaultLayout = m_docks->saveState(2);
    auto* details = new QToolButton(this);
    details->setObjectName("SclToggleDetails");
    details->setCheckable(true);
    details->setChecked(true);
    details->setText(QStringLiteral("详情"));
    details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    details->setToolTip(QStringLiteral("显示或收起详情"));
    navigation->addWidget(details);
    connect(details, &QToolButton::toggled, m_inspector, [this, details](bool visible) {
        m_inspectorDock->toggleView(visible);
        details->setIcon(style()->standardIcon(visible ? QStyle::SP_ArrowDown : QStyle::SP_ArrowUp));
        RefreshSourcePreview();
    });
    connect(m_inspectorDock, &ads::CDockWidget::viewToggled, details, &QToolButton::setChecked);
    auto* resetLayout = new QPushButton(QStringLiteral("重置工作台布局"), this);
    resetLayout->setObjectName("SclResetLayout");
    navigation->addWidget(resetLayout);
    connect(resetLayout, &QPushButton::clicked, this, &SclWorkspace::ResetWorkspaceLayout);

    auto* overview = new QWidget(m_tables);
    auto* overviewLayout = new QVBoxLayout(overview);
    m_summary = new QLabel(QStringLiteral("尚无可浏览的文档"), overview);
    m_summary->setObjectName("SclOverview");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(m_summary);
    m_scope = new QLabel(overview);
    m_scope->setObjectName("SclNodeOverview");
    m_scope->setTextFormat(Qt::PlainText);
    m_scope->setWordWrap(true);
    overviewLayout->addWidget(m_scope);
    m_scopeActions = new QWidget(overview);
    auto* scopeLayout = new QVBoxLayout(m_scopeActions);
    scopeLayout->setContentsMargins(0, 0, 0, 0);
    overviewLayout->addWidget(m_scopeActions);
    m_overviewCounts = new QLabel(overview);
    m_overviewCounts->setObjectName("SclOverviewMemberCounts");
    m_overviewCounts->setTextFormat(Qt::PlainText);
    m_overviewCounts->setWordWrap(true);
    overviewLayout->addWidget(m_overviewCounts);
    m_overviewCounts->hide();
    m_overviewQuery = new SclHierarchyQuerySession(this);
    m_overviewUsers = new QPushButton(QStringLiteral("查看数据集使用方"), m_scopeActions);
    m_overviewUsers->setObjectName("SclOverviewUsers");
    scopeLayout->addWidget(m_overviewUsers, 0, Qt::AlignLeft);
    connect(m_overviewUsers, &QPushButton::clicked, this, [this] { m_dataSets->OpenUsers(m_dataSet); });
    m_overviewRelation = new QPushButton(m_scopeActions);
    m_overviewRelation->setObjectName("SclOverviewRelation");
    scopeLayout->addWidget(m_overviewRelation, 0, Qt::AlignLeft);
    connect(m_overviewRelation, &QPushButton::clicked, this, [this] {
        if (m_relationTarget != NoNode)
            FollowRelationship(m_relationTarget, m_overviewRelation->text());
        else
        {
            m_inspectorDock->toggleView(true);
            m_inspector->setCurrentIndex(1);
        }
    });
    m_overviewTopology = new QPushButton(style()->standardIcon(QStyle::SP_FileLinkIcon),
        QStringLiteral("打开局部引用拓扑"), m_scopeActions);
    m_overviewTopology->setObjectName("SclOverviewTopology");
    scopeLayout->addWidget(m_overviewTopology, 0, Qt::AlignLeft);
    connect(m_overviewTopology, &QPushButton::clicked, this, [this] { OpenTopology(m_selected); });
    m_overviewStructure = new QPushButton(QStringLiteral("在结构树查看逻辑设备与逻辑节点"), m_scopeActions);
    scopeLayout->addWidget(m_overviewStructure, 0, Qt::AlignLeft);
    connect(m_overviewStructure, &QPushButton::clicked, this, [this] {
        m_treeDock->toggleView(true);
        const auto index = m_treeModel->ForNode(m_ied == NoNode ? 0 : m_ied);
        m_tree->expand(index);
        m_tree->scrollTo(index);
        m_tree->setFocus();
    });
    m_overviewDataSets = new QPushButton(m_scopeActions);
    m_overviewDataSets->setObjectName("SclOverviewDataSets");
    scopeLayout->addWidget(m_overviewDataSets, 0, Qt::AlignLeft);
    connect(m_overviewDataSets, &QPushButton::clicked, this, [this] { OpenAdvancedPage(m_dataSets); });
    m_scope->hide();
    m_scopeActions->hide();
    overviewLayout->addStretch();
    auto* overviewScroll = new QScrollArea(m_tables);
    overviewScroll->setFrameShape(QFrame::NoFrame);
    overviewScroll->setWidgetResizable(true);
    overviewScroll->setWidget(overview);
    m_tables->addTab(overviewScroll, QStringLiteral("概览"));

    m_memberContext = new QWidget(tablePane);
    auto* memberHeading = new QHBoxLayout(m_memberContext);
    memberHeading->setContentsMargins(4, 2, 4, 2);
    m_memberPath = new QLabel(m_memberContext);
    m_memberPath->setObjectName("SclMemberContext");
    m_memberPath->setTextFormat(Qt::PlainText);
    m_memberPath->setWordWrap(true);
    m_memberPath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    memberHeading->addWidget(m_memberPath, 1);
    auto* returnToDataSet = new QPushButton(QStringLiteral("返回数据集"), m_memberContext);
    returnToDataSet->setObjectName("SclMembersReturn");
    memberHeading->addWidget(returnToDataSet);
    auto* memberSource = new QPushButton(QStringLiteral("查看原文"), m_memberContext);
    memberSource->setObjectName("SclMembersSource");
    memberHeading->addWidget(memberSource);
    tableLayout->insertWidget(0, m_memberContext);
    m_memberContext->hide();
    connect(returnToDataSet, &QPushButton::clicked, this, [this] { OpenAdvancedPage(m_dataSets); });
    connect(memberSource, &QPushButton::clicked, this, [this] {
        const auto index = m_members->currentIndex();
        if (index.isValid())
        {
            m_inspectorDock->toggleView(true);
            Select(index.data(Qt::UserRole).toInt(), false, -1, true, true);
        }
    });

    const auto createTable = [this](Table table, QWidget* owner, const char* name) {
        auto* view = new QTableView(owner);
        view->setObjectName(QString::fromLatin1(name));
        auto* model = new SclTableModel(table, view);
        view->setModel(model);
        m_models.push_back(model);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setSelectionMode(QAbstractItemView::SingleSelection);
        view->setAlternatingRowColors(true);
        view->setShowGrid(false);
        view->setWordWrap(false);
        view->verticalHeader()->hide();
        view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
        view->verticalHeader()->setDefaultSectionSize(qMax(28, view->fontMetrics().height() + 8));
        view->horizontalHeader()->setDefaultSectionSize(135);
        if (table == Table::Findings)
        {
            view->setColumnWidth(0, 55);
            view->setColumnWidth(1, 75);
            view->setColumnWidth(2, 55);
            view->setColumnWidth(3, 135);
            view->setColumnWidth(4, 215);
        }
        else if (table == Table::Links)
        {
            view->setColumnWidth(0, 65);
            view->setColumnWidth(1, 165);
            view->setColumnWidth(2, 85);
        }
        else if (table == Table::Members)
        {
            view->setColumnWidth(0, 50);
            view->setColumnWidth(1, 200);
            view->setColumnWidth(2, 55);
            view->setColumnWidth(3, 85);
        }
        view->horizontalHeader()->setStretchLastSection(true);
        return view;
    };
    const auto addTable = [&](Table table, const QString& label, const char* name) {
        auto* view = createTable(table, m_tables, name);
        view->setAccessibleName(label);
        m_tables->addTab(view, label);
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this, table, view](const QModelIndex& index) {
            if (index.isValid() && !m_restoring)
            {
                if (table == Table::Findings)
                    SelectFinding(static_cast<SclTableModel*>(view->model())->OriginalRow(index.row()), false);
                else
                    Select(index.data(Qt::UserRole).toInt(), false, -1, false, table == Table::Members);
            }
        });
        return view;
    };
    m_routeTables.push_back(addTable(Table::Reports, QStringLiteral("报告"), "SclReports"));
    m_dataSets = new SclDataSetView(m_tables);
    m_tables->addTab(m_dataSets, QStringLiteral("数据集"));
    connect(m_dataSets, &SclDataSetView::Selected, this, [this](NodeId id) {
        if (!m_restoring)
            Select(id, false, -1, false, true);
    });
    connect(m_dataSets, &SclDataSetView::OpenSource, this, [this](NodeId id) {
        m_inspectorDock->toggleView(true);
        Select(id, false, -1, true, true);
    });
    connect(m_dataSets, &SclDataSetView::OpenMembers, this, &SclWorkspace::OpenMemberPage);
    connect(m_dataSets, &SclDataSetView::OpenTarget, this,
        [this](NodeId node) { FollowRelationship(node, QStringLiteral("成员引用目标")); });
    connect(m_dataSets, &SclDataSetView::OpenUser, this,
        [this](NodeId node) { FollowRelationship(node, QStringLiteral("数据集使用方")); });
    connect(m_dataSets, &SclDataSetView::OpenTopology, this, &SclWorkspace::OpenTopology);
    auto* dataSetText = m_dataSets->Tree()->findChild<SclTableInteraction*>();
    const auto addMmsAction = [this](SclTableInteraction* interaction) {
        connect(interaction, &SclTableInteraction::PopulateContextMenu, this, [this, interaction](QMenu& menu, const QModelIndex& index) {
            AddMmsAttributeAction(menu, index.data(Qt::UserRole).toInt(), interaction->View(), index);
        });
        AddNodeReferenceMenu(interaction);
    };
    addMmsAction(dataSetText);
    connect(m_dataSets, &SclDataSetView::LocationUpdated, this, &SclWorkspace::RememberView);
    connect(m_dataSets, &SclDataSetView::LocationUpdated, this, &SclWorkspace::BalanceAdvancedPanels, Qt::QueuedConnection);
    connect(m_dataSets, &SclDataSetView::FilterHidesSelection, this, [this](bool hidden) {
        if (m_tables->currentWidget() != m_dataSets || m_dataSet == NoNode)
            return;
        const bool outside = !m_snapshot->browse.InScope(m_dataSet, m_ied);
        m_clearNavigationFilter->setProperty("OutsideIed", outside);
        m_clearNavigationFilter->setText(outside ? QStringLiteral("目标不在当前 IED 范围 · 查看整个工程")
            : QStringLiteral("目标被当前筛选隐藏 · 清除筛选"));
        m_clearNavigationFilter->setVisible(hidden);
    });
    m_members = addTable(Table::Members, QStringLiteral("成员"), "SclMembers");
    auto* memberText = new SclTableInteraction(m_members);
    memberText->SetEnabled(true);
    addMmsAction(memberText);
    connect(memberText, &SclTableInteraction::OpenSource, memberSource, &QPushButton::click);
    m_routeTables.push_back(addTable(Table::Objects, QStringLiteral("对象"), "SclObjects"));
    m_routeTables.push_back(addTable(Table::Communication, QStringLiteral("通信"), "SclCommunication"));
    m_routeTables.push_back(addTable(Table::Templates, QStringLiteral("模板"), "SclTemplates"));
    for (auto* view : m_routeTables)
    {
        auto* button = new QPushButton(m_scopeActions);
        scopeLayout->addWidget(button, 0, Qt::AlignLeft);
        m_scopeButtons.push_back(button);
        connect(button, &QPushButton::clicked, this, [this, view] { OpenAdvancedPage(view); });
    }
    m_findings = addTable(Table::Findings, QStringLiteral("问题"), "SclFindings");
    AddNodeReferenceMenu(new SclTableInteraction(m_findings));
    connect(m_findings, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid())
            SelectFinding(static_cast<SclTableModel*>(m_findings->model())->OriginalRow(index.row()), true);
    });
    AttachSearchResults();
    const auto updateEmpty = [this, empty] {
        const auto* table = qobject_cast<QTableView*>(m_tables->currentWidget());
        if (!table || table->model()->rowCount() != 0)
        {
            empty->hide();
            return;
        }
        const auto message = !m_snapshot ? QStringLiteral("打开 SCL 文件后查看数据。")
            : table == m_results ? QStringLiteral("此处显示匹配结果，请查看搜索状态。")
            : table == m_members && m_dataSet == NoNode ? QStringLiteral("请先选择数据集以查看其成员。")
            : m_ied != NoNode ? QStringLiteral("当前 IED 无可显示的行，或被筛选隐藏。工程级内容请切回「整个工程」。")
            : QStringLiteral("此视图没有可显示的行，请检查当前对象或筛选条件。");
        if (empty->text() != message)
            empty->setText(message);
        empty->show();
    };
    connect(m_tables, &QTabWidget::currentChanged, this, updateEmpty);
    for (int i = 0; i < m_tables->count(); ++i)
    {
        if (const auto* table = qobject_cast<QTableView*>(m_tables->widget(i)))
        {
            const auto updateCurrent = [this, table, updateEmpty] {
                UpdateTabVisibility();
                if (m_tables->currentWidget() == table)
                    updateEmpty();
            };
            connect(table->model(), &QAbstractItemModel::modelReset, this, updateCurrent);
            connect(table->model(), &QAbstractItemModel::rowsInserted, this, updateCurrent);
        }
    }

    m_properties = new QTextBrowser(m_inspector);
    m_properties->setObjectName("SclProperties");
    m_properties->setReadOnly(true);
    m_properties->setOpenLinks(false);
    connect(m_properties, &QTextBrowser::anchorClicked, this, [this](const QUrl& link) {
        bool ok = false;
        const auto id = link.path().toInt(&ok);
        if (ok && link.isRelative())
            Select(id, true, -1, true);
    });
    m_inspector->addTab(m_properties, QStringLiteral("属性"));

    auto* relations = new QWidget(m_inspector);
    auto* relationsLayout = new QVBoxLayout(relations);
    relationsLayout->setContentsMargins(4, 4, 4, 4);
    m_relationshipSummary = new QLabel(relations);
    m_relationshipSummary->setTextFormat(Qt::PlainText);
    m_relationshipSummary->setWordWrap(true);
    m_relationshipSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_relationshipSummary->setObjectName("SclRelationshipSummary");
    relationsLayout->addWidget(m_relationshipSummary);
    m_referencePath = new QLabel(relations);
    m_referencePath->setObjectName("SclReferencePath");
    m_referencePath->setTextFormat(Qt::RichText);
    m_referencePath->setWordWrap(true);
    m_referencePath->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_referencePath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(m_referencePath, &QLabel::linkActivated, this, [this](const QString& link) {
        bool ok = false;
        const auto node = link.toInt(&ok);
        if (ok)
            FollowRelationship(node, QStringLiteral("引用路径依据"));
    });
    relationsLayout->addWidget(m_referencePath);
    auto* referenceActions = new QHBoxLayout;
    m_copyDeclared = new QPushButton(QStringLiteral("复制声明"), relations);
    m_copyDeclared->setObjectName("SclCopyDeclared");
    m_copyDerived = new QPushButton(QStringLiteral("复制推导引用"), relations);
    m_copyDerived->setObjectName("SclCopyDerived");
    for (auto* button : {m_copyDeclared, m_copyDerived})
    {
        button->setEnabled(false);
        referenceActions->addWidget(button);
        connect(button, &QPushButton::clicked, this, [button] {
            QApplication::clipboard()->setText(button->property("sclCopyValue").toString());
        });
    }
    auto* legend = new QToolButton(relations);
    legend->setObjectName("SclRelationshipHelp");
    legend->setIcon(style()->standardIcon(QStyle::SP_DialogHelpButton));
    legend->setText(QStringLiteral("关系说明"));
    legend->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    legend->setToolTip(QStringLiteral("查看包含、引用、候选和查阅来源（Ctrl+Shift+R）"));
    referenceActions->addWidget(legend);
    connect(legend, &QToolButton::clicked, this, &SclWorkspace::ShowRelationshipHelp);
    auto* helpKey = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R), this);
    helpKey->setContext(Qt::WidgetWithChildrenShortcut);
    connect(helpKey, &QShortcut::activated, this, &SclWorkspace::ShowRelationshipHelp);
    referenceActions->addStretch();
    relationsLayout->addLayout(referenceActions);
    auto* ownership = new QHBoxLayout;
    m_relationshipParent = new QPushButton(style()->standardIcon(QStyle::SP_DirIcon),
        QStringLiteral("所属数据集"), relations);
    m_relationshipParent->setObjectName("SclRelationshipOwner");
    m_relationshipUsers = new QPushButton(style()->standardIcon(QStyle::SP_ArrowBack),
        QStringLiteral("数据集使用方"), relations);
    m_relationshipUsers->setObjectName("SclRelationshipUsers");
    ownership->addWidget(m_relationshipParent);
    ownership->addWidget(m_relationshipUsers);
    ownership->addStretch();
    relationsLayout->addLayout(ownership);
    m_relationshipParent->hide();
    m_relationshipUsers->hide();
    connect(m_relationshipParent, &QPushButton::clicked, this, [this] {
        FollowRelationship(m_dataSet, QStringLiteral("所属数据集"));
    });
    connect(m_relationshipUsers, &QPushButton::clicked, this, [this] { m_dataSets->OpenUsers(m_dataSet); });
    CreateTopologyControls(relationsLayout);
    for (auto* table : m_routeTables)
        AddTopologyAction(table);
    auto* memberTopology = new QAction(QStringLiteral("打开引用拓扑"), m_members);
    m_members->addAction(memberTopology);
    connect(memberTopology, &QAction::triggered, this, [this] {
        if (const auto row = m_members->currentIndex(); row.isValid())
            OpenTopology(row.data(Qt::UserRole).toInt());
    });
    AddTopologyAction(m_tree);
    m_links = createTable(Table::Links, relations, "SclLinks");
    auto* linkText = new SclTableInteraction(m_links);
    connect(linkText, &SclTableInteraction::PopulateContextMenu, this, [this](QMenu& menu, const QModelIndex& index) {
        if (!m_snapshot) return;
        const auto& ref = static_cast<SclTableModel*>(m_links->model())->ReferenceAt(index.row());
        if (ref.kind == Scl::LinkKind::Fcda) AddMmsAttributeAction(menu, ref.source, m_links, index);
    });
    ConnectSclReferenceMenu(linkText, [this](const QModelIndex& index) {
        if (!m_snapshot) return SclReferenceMenuContext{};
        SclReferenceRequest request;
        request.anchor = SclReferenceAnchor::Link;
        request.record = static_cast<SclTableModel*>(m_links->model())->ReferenceIndex(index.row());
        request.limited = static_cast<SclTableModel*>(m_links->model())->LinksLimited();
        return ReferenceMenu(std::move(request));
    });
    m_candidates = createTable(Table::Candidates, relations, "SclCandidates");
    AddNodeReferenceMenu(new SclTableInteraction(m_candidates), true);
    relationsLayout->addWidget(m_links, 3);
    m_linksLimit = new QLabel(QStringLiteral("已显示 512 条关系 · 更多结果受限。"), relations);
    m_linksLimit->hide();
    relationsLayout->addWidget(m_linksLimit);
    m_candidatesLimit = new QLabel(relations);
    m_candidatesLimit->setTextFormat(Qt::PlainText);
    m_candidatesLimit->hide();
    relationsLayout->addWidget(m_candidatesLimit);
    relationsLayout->addWidget(m_candidates, 2);
    m_candidates->hide();
    auto* relationScroll = new QScrollArea(m_inspector);
    relationScroll->setFrameShape(QFrame::NoFrame);
    relationScroll->setWidgetResizable(true);
    relationScroll->setWidget(relations);
    m_inspector->addTab(relationScroll, QStringLiteral("引用与使用方"));

    auto* sourcePage = new QWidget(m_inspector);
    auto* sourceLayout = new QVBoxLayout(sourcePage);
    m_sourceLabel = new QLabel(sourcePage);
    m_sourceLabel->setObjectName("SclSourceLabel");
    m_sourceLabel->setTextFormat(Qt::PlainText);
    m_sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_structureTitle = new QLabel(sourcePage);
    m_structureTitle->setObjectName("SclStructureContext");
    m_structureTitle->setTextFormat(Qt::PlainText);
    m_structureTitle->setWordWrap(true);
    m_structureTitle->hide();
    auto* returnSignal = new QPushButton(QStringLiteral("返回所选信号"), sourcePage);
    returnSignal->hide();
    sourceLayout->addWidget(m_structureTitle);
    sourceLayout->addWidget(returnSignal);
    m_source = new SclSourcePreview(sourcePage);
    m_source->setMinimumSize(0, 0);
    m_source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    sourceLayout->addWidget(m_sourceLabel);
    sourceLayout->addWidget(m_source, 1);
    m_inspector->addTab(sourcePage, QStringLiteral("原文"));
    m_signalInspector = new QWidget(m_inspector);
    auto* signalLayout = new QVBoxLayout(m_signalInspector);
    auto* signalActions = new QHBoxLayout;
    m_signalTitle = new QLabel(m_signalInspector);
    m_signalTitle->setObjectName("SclSelectedSignal");
    m_signalTitle->setTextFormat(Qt::PlainText);
    m_signalTitle->setWordWrap(true);
    m_signalTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_locateSignal = new QPushButton(QStringLiteral("定位选中行"), m_signalInspector);
    m_locateSignal->setObjectName("SclLocateSignal");
    auto* copySignal = new QPushButton(QStringLiteral("复制引用"), m_signalInspector);
    signalActions->addWidget(m_signalTitle, 1);
    signalActions->addWidget(m_locateSignal);
    signalActions->addWidget(copySignal);
    signalLayout->addLayout(signalActions);
    auto* signalDetail = new QTextBrowser(m_signalInspector);
    signalDetail->setObjectName("SclSignalInspector");
    signalDetail->setOpenLinks(false);
    signalDetail->setMinimumSize(0, 0);
    signalLayout->addWidget(signalDetail);
    const int signalTab = m_inspector->addTab(m_signalInspector, QStringLiteral("所选信号"));
    m_inspector->setTabVisible(signalTab, false);
    auto* mmsView = m_taskBrowser->View()->MmsView();
    const int mmsTab = m_inspector->addTab(mmsView->Inspector(), QStringLiteral("MMS 字段与依据"));
    m_inspector->setTabVisible(mmsTab, false);
    connect(mmsView, &SclMmsView::BeforeNavigate, this, &SclWorkspace::RememberView);
    connect(mmsView, &SclMmsView::Navigated, this, &SclWorkspace::PushTaskHistory);
    connect(mmsView, &SclMmsView::LocationUpdated, this, &SclWorkspace::RememberView);
    connect(mmsView, &SclMmsView::HistoryRequested, this, &SclWorkspace::Navigate);
    connect(mmsView, &SclMmsView::LocationUpdated, this, &SclWorkspace::BalanceMmsPanels, Qt::QueuedConnection);
    connect(mmsView, &SclMmsView::GeometryChanged, this, &SclWorkspace::BalanceMmsPanels, Qt::QueuedConnection);
    for (auto* scroll : {m_source->horizontalScrollBar(), m_source->verticalScrollBar(),
        mmsView->Table()->horizontalScrollBar(), mmsView->Fields()->horizontalScrollBar()})
        connect(scroll, &QScrollBar::rangeChanged, this, &SclWorkspace::BalanceMmsPanels, Qt::QueuedConnection);
    connect(m_inspectorDock, &ads::CDockWidget::viewToggled, mmsView, &SclMmsView::SetInspectorVisible);
    connect(mmsView, &SclMmsView::RestoreInspector, this, [this, mmsView](bool visible) {
        m_inspectorDock->toggleView(visible);
        if (visible) m_inspector->setCurrentWidget(mmsView->Inspector());
    });
    connect(mmsView, &SclMmsView::SelectedNode, this, [this](int node) {
        // History has already restored this XML node; avoid rebuilding its scope twice.
        if (node == m_selected && m_finding < 0 && !m_showingFailure) return;
        QScopedValueRollback<bool> guard(m_restoring, true); Select(node);
    });
    connect(mmsView, &SclMmsView::ScopeRequested, this, [this](int ied) {
        QScopedValueRollback<bool> guard(m_restoring, true); SetIedScope(ied);
    });
    connect(mmsView, &SclMmsView::OpenEvidence, this, [this](int node, const QString& label) {
        RememberView(); m_inspectorDock->toggleView(true); Select(node, false, -1, true);
        m_signalEvidence = label; RefreshSourcePreview(); RememberView();
    });
    connect(mmsView, &SclMmsView::ShowInspector, this, [this, mmsView] {
        m_inspectorDock->toggleView(true); m_inspector->setCurrentWidget(mmsView->Inspector());
    });
    connect(mmsView, &SclMmsView::ExportIssues, this, &SclWorkspace::SaveIssues);
    const auto showMms = [this, mmsView, mmsTab] {
        const bool visible = m_tableModes->currentIndex() == 1 && m_taskBrowser->IsMmsPage();
        m_inspector->setTabVisible(mmsTab, visible);
        if (visible && !m_restoring) { m_inspectorDock->toggleView(true); m_inspector->setCurrentWidget(mmsView->Inspector()); }
    };
    connect(m_taskBrowser, &SclTaskBrowser::SignalPageChanged, this, showMms);
    connect(m_tableModes, &QStackedWidget::currentChanged, this, showMms);
    connect(m_locateSignal, &QPushButton::clicked, m_taskBrowser, &SclTaskBrowser::LocateSignal);
    connect(copySignal, &QPushButton::clicked, this, [this] {
        const auto path = m_taskBrowser->SignalPath();
        if (!path.isEmpty())
            QApplication::clipboard()->setText(path);
    });
    connect(returnSignal, &QPushButton::clicked, this, [this] {
        m_inspector->setCurrentWidget(m_signalInspector);
        m_taskBrowser->LocateSignal();
    });
    connect(signalDetail, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
        bool ok = false;
        const auto index = url.path().toInt(&ok);
        if (ok && url.isRelative())
            m_taskBrowser->OpenSignalEvidence(index);
    });
    connect(m_taskBrowser, &SclTaskBrowser::SignalDetailChanged, this,
        [this, signalDetail, copySignal, returnSignal](const QString& path, const QString& html) {
        m_signalTitle->setText(path.isEmpty() ? QStringLiteral("请选择一个信号") : QStringLiteral("所选信号 · ") + path);
        m_locateSignal->setEnabled(!path.isEmpty());
        copySignal->setEnabled(!path.isEmpty());
        returnSignal->setVisible(!path.isEmpty() && m_tableModes->currentIndex() == 1);
        signalDetail->setHtml(html);
        RefreshSourcePreview();
    });
    const auto showSignalTab = [this, signalTab, returnSignal] {
        const bool show = m_tableModes->currentIndex() == 1 && m_taskBrowser->IsSignalPage();
        m_inspector->setTabVisible(signalTab, show);
        returnSignal->setVisible(show && !m_taskBrowser->SignalPath().isEmpty());
        if (show && !m_restoring)
        {
            m_inspectorDock->toggleView(true);
            m_inspector->setCurrentWidget(m_signalInspector);
        }
        RefreshSourcePreview();
    };
    connect(m_taskBrowser, &SclTaskBrowser::SignalPageChanged, this, showSignalTab);
    connect(m_taskBrowser, &SclTaskBrowser::SignalSelectionChanged, this, [this] {
        if (m_tableModes->currentIndex() == 1)
        {
            m_signalEvidence.clear();
            m_inspector->setCurrentWidget(m_signalInspector);
            RememberView();
        }
    });
    connect(m_tableModes, &QStackedWidget::currentChanged, this, showSignalTab);
    connect(m_taskBrowser, &SclTaskBrowser::OpenSignalSource, this, [this](NodeId id, const QString& label) {
        RememberView();
        m_inspectorDock->toggleView(true);
        Select(id, false, -1, true);
        m_signalEvidence = label;
        RefreshSourcePreview();
        RememberView();
    });

    const auto selectTree = [this](const QModelIndex& index) {
        if (!m_restoring)
        {
            const bool signal = m_tableModes->currentIndex() == 1 && m_taskBrowser->IsSignalPage();
            if (signal)
                RememberView();
            Select(SclTreeModel::Node(index), true, -1, signal);
            if (m_taskBrowser->IsMmsPage() && m_tableModes->currentIndex() == 1)
                m_taskBrowser->View()->MmsView()->OpenNode(SclTreeModel::Node(index));
            if (signal)
            {
                m_signalEvidence.clear();
                RefreshSourcePreview();
                RememberView();
            }
        }
    };
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, selectTree);
    connect(m_tree, &QTreeView::clicked, this, selectTree);
    connect(m_links->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        auto* model = static_cast<SclTableModel*>(m_links->model());
        const bool unresolved = index.isValid() && !model->IsIncoming(index.row())
            && model->ReferenceAt(index.row()).state != Scl::LinkState::Resolved;
        const auto count = unresolved ? model->ReferenceAt(index.row()).candidates.size() : 0;
        auto targets = unresolved ? model->ReferenceAt(index.row()).candidates.mid(0, 64) : QList<NodeId>{};
        m_candidates->setVisible(!targets.isEmpty());
        m_candidatesLimit->setVisible(!targets.isEmpty());
        if (!targets.isEmpty())
            m_candidatesLimit->setText(count > targets.size() ? QStringLiteral("候选依据 · 显示前 %1 / %2 项")
                .arg(targets.size()).arg(count) : QStringLiteral("候选依据 · %1 项").arg(count));
        static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot, NoNode, std::move(targets));
        UpdateReferencePath(index);
        RememberView();
    });
    connect(m_candidates->selectionModel(), &QItemSelectionModel::currentChanged, this, &SclWorkspace::RememberView);
    connect(m_links, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        const auto id = index.data(Qt::UserRole).toInt();
        if (id == NoNode)
        {
            if (m_candidates->model()->rowCount())
                m_candidates->setFocus();
            return;
        }
        const auto* model = static_cast<SclTableModel*>(m_links->model());
        FollowRelationship(id, RelationshipName(*m_snapshot, model->ReferenceAt(index.row()), model->IsIncoming(index.row())));
    });
    connect(m_candidates, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid())
            FollowRelationship(index.data(Qt::UserRole).toInt(), QStringLiteral("候选依据"));
    });
    connect(m_back, &QPushButton::clicked, this, [this] { Navigate(-1); });
    connect(m_forward, &QPushButton::clicked, this, [this] { Navigate(1); });
    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    auto* forwardShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    forwardShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, m_back, &QPushButton::click);
    connect(forwardShortcut, &QShortcut::activated, m_forward, &QPushButton::click);
    connect(m_tables, &QTabWidget::currentChanged, this, [this] {
        m_memberContext->setVisible(m_tables->currentWidget() == m_members);
        RefreshOverviewSummary();
        if (!m_restoring)
            ChangeTable();
        if (m_searchMode->currentIndex() == 1 && !m_restoring)
            ScheduleSearch();
        else
            UpdateSearchStatus();
        RememberView();
        QTimer::singleShot(0, this, &SclWorkspace::BalanceAdvancedPanels);
    });
    connect(m_overviewQuery, &SclHierarchyQuerySession::Ready, this, [this] {
        const auto result = m_overviewQuery->result();
        if (result->request.dataSet != m_dataSet || m_tables->currentIndex() != 0)
            return;
        m_overviewCounts->setText(QStringLiteral("成员声明 %1 · 已解析 %2 · 歧义 %3 · 未解析 %4%5")
            .arg(result->scopeTotal).arg(result->resolved).arg(result->ambiguous).arg(result->unresolved)
            .arg(result->limited ? QStringLiteral("（状态计数为已发现下界）") : QString{}));
    });
    connect(m_inspector, &QTabWidget::currentChanged, this, [this] {
        RefreshSourcePreview();
        RememberView();
    });
    RestoreWorkspaceLayout();
    UpdateTabVisibility();
    connect(m_failedSource, &QPushButton::clicked, this, [this] {
        m_showingFailure = true;
        m_sourceDirty = true;
        m_inspector->setCurrentIndex(2);
        RefreshSourcePreview();
    });
}

SclWorkspace::~SclWorkspace()
{
    m_closing = true;
    for (auto* object : findChildren<QObject*>()) QObject::disconnect(object, nullptr, this, nullptr);
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    const auto state = m_docks->saveState(2);
    settings.setValue("SclWorkspace/Layout", state);
    settings.setValue("SclWorkspace/LayoutSha256", QCryptographicHash::hash(state, QCryptographicHash::Sha256));
    settings.setValue("SclWorkspace/TaskView", m_preferTaskView);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        qWarning("Cannot save SCL workspace layout");
    delete m_docks;
}

void SclWorkspace::hideEvent(QHideEvent* event)
{
    m_overviewQuery->Cancel();
    m_dataSets->CloseUsers();
    m_docks->hideManagerAndFloatingWidgets();
    QWidget::hideEvent(event);
}

void SclWorkspace::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_docks->show();
    RefreshOverviewSummary();
    RefreshSourcePreview();
    QTimer::singleShot(0, this, &SclWorkspace::BalanceAdvancedPanels);
}

void SclWorkspace::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    QTimer::singleShot(0, this, &SclWorkspace::BalanceAdvancedPanels);
}

void SclWorkspace::ResetWorkspaceLayout()
{
    m_docks->restoreState(m_defaultLayout, 2);
    m_treeDock->toggleView(true);
    m_tableDock->toggleView(true);
    m_inspectorDock->toggleView(true);
}

void SclWorkspace::RestoreWorkspaceLayout()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    m_preferTaskView = settings.value("SclWorkspace/TaskView", true).toBool();
    const auto state = settings.value("SclWorkspace/Layout").toByteArray();
    if (state.isEmpty())
        return;
    if (settings.value("SclWorkspace/LayoutSha256").toByteArray()
            != QCryptographicHash::hash(state, QCryptographicHash::Sha256)
        || !m_docks->restoreState(state, 2) || !m_treeDock->dockAreaWidget()
        || !m_tableDock->dockAreaWidget() || m_treeDock->isClosed() || m_tableDock->isClosed())
        ResetWorkspaceLayout();
}

bool SclWorkspace::UpdateSession(Snapshot snapshot, const Scl::Result& failure)
{
    if (m_showingFailure && m_failure.source != failure.source)
    {
        m_showingFailure = false;
        m_sourceDirty = true;
    }
    m_failure = failure;
    m_failedSource->setVisible(bool(failure.source));
    if (m_snapshot == snapshot)
    {
        RefreshSourcePreview();
        return false;
    }

    m_selected = NoNode;
    m_finding = -1;
    m_clearNavigationFilter->hide();
    m_showingFailure = false;
    m_sourceDirty = true;
    m_history.clear();
    m_relationship = {};
    m_signalEvidence.clear();
    m_historyIndex = -1;
    m_dataSet = NoNode;
    m_ied = NoNode;
    m_tabNodes.clear();
    m_tabFinding = -1;
    UpdateHistoryButtons();
    ResetSearch(true);
    m_snapshot = std::move(snapshot);
    m_dataSets->SetContext(m_snapshot, NoNode);
    m_overviewQuery->SetContext(m_snapshot);
    m_taskBrowser->SetSnapshot(m_snapshot);
    ShowTaskView(m_snapshot && m_preferTaskView);
    {
        const QSignalBlocker guard(m_browseScope);
        m_browseScope->clear();
        m_browseScope->addItem(QStringLiteral("整个工程"), NoNode);
        if (m_snapshot)
            for (auto ied : m_snapshot->browse.ieds)
                m_browseScope->addItem(Attribute(*m_snapshot, ied, "name"), ied);
        m_browseScope->setEnabled(bool(m_snapshot));
    }
    m_topologyPath.clear();
    m_topologyStatus->clear();
    UpdateTopologyControls();
    m_export->setEnabled(bool(m_snapshot) && !m_exporting);
    m_treeModel->Reset(m_snapshot);
    for (auto* model : m_models)
        model->Reset(m_snapshot);
    m_tables->setCurrentIndex(0);
    m_inspector->setCurrentIndex(0);
    Preview(nullptr, {}, {});
    if (!m_snapshot)
    {
        m_summary->show();
        m_scope->hide();
        m_scopeActions->hide();
        m_summary->setText(QStringLiteral("尚无可浏览的文档"));
        m_summary->setToolTip({});
        m_breadcrumb->setText(QStringLiteral("打开 SCL 文件以开始分析"));
        m_breadcrumb->setToolTip({});
        m_pathMenu->setEnabled(false);
        m_properties->clear();
        UpdateReferencePath({});
        m_candidates->hide();
        return false;
    }

    const auto& analysis = m_snapshot->analysis;
    const auto& counts = analysis.document->standardCounts;
    int errors = 0, warnings = 0, info = 0;
    for (const auto& finding : analysis.findings)
    {
        switch (finding.severity)
        {
        case Scl::Severity::Error: ++errors; break;
        case Scl::Severity::Warning: ++warnings; break;
        case Scl::Severity::Info: ++info; break;
        }
    }
    QStringList coverage;
    for (const auto& rule : analysis.coverage)
    {
        QString state;
        switch (rule.state)
        {
        case Scl::Coverage::Executed: state = QStringLiteral("已执行"); break;
        case Scl::Coverage::NotApplicable: state = QStringLiteral("不适用"); break;
        case Scl::Coverage::Blocked: state = QStringLiteral("前置失败"); break;
        case Scl::Coverage::NotImplemented: state = QStringLiteral("未执行"); break;
        }
        if (rule.limited > 0)
            state += QStringLiteral("（%1 项受限）").arg(rule.limited);
        coverage << rule.id + QStringLiteral(" ") + state;
    }
    QStringList version;
    for (const auto* key : {"version", "revision", "release"})
    {
        const auto value = Attribute(*m_snapshot, 0, key);
        version << (value.isEmpty() ? QStringLiteral("—") : value);
    }
    m_summary->setText(QStringLiteral("%1\n\n%2 MiB · %3 个元素\nIED %4 · LD %5 · LN / LN0 %6\n"
        "数据集 %7 · 报告定义 %8 · GOOSE %9 · SV %10\n版本线索 %11\n\n"
        "XML 读取：成功\n内部一致性：错误 %12 · 警告 %13 · 提示 %14\n完整 XSD：未执行\n\n规则集 %15\n%16")
        .arg(QFileInfo(m_snapshot->path).fileName())
        .arg(double(analysis.source->bytes().size()) / (1024 * 1024), 0, 'f', 2)
        .arg(analysis.document->nodes.size()).arg(counts.value("IED")).arg(counts.value("LDevice"))
        .arg(counts.value("LN") + counts.value("LN0")).arg(counts.value("DataSet")).arg(analysis.reports.size())
        .arg(counts.value("GSEControl")).arg(counts.value("SampledValueControl"))
        .arg(version.join(QStringLiteral(" / "))).arg(errors).arg(warnings).arg(info)
        .arg(analysis.ruleSetVersion, coverage.join(QStringLiteral(" · "))));
    m_summary->setToolTip(m_snapshot->path.toHtmlEscaped());
    m_tree->expand(m_treeModel->ForNode(0));
    Select(0);
    return true;
}

void SclWorkspace::Select(NodeId id, bool route, int finding, bool openSource, bool keepScope,
    std::optional<Relationship> relationship)
{
    if (!m_snapshot || id < 0 || size_t(id) >= m_snapshot->analysis.document->nodes.size())
        return;
    const bool changed = id != m_selected || finding != m_finding;
    const bool refreshDetails = changed || m_showingFailure;
    const bool newHistory = changed || (openSource && m_inspector->currentIndex() != 2);
    if (newHistory)
        RememberView();
    const auto* previousTable = m_tables->currentWidget();
    const auto previousScope = m_dataSet;
    const bool restoring = m_restoring;
    {
        const QScopedValueRollback<bool> guard(m_restoring, true);
        m_showingFailure = false;
        if (changed && !restoring)
            m_relationship = relationship.value_or(Relationship{});
        const auto owner = m_snapshot->browse.iedOwners[id];
        const bool mmsSelection = m_tableModes->currentIndex() == 1 && m_taskBrowser->IsMmsPage() && !route;
        const bool signalSource = mmsSelection || (openSource && m_tableModes->currentIndex() == 1
            && (m_taskBrowser->IsSignalPage() || m_taskBrowser->IsPublicationPage()));
        if (!restoring && !signalSource && !keepScope && (id == 0 || owner != NoNode))
            SetIedScope(id == 0 ? NoNode : owner);
        m_selected = id;
        m_finding = finding;
        const auto& selectedNode = m_snapshot->analysis.document->nodes[id];
        const auto dataSet = selectedNode.isScl() && selectedNode.name == "DataSet" ? id
            : selectedNode.isScl() && selectedNode.name == "FCDA" ? selectedNode.parent : NoNode;
        SetMemberScope(m_snapshot->browse.dataSetMembers.contains(dataSet) ? dataSet : NoNode);
        const auto index = m_treeModel->ForNode(id);
        if (m_tree->currentIndex() != index)
        {
            m_tree->scrollTo(index);
            m_tree->setCurrentIndex(index);
        }
        if (refreshDetails)
        {
            m_sourceDirty = true;
            if (!mmsSelection) UpdateDetails(); else UpdateOverview();
        }
        else
            UpdateOverview();
        if (openSource)
            m_inspector->setCurrentIndex(2);
        RefreshSourcePreview();
        if (route)
        {
            QWidget* page = m_tables->widget(0);
            const auto& node = m_snapshot->analysis.document->nodes[id];
            if (finding >= 0)
                page = m_findings;
            else if (m_dataSet == id)
                page = m_dataSets;
            else if (node.isScl() && node.name == "FCDA"
                && static_cast<SclTableModel*>(m_members->model())->RowForNode(id) >= 0)
                page = m_members;
            else if (node.name != "LN" && node.name != "LN0")
            {
                for (auto* table : m_routeTables)
                {
                    if (static_cast<SclTableModel*>(table->model())->RowForNode(id) >= 0)
                    {
                        page = table;
                        break;
                    }
                }
            }
            m_tables->setCurrentWidget(m_tables->isTabVisible(m_tables->indexOf(page)) ? page : m_tables->widget(0));
        }
    }
    if (restoring)
        return;
    UpdateTopologyControls();
    if (newHistory)
    {
        m_history.resize(size_t(m_historyIndex + 1));
        m_history.push_back({});
        m_historyIndex = int(m_history.size()) - 1;
    }
    UpdateHistoryButtons();
    RememberView();
    if (m_searchMode->currentIndex() == 1 && (previousTable != m_tables->currentWidget()
        || (m_tables->currentWidget() == m_members && previousScope != m_dataSet)))
        ScheduleSearch();
    LocateTableRow();
}

void SclWorkspace::FollowRelationship(NodeId target, const QString& action)
{
    if (!m_snapshot || target == NoNode || target == m_selected)
        return;
    Select(target, true, -1, false, false, Relationship{m_selected, action});
    m_dataSets->CloseUsers();
    RememberView();
}

void SclWorkspace::ShowRelationshipHelp()
{
    auto* help = new QMessageBox(QMessageBox::Information, QStringLiteral("SCL 关系说明"), {},
        QMessageBox::Ok, this);
    help->setObjectName("SclRelationshipGuide");
    help->setAttribute(Qt::WA_DeleteOnClose);
    help->setTextFormat(Qt::PlainText);
    help->setText(QStringLiteral(
        "包含 · 文件中数据集按声明顺序包含 FCDA 成员。每条成员都是一次出现，同一信号可重复出现。\n\n"
        "引用 · 报告、GOOSE 和 SV 控制块可使用数据集。成员的 DO/DA、FC 和 ix 共同限定引用范围。"
        "模板定义可被不同逻辑节点及对象复用。\n\n"
        "候选 · 存在多个可能的目标或证据不足时，可分别查看候选依据。\n\n"
        "查阅来源 · 跳转时记录从哪里进入。按 Alt+← 返回原位置。用 Tab 切换控件，按 Enter 执行当前操作。\n\n"
        "局部引用拓扑在本地浏览器打开。包含、引用、候选和派生依据各有边类型。"
        "这里展示文件配置，现场运行情况尚未获取。"));
    help->open();
}

void SclWorkspace::UpdateRelationshipSummary()
{
    const bool available = m_snapshot && m_selected != NoNode;
    const bool inDataSet = available && m_dataSet != NoNode;
    m_relationshipParent->setVisible(inDataSet && m_selected != m_dataSet);
    m_relationshipUsers->setVisible(inDataSet && m_selected == m_dataSet);
    QStringList lines;
    if (inDataSet)
    {
        if (m_selected == m_dataSet)
            lines << QStringLiteral("包含 · %1 条 FCDA 声明").arg(m_snapshot->browse.dataSetMembers.value(m_dataSet).count);
        else
            lines << QStringLiteral("所属 · %1").arg(SclTaskPath(*m_snapshot->analysis.document, m_dataSet, true));
    }
    if (available && m_relationship.origin != NoNode)
        lines << QStringLiteral("查阅来源 · %1 · %2").arg(m_relationship.action, Path(*m_snapshot, m_relationship.origin));
    m_relationshipSummary->setText(lines.join('\n'));
    m_relationshipSummary->setVisible(!lines.isEmpty());
}

void SclWorkspace::SelectFinding(int finding, bool openSource, bool route)
{
    const auto id = m_snapshot->analysis.findings[finding].node;
    Select(id == NoNode ? m_selected : id, route, finding, openSource);
}

void SclWorkspace::LocateTableRow()
{
    if (m_tables->currentWidget() == m_dataSets)
    {
        m_dataSets->SyncSelection(m_selected, m_dataSet);
        if (m_dataSet != NoNode)
            m_tabNodes[m_tables->currentIndex()] = m_selected;
        return;
    }
    m_clearNavigationFilter->hide();
    auto* table = qobject_cast<QTableView*>(m_tables->currentWidget());
    if (!m_snapshot || m_selected == NoNode || !table || table == m_results)
        return;
    auto* model = static_cast<SclTableModel*>(table->model());
    const int original = table == m_findings ? m_finding : model->RowForNode(m_selected);
    const int visible = model->VisibleRow(original);
    const QScopedValueRollback<bool> guard(m_restoring, true);
    if (visible < 0)
        table->selectionModel()->clear();
    else
    {
        const auto current = table->currentIndex();
        const auto index = model->index(visible, current.isValid() ? current.column() : 0);
        table->setCurrentIndex(index);
        table->scrollTo(index);
        m_tabNodes[m_tables->currentIndex()] = m_selected;
        if (table == m_findings)
            m_tabFinding = m_finding;
    }
    m_clearNavigationFilter->setVisible(original >= 0 && visible < 0);
    const bool outside = original >= 0 && visible < 0 && !m_snapshot->browse.InScope(m_selected, m_ied);
    m_clearNavigationFilter->setProperty("OutsideIed", outside);
    m_clearNavigationFilter->setText(outside ? QStringLiteral("目标不在当前 IED 范围 · 查看整个工程")
        : QStringLiteral("目标被当前筛选隐藏 · 清除筛选"));
}

void SclWorkspace::SetIedScope(NodeId ied)
{
    if (m_ied == ied)
        return;
    ++m_referenceGeneration;
    const QScopedValueRollback<bool> guard(m_restoring, true);
    if (m_searchMode->currentIndex() == 1)
        ResetSearch();
    m_ied = ied;
    m_dataSets->SetContext(m_snapshot, ied);
    m_overviewQuery->SetContext(m_snapshot, {ied});
    m_taskBrowser->SetScope(ied);
    m_tabNodes.clear();
    m_tabFinding = -1;
    SetMemberScope(NoNode);
    for (auto* table : m_routeTables)
        static_cast<SclTableModel*>(table->model())->SetIed(
            table == m_routeTables.back() && m_allTemplates->isChecked() ? NoNode : ied);
    static_cast<SclTableModel*>(m_findings->model())->SetIed(ied);
    const QSignalBlocker blocked(m_browseScope);
    m_browseScope->setCurrentIndex(m_browseScope->findData(ied));
    UpdateTabVisibility();
    if (m_searchMode->currentIndex() == 1)
        ScheduleSearch();
}

void SclWorkspace::ChangeTable()
{
    if (m_tables->currentWidget() == m_dataSets)
    {
        const auto remembered = m_tabNodes.value(m_tables->currentIndex(), NoNode);
        if (m_dataSet == NoNode && remembered != NoNode)
            Select(remembered, false, -1, false, true);
        LocateTableRow();
        return;
    }
    auto* table = qobject_cast<QTableView*>(m_tables->currentWidget());
    if (!m_snapshot || !table || table == m_results)
        return;
    auto* model = static_cast<SclTableModel*>(table->model());
    const auto remembered = m_tabNodes.value(m_tables->currentIndex(), NoNode);
    const int row = table == m_findings ? m_tabFinding : remembered == NoNode ? -1 : model->RowForNode(remembered);
    if (model->VisibleRow(row) >= 0)
    {
        if (table == m_findings)
            SelectFinding(row, false);
        else
            Select(remembered);
    }
    else if (remembered != NoNode)
    {
        // Filtering hides the previous target: keep its tree and details and
        // show the existing clear-filter affordance rather than choosing a peer.
        if (table == m_findings && row >= 0)
            SelectFinding(row, false);
        else
            Select(remembered);
    }
    else if (model->rowCount() > 0)
    {
        if (table == m_findings)
            SelectFinding(model->OriginalRow(0), false);
        else
            Select(model->index(0, 0).data(Qt::UserRole).toInt());
    }
    LocateTableRow();
}

void SclWorkspace::UpdateTabVisibility()
{
    // Hiding the current tab must not select a row in Qt's fallback tab.
    const QScopedValueRollback<bool> guard(m_restoring, true);
    const int dataSetTab = m_tables->indexOf(m_dataSets);
    if (dataSetTab >= 0)
    {
        bool visible = false;
        if (m_snapshot)
        {
            const auto& browse = m_snapshot->browse;
            const auto scoped = browse.scopedRows.constFind(m_ied);
            visible = m_ied == NoNode ? !browse.dataSetMembers.isEmpty()
                : scoped != browse.scopedRows.cend() && !(*scoped)[size_t(SclView::DataSets)].empty();
        }
        if (!visible && m_tables->currentWidget() == m_dataSets)
            m_tables->setCurrentIndex(0);
        m_tables->setTabVisible(dataSetTab, visible);
    }
    for (int tab = 1; tab < m_tables->count(); ++tab)
    {
        auto* table = qobject_cast<QTableView*>(m_tables->widget(tab));
        if (!table || table == m_results)
            continue;
        // A text filter with no hits keeps its tab so it can be cleared.
        const bool visible = static_cast<SclTableModel*>(table->model())->ContentRowCount() > 0;
        if (!visible && m_tables->currentWidget() == table)
            m_tables->setCurrentIndex(0);
        m_tables->setTabVisible(tab, visible);
    }
}

void SclWorkspace::RememberView()
{
    if (!m_restoring && m_historyIndex >= 0 && !m_taskBrowser->View()->MmsView()->IsNavigating())
    {
        const auto link = m_links->currentIndex();
        const auto candidate = m_candidates->currentIndex();
        m_history[m_historyIndex] = {m_selected, m_dataSet, m_tables->currentIndex(), m_inspector->currentIndex(),
            m_finding, m_ied, m_tableModes->currentIndex() == 1, m_taskBrowser->CurrentLocation(),
            !m_inspectorDock->isClosed(), m_signalEvidence, m_dataSets->CurrentLocation(),
            m_dataSets->CurrentUsers(), m_relationship,
            link.isValid() ? static_cast<SclTableModel*>(m_links->model())->ReferenceIndex(link.row()) : -1,
            candidate.isValid() ? candidate.data(Qt::UserRole).toInt() : NoNode};
    }
}

void SclWorkspace::ShowTaskView(bool show, bool remember)
{
    const bool changed = show != (m_tableModes->currentIndex() == 1);
    if (remember) RememberView();
    {
    const QScopedValueRollback<bool> restoring(m_restoring, true);
    if (remember)
        m_preferTaskView = show;
    if (show != (m_tableModes->currentIndex() == 1))
    {
        if (show)
            m_advancedInspectorVisible = !m_inspectorDock->isClosed();
        m_tableModes->setCurrentIndex(show ? 1 : 0);
        m_inspectorDock->toggleView(show ? m_taskBrowser->IsSignalPage() || m_taskBrowser->IsMmsPage() : m_advancedInspectorVisible);
        if (!show && m_snapshot && m_selected >= 0) {
            const QScopedValueRollback<bool> guard(m_restoring, true);
            Select(m_selected, true, m_finding);
            UpdateDetails();
        }
    }
    m_advancedScope->setVisible(!show);
    m_advancedSearch->setVisible(!show);
    m_navigationKind->setCurrentIndex(show ? 0 : 1);
    m_taskSwitch->setText(show ? QStringLiteral("高级视图") : QStringLiteral("任务视图"));
    RefreshOverviewSummary();
    if (!show && m_snapshot) LocateTableRow();
    if (!show)
        QTimer::singleShot(0, this, &SclWorkspace::BalanceAdvancedPanels);
    if (show && remember && m_taskBrowser->IsMmsPage() && m_snapshot && (m_snapshot->mmsIndex.reports.contains(m_selected)
        || m_snapshot->browse.dataSetMembers.contains(m_selected) || m_snapshot->mmsIndex.memberReferences.contains(m_selected))) {
        auto* mms = m_taskBrowser->View()->MmsView();
        if (!mms->Selected() || mms->Selected()->node != m_selected) mms->OpenNode(m_selected);
    }
    }
    if (remember) { if (changed) PushTaskHistory(); else RememberView(); }
}

void SclWorkspace::OpenMmsAttributeTarget(NodeId node)
{
    if (!m_snapshot || !IsSclMmsAttributeTarget(*m_snapshot, node)) return;
    RememberView();
    const bool detailsVisible = !m_inspectorDock->isClosed();
    {
        const QScopedValueRollback<bool> restoring(m_restoring, true);
        ShowTaskView(true);
        m_taskBrowser->View()->SetPage(SclTaskPages::Mms);
        m_taskBrowser->View()->MmsView()->OpenAttributeTarget(node, detailsVisible);
        const QSignalBlocker block(m_tasks);
        m_tasks->setCurrentItem(m_mmsAttributeTask);
    }
    PushTaskHistory();
}
void SclWorkspace::PushTaskHistory()
{
    if (m_restoring || !m_snapshot) return;
    m_history.resize(size_t(m_historyIndex + 1)); m_history.push_back({});
    m_historyIndex = int(m_history.size()) - 1; RememberView(); UpdateHistoryButtons();
}

void SclWorkspace::BalanceMmsPanels()
{
    if (m_closing || window()->height() >= 650 || !isVisible() || !m_snapshot || m_tableModes->currentIndex() != 1
        || !m_taskBrowser->IsMmsPage() || m_inspectorDock->isClosed()) return;
    auto* view = m_taskBrowser->View()->MmsView();
    const bool source = m_inspector->currentIndex() == 2 && m_source->isVisible();
    if (!view->Table()->isVisible() || (!source && !view->Fields()->isVisible())) return;
    auto* area = m_tableDock->dockAreaWidget();
    if (!area || !m_inspectorDock->dockAreaWidget() || area->parentWidget() != m_inspectorDock->dockAreaWidget()->parentWidget()) return;
    auto* splitter = qobject_cast<QSplitter*>(area->parentWidget());
    if (!splitter || splitter->orientation() != Qt::Vertical || splitter->widget(0) != area) return;
    const auto sizes = m_docks->splitterSizes(area);
    if (sizes.size() != 2) return; // Custom/tabbed/floating layouts retain their own arrangement.
    const int available = sizes[0] + sizes[1];
    const auto scrollbar = [](QAbstractScrollArea* view) { return view->horizontalScrollBar()->sizeHint().height(); };
    // Reserve the scrollbar before its deferred range/layout update and the bottom frame/margins.
    const int row = view->Table()->viewport()->mapTo(area, QPoint()).y() + view->Table()->fontMetrics().height() + scrollbar(view->Table()) + 18;
    const int field = source ? m_source->viewport()->mapTo(m_inspectorDock->dockAreaWidget(), QPoint()).y() + m_source->fontMetrics().height() + scrollbar(m_source) + 18
        : view->Fields()->viewport()->mapTo(m_inspectorDock->dockAreaWidget(), QPoint()).y() + view->Fields()->fontMetrics().height() + scrollbar(view->Fields()) + 18;
    const int target = std::min(std::max(sizes[0], row), std::max(40, available - field));
    if (std::abs(target - sizes[0]) > 1) m_docks->setSplitterSizes(area, {target, available - target});
    if (source) m_source->ensureCursorVisible();
}

void SclWorkspace::BalanceAdvancedPanels()
{
    if (m_closing || !isVisible() || !m_snapshot || window()->height() >= 650
        || m_tableModes->currentIndex() != 0 || m_inspectorDock->isClosed())
        return;
    QAbstractItemView* table = nullptr;
    if (m_tables->currentWidget() == m_members)
        table = m_members;
    else if (m_tables->currentWidget() == m_dataSets)
        table = m_dataSets->Tree();
    if (!table || !table->isVisible())
        return;
    const int rowHeight = table->fontMetrics().height() + 8;
    const int extra = 2 * rowHeight + table->horizontalScrollBar()->sizeHint().height() - table->viewport()->height();
    if (extra <= 0)
        return;
    auto* area = m_tableDock->dockAreaWidget();
    auto* details = m_inspectorDock->dockAreaWidget();
    if (!area || !details || area->parentWidget() != details->parentWidget())
        return;
    auto* splitter = qobject_cast<QSplitter*>(area->parentWidget());
    if (!splitter || splitter->orientation() != Qt::Vertical
        || splitter->count() != 2 || splitter->widget(0) != area || splitter->widget(1) != details)
        return;
    const auto sizes = m_docks->splitterSizes(area);
    const int detailMinimum = m_inspector->mapTo(details, QPoint()).y() + m_inspector->tabBar()->height() + rowHeight;
    const int shift = qMin(extra, sizes[1] - detailMinimum);
    if (shift > 0)
        m_docks->setSplitterSizes(area, {sizes[0] + shift, sizes[1] - shift});
}

void SclWorkspace::UpdateHistoryButtons()
{
    m_back->setEnabled(m_historyIndex > 0);
    m_forward->setEnabled(m_historyIndex + 1 < int(m_history.size()));
    m_taskBrowser->View()->MmsView()->SetHistoryEnabled(m_back->isEnabled(), m_forward->isEnabled());
}

void SclWorkspace::Navigate(int direction)
{
    const int next = m_historyIndex + direction;
    if (next < 0 || next >= int(m_history.size()))
        return;
    RememberView();
    const auto location = m_history[next];
    const auto previousTable = m_tables->currentIndex();
    const auto previousScope = m_dataSet;
    {
        const QScopedValueRollback<bool> restoring(m_restoring, true);
        m_historyIndex = next;
        m_signalEvidence = location.signalEvidence;
        m_relationship = location.relationship;
        SetIedScope(location.ied);
        Select(location.node, false, location.finding, location.taskView
            && location.task.page == SclTaskPages::Signals && !location.task.inputs && location.inspector == 2);
        SetMemberScope(location.dataSet);
        ShowTaskView(location.taskView);
        m_tables->setCurrentIndex(location.table);
        UpdateTabVisibility();
        m_inspectorDock->toggleView(location.inspectorVisible);
        m_taskBrowser->RestoreLocation(location.task);
        m_inspector->setCurrentIndex(location.inspector);
        const auto* relations = static_cast<SclTableModel*>(m_links->model());
        const int reference = relations->RowForReference(location.reference);
        if (reference >= 0)
        {
            m_links->setCurrentIndex(relations->index(reference, 0));
            for (int row = 0; row < m_candidates->model()->rowCount(); ++row)
            {
                const auto candidate = m_candidates->model()->index(row, 0);
                if (candidate.data(Qt::UserRole).toInt() == location.candidate)
                {
                    m_candidates->setCurrentIndex(candidate);
                    break;
                }
            }
        }
        UpdateRelationshipSummary();
        if (m_tables->currentWidget() == m_results)
            m_results->selectionModel()->clear();
        if (m_tables->currentWidget() == m_dataSets && !location.taskView)
        {
            m_searchDelay->stop();
            const auto& filter = location.dataSets.filter;
            const QSignalBlocker text(m_query), ied(m_iedFilter), type(m_typeFilter), fc(m_fcFilter),
                severity(m_severityFilter), exact(m_exact), mode(m_searchMode);
            m_query->setText(filter.text);
            m_iedFilter->setText(filter.iedName);
            m_typeFilter->setText(filter.type);
            m_fcFilter->setText(filter.fc);
            m_severityFilter->setCurrentIndex(filter.severity + 1);
            m_exact->setChecked(filter.exact);
            if (!filter.text.isEmpty() || !filter.iedName.isEmpty() || !filter.type.isEmpty()
                || !filter.fc.isEmpty() || filter.severity >= 0)
                m_searchMode->setCurrentIndex(1);
            m_dataSets->SyncSelection(location.node, location.dataSet);
            m_dataSets->Restore(location.dataSets);
        }
        m_dataSets->RestoreUsers(location.taskView ? SclDataSetView::UsersLocation{} : location.users);
    }
    UpdateHistoryButtons();
    UpdateTopologyControls();
    if (m_tables->currentWidget() != m_dataSets && m_searchMode->currentIndex() == 1 && (previousTable != location.table
        || (m_tables->currentWidget() == m_members && previousScope != m_dataSet)))
        ScheduleSearch();
    LocateTableRow();
}

void SclWorkspace::SetMemberScope(NodeId dataSet)
{
    if (m_dataSet != dataSet)
    {
        if (m_filteredTable == m_members)
            ResetSearch();
        m_tabNodes.remove(m_tables->indexOf(m_members));
    }
    m_dataSet = dataSet;
    static_cast<SclTableModel*>(m_members->model())->SetMembers(m_snapshot, dataSet);
    m_memberPath->setText(dataSet == NoNode ? QString{} : QStringLiteral("%1 · %2 条成员声明")
        .arg(SclTaskPath(*m_snapshot->analysis.document, dataSet, true))
        .arg(m_snapshot->browse.dataSetMembers.value(dataSet).count));
}

void SclWorkspace::OpenAdvancedPage(QWidget* page)
{
    const int tab = m_tables->indexOf(page);
    if (m_tables->currentWidget() == page || !m_tables->isTabVisible(tab))
        return;
    RememberView();
    {
        const QScopedValueRollback<bool> guard(m_restoring, true);
        m_tables->setCurrentWidget(page);
    }
    LocateTableRow();
    if (m_searchMode->currentIndex() == 1)
        ScheduleSearch();
    PushTaskHistory();
}

void SclWorkspace::OpenMemberPage(NodeId dataSet)
{
    if (dataSet == m_dataSet)
        OpenAdvancedPage(m_members);
}

void SclWorkspace::RefreshOverviewSummary()
{
    const bool active = m_snapshot && m_dataSet != NoNode && isVisible()
        && m_tableModes->currentIndex() == 0 && m_tables->currentIndex() == 0;
    m_overviewCounts->setVisible(active);
    if (!active)
    {
        m_overviewQuery->Cancel();
        return;
    }
    const auto previous = m_overviewQuery->result();
    if (previous && previous->request.dataSet == m_dataSet)
        return;
    m_overviewCounts->setText(QStringLiteral("正在统计成员解析状态…"));
    SclHierarchyRequest request;
    request.kind = SclHierarchyKind::Summary;
    request.dataSet = m_dataSet;
    m_overviewQuery->Query(request);
}

void SclWorkspace::UpdateOverview()
{
    const auto& nodes = m_snapshot->analysis.document->nodes;
    const auto id = m_selected;
    m_summary->setVisible(id == 0);
    m_scope->show();
    m_scopeActions->show();
    const auto& browse = m_snapshot->browse;
    const auto structure = browse.structure.value(m_ied);
    QString text = QStringLiteral("浏览范围：%1 · %2\n当前对象：%3\n%4\n直接子节点 %5")
        .arg(m_ied == NoNode ? QStringLiteral("整个工程") : SclTaskPath(*m_snapshot->analysis.document, m_ied, true),
            m_allTemplates->isChecked() || m_ied == NoNode ? QStringLiteral("全部模板") : QStringLiteral("当前 IED 使用的模板"),
            nodes[id].name, id == 0 ? QStringLiteral("SCL 工程") : Path(*m_snapshot, id))
        .arg(browse.branches[id].count);
    const auto description = Attribute(*m_snapshot, id, "desc");
    if (!description.isEmpty())
        text += QStringLiteral("\n声明描述：%1").arg(description);
    m_overviewStructure->setText(QStringLiteral("在结构树查看范围 · %1 个 LD / %2 个 LN")
        .arg(structure.devices).arg(structure.logicalNodes));
    m_overviewStructure->setVisible(structure.devices > 0 || structure.logicalNodes > 0);
    m_overviewUsers->setVisible(m_dataSet != NoNode);
    m_overviewTopology->setVisible(CanShowSclTopology(*m_snapshot, id));
    m_relationTarget = NoNode;
    QString relationship;
    const auto& analysis = m_snapshot->analysis;
    const auto outgoing = analysis.outgoing.equal_range(id);
    for (auto it = outgoing.first; it != outgoing.second; ++it)
    {
        const auto& ref = analysis.references[it.value()];
        if (ref.kind != Scl::LinkKind::ControlDataSet && ref.kind != Scl::LinkKind::Fcda
            && ref.kind != Scl::LinkKind::LogicalNodeType && ref.kind != Scl::LinkKind::MemberType)
            continue;
        relationship = RelationshipName(*m_snapshot, ref, false);
        if (ref.state == Scl::LinkState::Resolved && ref.candidates.size() == 1)
            m_relationTarget = ref.candidates.front();
        text += QStringLiteral("\n%1：%2 · %3").arg(relationship,
            m_relationTarget == NoNode ? ref.declared : Path(*m_snapshot, m_relationTarget, true), LinkState(ref.state));
        break;
    }
    if (m_relationship.origin != NoNode)
        text += QStringLiteral("\n查阅来源 · %1 · %2").arg(m_relationship.action, Path(*m_snapshot, m_relationship.origin));
    m_scope->setText(text);
    m_overviewRelation->setVisible(!relationship.isEmpty());
    m_overviewRelation->setText(m_relationTarget != NoNode ? relationship : QStringLiteral("查看引用依据"));
    const auto scoped = browse.scopedRows.constFind(m_ied);
    const int datasets = m_ied == NoNode ? int(browse.dataSetMembers.size())
        : scoped != browse.scopedRows.cend() ? int((*scoped)[size_t(SclView::DataSets)].size()) : 0;
    m_overviewDataSets->setVisible(datasets > 0);
    m_overviewDataSets->setText(QStringLiteral("数据集 · 当前范围 %1 项").arg(datasets));
    auto end = NodeId(nodes.size());
    for (auto ancestor = id; ancestor != NoNode; ancestor = nodes[ancestor].parent)
    {
        if (nodes[ancestor].nextSibling != NoNode)
        {
            end = nodes[ancestor].nextSibling;
            break;
        }
    }
    for (size_t i = 0; i < m_routeTables.size(); ++i)
    {
        const auto* table = m_routeTables[i];
        const auto* model = static_cast<SclTableModel*>(table->model());
        const int count = id == 0 || id == m_ied ? model->ContentRowCount()
            : model->FirstAtOrAfter(end) - model->FirstAtOrAfter(id + 1);
        m_scopeButtons[i]->setVisible(count > 0);
        m_scopeButtons[i]->setText(QStringLiteral("%1 · %2 项")
            .arg(m_tables->tabText(m_tables->indexOf(table))).arg(count));
    }
    RefreshOverviewSummary();
}

void SclWorkspace::UpdateDetails()
{
    const SclTiming detailsTiming("UpdateDetails");
    const auto id = m_selected;
    const auto& snapshot = *m_snapshot;
    const auto& analysis = snapshot.analysis;
    const auto& node = analysis.document->nodes[id];
    UpdatePath();
    UpdateOverview();

    QString html = QStringLiteral("<h3>%1</h3>").arg(Title(snapshot, id).toHtmlEscaped());
    const auto group = [&](const QString& heading) {
        html += QStringLiteral("<h4>%1</h4><table cellspacing='4'>").arg(heading);
    };
    const auto field = [&](const QString& name, const QString& value) {
        html += QStringLiteral("<tr><td><b>%1</b></td><td>%2</td></tr>")
            .arg(name.toHtmlEscaped(), value.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
    };
    const auto mapped = [](const QString& value) { return value.isEmpty() ? QStringLiteral("无法推导") : value; };
    group(QStringLiteral("位置"));
    field(QStringLiteral("命名空间"), node.namespaceUri);
    field(QStringLiteral("原文行"), QString::number(node.source.line));
    html += QStringLiteral("</table>");

    if (node.isScl() && node.name == "ReportControl")
    {
        const auto report = std::lower_bound(analysis.reports.begin(), analysis.reports.end(), id,
            [](const auto& entry, NodeId needle) { return entry.source < needle; });
        if (report != analysis.reports.end() && report->source == id)
        {
            group(QStringLiteral("报告声明"));
            field(QStringLiteral("报告定义名"), Attribute(snapshot, id, "name"));
            field(QStringLiteral("原始 rptID"), report->rptId.isNull() ? QStringLiteral("未声明")
                : report->rptId.isEmpty() ? QStringLiteral("已声明空值") : report->rptId);
            field(QStringLiteral("声明的数据集"), report->declaredDataSet.isNull() ? QStringLiteral("未声明")
                : report->declaredDataSet.isEmpty() ? QStringLiteral("已声明空值") : report->declaredDataSet);
            html += QStringLiteral("</table>");
            group(QStringLiteral("派生报告映射"));
            field(QStringLiteral("定义引用"), mapped(report->definitionReference));
            field(QStringLiteral("数据集引用"), mapped(report->dataSetReference));
            field(QStringLiteral("MMS domain"), mapped(report->mmsDomain));
            field(QStringLiteral("MMS item"), mapped(report->mmsItem));
            field(QStringLiteral("运行时实际报告标识"), QStringLiteral("需在线确认"));
            html += QStringLiteral("</table>");
        }
    }
    const auto link = analysis.outgoing.constFind(id);
    if (link != analysis.outgoing.cend() && analysis.references[*link].kind == Scl::LinkKind::Fcda)
    {
        const auto& ref = analysis.references[*link];
        group(QStringLiteral("FCDA 派生映射"));
        field(QStringLiteral("解析状态"), LinkState(ref.state));
        field(QStringLiteral("对象引用"), mapped(ref.objectReference));
        field(QStringLiteral("MMS domain"), mapped(ref.mmsDomain));
        field(QStringLiteral("MMS item"), mapped(ref.mmsItem));
        if (ref.arrayIndex >= 0)
        {
            field(QStringLiteral("下标"), QString::number(ref.arrayIndex));
            field(QStringLiteral("后续组件"), ref.mmsComponent);
        }
        html += QStringLiteral("</table>");
        if (ref.instanceNode != NoNode)
            html += QStringLiteral("<p>实例 %1</p>").arg(NodeLink(snapshot, ref.instanceNode, palette()));
        if (ref.instanceDescription != NoNode)
        {
            const auto& value = analysis.document->nodes[ref.instanceDescription];
            html += QStringLiteral("<p>实例描述（dU） <a href='%1'>%2</a></p>").arg(ref.instanceDescription)
                .arg(value.text.toHtmlEscaped().replace('\n', QStringLiteral("<br>")));
        }
    }
    group(QStringLiteral("原始属性"));
    int shown = 0;
    for (; shown < node.attributeCount && shown < 100 && html.size() < 24000; ++shown)
    {
        const auto& attr = analysis.document->attributes[node.firstAttribute + shown];
        const auto key = attr.namespaceUri.isEmpty() ? attr.name : '{' + attr.namespaceUri + '}' + attr.name;
        field(key, attr.value.size() > 2000 ? attr.value.left(2000) + QStringLiteral("…") : attr.value);
    }
    html += QStringLiteral("</table>");
    if (shown < node.attributeCount)
        html += QStringLiteral("<p>属性预览已截断</p>");
    if (!node.text.isEmpty())
        html += QStringLiteral("<h4>原始文本</h4><p>%1</p>").arg(node.text.left(2000).toHtmlEscaped());
    {
        const SclTiming timing("setHtml");
        m_properties->setHtml(html);
    }
    {
        const SclTiming timing("Links Reset");
        auto* model = static_cast<SclTableModel*>(m_links->model());
        model->Reset(m_snapshot, id);
        m_linksLimit->setVisible(model->LinksLimited());
    }
    static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot);
    m_candidates->hide();
    m_candidatesLimit->hide();
    if (m_links->model()->rowCount())
        m_links->setCurrentIndex(m_links->model()->index(0, 0));
    else
        UpdateReferencePath({});

}

void SclWorkspace::UpdatePath()
{
    std::vector<NodeId> nodes;
    for (auto id = m_selected; id != NoNode; id = m_snapshot->analysis.document->nodes[id].parent)
        nodes.push_back(id);
    QStringList links;
    links << NodeLink(*m_snapshot, nodes.back(), palette(), 24);
    if (nodes.size() > 4)
        links << QStringLiteral("…");
    for (auto i = qMin(size_t(3), nodes.size() - 1); i > 0; --i)
        links << NodeLink(*m_snapshot, nodes[i - 1], palette(), 24);
    m_breadcrumb->setText(links.join(QStringLiteral(" › ")));
    m_breadcrumb->setToolTip(Path(*m_snapshot, m_selected).toHtmlEscaped());
    m_pathMenu->setEnabled(true);
}

void SclWorkspace::UpdateReferencePath(const QModelIndex& index)
{
    QString declared;
    QString derived;
    QString html = QStringLiteral("当前节点没有可查看的引用。");
    if (index.isValid() && m_snapshot)
    {
        const auto* model = static_cast<SclTableModel*>(m_links->model());
        const auto& ref = model->ReferenceAt(index.row());
        const auto& snapshot = *m_snapshot;
        const auto& nodes = snapshot.analysis.document->nodes;
        QStringList segments{NodeLink(snapshot, ref.source, palette())};
        declared = ref.declared;
        if (ref.kind == Scl::LinkKind::Fcda)
        {
            QStringList fields;
            for (const auto* key : {"ldInst", "prefix", "lnClass", "lnInst", "doName", "daName", "fc", "ix"})
            {
                const auto value = Attribute(snapshot, ref.source, key);
                if (!value.isNull())
                    fields << QString::fromLatin1(key) + '=' + value;
            }
            declared = fields.join('\n');
            derived = ref.objectReference;
            if (ref.logicalNode != NoNode)
            {
                segments << NodeLink(snapshot, nodes[ref.logicalNode].parent, palette())
                         << NodeLink(snapshot, ref.logicalNode, palette());
            }
            else
            {
                const auto ld = Attribute(snapshot, ref.source, "ldInst");
                const auto ln = Attribute(snapshot, ref.source, "prefix") + Attribute(snapshot, ref.source, "lnClass")
                    + Attribute(snapshot, ref.source, "lnInst");
                if (!ld.isEmpty())
                    segments << QStringLiteral("LD 声明 %1").arg(ld.left(100).toHtmlEscaped());
                if (!ln.isEmpty())
                    segments << QStringLiteral("LN 声明 %1").arg(ln.left(100).toHtmlEscaped());
            }
            for (const auto* key : {"doName", "daName", "fc", "ix"})
            {
                const auto value = Attribute(snapshot, ref.source, key);
                if (!value.isEmpty())
                    segments << QStringLiteral("%1 %2").arg(QString::fromLatin1(key), value.left(100).toHtmlEscaped());
            }
        }
        else if (!ref.declared.isEmpty())
            segments << QStringLiteral("声明 %1").arg(ref.declared.left(160).toHtmlEscaped());
        if (ref.state == Scl::LinkState::Resolved && ref.candidates.size() == 1)
        {
            const auto target = ref.candidates.front();
            const auto owner = snapshot.browse.iedOwners[target];
            const auto sourceOwner = snapshot.browse.iedOwners[ref.source];
            if (owner != NoNode && owner != sourceOwner)
                segments << QStringLiteral("跨 IED：%1").arg(Attribute(snapshot, owner, "name").left(100).toHtmlEscaped());
            segments << NodeLink(snapshot, target, palette());
        }
        if (ref.kind == Scl::LinkKind::ControlDataSet && nodes[ref.source].name == "ReportControl")
        {
            const auto& reports = snapshot.analysis.reports;
            const auto report = std::lower_bound(reports.begin(), reports.end(), ref.source,
                [](const auto& entry, NodeId id) { return entry.source < id; });
            if (report != reports.end() && report->source == ref.source)
                derived = report->dataSetReference;
        }
        html = QStringLiteral("<b>%1 · %2</b><br>%3").arg(
            RelationshipName(snapshot, ref, model->IsIncoming(index.row())),
            LinkState(ref.state), segments.join(QStringLiteral(" → ")));
        if (!ref.detail.isEmpty())
            html += QStringLiteral("<br>%1").arg(ref.detail.left(400).toHtmlEscaped());
        if (ref.state != Scl::LinkState::Resolved && !ref.candidates.isEmpty())
            html += QStringLiteral("<br>候选 %1 项，见下方列表。").arg(ref.candidates.size());
    }
    m_referencePath->setText(html);
    m_copyDeclared->setProperty("sclCopyValue", declared);
    m_copyDerived->setProperty("sclCopyValue", derived);
    m_copyDeclared->setEnabled(!declared.isEmpty());
    m_copyDerived->setEnabled(!derived.isEmpty());
    UpdateRelationshipSummary();
}

void SclWorkspace::RefreshSourcePreview()
{
    const bool mms = m_snapshot && !m_showingFailure && m_tableModes->currentIndex() == 1 && m_taskBrowser->IsMmsPage();
    const bool signal = m_snapshot && !m_showingFailure && m_selected != NoNode
        && m_tableModes->currentIndex() == 1 && m_taskBrowser->IsSignalPage();
    m_structureTitle->setVisible(signal || mms);
    if (mms) {
        const auto label = QStringLiteral("MMS 字段与依据 → ") + m_signalEvidence;
        m_structureTitle->setText(label); m_structureTitle->setToolTip(label.toHtmlEscaped());
        m_structureTitle->setMaximumHeight(window()->height() < 650 ? m_structureTitle->fontMetrics().lineSpacing() + 4 : QWIDGETSIZE_MAX);
        QTimer::singleShot(0, this, &SclWorkspace::BalanceMmsPanels);
    } else m_structureTitle->setMaximumHeight(QWIDGETSIZE_MAX);
    if (signal)
    {
        const auto& node = m_snapshot->analysis.document->nodes[m_selected];
        const auto name = Attribute(*m_snapshot, m_selected, "name");
        m_structureTitle->setText(m_signalEvidence.isEmpty()
            ? QStringLiteral("结构浏览 · %1 %2").arg(node.name, name)
            : QStringLiteral("%1 → %2").arg(m_taskBrowser->SignalPath(), m_signalEvidence));
    }
    if (!m_sourceDirty || m_inspector->currentIndex() != 2 || m_inspector->isHidden())
        return;
    m_sourceDirty = false;
    if (m_showingFailure)
        Preview(m_failure.source.get(), {-1, int(m_failure.diagnostic.line)}, QStringLiteral("失败的加载请求"));
    else if (m_snapshot && m_selected != NoNode)
    {
        const auto& analysis = m_snapshot->analysis;
        const auto location = m_finding >= 0 ? analysis.findings[m_finding].location
            : analysis.document->nodes[m_selected].source;
        Preview(analysis.source.get(), location, QFileInfo(m_snapshot->path).fileName());
    }
    else
        Preview(nullptr, {}, {});
}

void SclWorkspace::Preview(const Scl::SourceIndex* source, Scl::SourceLocation location, const QString& path)
{
    const auto* document = m_snapshot && m_snapshot->analysis.source.get() == source
        ? m_snapshot->analysis.document.get() : nullptr;
    const auto status = m_source->Show(source, location, document);
    m_sourceLabel->setText(status.isEmpty() ? path : path + QStringLiteral(" · ") + status);
    m_sourceLabel->setToolTip(m_sourceLabel->text().toHtmlEscaped());
}
} // namespace iec61850sim
