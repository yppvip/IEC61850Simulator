#include "SclWorkspace.hpp"
#include "SclTiming.hpp"
#include <QStackedWidget>
#include "SclSourcePreview.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QCryptographicHash>
#include <QComboBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSettings>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <DockManager.h>
#include <DockWidget.h>
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

enum class Table { DataSets, Reports, Objects, Communication, Templates, Findings, Members, Links, Candidates };

QStringList Headers(Table table)
{
    switch (table)
    {
    case Table::DataSets: return {QStringLiteral("数据集"), QStringLiteral("所属位置"), QStringLiteral("成员数"), QStringLiteral("使用方"), QStringLiteral("描述")};
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
        m_candidates = std::move(candidates);
        m_displayRow = -1;
        m_displayCells.clear();
        if (m_snapshot && context != NoNode && m_table == Table::Links)
        {
            const auto& analysis = m_snapshot->analysis;
            for (int index : analysis.outgoing.values(context))
                m_links.push_back({index, false});
            for (int index : analysis.incoming.values(context))
                m_links.push_back({index, true});
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
                case Table::DataSets: view = int(SclView::DataSets); break;
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
            const auto targets = Targets(row);
            return targets.size() == 1 ? targets.front() : NoNode;
        }
        default: return UnfilteredNode(row);
        }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::UserRole))
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

    const std::vector<NodeId>& Category() const
    {
        switch (m_table)
        {
        case Table::DataSets: return m_snapshot->browse.categories[size_t(SclCategory::DataSets)];
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
        case Table::DataSets:
            return {Title(snapshot, id), Path(snapshot, id, true),
                    QString::number(snapshot.browse.dataSetMembers.value(id).count), QString::number(analysis.incoming.count(id)),
                    description(id)};
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
    m_taskBrowser = new SclTaskBrowser(m_tableModes);
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
    m_treeDock = dock(QStringLiteral("工程导航"), "SclTreeDock", m_tree);
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
    m_scope->hide();
    m_scopeActions->hide();
    overviewLayout->addStretch();
    m_tables->addTab(overview, QStringLiteral("概览"));

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
                    Select(index.data(Qt::UserRole).toInt());
            }
        });
        return view;
    };
    m_routeTables.push_back(addTable(Table::Reports, QStringLiteral("报告"), "SclReports"));
    m_routeTables.push_back(addTable(Table::DataSets, QStringLiteral("数据集"), "SclDataSets"));
    m_members = addTable(Table::Members, QStringLiteral("成员"), "SclMembers");
    m_routeTables.push_back(addTable(Table::Objects, QStringLiteral("对象"), "SclObjects"));
    m_routeTables.push_back(addTable(Table::Communication, QStringLiteral("通信"), "SclCommunication"));
    m_routeTables.push_back(addTable(Table::Templates, QStringLiteral("模板"), "SclTemplates"));
    for (auto* view : m_routeTables)
    {
        auto* button = new QPushButton(m_scopeActions);
        scopeLayout->addWidget(button, 0, Qt::AlignLeft);
        m_scopeButtons.push_back(button);
        connect(button, &QPushButton::clicked, this, [this, view] {
            auto* model = static_cast<SclTableModel*>(view->model());
            const auto id = m_selected == m_ied && model->rowCount() > 0
                ? model->index(0, 0).data(Qt::UserRole).toInt()
                : model->UnfilteredNode(model->FirstAtOrAfter(m_selected + 1));
            Select(id, true);
            const QScopedValueRollback<bool> guard(m_restoring, true);
            m_tables->setCurrentWidget(m_tables->isTabVisible(m_tables->indexOf(view)) ? view : m_tables->widget(0));
            LocateTableRow();
        });
    }
    m_findings = addTable(Table::Findings, QStringLiteral("问题"), "SclFindings");
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
    relationsLayout->addWidget(new QLabel(QStringLiteral("双击目标跳转；跨 IED 目标显示所属设备，跳转后可返回。"), relations));
    m_referencePath = new QLabel(relations);
    m_referencePath->setObjectName("SclReferencePath");
    m_referencePath->setTextFormat(Qt::RichText);
    m_referencePath->setWordWrap(true);
    m_referencePath->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_referencePath->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    connect(m_referencePath, &QLabel::linkActivated, this, jumpLink);
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
    referenceActions->addStretch();
    relationsLayout->addLayout(referenceActions);
    CreateTopologyControls(relationsLayout);
    for (auto* table : m_routeTables)
        AddTopologyAction(table);
    AddTopologyAction(m_members);
    AddTopologyAction(m_tree);
    m_links = createTable(Table::Links, relations, "SclLinks");
    m_candidates = createTable(Table::Candidates, relations, "SclCandidates");
    relationsLayout->addWidget(m_links, 3);
    relationsLayout->addWidget(m_candidates, 2);
    m_candidates->hide();
    m_inspector->addTab(relations, QStringLiteral("引用与使用方"));

    auto* sourcePage = new QWidget(m_inspector);
    auto* sourceLayout = new QVBoxLayout(sourcePage);
    m_sourceLabel = new QLabel(sourcePage);
    m_sourceLabel->setObjectName("SclSourceLabel");
    m_sourceLabel->setTextFormat(Qt::PlainText);
    m_sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_source = new SclSourcePreview(sourcePage);
    sourceLayout->addWidget(m_sourceLabel);
    sourceLayout->addWidget(m_source);
    m_inspector->addTab(sourcePage, QStringLiteral("原文"));

    const auto selectTree = [this](const QModelIndex& index) {
        if (!m_restoring)
            Select(SclTreeModel::Node(index), true);
    };
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, selectTree);
    connect(m_tree, &QTreeView::clicked, this, selectTree);
    connect(m_links->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        auto* model = static_cast<SclTableModel*>(m_links->model());
        auto targets = index.isValid() ? model->Targets(index.row()) : QList<NodeId>{};
        const bool unresolved = index.isValid() && !model->IsIncoming(index.row())
            && model->ReferenceAt(index.row()).state != Scl::LinkState::Resolved;
        m_candidates->setVisible(targets.size() > 1 || (unresolved && !targets.isEmpty()));
        static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot, NoNode, std::move(targets));
        UpdateReferencePath(index);
    });
    const auto jump = [this](const QModelIndex& index) {
        const auto id = index.data(Qt::UserRole).toInt();
        if (!index.isValid() || id == NoNode)
            return;
        Select(id, true);
    };
    connect(m_links, &QTableView::activated, this, jump);
    connect(m_candidates, &QTableView::activated, this, jump);
    connect(m_back, &QPushButton::clicked, this, [this] { Navigate(-1); });
    connect(m_forward, &QPushButton::clicked, this, [this] { Navigate(1); });
    auto* backShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Left), this);
    auto* forwardShortcut = new QShortcut(QKeySequence(Qt::ALT | Qt::Key_Right), this);
    backShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    forwardShortcut->setContext(Qt::WidgetWithChildrenShortcut);
    connect(backShortcut, &QShortcut::activated, m_back, &QPushButton::click);
    connect(forwardShortcut, &QShortcut::activated, m_forward, &QPushButton::click);
    connect(m_tables, &QTabWidget::currentChanged, this, [this] {
        if (!m_restoring)
            ChangeTable();
        RememberView();
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
    m_docks->hideManagerAndFloatingWidgets();
    QWidget::hideEvent(event);
}

void SclWorkspace::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_docks->show();
    RefreshSourcePreview();
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
    m_historyIndex = -1;
    m_dataSet = NoNode;
    m_ied = NoNode;
    m_tabNodes.clear();
    m_tabFinding = -1;
    UpdateHistoryButtons();
    ResetSearch(true);
    m_snapshot = std::move(snapshot);
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

void SclWorkspace::Select(NodeId id, bool route, int finding, bool openSource)
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
        const auto owner = m_snapshot->browse.iedOwners[id];
        if (id == 0 || owner != NoNode)
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
            UpdateDetails();
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

void SclWorkspace::SelectFinding(int finding, bool openSource, bool route)
{
    const auto id = m_snapshot->analysis.findings[finding].node;
    Select(id == NoNode ? m_selected : id, route, finding, openSource);
}

void SclWorkspace::LocateTableRow()
{
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
    const QScopedValueRollback<bool> guard(m_restoring, true);
    if (m_searchMode->currentIndex() == 1)
        ResetSearch();
    m_ied = ied;
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
    if (m_searchMode->currentIndex() == 1)
        ScheduleSearch();
}

void SclWorkspace::ChangeTable()
{
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
    if (!m_restoring && m_historyIndex >= 0)
        m_history[m_historyIndex] = {m_selected, m_dataSet, m_tables->currentIndex(), m_inspector->currentIndex(),
            m_finding, m_ied, m_tableModes->currentIndex() == 1, m_taskBrowser->CurrentLocation(),
            !m_inspectorDock->isClosed()};
}

void SclWorkspace::ShowTaskView(bool show, bool remember)
{
    if (remember)
        m_preferTaskView = show;
    if (show != (m_tableModes->currentIndex() == 1))
    {
        if (show)
            m_advancedInspectorVisible = !m_inspectorDock->isClosed();
        m_tableModes->setCurrentIndex(show ? 1 : 0);
        m_inspectorDock->toggleView(!show && m_advancedInspectorVisible);
    }
    m_advancedScope->setVisible(!show);
    m_taskSwitch->setText(show ? QStringLiteral("高级视图") : QStringLiteral("任务视图"));
    if (remember)
        RememberView();
}

void SclWorkspace::UpdateHistoryButtons()
{
    m_back->setEnabled(m_historyIndex > 0);
    m_forward->setEnabled(m_historyIndex + 1 < int(m_history.size()));
}

void SclWorkspace::Navigate(int direction)
{
    const int next = m_historyIndex + direction;
    if (next < 0 || next >= int(m_history.size()))
        return;
    const auto location = m_history[next];
    const auto previousTable = m_tables->currentIndex();
    const auto previousScope = m_dataSet;
    {
        const QScopedValueRollback<bool> restoring(m_restoring, true);
        m_historyIndex = next;
        SetIedScope(location.ied);
        Select(location.node, false, location.finding);
        SetMemberScope(location.dataSet);
        m_tables->setCurrentIndex(location.table);
        UpdateTabVisibility();
        m_inspector->setCurrentIndex(location.inspector);
        ShowTaskView(location.taskView);
        m_inspectorDock->toggleView(location.inspectorVisible);
        m_taskBrowser->RestoreLocation(location.task);
        if (m_tables->currentWidget() == m_results)
            m_results->selectionModel()->clear();
    }
    UpdateHistoryButtons();
    UpdateTopologyControls();
    if (m_searchMode->currentIndex() == 1 && (previousTable != location.table
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
}

void SclWorkspace::UpdateOverview()
{
    const auto& nodes = m_snapshot->analysis.document->nodes;
    const auto id = m_selected;
    m_summary->setVisible(id == 0);
    m_scope->setVisible(id != 0);
    m_scopeActions->setVisible(id != 0);
    if (id == 0)
        return;
    m_scope->setText(QStringLiteral("浏览范围：%1\n当前节点\n%2\n直接子节点：%3")
        .arg(m_ied == NoNode ? QStringLiteral("整个工程") : Attribute(*m_snapshot, m_ied, "name"),
            Path(*m_snapshot, id)).arg(m_snapshot->browse.branches[id].count));
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
        const int count = id == m_ied ? model->rowCount()
            : model->FirstAtOrAfter(end) - model->FirstAtOrAfter(id + 1);
        m_scopeButtons[i]->setVisible(count > 0);
        m_scopeButtons[i]->setText(QStringLiteral("%1 · %2 项 · 定位首项")
            .arg(m_tables->tabText(m_tables->indexOf(table))).arg(count));
    }
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
        static_cast<SclTableModel*>(m_links->model())->Reset(m_snapshot, id);
    }
    static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot);
    m_candidates->hide();
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
            model->IsIncoming(index.row()) ? QStringLiteral("使用方") : QStringLiteral("引用"),
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
}

void SclWorkspace::RefreshSourcePreview()
{
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
