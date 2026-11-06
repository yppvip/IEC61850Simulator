#include "SclWorkspace.hpp"
#include "SclSourcePreview.hpp"

#include <QAbstractItemModel>
#include <QApplication>
#include <QClipboard>
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
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTextBrowser>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>

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
    case Table::DataSets: return {QStringLiteral("数据集"), QStringLiteral("所属位置"), QStringLiteral("成员数"), QStringLiteral("使用方")};
    case Table::Reports: return {QStringLiteral("报告"), QStringLiteral("所属位置"), QStringLiteral("原始 rptID"),
        QStringLiteral("datSet"), QStringLiteral("BRCB / URCB"), QStringLiteral("定义引用")};
    case Table::Objects: return {QStringLiteral("对象"), QStringLiteral("所属位置"), QStringLiteral("描述"),
        QStringLiteral("lnType / type"), QStringLiteral("bType"), QStringLiteral("FC"), QStringLiteral("count / ix")};
    case Table::Communication: return {QStringLiteral("通信对象"), QStringLiteral("所属位置"), QStringLiteral("IED / AP"),
        QStringLiteral("控制块 / 来源"), QStringLiteral("地址参数")};
    case Table::Templates: return {QStringLiteral("模板"), QStringLiteral("LN 类 / CDC"), QStringLiteral("使用方")};
    case Table::Findings: return {QStringLiteral("级别"), QStringLiteral("规则"), QStringLiteral("行"), QStringLiteral("对象"), QStringLiteral("发现"), QStringLiteral("建议")};
    case Table::Members: return {QStringLiteral("序号"), QStringLiteral("FCDA 声明"), QStringLiteral("FC"), QStringLiteral("状态"), QStringLiteral("对象引用")};
    case Table::Links: return {QStringLiteral("方向"), QStringLiteral("声明"), QStringLiteral("状态"), QStringLiteral("相关对象"), QStringLiteral("说明")};
    case Table::Candidates: return {QStringLiteral("候选目标"), QStringLiteral("所属位置"), QStringLiteral("行")};
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
        m_links.clear();
        m_candidates = std::move(candidates);
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
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        if (parent.isValid() || !m_snapshot)
            return 0;
        if (m_filter)
            return int(m_filter->size());
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
        return Cells(OriginalRow(index.row()), id).value(index.column());
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
        if (row < 0 || !m_filter)
            return row;
        const auto found = std::lower_bound(m_filter->begin(), m_filter->end(), row);
        return found != m_filter->end() && *found == row ? int(found - m_filter->begin()) : -1;
    }

    int OriginalRow(int row) const { return m_filter ? (*m_filter)[row] : row; }

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
        if (m_table == Table::Links)
        {
            const auto link = m_links[row];
            const auto& ref = analysis.references[link.index];
            auto target = id == NoNode ? QString::number(ref.candidates.size()) + QStringLiteral(" 个候选") : Title(snapshot, id);
            if (ref.candidates.isEmpty() && !link.incoming)
                target = QStringLiteral("—");
            return {link.incoming ? QStringLiteral("使用方") : QStringLiteral("引用"), ref.declared,
                    LinkState(ref.state), target, ref.detail};
        }
        if (m_table == Table::Findings)
        {
            const auto& finding = analysis.findings[row];
            return {Severity(finding.severity), finding.rule, QString::number(finding.location.line),
                    finding.objectPath, finding.detail, finding.suggestion};
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
                    mode, report.definitionReference};
        }
        case Table::DataSets:
            return {Title(snapshot, id), Path(snapshot, id, true),
                    QString::number(snapshot.browse.dataSetMembers.value(id).count), QString::number(analysis.incoming.count(id))};
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
                    attr("ldInst") + ' ' + attr("cbName") + ' ' + attr("doName"), address.join(QStringLiteral(" · "))};
        }
        case Table::Templates:
            return {Title(snapshot, id), attr("lnClass") + attr("cdc"), QString::number(analysis.incoming.count(id))};
        case Table::Members: {
            const auto found = analysis.outgoing.constFind(id);
            if (found == analysis.outgoing.cend())
                return {{}, Title(snapshot, id)};
            const auto& ref = analysis.references[*found];
            return {QString::number(ref.memberOrdinal), ref.declared, attr("fc"), LinkState(ref.state), ref.objectReference};
        }
        case Table::Candidates:
            return {Title(snapshot, id), Path(snapshot, id, true), QString::number(node.source.line)};
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
    setStyleSheet(QStringLiteral(
        "#SclWorkspace QTreeView, #SclWorkspace QTableView {"
        " background: palette(base); alternate-background-color: palette(alternate-base);"
        " selection-background-color: #2355a4; selection-color: white;"
        " border: 1px solid palette(mid); }"
        "#SclWorkspace QTreeView::item:selected, #SclWorkspace QTableView::item:selected { color: white; }"
        "#SclWorkspace QTreeView:focus, #SclWorkspace QTableView:focus { border-color: palette(highlight); }"
        "#SclWorkspace QHeaderView::section { background: palette(window); color: palette(window-text);"
        " border: 0; border-bottom: 1px solid palette(mid); padding: 5px 7px; }"
        "#SclWorkspace QTreeView::item { padding-top: 3px; padding-bottom: 3px; }"
        "#SclWorkspace QLabel#SclTableEmpty { background: palette(alternate-base); padding: 8px; }"));
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
    m_clearNavigationFilter = new QPushButton(QStringLiteral("目标被当前筛选隐藏 · 清除筛选"), this);
    m_clearNavigationFilter->setObjectName("SclClearNavigationFilter");
    m_clearNavigationFilter->hide();
    outer->addWidget(m_clearNavigationFilter, 0, Qt::AlignLeft);
    connect(m_clearNavigationFilter, &QPushButton::clicked, this, [this] { ResetSearch(true); });

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    outer->addWidget(splitter, 1);
    m_tree = new QTreeView(splitter);
    m_tree->setObjectName("SclTree");
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setAccessibleName(QStringLiteral("SCL 对象树"));
    m_treeModel = new SclTreeModel(m_tree);
    m_tree->setModel(m_treeModel);
    auto* tablePane = new QWidget(splitter);
    auto* tableLayout = new QVBoxLayout(tablePane);
    tableLayout->setContentsMargins(0, 0, 0, 0);
    m_tables = new QTabWidget(tablePane);
    tableLayout->addWidget(m_tables, 1);
    auto* empty = new QLabel(tablePane);
    empty->setObjectName("SclTableEmpty");
    empty->setWordWrap(true);
    tableLayout->addWidget(empty);
    empty->hide();
    m_tables->setObjectName("SclTables");
    m_tables->setUsesScrollButtons(true);
    m_inspector = new QTabWidget(splitter);
    m_inspector->setObjectName("SclInspector");
    splitter->setSizes({253, 517, 380});
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setCollapsible(2, false);
    auto* details = new QToolButton(this);
    details->setObjectName("SclToggleDetails");
    details->setCheckable(true);
    details->setChecked(true);
    details->setText(QStringLiteral("详情"));
    details->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    details->setIcon(style()->standardIcon(QStyle::SP_ArrowRight));
    details->setToolTip(QStringLiteral("显示或收起详情"));
    navigation->addWidget(details);
    connect(details, &QToolButton::toggled, m_inspector, [this, details](bool visible) {
        m_inspector->setVisible(visible);
        details->setIcon(style()->standardIcon(visible ? QStyle::SP_ArrowRight : QStyle::SP_ArrowLeft));
    });

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
            const auto id = model->UnfilteredNode(model->FirstAtOrAfter(m_selected + 1));
            Select(id);
            m_tables->setCurrentWidget(view);
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
        empty->setVisible(table && table->model()->rowCount() == 0);
        if (table)
            empty->setText(!m_snapshot ? QStringLiteral("打开 SCL 文件后查看数据。")
                : table == m_results ? QStringLiteral("此处显示匹配结果，请查看搜索状态。")
                : QStringLiteral("此视图没有可显示的行，请检查当前对象或筛选条件。"));
    };
    connect(m_tables, &QTabWidget::currentChanged, this, updateEmpty);
    for (int i = 0; i < m_tables->count(); ++i)
    {
        if (const auto* table = qobject_cast<QTableView*>(m_tables->widget(i)))
        {
            connect(table->model(), &QAbstractItemModel::modelReset, this, updateEmpty);
            connect(table->model(), &QAbstractItemModel::rowsInserted, this, updateEmpty);
        }
    }

    m_properties = new QTextBrowser(m_inspector);
    m_properties->setObjectName("SclProperties");
    m_properties->setReadOnly(true);
    m_inspector->addTab(m_properties, QStringLiteral("属性"));

    auto* relations = new QWidget(m_inspector);
    auto* relationsLayout = new QVBoxLayout(relations);
    relationsLayout->setContentsMargins(4, 4, 4, 4);
    relationsLayout->addWidget(new QLabel(QStringLiteral("双击引用、使用方或候选目标即可跳转"), relations));
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
            LocateTableRow();
        RememberView();
    });
    connect(m_inspector, &QTabWidget::currentChanged, this, [this] { RememberView(); });
    connect(m_failedSource, &QPushButton::clicked, this, [this] {
        m_showingFailure = true;
        Preview(m_failure.source.get(), {-1, int(m_failure.diagnostic.line)}, QStringLiteral("失败的加载请求"));
        m_inspector->setCurrentIndex(2);
    });
}

bool SclWorkspace::UpdateSession(Snapshot snapshot, const Scl::Result& failure)
{
    if (m_showingFailure && m_failure.source != failure.source)
    {
        m_showingFailure = false;
        if (m_snapshot && m_selected != NoNode)
        {
            const auto& analysis = m_snapshot->analysis;
            const auto location = m_finding >= 0 ? analysis.findings[m_finding].location
                : analysis.document->nodes[m_selected].source;
            Preview(analysis.source.get(), location, QFileInfo(m_snapshot->path).fileName());
        }
        else
            Preview(nullptr, {}, {});
    }
    m_failure = failure;
    m_failedSource->setVisible(bool(failure.source));
    if (m_snapshot == snapshot)
        return false;

    m_selected = NoNode;
    m_finding = -1;
    m_clearNavigationFilter->hide();
    m_showingFailure = false;
    m_history.clear();
    m_historyIndex = -1;
    m_dataSet = NoNode;
    UpdateHistoryButtons();
    ResetSearch(true);
    m_snapshot = std::move(snapshot);
    m_topologyPath.clear();
    m_topologyStatus->clear();
    UpdateTopologyControls();
    m_export->setEnabled(bool(m_snapshot) && !m_exporting);
    m_treeModel->Reset(m_snapshot);
    for (auto* model : m_models)
        model->Reset(m_snapshot);
    m_tables->setCurrentIndex(0);
    m_inspector->setCurrentIndex(0);
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
        Preview(nullptr, {}, {});
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
    const auto* previousTable = m_tables->currentWidget();
    const auto previousScope = m_dataSet;
    const bool restoring = m_restoring;
    {
        const QScopedValueRollback<bool> guard(m_restoring, true);
        m_showingFailure = false;
        m_selected = id;
        m_finding = finding;
        const auto index = m_treeModel->ForNode(id);
        if (m_tree->currentIndex() != index)
        {
            m_tree->scrollTo(index);
            m_tree->setCurrentIndex(index);
        }
        if (refreshDetails)
            UpdateDetails();
        if (finding >= 0 && (refreshDetails || openSource))
            Preview(m_snapshot->analysis.source.get(), m_snapshot->analysis.findings[finding].location,
                    QFileInfo(m_snapshot->path).fileName());
        if (openSource)
            m_inspector->setCurrentIndex(2);
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
            m_tables->setCurrentWidget(page);
        }
    }
    if (restoring)
        return;
    UpdateTopologyControls();
    if (changed)
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
    }
    m_clearNavigationFilter->setVisible(original >= 0 && visible < 0);
}

void SclWorkspace::RememberView()
{
    if (!m_restoring && m_historyIndex >= 0)
        m_history[m_historyIndex] = {m_selected, m_dataSet, m_tables->currentIndex(), m_inspector->currentIndex(), m_finding};
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
        Select(location.node, false, location.finding);
        SetMemberScope(location.dataSet);
        m_tables->setCurrentIndex(location.table);
        m_inspector->setCurrentIndex(location.inspector);
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
    m_scope->setText(QStringLiteral("当前节点\n%1\n直接子节点：%2")
        .arg(Path(*m_snapshot, id)).arg(m_snapshot->browse.branches[id].count));
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
        const int count = model->FirstAtOrAfter(end) - model->FirstAtOrAfter(id + 1);
        m_scopeButtons[i]->setVisible(count > 0);
        m_scopeButtons[i]->setText(QStringLiteral("%1 · %2 项 · 定位首项")
            .arg(m_tables->tabText(m_tables->indexOf(table))).arg(count));
    }
}

void SclWorkspace::UpdateDetails()
{
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
    m_properties->setHtml(html);
    static_cast<SclTableModel*>(m_links->model())->Reset(m_snapshot, id);
    static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot);
    m_candidates->hide();
    if (m_links->model()->rowCount())
        m_links->setCurrentIndex(m_links->model()->index(0, 0));
    else
        UpdateReferencePath({});

    const auto dataSet = node.name == "DataSet" ? id : node.name == "FCDA" ? node.parent : NoNode;
    if (m_snapshot->browse.dataSetMembers.contains(dataSet))
        SetMemberScope(dataSet);
    Preview(analysis.source.get(), node.source, QFileInfo(snapshot.path).fileName());
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
            segments << NodeLink(snapshot, ref.candidates.front(), palette());
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

void SclWorkspace::Preview(const Scl::SourceIndex* source, Scl::SourceLocation location, const QString& path)
{
    const auto* document = m_snapshot && m_snapshot->analysis.source.get() == source
        ? m_snapshot->analysis.document.get() : nullptr;
    const auto status = m_source->Show(source, location, document);
    m_sourceLabel->setText(status.isEmpty() ? path : path + QStringLiteral(" · ") + status);
    m_sourceLabel->setToolTip(m_sourceLabel->text().toHtmlEscaped());
}
} // namespace iec61850sim
