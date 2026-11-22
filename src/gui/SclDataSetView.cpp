#include "SclDataSetView.hpp"
#include "SclAnalysisService.hpp"
#include "SclMmsQuery.hpp"
#include "SclTableInteraction.hpp"

#include <QAbstractItemModel>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QShortcut>
#include <QStyle>
#include <QTreeView>
#include <QVBoxLayout>
#include <algorithm>
#include <utility>

namespace iec61850sim
{
using Scl::NodeId;
using Scl::NoNode;
namespace
{
constexpr int PageSize = 200;
constexpr int CacheLimit = 2000;
constexpr int TreeCacheLimit = CacheLimit - PageSize; // Reserve a page for users.

QString RowStatus(const SclHierarchyResult& page)
{
    const auto n = int(page.rows.size());
    const auto range = n ? QStringLiteral("%1–%2").arg(page.offset + 1).arg(page.offset + n)
                         : QStringLiteral("0");
    return page.limited ? QStringLiteral("显示 %1 · 已发现至少 %2 项（结果受限）").arg(range).arg(page.total)
                        : QStringLiteral("显示 %1 / %2 项").arg(range).arg(page.total);
}

bool SameFilter(const SclHierarchyRequest& a, const SclHierarchyRequest& b)
{
    return a.text == b.text && a.exact == b.exact && a.iedName == b.iedName
        && a.type == b.type && a.fc == b.fc && a.severity == b.severity;
}
}

class SclDataSetModel final : public QAbstractItemModel
{
public:
    using QAbstractItemModel::QAbstractItemModel;
    struct Branch
    {
        std::vector<SclHierarchyRow> rows;
        int offset = 0;
        bool more = false;
        bool limited = false;
        quint64 used = 0;
    };

    QModelIndex index(int row, int column, const QModelIndex& parent = {}) const override
    {
        if (!hasIndex(row, column, parent))
            return {};
        return createIndex(row, column, parent.isValid() ? quintptr(Node(parent) + 1) : quintptr(0));
    }

    QModelIndex parent(const QModelIndex& index) const override
    {
        return index.internalId() ? Root(NodeId(index.internalId() - 1)) : QModelIndex{};
    }

    int columnCount(const QModelIndex& = {}) const override { return 5; }

    int rowCount(const QModelIndex& parent = {}) const override
    {
        if (!parent.isValid())
            return page ? int(page->rows.size()) : 0;
        if (parent.column() != 0 || parent.internalId() || flat || !page->rows[parent.row()].declaredMembers)
            return 0;
        const auto branch = branches.constFind(Node(parent));
        return branch == branches.cend() ? 1
            : int(branch->rows.size()) + (branch->offset > 0) + (branch->more || branch->limited);
    }

    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        static const QStringList headers{tr("名称 / 成员序号"), tr("所属位置 / 声明范围"), tr("类别 / FC"),
            tr("状态 / 数量"), tr("描述")};
        return role == Qt::DisplayRole && orientation == Qt::Horizontal ? QVariant(headers.value(section)) : QVariant{};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid())
            return {};
        const auto* row = Record(index);
        if (!row)
        {
            if (index.column() != 0 || role != Qt::DisplayRole)
                return {};
            const auto id = Node(parent(index));
            if (pending == id)
                return tr("正在加载成员…");
            const auto branch = branches.constFind(id);
            if (branch == branches.cend()) return tr("加载成员");
            if (index.row() == 0 && branch->offset > 0) return tr("加载上一批");
            return branch->more ? tr("加载后续成员") : tr("已达到查询上限");
        }
        if (role == Qt::UserRole)
            return row->node;
        if (role != Qt::DisplayRole && role != SclTableInteraction::FullValueRole)
            return {};
        QString value;
        if (!row->member)
        {
            switch (index.column())
            {
            case 0: value = row->name; break;
            case 1: value = row->path; break;
            case 2: value = tr("数据集"); break;
            case 3: value = tr("%1 条声明").arg(row->declaredMembers); break;
            case 4: value = row->description; break;
            }
        }
        else
        {
            const auto& member = *row->member;
            switch (index.column())
            {
            case 0: value = QStringLiteral("#%1  %2").arg(member.ordinal).arg(row->name); break;
            case 1: value = member.ldInst + '/' + member.prefix + member.lnClass + member.lnInst
                + (member.ix.isNull() ? QString{} : tr(" · ix=%1").arg(member.ix)); break;
            case 2: value = (member.daName.isEmpty() ? QStringLiteral("DO") : QStringLiteral("DA"))
                + (member.fc.isEmpty() ? QString{} : " / " + member.fc); break;
            case 3: value = SclMmsStateText(member.state); break;
            case 4:
                if (const auto* title = member.description.ObjectTitle())
                    value = title->value;
                else
                    value = row->description;
                break;
            }
        }
        if (role == Qt::DisplayRole && value.size() > SclTableInteraction::PreviewLimit)
            value = value.left(SclTableInteraction::PreviewLimit) + tr("…（查看完整值）");
        return value;
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override
    {
        return !index.isValid() ? Qt::NoItemFlags : Record(index)
            ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::ItemIsEnabled;
    }

    void SetPage(std::shared_ptr<const SclHierarchyResult> result, bool showFlat)
    {
        beginResetModel();
        page = std::move(result);
        flat = showFlat;
        pending = NoNode;
        branches.clear();
        endResetModel();
    }

    const SclHierarchyRow* Record(const QModelIndex& index) const
    {
        if (!index.isValid())
            return nullptr;
        if (!index.internalId())
            return &page->rows[index.row()];
        const auto branch = branches.constFind(NodeId(index.internalId() - 1));
        if (branch == branches.cend())
            return nullptr;
        const int row = index.row() - (branch->offset > 0);
        return row >= 0 && row < int(branch->rows.size()) ? &branch->rows[row] : nullptr;
    }

    NodeId Node(const QModelIndex& index) const
    {
        const auto* row = Record(index);
        return row ? row->node : NoNode;
    }

    QModelIndex Root(NodeId id) const
    {
        if (page)
            for (int i = 0; i < int(page->rows.size()); ++i)
                if (page->rows[i].node == id)
                    return index(i, 0);
        return {};
    }

    QModelIndex Find(NodeId node, NodeId dataSet) const
    {
        const auto root = Root(dataSet);
        if (node == dataSet || !root.isValid() || flat)
            return node == dataSet ? root : QModelIndex{};
        const auto branch = branches.constFind(dataSet);
        if (branch != branches.cend())
            for (int i = 0; i < int(branch->rows.size()); ++i)
                if (branch->rows[i].node == node)
                    return index(i + (branch->offset > 0), 0, root);
        return {};
    }

    void SetPending(NodeId id)
    {
        const auto previous = std::exchange(pending, id);
        for (auto node : {previous, id})
        {
            const auto root = Root(node);
            if (root.isValid() && rowCount(root))
                emit dataChanged(index(0, 0, root), index(rowCount(root) - 1, 0, root), {Qt::DisplayRole});
        }
    }

    void SetBranch(NodeId id, Branch next)
    {
        const auto root = Root(id);
        if (!root.isValid())
            return;
        const int oldCount = rowCount(root);
        const int newCount = int(next.rows.size()) + (next.offset > 0) + (next.more || next.limited);
        if (oldCount)
        {
            beginRemoveRows(root, 0, oldCount - 1);
            branches.insert(id, Branch{});
            endRemoveRows();
        }
        if (newCount)
            beginInsertRows(root, 0, newCount - 1);
        next.used = ++clock;
        branches.insert(id, std::move(next));
        if (newCount)
            endInsertRows();
    }

    int CachedRows() const
    {
        int count = rowCount();
        for (const auto& branch : branches)
            count += int(branch.rows.size()) + 2;
        return count;
    }

    std::shared_ptr<const SclHierarchyResult> page;
    QHash<NodeId, Branch> branches;
    NodeId pending = NoNode;
    quint64 clock = 0;
    bool flat = false;
};

SclDataSetView::SclDataSetView(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclDataSetPage");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(3, 3, 3, 3);
    auto* controls = new QHBoxLayout;
    m_mode = new QComboBox(this);
    m_mode->setObjectName("SclDataSetMode");
    m_mode->setAccessibleName(tr("数据集浏览方式"));
    m_mode->addItems({tr("层级浏览"), tr("平面列表")});
    m_order = new QComboBox(this);
    m_order->setObjectName("SclDataSetOrder");
    m_order->setAccessibleName(tr("数据集排序"));
    m_order->addItems({tr("声明顺序"), tr("所属路径升序"), tr("所属路径降序")});
    auto button = [this](const QString& text, const char* name) {
        auto* value = new QPushButton(text, this);
        value->setObjectName(name);
        return value;
    };
    m_previous = button(tr("上一页"), "SclDataSetPrevious");
    m_next = button(tr("下一页"), "SclDataSetNext");
    m_status = new QLabel(this);
    m_status->setObjectName("SclDataSetStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    for (auto* widget : {static_cast<QWidget*>(m_mode), static_cast<QWidget*>(m_order),
            static_cast<QWidget*>(m_previous), static_cast<QWidget*>(m_next)})
        controls->addWidget(widget);
    controls->addStretch();
    layout->addLayout(controls);
    layout->addWidget(m_status);

    m_tree = new QTreeView(this);
    m_tree->setObjectName("SclDataSets");
    m_tree->setAccessibleName(tr("数据集与成员"));
    m_tree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setAlternatingRowColors(true);
    m_tree->setExpandsOnDoubleClick(false);
    m_tree->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_model = new SclDataSetModel(m_tree);
    m_tree->setModel(m_model);
    auto* topology = new QAction(tr("打开引用拓扑"), m_tree);
    m_tree->addAction(topology);
    connect(topology, &QAction::triggered, this, [this] {
        if (const auto id = m_model->Node(m_tree->currentIndex()); id != NoNode)
            emit OpenTopology(id);
    });
    m_tree->header()->setStretchLastSection(true);
    for (int column = 0; column < 5; ++column)
        m_tree->setColumnWidth(column, column == 0 ? 180 : column == 1 ? 225 : 105);
    auto* text = new SclTableInteraction(m_tree);
    text->SetEnabled(true);
    layout->addWidget(m_tree, 1);

    auto* actions = new QHBoxLayout;
    m_source = button(tr("查看原文"), "SclDataSetSource");
    m_openMembers = button(tr("打开成员专页"), "SclDataSetOpenMembers");
    m_showUsers = button(tr("查看使用方"), "SclDataSetUsers");
    m_target = button(tr("引用目标"), "SclDataSetTarget");
    actions->addWidget(m_source);
    actions->addWidget(m_openMembers);
    actions->addWidget(m_showUsers);
    actions->addWidget(m_target);
    m_showUsers->setIcon(style()->standardIcon(QStyle::SP_ArrowBack));
    m_target->setIcon(style()->standardIcon(QStyle::SP_ArrowForward));
    m_showUsers->setToolTip(tr("查看引用此数据集的已解析控制块"));
    m_target->setToolTip(tr("沿成员声明已解析的唯一目标查阅"));
    actions->addStretch();
    layout->addLayout(actions);

    m_userDialog = new QDialog(this);
    m_userDialog->setObjectName("SclDataSetUsersDialog");
    m_userDialog->setWindowTitle(tr("数据集使用方"));
    m_userDialog->resize(600, 380);
    auto* userLayout = new QVBoxLayout(m_userDialog);
    m_userStatus = new QLabel(m_userDialog);
    m_userStatus->setTextFormat(Qt::PlainText);
    m_userStatus->setWordWrap(true);
    userLayout->addWidget(m_userStatus);
    m_userKind = new QComboBox(m_userDialog);
    m_userKind->setObjectName("SclDataSetUserKind");
    m_userKind->setAccessibleName(tr("使用方类型"));
    m_userKind->addItems({tr("全部使用方"), tr("报告"), tr("GSE（含 GOOSE）"), tr("SV")});
    userLayout->addWidget(m_userKind);
    m_userList = new QListWidget(m_userDialog);
    m_userList->setObjectName("SclDataSetUsersList");
    m_userList->setWordWrap(true);
    userLayout->addWidget(m_userList, 1);
    auto* userPaging = new QHBoxLayout;
    m_userPrevious = new QPushButton(tr("上一页"), m_userDialog);
    m_userNext = new QPushButton(tr("下一页"), m_userDialog);
    m_userOpen = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), tr("定位使用方"), m_userDialog);
    m_userOpen->setObjectName("SclDataSetOpenUser");
    userPaging->addWidget(m_userPrevious);
    userPaging->addWidget(m_userNext);
    userPaging->addStretch();
    userPaging->addWidget(m_userOpen);
    userLayout->addLayout(userPaging);
    connect(m_userDialog, &QDialog::finished, this, [this] {
        m_users.Cancel();
        emit LocationUpdated();
    });
    connect(m_userKind, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        m_userSelection = NoNode;
        LoadUsers(0);
    });
    connect(m_userList, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        m_userOpen->setEnabled(item != nullptr);
        if (item)
            m_userSelection = item->data(Qt::UserRole).toInt();
    });
    connect(m_userList, &QListWidget::itemActivated, this, &SclDataSetView::ActivateUser);
    auto* enterUser = new QShortcut(QKeySequence(Qt::Key_Return), m_userList);
    enterUser->setContext(Qt::WidgetShortcut);
    connect(enterUser, &QShortcut::activated, this, &SclDataSetView::ActivateUser);
    connect(m_userOpen, &QPushButton::clicked, this, &SclDataSetView::ActivateUser);
    connect(m_userPrevious, &QPushButton::clicked, this, [this] { LoadUsers(m_users.result()->offset - PageSize); });
    connect(m_userNext, &QPushButton::clicked, this, [this] { LoadUsers(m_users.result()->offset + PageSize); });
    connect(&m_users, &SclHierarchyQuerySession::Ready, this, [this] {
        const auto result = m_users.result();
        if (!m_userDialog->isVisible() || result->requestId != m_usersId || result->request.dataSet != m_userSet)
            return;
        m_userStatus->setText(result->parentPath + '\n' + (result->rows.empty() && !result->limited
            ? tr("当前未发现已解析的报告、GOOSE 或 SV 使用方。") : RowStatus(*result)));
        for (const auto& row : result->rows)
        {
            const auto& doc = *result->snapshot->analysis.document;
            const auto type = row.kind == SclHierarchyRowKind::Report ? tr("报告")
                : row.kind == SclHierarchyRowKind::Sv ? QStringLiteral("SV")
                : doc.attribute(row.node, "type") == "GSSE" ? QStringLiteral("GSSE") : QStringLiteral("GOOSE");
            const auto owner = result->snapshot->browse.iedOwners[row.node];
            const auto sourceOwner = result->snapshot->browse.iedOwners[row.dataSet];
            const auto cross = owner != NoNode && owner != sourceOwner ? tr("跨 IED · ") : QString{};
            auto* entry = new QListWidgetItem(cross + type + QStringLiteral(" · ") + row.path, m_userList);
            entry->setData(Qt::UserRole, row.node);
            if (row.node == m_userSelection)
            {
                m_userList->setCurrentItem(entry);
                m_userList->scrollToItem(entry);
            }
        }
        m_userPrevious->setEnabled(result->offset > 0);
        m_userNext->setEnabled(result->hasMore);
    });

    connect(&m_list, &SclHierarchyQuerySession::Ready, this, [this] { ReceiveList(m_list); });
    connect(&m_locate, &SclHierarchyQuerySession::Ready, this, [this] { ReceiveList(m_locate); });
    connect(&m_branch, &SclHierarchyQuerySession::Ready, this, &SclDataSetView::ReceiveBranch);
    connect(m_mode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] {
        const QScopedValueRollback<bool> guard(m_applying, true);
        m_restore.reset();
        m_branch.Cancel();
        m_pendingBranch = NoNode;
        m_model->SetPage(m_model->page, m_mode->currentIndex() == 1);
        m_tree->setRootIsDecorated(!m_model->flat);
        ApplySelection(!m_model->flat);
        emit LocationUpdated();
    });
    connect(m_order, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int order) {
        m_restore.reset();
        m_filter.order = SclHierarchyOrder(order);
        m_offset = 0;
        LoadList(m_selectedSet != NoNode);
        emit LocationUpdated();
    });
    connect(m_previous, &QPushButton::clicked, this, [this] { m_restore.reset(); m_offset -= PageSize; LoadList(false); });
    connect(m_next, &QPushButton::clicked, this, [this] { m_restore.reset(); m_offset += PageSize; LoadList(false); });
    connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!m_applying)
            if (const auto* row = m_model->Record(index))
            {
                m_restore.reset();
                m_node = row->node;
                m_selectedSet = row->dataSet;
                emit Selected(m_node);
                UpdateActions();
            }
    });
    connect(m_tree, &QTreeView::expanded, this, [this](const QModelIndex& index) {
        const auto id = m_model->Node(index);
        if (!m_applying && id != NoNode)
        {
            m_restore.reset();
            if (auto branch = m_model->branches.find(id); branch != m_model->branches.end() && !branch->rows.empty())
                branch->used = ++m_model->clock;
            else
                LoadBranch(id, 0);
            emit LocationUpdated();
        }
    });
    connect(m_tree, &QTreeView::collapsed, this, [this](const QModelIndex& index) {
        const auto id = m_model->Node(index);
        if (m_applying || id == NoNode)
            return;
        m_restore.reset();
        if (m_pendingBranch == id)
        {
            m_branch.Cancel();
            m_model->SetPending(m_pendingBranch = NoNode);
        }
        if (m_selectedSet == id && m_node != id)
        {
            m_tree->setCurrentIndex(index);
            if (m_node != id)
                emit Selected(id);
        }
        emit LocationUpdated();
    });
    connect(m_tree, &QTreeView::clicked, this, [this](const QModelIndex& index) {
        if (m_model->flat && m_node != m_selectedSet && m_model->Node(index) == m_selectedSet)
        {
            m_node = m_selectedSet;
            emit Selected(m_node);
            UpdateActions();
        }
        Activate(index);
    });
    connect(m_tree, &QTreeView::activated, this, &SclDataSetView::Activate);
    connect(text, &SclTableInteraction::OpenSource, m_source, &QPushButton::click);
    connect(m_source, &QPushButton::clicked, this, [this] {
        const auto node = m_model->flat ? m_node : m_model->Node(m_tree->currentIndex());
        if (node != NoNode)
            emit OpenSource(node);
    });
    connect(m_openMembers, &QPushButton::clicked, this, [this] { emit OpenMembers(m_selectedSet); });
    connect(m_showUsers, &QPushButton::clicked, this, [this] { OpenUsers(m_selectedSet); });
    connect(m_target, &QPushButton::clicked, this, [this] {
        if (const auto* row = m_model->Record(m_tree->currentIndex()); row && row->member
            && row->member->target != NoNode)
            emit OpenTarget(row->member->target);
    });
    UpdateActions();
}

void SclDataSetView::SetContext(std::shared_ptr<const SclSnapshot> snapshot, NodeId ied)
{
    if (m_snapshot == snapshot && m_ied == ied)
        return;
    if (m_snapshot != snapshot)
    {
        m_filter = {};
        const QSignalBlocker blocked(m_order);
        m_order->setCurrentIndex(0);
    }
    m_snapshot = std::move(snapshot);
    m_ied = ied;
    m_node = m_selectedSet = NoNode;
    m_locateMiss = m_locateLimited = false;
    m_offset = 0;
    m_restore.reset();
    m_userDialog->close();
    for (auto* session : {&m_list, &m_locate, &m_branch, &m_users})
        session->SetContext(m_snapshot, {m_ied});
    LoadList(false);
}

void SclDataSetView::SetFilter(const SclHierarchyRequest& filter)
{
    if (SameFilter(m_filter, filter))
        return;
    m_restore.reset();
    const auto order = m_filter.order;
    m_filter = filter;
    m_filter.order = order;
    m_offset = 0;
    LoadList(m_selectedSet != NoNode);
    emit LocationUpdated();
}

void SclDataSetView::LoadList(bool locate)
{
    m_list.Cancel();
    m_locate.Cancel();
    m_branch.Cancel();
    m_pendingBranch = NoNode;
    m_listReady = false;
    m_loadingList = false;
    m_locatingSelection = locate && m_selectedSet != NoNode;
    if (m_locatingSelection)
    {
        m_locateMiss = m_locateLimited = false;
        emit FilterHidesSelection(false);
    }
    const QScopedValueRollback<bool> guard(m_applying, true);
    m_model->SetPage({}, m_mode->currentIndex() == 1);
    if (m_snapshot && isVisible())
    {
        auto request = m_filter;
        request.offset = m_offset;
        if (m_locatingSelection)
        {
            request.locate = m_selectedSet;
            m_locateId = m_locate.Query(request);
        }
        else
            m_listId = m_list.Query(request);
        m_loadingList = true;
        m_status->setText(tr("正在加载数据集…"));
    }
    UpdateActions();
}

void SclDataSetView::ReceiveList(SclHierarchyQuerySession& session)
{
    const auto result = session.result();
    const bool locate = &session == &m_locate;
    if (!isVisible() || result->requestId != (locate ? m_locateId : m_listId))
        return;
    if (locate && !result->locateFound)
    {
        m_locateMiss = true;
        m_locateLimited = result->limited;
        emit FilterHidesSelection(!result->limited);
        m_offset = 0;
        LoadList(false);
        return;
    }
    const QScopedValueRollback<bool> guard(m_applying, true);
    m_loadingList = false;
    m_listReady = true;
    m_locatingSelection = false;
    m_offset = result->offset;
    m_model->SetPage(result, m_mode->currentIndex() == 1);
    m_status->setText(result->total == 0 && !result->limited && result->scopeTotal > 0
        ? tr("没有匹配的数据集 · 当前范围 %1 项").arg(result->scopeTotal)
        : tr("范围 %1%2 项 · %3").arg(result->limited ? tr("至少 ") : QString{})
            .arg(result->scopeTotal).arg(RowStatus(*result)));
    if (m_locateLimited)
        m_status->setText(m_status->text() + tr(" · 所选数据集定位受限"));
    m_status->setToolTip(m_status->text().toHtmlEscaped());
    ApplySelection(!m_locateMiss && (locate || bool(m_restore)));
    if (m_restore)
    {
        for (auto id : m_restore->expanded)
        {
            const auto root = m_model->Root(id);
            if (root.isValid() && !m_model->flat)
                m_tree->setExpanded(root, true);
        }
        ContinueRestore();
    }
    UpdateActions();
    emit LocationUpdated();
}

void SclDataSetView::SyncSelection(NodeId node, NodeId dataSet)
{
    if (dataSet == NoNode)
        node = NoNode;
    if (m_node == node && m_selectedSet == dataSet)
        return;
    m_node = node;
    m_selectedSet = dataSet;
    m_locateMiss = m_locateLimited = false;
    emit FilterHidesSelection(false);
    if (isVisible())
        ApplySelection(true);
    UpdateActions();
}

void SclDataSetView::ApplySelection(bool locate)
{
    const QScopedValueRollback<bool> guard(m_applying, true);
    const auto root = m_model->Root(m_selectedSet);
    auto index = m_model->Find(m_node, m_selectedSet);
    if (m_model->flat && m_node != m_selectedSet)
        index = root;
    if (index.isValid())
    {
        const auto column = m_restore ? m_restore->column : qMax(0, m_tree->currentIndex().column());
        index = index.siblingAtColumn(column);
        if (index.parent().isValid())
            m_tree->expand(index.parent());
        if (m_tree->currentIndex() != index)
        {
            m_tree->setCurrentIndex(index);
            m_tree->scrollTo(index);
        }
        emit FilterHidesSelection(false);
    }
    else
    {
        m_tree->selectionModel()->clear();
        if (locate && m_selectedSet != NoNode && m_listReady && !m_locatingSelection && !root.isValid())
            LoadList(true);
        else if (locate && m_locatingSelection && m_loadingList)
            LoadList(true);
        else if (locate && root.isValid() && !m_model->flat && m_pendingBranch != m_selectedSet)
        {
            m_tree->expand(root);
            LoadBranch(m_selectedSet, 0, m_node);
        }
        else if (locate && m_selectedSet != NoNode && m_loadingList && !root.isValid())
            LoadList(true);
    }
}

void SclDataSetView::LoadBranch(NodeId dataSet, int offset, NodeId locate)
{
    auto request = SclHierarchyRequest{};
    request.kind = SclHierarchyKind::Members;
    request.dataSet = dataSet;
    request.offset = offset;
    request.locate = locate;
    m_pendingBranch = dataSet;
    m_model->SetPending(dataSet);
    m_branchId = m_branch.Query(request);
}

void SclDataSetView::ReceiveBranch()
{
    const auto result = m_branch.result();
    const auto id = result->request.dataSet;
    const auto root = m_model->Root(id);
    if (!isVisible() || result->requestId != m_branchId || id != m_pendingBranch
        || !root.isValid() || !m_tree->isExpanded(root))
        return;
    QElapsedTimer delivery;
    delivery.start();
    const auto anchor = CurrentLocation();
    const QScopedValueRollback<bool> guard(m_applying, true);
    m_model->SetPending(m_pendingBranch = NoNode);
    m_branch.Cancel();
    if (result->request.locate != NoNode && !result->locateFound)
    {
        m_status->setText(result->limited ? tr("成员定位受限") : tr("未找到所选成员"));
        ContinueRestore();
        return;
    }
    auto branch = m_model->branches.value(id);
    const int currentSize = int(branch.rows.size());
    const int added = int(result->rows.size());
    while (m_model->CachedRows() + added + 2 > TreeCacheLimit)
    {
        NodeId victim = NoNode;
        quint64 oldest = m_model->clock + 1;
        bool preferCollapsed = false;
        for (auto it = m_model->branches.cbegin(); it != m_model->branches.cend(); ++it)
        {
            if (it.key() == id || it.key() == m_selectedSet || it->rows.empty())
                continue;
            const bool collapsed = !m_tree->isExpanded(m_model->Root(it.key()));
            if ((collapsed && !preferCollapsed) || (collapsed == preferCollapsed && it->used < oldest))
            {
                victim = it.key();
                oldest = it->used;
                preferCollapsed = collapsed;
            }
        }
        if (victim == NoNode)
            break;
        m_tree->collapse(m_model->Root(victim));
        SclDataSetModel::Branch retry;
        retry.more = true;
        m_model->SetBranch(victim, std::move(retry));
    }
    const int replaceCost = m_model->CachedRows() + added + 2
        - (m_model->branches.contains(id) ? currentSize + 2 : 0);
    if (replaceCost > TreeCacheLimit)
    {
        m_status->setText(tr("成员缓存已满 · 选择此数据集后可继续加载"));
        m_restore.reset();
        return;
    }
    const bool fits = currentSize && m_model->CachedRows() + added + 2 <= TreeCacheLimit;
    const bool append = fits && result->offset == branch.offset + currentSize;
    const bool prepend = fits && result->offset + added == branch.offset;
    if (prepend)
    {
        branch.rows.insert(branch.rows.begin(), result->rows.begin(), result->rows.end());
        branch.offset = result->offset;
        branch.limited |= result->limited;
    }
    else
    {
        if (!append)
        {
            branch.rows.clear();
            branch.offset = result->offset;
        }
        branch.rows.insert(branch.rows.end(), result->rows.begin(), result->rows.end());
        branch.more = result->hasMore;
        branch.limited = result->limited;
    }
    m_model->SetBranch(id, std::move(branch));
    m_tree->setExpanded(root, true);
    ApplySelection(false);
    if (m_restore)
        ContinueRestore();
    else
        RestoreAnchor(anchor);
    UpdateActions();
    m_tree->setProperty("SclBranchWorkerUs", result->workerUs);
    m_tree->setProperty("SclBranchApplyUs", delivery.nsecsElapsed() / 1000);
    m_tree->setProperty("SclBranchOffset", result->offset);
    m_tree->setProperty("SclTreeCacheRows", m_model->CachedRows());
    m_tree->setProperty("SclTreeMeasureUs", 0);
    emit LocationUpdated();
}

void SclDataSetView::Activate(const QModelIndex& index)
{
    if (!index.parent().isValid() || m_model->Record(index))
        return;
    const auto id = m_model->Node(index.parent());
    if (id == m_pendingBranch)
        return;
    m_restore.reset();
    const auto branch = m_model->branches.constFind(id);
    if (branch == m_model->branches.cend())
        LoadBranch(id, 0);
    else if (index.row() == 0 && branch->offset > 0)
        LoadBranch(id, qMax(0, branch->offset - PageSize));
    else if (branch->more)
        LoadBranch(id, branch->offset + int(branch->rows.size()));
}

void SclDataSetView::UpdateActions()
{
    m_previous->setEnabled(m_listReady && m_offset > 0);
    m_next->setEnabled(m_listReady && m_model->page && m_model->page->hasMore);
    m_source->setEnabled(m_model->Node(m_tree->currentIndex()) != NoNode);
    m_source->setText(m_model->flat && m_node != m_selectedSet ? tr("查看成员原文") : tr("查看原文"));
    const auto* dataSet = m_model->Record(m_model->Root(m_selectedSet));
    m_openMembers->setEnabled(dataSet && dataSet->declaredMembers > 0);
    m_showUsers->setEnabled(m_selectedSet != NoNode);
    const auto* row = m_model->Record(m_tree->currentIndex());
    m_target->setEnabled(row && row->member && row->member->target != NoNode);
    m_openMembers->setToolTip(m_model->flat && m_node != m_selectedSet ? tr("在成员专页恢复所选 FCDA") : QString{});
}

SclDataSetView::Location SclDataSetView::CurrentLocation() const
{
    if (m_restore)
        return *m_restore;
    Location value;
    value.filter = m_filter;
    value.flat = m_mode->currentIndex() == 1;
    value.offset = m_offset;
    value.column = qMax(0, m_tree->currentIndex().column());
    const auto top = m_tree->indexAt(QPoint(0, 0));
    value.anchor = m_model->Node(top);
    value.anchorParent = m_model->Node(top.parent());
    value.anchorDelta = top.isValid() ? -m_tree->visualRect(top).top() : 0;
    for (int row = 0; row < m_model->rowCount() && value.expanded.size() < 8; ++row)
    {
        const auto index = m_model->index(row, 0);
        if (m_tree->isExpanded(index))
            value.expanded.push_back(m_model->Node(index));
    }
    return value;
}

void SclDataSetView::RestoreAnchor(const Location& location)
{
    const auto index = location.anchorParent == NoNode ? m_model->Root(location.anchor)
        : m_model->Find(location.anchor, location.anchorParent);
    if (index.isValid() && (!index.parent().isValid() || m_tree->isExpanded(index.parent())))
    {
        m_tree->scrollTo(index, QAbstractItemView::PositionAtTop);
        m_tree->verticalScrollBar()->setValue(m_tree->verticalScrollBar()->value() + location.anchorDelta);
    }
}

void SclDataSetView::ContinueRestore()
{
    if (!m_restore || m_pendingBranch != NoNode)
        return;
    const auto anchorParent = m_restore->anchorParent;
    if (!m_model->flat && !m_triedAnchor && m_model->Root(anchorParent).isValid()
        && !m_model->Find(m_restore->anchor, anchorParent).isValid())
    {
        m_triedAnchor = true;
        m_tree->expand(m_model->Root(anchorParent));
        LoadBranch(anchorParent, 0, m_restore->anchor);
        return;
    }
    RestoreAnchor(*m_restore);
    while (!m_model->flat && m_restoreBranch < m_restore->expanded.size())
    {
        const auto id = m_restore->expanded[m_restoreBranch++];
        const auto root = m_model->Root(id);
        if (root.isValid() && m_tree->isExpanded(root) && !m_model->branches.contains(id))
        {
            LoadBranch(id, 0);
            return;
        }
    }
    m_restore.reset();
}

void SclDataSetView::Restore(const Location& location)
{
    m_restore = location;
    m_triedAnchor = false;
    m_restoreBranch = 0;
    m_filter = location.filter;
    m_offset = location.offset;
    const QSignalBlocker mode(m_mode), order(m_order);
    m_mode->setCurrentIndex(location.flat ? 1 : 0);
    m_tree->setRootIsDecorated(!location.flat);
    m_order->setCurrentIndex(int(location.filter.order));
    LoadList(m_selectedSet != NoNode);
}

void SclDataSetView::OpenUsers(NodeId dataSet)
{
    RestoreUsers({dataSet});
}

void SclDataSetView::CloseUsers()
{
    m_userDialog->close();
}

SclDataSetView::UsersLocation SclDataSetView::CurrentUsers() const
{
    if (!m_userDialog->isVisible())
        return {};
    return {m_userSet, m_userSelection, m_userOffset, m_userKind->currentIndex()};
}

void SclDataSetView::RestoreUsers(const UsersLocation& location)
{
    if (!m_snapshot || location.dataSet == NoNode)
    {
        CloseUsers();
        return;
    }
    m_userSet = location.dataSet;
    m_userSelection = location.selected;
    const QSignalBlocker blocked(m_userKind);
    m_userKind->setCurrentIndex(location.kind);
    m_userDialog->show();
    m_userDialog->raise();
    LoadUsers(location.offset);
    m_userList->setFocus();
}

void SclDataSetView::ActivateUser()
{
    if (const auto* item = m_userList->currentItem(); m_userDialog->isVisible() && item)
        emit OpenUser(item->data(Qt::UserRole).toInt());
}

void SclDataSetView::LoadUsers(int offset)
{
    m_userOffset = offset;
    m_userList->clear();
    m_userStatus->setText(tr("正在查询使用方…"));
    m_userPrevious->setEnabled(false);
    m_userNext->setEnabled(false);
    m_userOpen->setEnabled(false);
    SclHierarchyRequest request;
    request.kind = SclHierarchyKind::Users;
    request.dataSet = m_userSet;
    request.offset = offset;
    switch (m_userKind->currentIndex())
    {
    case 1: request.userKind = SclHierarchyRowKind::Report; break;
    case 2: request.userKind = SclHierarchyRowKind::Gse; break;
    case 3: request.userKind = SclHierarchyRowKind::Sv; break;
    }
    m_usersId = m_users.Query(request);
}

void SclDataSetView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (!m_listReady)
        LoadList(m_selectedSet != NoNode);
    else
        ApplySelection(true);
}

void SclDataSetView::hideEvent(QHideEvent* event)
{
    m_list.Cancel();
    m_locate.Cancel();
    m_branch.Cancel();
    m_model->SetPending(m_pendingBranch = NoNode);
    m_loadingList = false;
    m_userDialog->close();
    QWidget::hideEvent(event);
}
} // namespace iec61850sim
