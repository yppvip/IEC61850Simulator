#include "SclWorkspace.hpp"

#include <QAbstractItemModel>
#include <QComboBox>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QTextBlock>
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

    NodeId Node(int row) const
    {
        row = m_filter ? (*m_filter)[row] : row;
        switch (m_table)
        {
        case Table::Reports: return m_snapshot->analysis.reports[row].source;
        case Table::Findings: return m_snapshot->analysis.findings[row].node;
        case Table::Candidates: return m_candidates[row];
        case Table::Members:
            return m_snapshot->browse.memberRows[m_snapshot->browse.dataSetMembers.value(m_context).first + row];
        case Table::Links:
        {
            const auto targets = Targets(row);
            return targets.size() == 1 ? targets.front() : NoNode;
        }
        default: return Category()[row];
        }
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole && role != Qt::UserRole))
            return {};
        const auto id = Node(index.row());
        if (role == Qt::UserRole)
            return id;
        return Cells(m_filter ? (*m_filter)[index.row()] : index.row(), id).value(index.column());
    }

    void Filter(std::optional<std::vector<int>> rows)
    {
        if (!rows && !m_filter)
            return;
        beginResetModel();
        m_filter = std::move(rows);
        endResetModel();
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
        static_cast<SclTableModel*>(table->model())->Filter(std::move(rows));
}

SclWorkspace::SclWorkspace(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclWorkspace");
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
    m_breadcrumb->setTextFormat(Qt::PlainText);
    m_breadcrumb->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_failedSource = new QPushButton(QStringLiteral("查看失败文件原文"), this);
    m_failedSource->hide();
    navigation->addWidget(m_back);
    navigation->addWidget(m_forward);
    navigation->addWidget(m_breadcrumb, 1);
    navigation->addWidget(m_failedSource);
    outer->addLayout(navigation);
    CreateSearchBar(outer);

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    outer->addWidget(splitter, 1);
    m_tree = new QTreeView(splitter);
    m_tree->setObjectName("SclTree");
    m_tree->setHeaderHidden(true);
    m_tree->setUniformRowHeights(true);
    m_treeModel = new SclTreeModel(m_tree);
    m_tree->setModel(m_treeModel);
    m_tables = new QTabWidget(splitter);
    m_tables->setObjectName("SclTables");
    m_tables->setUsesScrollButtons(true);
    m_inspector = new QTabWidget(splitter);
    m_inspector->setObjectName("SclInspector");
    splitter->setSizes({210, 520, 420});
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    splitter->setCollapsible(2, false);

    auto* overview = new QWidget(m_tables);
    auto* overviewLayout = new QVBoxLayout(overview);
    m_summary = new QLabel(QStringLiteral("尚无可浏览的文档"), overview);
    m_summary->setObjectName("SclOverview");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setWordWrap(true);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    overviewLayout->addWidget(m_summary);
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
        view->setWordWrap(false);
        view->verticalHeader()->hide();
        view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
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
        m_tables->addTab(view, label);
        connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
            if (index.isValid())
                Select(index.data(Qt::UserRole).toInt());
        });
        return view;
    };
    addTable(Table::Reports, QStringLiteral("报告"), "SclReports");
    addTable(Table::DataSets, QStringLiteral("数据集"), "SclDataSets");
    m_members = addTable(Table::Members, QStringLiteral("成员"), "SclMembers");
    addTable(Table::Objects, QStringLiteral("对象"), "SclObjects");
    addTable(Table::Communication, QStringLiteral("通信"), "SclCommunication");
    addTable(Table::Templates, QStringLiteral("模板"), "SclTemplates");
    auto* findings = addTable(Table::Findings, QStringLiteral("问题"), "SclFindings");
    connect(findings, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (index.isValid() && index.data(Qt::UserRole).toInt() != NoNode)
        {
            Select(index.data(Qt::UserRole).toInt());
            m_inspector->setCurrentIndex(2);
        }
    });
    AttachSearchResults();

    m_properties = new QPlainTextEdit(m_inspector);
    m_properties->setObjectName("SclProperties");
    m_properties->setReadOnly(true);
    m_inspector->addTab(m_properties, QStringLiteral("属性"));

    auto* relations = new QWidget(m_inspector);
    auto* relationsLayout = new QVBoxLayout(relations);
    relationsLayout->setContentsMargins(4, 4, 4, 4);
    relationsLayout->addWidget(new QLabel(QStringLiteral("双击引用、使用方或候选目标即可跳转"), relations));
    m_links = createTable(Table::Links, relations, "SclLinks");
    m_candidates = createTable(Table::Candidates, relations, "SclCandidates");
    relationsLayout->addWidget(m_links, 3);
    relationsLayout->addWidget(m_candidates, 2);
    m_candidates->hide();
    m_inspector->addTab(relations, QStringLiteral("引用与使用方"));

    auto* sourcePage = new QWidget(m_inspector);
    auto* sourceLayout = new QVBoxLayout(sourcePage);
    m_sourceLabel = new QLabel(sourcePage);
    m_sourceLabel->setTextFormat(Qt::PlainText);
    m_sourceLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_source = new QPlainTextEdit(sourcePage);
    m_source->setObjectName("SclSource");
    m_source->setReadOnly(true);
    m_source->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_source->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    sourceLayout->addWidget(m_sourceLabel);
    sourceLayout->addWidget(m_source);
    m_inspector->addTab(sourcePage, QStringLiteral("原文"));

    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        Select(SclTreeModel::Node(index));
    });
    connect(m_links->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        auto* model = static_cast<SclTableModel*>(m_links->model());
        auto targets = index.isValid() ? model->Targets(index.row()) : QList<NodeId>{};
        m_candidates->setVisible(targets.size() > 1);
        static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot, NoNode, std::move(targets));
    });
    const auto jump = [this](const QModelIndex& index) {
        const auto id = index.data(Qt::UserRole).toInt();
        if (!index.isValid() || id == NoNode)
            return;
        Select(id);
        if (m_snapshot->analysis.document->nodes[id].name == "DataSet")
            m_tables->setCurrentWidget(m_members);
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
    connect(m_tables, &QTabWidget::currentChanged, this, [this] { RememberView(); });
    connect(m_inspector, &QTabWidget::currentChanged, this, [this] { RememberView(); });
    connect(m_failedSource, &QPushButton::clicked, this, [this] {
        m_showingFailure = true;
        Preview(m_failure.source.get(), int(m_failure.diagnostic.line), QStringLiteral("失败的加载请求"));
        m_inspector->setCurrentIndex(2);
    });
}

bool SclWorkspace::UpdateSession(Snapshot snapshot, const Scl::Result& failure)
{
    if (m_showingFailure && m_failure.source != failure.source)
    {
        m_showingFailure = false;
        if (m_snapshot && m_selected != NoNode)
            Preview(m_snapshot->analysis.source.get(), m_snapshot->analysis.document->nodes[m_selected].source.line,
                    QFileInfo(m_snapshot->path).fileName());
        else
            Preview(nullptr, 0, {});
    }
    m_failure = failure;
    m_failedSource->setVisible(bool(failure.source));
    if (m_snapshot == snapshot)
        return false;

    m_selected = NoNode;
    m_showingFailure = false;
    m_history.clear();
    m_historyIndex = -1;
    m_dataSet = NoNode;
    UpdateHistoryButtons();
    ResetSearch(true);
    m_snapshot = std::move(snapshot);
    m_export->setEnabled(bool(m_snapshot) && !m_exporting);
    m_treeModel->Reset(m_snapshot);
    for (auto* model : m_models)
        model->Reset(m_snapshot);
    m_tables->setCurrentIndex(0);
    m_inspector->setCurrentIndex(0);
    if (!m_snapshot)
    {
        m_summary->setText(QStringLiteral("尚无可浏览的文档"));
        m_summary->setToolTip({});
        m_breadcrumb->setText(QStringLiteral("打开 SCL 文件以开始分析"));
        m_breadcrumb->setToolTip({});
        m_properties->clear();
        Preview(nullptr, 0, {});
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
        "错误 %12 · 警告 %13 · 提示 %14\n规则集 %15\n%16")
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

void SclWorkspace::Select(NodeId id, bool remember)
{
    if (!m_snapshot || id == NoNode || (id == m_selected && !m_showingFailure))
        return;
    if (remember && id != m_selected)
    {
        m_history.resize(size_t(m_historyIndex + 1));
        m_history.push_back({id, m_dataSet, m_tables->currentIndex(), m_inspector->currentIndex()});
        m_historyIndex = int(m_history.size()) - 1;
    }
    m_showingFailure = false;
    m_selected = id;
    UpdateHistoryButtons();

    const auto index = m_treeModel->ForNode(id);
    if (m_tree->currentIndex() != index)
    {
        const QSignalBlocker blocked(m_tree->selectionModel());
        m_tree->scrollTo(index);
        m_tree->setCurrentIndex(index);
    }
    UpdateDetails();
    RememberView();
}

void SclWorkspace::RememberView()
{
    if (!m_restoring && m_historyIndex >= 0)
    {
        const auto* table = qobject_cast<QTableView*>(m_tables->currentWidget());
        m_history[m_historyIndex] = {m_selected, m_dataSet, m_tables->currentIndex(), m_inspector->currentIndex(),
            table ? QPersistentModelIndex(table->currentIndex()) : QPersistentModelIndex{}};
    }
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
    const QScopedValueRollback<bool> restoring(m_restoring, true);
    m_historyIndex = next;
    const auto location = m_history[next];
    Select(location.node, false);
    m_dataSet = location.dataSet;
    static_cast<SclTableModel*>(m_members->model())->SetMembers(m_snapshot, m_dataSet);
    m_tables->setCurrentIndex(location.table);
    m_inspector->setCurrentIndex(location.inspector);
    if (auto* table = qobject_cast<QTableView*>(m_tables->currentWidget());
        table && location.row.isValid() && location.row.model() == table->model())
    {
        const QSignalBlocker blocked(table->selectionModel());
        table->setCurrentIndex(location.row);
        table->scrollTo(location.row);
    }
    UpdateHistoryButtons();
    if (m_searchMode->currentIndex() == 1)
        ScheduleSearch();
}

void SclWorkspace::UpdateDetails()
{
    const auto id = m_selected;
    const auto& snapshot = *m_snapshot;
    const auto& analysis = snapshot.analysis;
    const auto& node = analysis.document->nodes[id];
    const auto path = Path(snapshot, id);
    m_breadcrumb->setText(path);
    m_breadcrumb->setToolTip(path.toHtmlEscaped());

    QString text = QStringLiteral("%1\n命名空间：%2\n原文：第 %3 行\n\n原始属性\n")
        .arg(Title(snapshot, id), node.namespaceUri).arg(node.source.line);
    for (int i = 0; i < node.attributeCount && i < 100 && text.size() < 24000; ++i)
    {
        const auto& attr = analysis.document->attributes[node.firstAttribute + i];
        const auto key = attr.namespaceUri.isEmpty() ? attr.name : '{' + attr.namespaceUri + '}' + attr.name;
        text += QStringLiteral("%1 = %2%3\n").arg(key, attr.value.left(2000),
            attr.value.size() > 2000 ? QStringLiteral("…") : QString());
    }
    if (node.attributeCount > 100 || text.size() >= 24000)
        text += QStringLiteral("…属性预览已截断\n");
    if (!node.text.isEmpty())
        text += QStringLiteral("\n原始文本\n%1\n").arg(node.text.left(2000));
    if (node.name == "ReportControl")
    {
        const auto report = std::lower_bound(analysis.reports.begin(), analysis.reports.end(), id,
            [](const auto& entry, NodeId needle) { return entry.source < needle; });
        if (report != analysis.reports.end() && report->source == id)
        {
            text += QStringLiteral("\n推导映射\n定义引用：%1\n数据集引用：%2\nMMS domain：%3\nMMS item：%4\n")
                .arg(report->definitionReference, report->dataSetReference, report->mmsDomain, report->mmsItem);
        }
    }
    const auto link = analysis.outgoing.constFind(id);
    if (link != analysis.outgoing.cend() && analysis.references[*link].kind == Scl::LinkKind::Fcda)
    {
        const auto& ref = analysis.references[*link];
        text += QStringLiteral("\n推导映射\n对象引用：%1\nMMS domain：%2\nMMS item：%3\n")
            .arg(ref.objectReference, ref.mmsDomain, ref.mmsItem);
        if (ref.arrayIndex >= 0)
            text += QStringLiteral("下标：%1\n后续组件：%2\n").arg(ref.arrayIndex).arg(ref.mmsComponent);
    }
    m_properties->setPlainText(text);
    static_cast<SclTableModel*>(m_links->model())->Reset(m_snapshot, id);
    static_cast<SclTableModel*>(m_candidates->model())->Reset(m_snapshot);
    m_candidates->hide();

    const auto dataSet = node.name == "DataSet" ? id : node.name == "FCDA" ? node.parent : NoNode;
    if (dataSet != NoNode && m_dataSet != dataSet)
    {
        m_dataSet = dataSet;
        static_cast<SclTableModel*>(m_members->model())->SetMembers(m_snapshot, dataSet);
        if (m_tables->currentWidget() == m_members && !m_restoring && m_searchMode->currentIndex() == 1)
            ScheduleSearch();
    }
    Preview(analysis.source.get(), node.source.line, QFileInfo(snapshot.path).fileName());
}

void SclWorkspace::Preview(const Scl::SourceIndex* source, int line, const QString& path)
{
    m_sourceLabel->setText(path);
    if (!source || source->lineCount() == 0)
    {
        m_source->clear();
        if (source)
            m_sourceLabel->setText(path + QStringLiteral(" · 原文行索引不可用"));
        return;
    }
    line = qBound(1, line, source->lineCount());
    const int first = qMax(1, line - 15);
    const int last = qMin(source->lineCount(), line + 15);
    QStringList lines;
    for (int n = first; n <= last; ++n)
    {
        auto entry = source->line(n);
        while (entry.text.endsWith('\r') || entry.text.endsWith('\n'))
            entry.text.chop(1);
        const bool clipped = entry.truncated || entry.text.size() > 1024;
        if (entry.text.size() > 1024)
            entry.text.truncate(entry.text.at(1023).isHighSurrogate() ? 1023 : 1024);
        lines << QStringLiteral("%1  %2%3").arg(n, 7).arg(entry.text,
            clipped ? QStringLiteral(" …[已截断]") : QString());
    }
    m_sourceLabel->setText(QStringLiteral("%1 · 第 %2 行（%3–%4）").arg(path).arg(line).arg(first).arg(last));
    m_source->setPlainText(lines.join('\n'));
    QTextCursor cursor(m_source->document()->findBlockByNumber(line - first));
    m_source->setTextCursor(cursor);
    QTextEdit::ExtraSelection highlight;
    highlight.cursor = cursor;
    highlight.format.setBackground(palette().alternateBase());
    highlight.format.setProperty(QTextFormat::FullWidthSelection, true);
    m_source->setExtraSelections({highlight});
    m_source->centerCursor();
}
} // namespace iec61850sim
