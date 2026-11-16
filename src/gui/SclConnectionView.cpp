#include "SclConnectionView.hpp"
#include "SclRelationGraph.hpp"
#include "SclTableInteraction.hpp"

// finesse · register=product · native palette/type · direction tree + member columns
// + bounded local graph · Qt Graphics View · feedback-only · density=9

#include <QCheckBox>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFutureWatcher>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>
#include <cmath>
#include <functional>
#include <optional>

namespace iec61850sim
{
namespace
{
class ConnectionGraph final : public QGraphicsView
{
public:
    explicit ConnectionGraph(QWidget* parent) : QGraphicsView(parent)
    {
        setScene(new QGraphicsScene(this));
        setRenderHint(QPainter::Antialiasing);
        setDragMode(ScrollHandDrag);
        setTransformationAnchor(AnchorUnderMouse);
        setAccessibleName(QStringLiteral("局部虚端子关系图，Ctrl 加滚轮缩放，拖动平移，双击节点查看原文"));
    }
    std::function<void(int)> activate;
    std::function<void(int, int)> selectIdentity;
    void Fit()
    {
        m_autoFit = true;
        if (m_fitting || scene()->items().isEmpty()) return;
        m_fitting = true;
        fitInView(scene()->sceneRect(), Qt::KeepAspectRatio);
        m_fitting = false;
    }
    void Zoom(double factor)
    {
        const auto next = transform().m11() * factor;
        if (next > 3. || (factor < 1. && next < .04)) return;
        m_autoFit = false;
        scale(factor, factor);
    }
protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        auto* item = itemAt(event->pos());
        while (item && !item->data(0).isValid())
            item = item->parentItem();
        if (event->button() == Qt::LeftButton && item && item->data(1).isValid() && selectIdentity)
            selectIdentity(item->data(1).toInt(), item->data(2).toInt());
        QGraphicsView::mousePressEvent(event);
    }
    void resizeEvent(QResizeEvent* event) override
    {
        QGraphicsView::resizeEvent(event);
        if (m_autoFit) Fit();
    }
    void showEvent(QShowEvent* event) override
    {
        QGraphicsView::showEvent(event);
        if (m_autoFit) Fit();
    }
    void wheelEvent(QWheelEvent* event) override
    {
        if (event->modifiers() & Qt::ControlModifier)
        {
            const auto factor = event->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
            Zoom(factor);
            event->accept();
        }
        else
            QGraphicsView::wheelEvent(event);
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        auto* item = itemAt(event->pos());
        while (item && !item->data(0).isValid())
            item = item->parentItem();
        if (item && item->data(0).toInt() >= 0 && activate)
            activate(item->data(0).toInt());
        QGraphicsView::mouseDoubleClickEvent(event);
    }
private:
    bool m_autoFit = true, m_fitting = false;
};

QColor ServiceColor(const QString& service)
{
    return service == "SV" ? QColor("#176E83") : service == "GOOSE" ? QColor("#69539E") : QColor("#8A6320");
}
}

SclConnectionView::SclConnectionView(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclConnections");
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* tools = new QHBoxLayout;
    m_service = new QComboBox(this);
    m_service->setObjectName("SclConnectionService");
    m_service->addItem(QStringLiteral("全部服务"), QString{});
    m_service->addItem(QStringLiteral("GOOSE · 事件"), QStringLiteral("GOOSE"));
    m_service->addItem(QStringLiteral("SV · 采样"), QStringLiteral("SV"));
    m_filter = new QLineEdit(this);
    m_filter->setObjectName("SclConnectionFilter");
    m_filter->setPlaceholderText(QStringLiteral("查找发端、收端、控制块、APPID 或 MAC"));
    m_filter->setClearButtonEnabled(true);
    tools->addWidget(m_service);
    tools->addWidget(m_filter, 1);
    layout->addLayout(tools);
    m_stats = new QLabel(this);
    m_stats->setObjectName("SclConnectionStatistics");
    m_stats->setTextFormat(Qt::PlainText);
    m_stats->setWordWrap(true);
    layout->addWidget(m_stats);

    auto* split = new QSplitter(this);
    split->setChildrenCollapsible(false);
    layout->addWidget(split, 1);
    m_tree = new QTreeWidget(split);
    m_tree->setObjectName("SclConnectionTree");
    m_tree->setHeaderLabel(QStringLiteral("方向 → 对端装置 → 发布流"));
    m_tree->setUniformRowHeights(true);
    m_tree->setMinimumWidth(150);
    m_tree->setMaximumWidth(440);
    m_tree->setAccessibleName(QStringLiteral("发送与接收关系导航"));
    auto* body = new QWidget(split);
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(8, 0, 0, 0);
    auto* pathBar = new QHBoxLayout;
    m_breadcrumb = new QLabel(body);
    m_breadcrumb->setObjectName("SclConnectionBreadcrumb");
    m_breadcrumb->setWordWrap(true);
    m_breadcrumb->setTextFormat(Qt::RichText);
    m_breadcrumb->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    pathBar->addWidget(m_breadcrumb, 1);
    auto* treeToggle = new QPushButton(QStringLiteral("关系树"), body);
    treeToggle->setObjectName("SclConnectionTreeToggle");
    treeToggle->setCheckable(true);
    treeToggle->setChecked(true);
    pathBar->addWidget(treeToggle);
    bodyLayout->addLayout(pathBar);
    m_summary = new QLabel(body);
    m_summary->setObjectName("SclConnectionSummary");
    m_summary->setTextFormat(Qt::PlainText);
    m_summary->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_summary->setWordWrap(true);
    auto* summaryScroll = new QScrollArea(body);
    summaryScroll->setWidgetResizable(true);
    summaryScroll->setFrameShape(QFrame::NoFrame);
    summaryScroll->setMinimumHeight(fontMetrics().lineSpacing() * 4 + 8);
    summaryScroll->setMaximumHeight(fontMetrics().lineSpacing() * 6 + 12);
    summaryScroll->setWidget(m_summary);
    bodyLayout->addWidget(summaryScroll);
    auto* tabs = new QTabWidget(body);
    tabs->setObjectName("SclConnectionModes");
    bodyLayout->addWidget(tabs, 1);
    auto* mapping = new QWidget(tabs);
    auto* mapLayout = new QVBoxLayout(mapping);
    mapLayout->setContentsMargins(0, 4, 0, 0);
    auto* paging = new QHBoxLayout;
    m_mappedOnly = new QCheckBox(QStringLiteral("仅看有映射的成员"), mapping);
    m_mappedOnly->setObjectName("SclConnectionMappedOnly");
    m_paging = new QLabel(mapping);
    m_previous = new QPushButton(QStringLiteral("上一页"), mapping);
    m_next = new QPushButton(QStringLiteral("下一页"), mapping);
    paging->addWidget(m_mappedOnly);
    paging->addWidget(m_paging, 1);
    paging->addWidget(m_previous);
    paging->addWidget(m_next);
    mapLayout->addLayout(paging);
    m_members = new QTableWidget(mapping);
    m_members->setObjectName("SclConnectionMembers");
    m_members->setColumnCount(6);
    m_members->setHorizontalHeaderLabels({QStringLiteral("序号"), QStringLiteral("发送成员 / FC"),
        QStringLiteral("对象描述"), QStringLiteral("接收 IED"), QStringLiteral("接收内部地址 intAddr"), QStringLiteral("证据")});
    m_members->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_members->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_members->setSelectionMode(QAbstractItemView::SingleSelection);
    m_members->setAlternatingRowColors(true);
    m_members->setWordWrap(false);
    m_members->setShowGrid(false);
    m_members->verticalHeader()->hide();
    m_members->verticalHeader()->setDefaultSectionSize(30);
    m_members->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    const int widths[] = {45, 205, 110, 85, 220, 180};
    for (int i = 0; i < 6; ++i)
        m_members->setColumnWidth(i, widths[i]);
    mapLayout->addWidget(m_members, 1);
    auto* memberInteraction = new SclTableInteraction(m_members);
    memberInteraction->SetEnabled(true);
    tabs->addTab(mapping, style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("成员映射"));

    auto* graphPanel = new QWidget(tabs);
    auto* graphLayout = new QVBoxLayout(graphPanel);
    auto* graphActions = new QHBoxLayout;
    auto* legend = new QLabel(QStringLiteral("实线：明确绑定 · 虚线：候选 · 点线：归属或依据 · 双击：原文"), graphPanel);
    legend->setWordWrap(true);
    graphActions->addWidget(legend, 1);
    auto* fit = new QPushButton(QStringLiteral("适应"), graphPanel);
    auto* zoomIn = new QPushButton(QStringLiteral("放大 +"), graphPanel);
    auto* zoomOut = new QPushButton(QStringLiteral("缩小 −"), graphPanel);
    graphActions->addWidget(zoomOut);
    graphActions->addWidget(zoomIn);
    graphActions->addWidget(fit);
    m_expandGraph = new QPushButton(QStringLiteral("展开更多"), graphPanel);
    m_expandGraph->setObjectName("SclExpandRelationGraph");
    m_expandGraph->setCheckable(true);
    graphActions->addWidget(m_expandGraph);
    graphLayout->addLayout(graphActions);
    m_graphMember = new QComboBox(graphPanel);
    m_graphMember->setObjectName("SclConnectionGraphMember");
    m_graphMember->setAccessibleName(QStringLiteral("关系图当前成员"));
    m_graphMember->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    graphLayout->addWidget(m_graphMember);
    auto* graph = new ConnectionGraph(graphPanel);
    m_graph = graph;
    m_graph->setObjectName("SclConnectionGraph");
    m_graph->setBackgroundBrush(palette().brush(QPalette::AlternateBase));
    graph->activate = [this](int node) { emit OpenEvidence(node); };
    graph->selectIdentity = [this](int member, int input) {
        for (int i = 0; i < int(m_detail.members.size()); ++i)
            if (m_detail.members[i].member == member && m_detail.members[i].input == input
                && i != m_members->currentRow())
            {
                // Let the mouse event finish before the selected row can replace the scene.
                QTimer::singleShot(0, this, [this, request = m_graphRequest, i, member, input] {
                    if (request && m_graphRequest == request && i < int(m_detail.members.size())
                        && m_detail.members[i].member == member && m_detail.members[i].input == input)
                        m_members->setCurrentCell(i, 0);
                });
                break;
            }
    };
    m_graphStatus = new QLabel(graphPanel);
    m_graphStatus->setObjectName("SclRelationGraphStatus");
    m_graphStatus->setWordWrap(true);
    graphLayout->addWidget(m_graphStatus);
    graphLayout->addWidget(graph, 1);
    tabs->addTab(graphPanel, style()->standardIcon(QStyle::SP_ComputerIcon), QStringLiteral("关系图"));
    connect(m_expandGraph, &QPushButton::toggled, this, [this](bool expanded) {
        m_graphExpanded = expanded;
        LoadGraph();
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1)
            QTimer::singleShot(0, this, &SclConnectionView::LoadGraph);
        else
            CancelGraph();
    });

    auto* inspector = new QWidget(tabs);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    m_inspector = new QTextBrowser(inspector);
    m_inspector->setObjectName("SclConnectionInspector");
    inspectorLayout->addWidget(m_inspector);
    tabs->addTab(inspector, QStringLiteral("成员详情"));
    m_endpoints = new QTableWidget(tabs);
    m_endpoints->setObjectName("SclPublicationEndpoints");
    m_endpoints->setColumnCount(6);
    m_endpoints->setHorizontalHeaderLabels({QStringLiteral("SubNetwork"), QStringLiteral("AP"),
        QStringLiteral("APPID"), QStringLiteral("MAC"), QStringLiteral("VLAN-ID"), QStringLiteral("VLAN 优先级")});
    m_endpoints->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_endpoints->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_endpoints->verticalHeader()->hide();
    auto* endpointInteraction = new SclTableInteraction(m_endpoints);
    endpointInteraction->SetEnabled(true);
    connect(endpointInteraction, &SclTableInteraction::OpenSource, this, [this] {
        const auto row = m_endpoints->currentRow();
        if (row >= 0 && row < int(m_detail.endpoints.size()))
            emit OpenEvidence(m_detail.endpoints[row].node);
    });
    tabs->addTab(m_endpoints, QStringLiteral("网络端点"));
    tabs->setTabVisible(tabs->indexOf(m_endpoints), false);
    auto* actions = new QHBoxLayout;
    m_source = new QPushButton(QStringLiteral("发送成员原文"), body);
    connect(memberInteraction, &SclTableInteraction::OpenSource, m_source, &QPushButton::click);
    m_input = new QPushButton(QStringLiteral("接收声明原文"), body);
    m_evidence = new QComboBox(body);
    m_evidence->setObjectName("SclConnectionEvidence");
    m_evidence->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_evidence->setMinimumContentsLength(7);
    auto* evidence = new QPushButton(QStringLiteral("查看所选依据"), body);
    evidence->setObjectName("SclConnectionOpenEvidence");
    actions->addWidget(m_source);
    actions->addWidget(m_input);
    actions->addWidget(m_evidence, 1);
    actions->addWidget(evidence);
    bodyLayout->addLayout(actions);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 1);
    split->setSizes({240, 720});

    m_delay.setSingleShot(true);
    m_delay.setInterval(160);
    connect(&m_delay, &QTimer::timeout, this, &SclConnectionView::Reload);
    connect(m_filter, &QLineEdit::textChanged, this, &SclConnectionView::Schedule);
    connect(m_service, &QComboBox::currentIndexChanged, this, &SclConnectionView::Schedule);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item && item->data(0, Qt::UserRole).isValid())
            SelectConnection(item->data(0, Qt::UserRole).toInt());
        else if (item)
        {
            if (m_selection) m_selection->cancelled = true;
            m_selection.reset();
            ClearDetail();
        }
    });
    connect(m_members, &QTableWidget::currentCellChanged, this, [this](int row) { SelectMember(row); });
    connect(m_graphMember, &QComboBox::currentIndexChanged, this, [this](int row) {
        if (row >= 0) m_members->setCurrentCell(row, 0);
    });
    connect(m_mappedOnly, &QCheckBox::toggled, this, [this] { m_offset = 0; LoadMembers(); });
    connect(m_previous, &QPushButton::clicked, this, [this] {
        m_offset = qMax(0, m_offset - SclConnectionDetail::PageSize); LoadMembers();
    });
    connect(m_next, &QPushButton::clicked, this, [this] {
        m_offset += SclConnectionDetail::PageSize; LoadMembers();
    });
    connect(treeToggle, &QPushButton::toggled, m_tree, &QWidget::setVisible);
    connect(fit, &QPushButton::clicked, this, [this] {
        static_cast<ConnectionGraph*>(m_graph)->Fit();
    });
    connect(zoomIn, &QPushButton::clicked, this, [this] { static_cast<ConnectionGraph*>(m_graph)->Zoom(1.2); });
    connect(zoomOut, &QPushButton::clicked, this, [this] { static_cast<ConnectionGraph*>(m_graph)->Zoom(1/1.2); });
    connect(m_breadcrumb, &QLabel::linkActivated, this, [this](const QString& link) {
        bool ok = false;
        const int node = link.toInt(&ok);
        if (ok) emit ScopeChanged(node);
    });
    connect(evidence, &QPushButton::clicked, this, [this] {
        if (m_evidence->currentIndex() >= 0)
            emit OpenEvidence(m_evidence->currentData().toInt());
    });
    connect(m_source, &QPushButton::clicked, this, [this] {
        const int row = m_members->currentRow();
        if (row >= 0 && row < int(m_detail.members.size()) && m_detail.members[row].member != Scl::NoNode)
            emit OpenEvidence(m_detail.members[row].member);
    });
    connect(m_input, &QPushButton::clicked, this, [this] {
        const int row = m_members->currentRow();
        if (row >= 0 && row < int(m_detail.members.size()) && m_detail.members[row].input != Scl::NoNode)
            emit OpenEvidence(m_detail.members[row].input);
    });
    ClearDetail();
}

SclConnectionView::~SclConnectionView() { Cancel(); }

void SclConnectionView::Cancel()
{
    CancelGraph();
    m_delay.stop();
    if (m_query) m_query->cancelled = true;
    if (m_selection) m_selection->cancelled = true;
    m_query.reset();
    m_selection.reset();
}

void SclConnectionView::ClearDetail()
{
    m_detail = {};
    m_current = -1;
    m_members->setRowCount(0);
    m_endpoints->setRowCount(0);
    m_graph->scene()->clear();
    m_graphMember->clear();
    m_inspector->clear();
    m_source->setEnabled(false);
    m_input->setEnabled(false);
    m_evidence->clear();
    m_previous->setEnabled(false);
    m_next->setEnabled(false);
    m_paging->clear();
    m_breadcrumb->setText(QStringLiteral("选择关系树中的发布流"));
    m_summary->setText(QStringLiteral("从发端看去向，从收端查来源；缺省的源控制块将保留为成员匹配候选。"));
}

void SclConnectionView::SetPublicationMode()
{
    m_publication = true;
    m_expandGraph->setObjectName("SclPublicationExpandGraph");
    m_graphStatus->setObjectName("SclPublicationGraphStatus");
    m_service->hide();
    m_filter->hide();
    m_stats->hide();
    m_tree->hide();
    findChild<QPushButton*>("SclConnectionTreeToggle")->hide();
    m_breadcrumb->hide();
    m_mappedOnly->hide();
    m_input->hide();
    auto* tabs = findChild<QTabWidget*>("SclConnectionModes");
    tabs->setTabVisible(1, false);
    tabs->setTabVisible(tabs->indexOf(m_endpoints), true);
    m_members->setObjectName("SclPublicationMembers");
    for (auto* child : findChildren<QWidget*>())
        if (child->objectName().startsWith(QStringLiteral("SclConnection")))
            child->setObjectName(QStringLiteral("SclPublication") + child->objectName().mid(13));
    m_members->setHorizontalHeaderItem(3, new QTableWidgetItem(QStringLiteral("属性描述")));
    m_members->setHorizontalHeaderItem(4, new QTableWidgetItem(QStringLiteral("FC")));
    m_members->setHorizontalHeaderItem(5, new QTableWidgetItem(QStringLiteral("接收映射")));
    setObjectName("SclPublication");
}

void SclConnectionView::SetPublication(Scl::NodeId control, const std::optional<Location>& restore)
{
    Cancel();
    ClearDetail();
    m_control = control;
    m_restore = restore;
    m_offset = restore ? restore->offset : 0;
    if (control != Scl::NoNode && isVisible())
        LoadMembers();
    else
        m_summary->setText(QStringLiteral("选择发布流查看网络端点和数据集成员。"));
}

void SclConnectionView::SetContext(std::shared_ptr<const SclSnapshot> snapshot, Scl::NodeId scope)
{
    if (m_snapshot == snapshot && m_scope == scope)
        return;
    Cancel();
    if (m_snapshot != snapshot)
    {
        const QSignalBlocker filter(m_filter), service(m_service);
        m_filter->clear();
        m_service->setCurrentIndex(0);
    }
    m_snapshot = std::move(snapshot);
    m_scope = scope;
    m_restore.reset();
    m_list = {};
    m_tree->clear();
    ClearDetail();
    if (m_publication)
    {
        m_control = Scl::NoNode;
        m_summary->setText(QStringLiteral("选择发布流查看网络端点和数据集成员。"));
        return;
    }
    Schedule();
}

void SclConnectionView::SetService(const QString& service)
{
    m_service->setCurrentIndex(qMax(0, m_service->findData(service)));
}

void SclConnectionView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_publication)
    {
        LoadMembers();
        return;
    }
    if (m_dirty)
    {
        const auto restore = m_restore;
        Schedule();
        m_restore = restore;
    }
}

void SclConnectionView::hideEvent(QHideEvent* event)
{
    m_restore = CurrentLocation();
    Cancel();
    m_dirty = true;
    QWidget::hideEvent(event);
}

void SclConnectionView::Schedule()
{
    Cancel();
    m_dirty = true;
    m_restore.reset();
    m_tree->clear();
    ClearDetail();
    m_stats->setText(m_snapshot ? QStringLiteral("正在整理当前 IED 的收发关系…") : QStringLiteral("打开 SCL 后查看收发关系"));
    if (isVisible() && m_snapshot) m_delay.start();
}

void SclConnectionView::Reload()
{
    if (!m_snapshot) return;
    const auto snapshot = m_snapshot;
    const auto scope = m_scope;
    const auto service = m_service->currentData().toString();
    const auto filter = m_filter->text().trimmed();
    const auto task = m_query = std::make_shared<Scl::TaskControl>();
    auto* watcher = new QFutureWatcher<std::optional<SclConnectionList>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, snapshot, task, watcher] {
        watcher->deleteLater();
        if (m_query != task || m_snapshot != snapshot) return;
        auto result = watcher->future().takeResult();
        if (!result) { m_stats->setText(QStringLiteral("关系查询失败，请重试或重载")); return; }
        m_list = std::move(*result);
        m_dirty = false;
        m_stats->setText(m_list.statistics);
        QTreeWidgetItem* selected = nullptr;
        std::map<std::pair<bool, QString>, QTreeWidgetItem*> groups;
        auto* outgoing = new QTreeWidgetItem(m_tree, {QStringLiteral("↑ 发送到 / 发布配置")});
        auto* incoming = new QTreeWidgetItem(m_tree, {QStringLiteral("↓ 接收自 / 外部输入")});
        outgoing->setIcon(0, style()->standardIcon(QStyle::SP_ArrowUp));
        incoming->setIcon(0, style()->standardIcon(QStyle::SP_ArrowDown));
        for (int i = 0; i < int(m_list.rows.size()); ++i)
        {
            const auto& connection = m_list.rows[i];
            const bool receive = m_scope != Scl::NoNode && connection.receiver == m_scope && connection.sender != m_scope;
            const auto peer = receive ? connection.sourceName : connection.targetName;
            auto*& group = groups[{receive, peer}];
            if (!group)
            {
                group = new QTreeWidgetItem(receive ? incoming : outgoing, {peer});
                group->setIcon(0, style()->standardIcon(QStyle::SP_ComputerIcon));
            }
            const auto label = connection.service + QStringLiteral(" · ")
                + (connection.controlName.isEmpty() ? QStringLiteral("未定位发布流") : connection.controlName.section('/', -1))
                + QStringLiteral("  [%1 / %2]").arg(connection.bound).arg(connection.candidates);
            auto* item = new QTreeWidgetItem(group, {label});
            item->setData(0, Qt::UserRole, i);
            item->setToolTip(0, (connection.sourceName + QStringLiteral(" → ") + connection.targetName + '\n' + connection.addresses).toHtmlEscaped());
            // Text and icons carry the meaning as well as the service color.
            item->setForeground(0, palette().color(QPalette::Base).lightness() > 128
                ? ServiceColor(connection.service) : palette().color(QPalette::Text));
            item->setIcon(0, style()->standardIcon(connection.service == "SV" ? QStyle::SP_DriveNetIcon : QStyle::SP_FileLinkIcon));
            if (!selected) selected = item;
            if (m_restore && connection.control == m_restore->control && connection.receiver == m_restore->receiver
                && connection.unresolvedInput == m_restore->input)
                selected = item;
        }
        m_tree->expandToDepth(1);
        if (!outgoing->childCount()) outgoing->setHidden(true);
        if (!incoming->childCount()) incoming->setHidden(true);
        if (selected) m_tree->setCurrentItem(selected);
        else m_summary->setText(QStringLiteral("当前范围没有匹配关系。可清除筛选、切换服务或返回整个工程。"));
    });
    watcher->setFuture(QtConcurrent::run([snapshot, scope, service, filter, task]() -> std::optional<SclConnectionList> {
        try { return QuerySclConnections(*snapshot, scope, service, filter, task.get()); }
        catch (const Scl::TaskCancelled&) { return {}; }
        catch (const std::exception&) { return {}; }
    }));
}

void SclConnectionView::SelectConnection(int index)
{
    if (index < 0 || index >= int(m_list.rows.size())) return;
    m_current = index;
    m_offset = m_restore ? m_restore->offset : 0;
    LoadMembers();
}

void SclConnectionView::LoadMembers()
{
    if (!m_snapshot || (m_publication ? m_control == Scl::NoNode
        : m_current < 0 || m_current >= int(m_list.rows.size()))) return;
    CancelGraph();
    if (m_selection) m_selection->cancelled = true;
    const auto snapshot = m_snapshot;
    const auto connection = m_publication ? SclConnection{} : m_list.rows[m_current];
    const auto control = m_control;
    const bool publication = m_publication;
    const auto offset = m_offset;
    const bool mapped = m_mappedOnly->isChecked();
    const auto task = m_selection = std::make_shared<Scl::TaskControl>();
    m_detail = {};
    m_members->setRowCount(0);
    m_endpoints->setRowCount(0);
    m_graphMember->clear();
    m_source->setEnabled(false);
    m_input->setEnabled(false);
    m_graph->scene()->clear();
    m_inspector->clear();
    m_evidence->clear();
    m_previous->setEnabled(false);
    m_next->setEnabled(false);
    m_summary->setText(QStringLiteral("正在整理成员与接收内部地址…"));
    auto* watcher = new QFutureWatcher<std::optional<SclConnectionDetail>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, snapshot, task, connection] {
        watcher->deleteLater();
        if (m_selection != task || m_snapshot != snapshot) return;
        auto result = watcher->future().takeResult();
        if (!result) { m_summary->setText(QStringLiteral("成员查询失败")); return; }
        m_detail = std::move(*result);
        const auto crumb = [](Scl::NodeId id, const QString& label) {
            if (id == Scl::NoNode) return label.toHtmlEscaped();
            return QStringLiteral("<a href=\"%1\">%2</a>").arg(id).arg(label.toHtmlEscaped());
        };
        m_breadcrumb->setText(QStringLiteral("<a href=\"-1\">工程</a> / ")
            + crumb(connection.sender, connection.sourceName) + QStringLiteral(" → ")
            + crumb(connection.receiver, connection.targetName));
        m_summary->setText(m_detail.description);
        if (m_publication)
        {
            if (m_detail.endpoints.empty())
                m_summary->setText(m_summary->text() + QStringLiteral("\n网络端点未解析。"));
            if (m_detail.endpointsLimited)
                m_summary->setText(m_summary->text() + QStringLiteral("\n端点超过展示上限，完整配置见原文。"));
            m_endpoints->setRowCount(int(m_detail.endpoints.size()));
            for (int row = 0; row < m_endpoints->rowCount(); ++row)
                for (int col = 0; col < m_endpoints->columnCount(); ++col)
                {
                    const auto& value = m_detail.endpoints[row].cells[col];
                    auto* cell = new QTableWidgetItem(value.size() > SclTableInteraction::PreviewLimit
                        ? tr("完整值 %1 字符（右键查看）").arg(value.size())
                        : value.isEmpty() ? QStringLiteral("未声明") : value);
                    cell->setData(SclTableInteraction::FullValueRole, value);
                    m_endpoints->setItem(row, col, cell);
                }
        }
        for (const auto& item : m_detail.evidence)
            m_evidence->addItem(item.label, item.node);
        m_members->setRowCount(int(m_detail.members.size()));
        for (int row = 0; row < int(m_detail.members.size()); ++row)
        {
            const auto& member = m_detail.members[row];
            for (int col = 0; col < member.cells.size(); ++col)
            {
                const auto& value = member.cells[col];
                const auto display = value.size() > SclTableInteraction::PreviewLimit
                    ? tr("完整值 %1 字符（右键查看）").arg(value.size()) : value;
                auto* cell = new QTableWidgetItem(display);
                cell->setData(SclTableInteraction::FullValueRole, value);
                cell->setToolTip(display.toHtmlEscaped());
                if (col == 5 && member.candidate)
                    cell->setIcon(style()->standardIcon(QStyle::SP_MessageBoxQuestion));
                m_members->setItem(row, col, cell);
            }
            const QSignalBlocker blocker(m_graphMember);
            m_graphMember->addItem(member.cells.value(0) + QStringLiteral(" · ") + member.cells.value(1)
                + QStringLiteral(" → ") + member.cells.value(4));
        }
        m_paging->setText(QStringLiteral("%1–%2 / %3 行").arg(m_detail.total ? m_offset + 1 : 0)
            .arg(m_offset + m_detail.members.size()).arg(m_detail.total));
        m_previous->setEnabled(m_offset > 0);
        m_next->setEnabled(m_offset + int(m_detail.members.size()) < m_detail.total);
        int selected = m_restore ? m_restore->member : 0;
        if (m_publication && m_restore && m_restore->memberNode != Scl::NoNode)
        {
            const auto found = std::find_if(m_detail.members.cbegin(), m_detail.members.cend(), [this](const auto& row) {
                return row.member == m_restore->memberNode;
            });
            selected = found == m_detail.members.cend() ? 0 : int(found - m_detail.members.cbegin());
        }
        m_restore.reset();
        if (!m_detail.members.empty()) m_members->setCurrentCell(qBound(0, selected, int(m_detail.members.size()) - 1), 0);
        else SelectMember(-1);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, connection, control, publication, offset, mapped, task]() -> std::optional<SclConnectionDetail> {
        try { return publication ? DescribeSclPublication(*snapshot, control, offset, task.get())
            : DescribeSclConnection(*snapshot, connection, offset, mapped, task.get()); }
        catch (const Scl::TaskCancelled&) { return {}; }
        catch (const std::exception&) { return {}; }
    }));
}

void SclConnectionView::SelectMember(int row)
{
    if (row < 0 || row >= int(m_detail.members.size()) || (!m_publication && m_current < 0)) return;
    const auto& member = m_detail.members[row];
    { const QSignalBlocker blocker(m_graphMember); m_graphMember->setCurrentIndex(row); }
    m_source->setEnabled(member.member != Scl::NoNode);
    m_input->setEnabled(member.input != Scl::NoNode);
    m_evidence->clear();
    for (const auto& item : m_detail.evidence)
        m_evidence->addItem(item.label, item.node);
    QStringList descriptions;
    for (const auto& field : member.description.fields)
    {
        const auto label = SclDescriptionLabel(field.source);
        descriptions << label + QStringLiteral("  ") + SclDescriptionText(field).left(512);
        if (field.node != Scl::NoNode)
            m_evidence->addItem(label, field.node);
        for (int i = 0; i < field.candidates.size(); ++i)
            m_evidence->addItem(label + QStringLiteral(" · 候选 %1").arg(i + 1), field.candidates[i]);
    }
    if (m_publication)
    {
        for (const auto& item : member.reception)
            m_evidence->addItem(item.label, item.node);
        m_inspector->setPlainText(member.cells.value(1) + '\n' + descriptions.join('\n')
            + QStringLiteral("\n%1\nMMS Domain（推导） · %2\nMMS Item（推导） · %3\n数组索引 · %4")
                .arg(member.cells.value(5), member.mmsDomain.isEmpty() ? QStringLiteral("未确认") : member.mmsDomain,
                    member.mmsItem.isEmpty() ? QStringLiteral("未确认") : member.mmsItem,
                    member.arrayIndex < 0 ? QStringLiteral("未指定") : QString::number(member.arrayIndex)));
        return;
    }
    const auto& connection = m_list.rows[m_current];
    m_inspector->setPlainText(QStringLiteral("发送装置：%1\n发送成员：%2\n接收装置：%3\n内部地址：%4\n证据：%5\n\n%6\n\n%7")
        .arg(connection.sourceName, member.cells.value(1), member.cells.value(3),
            member.cells.value(4), member.cells.value(5), descriptions.join('\n'), m_detail.description));
    LoadGraph();
}

void SclConnectionView::CancelGraph()
{
    if (m_graphRequest)
        m_graphRequest->cancelled = true;
    m_graphRequest.reset();
    if (m_graph)
        m_graph->scene()->clear();
    m_graphStatus->clear();
}

void SclConnectionView::LoadGraph()
{
    CancelGraph();
    const int row = m_members->currentRow();
    const auto* modes = findChild<QTabWidget*>("SclConnectionModes");
    if (m_publication || !m_snapshot || !isVisible() || !modes || modes->currentIndex() != 1 || m_current < 0
        || row < 0 || row >= int(m_detail.members.size()))
        return;
    m_graphStatus->setText(QStringLiteral("正在整理关系…"));
    const auto snapshot = m_snapshot;
    const auto connection = m_list.rows[m_current];
    const auto member = m_detail.members[row];
    const auto task = m_graphRequest = std::make_shared<Scl::TaskControl>();
    auto* watcher = new QFutureWatcher<std::optional<SclRelationGraph>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, snapshot, task, service = connection.service] {
        watcher->deleteLater();
        if (m_graphRequest != task || m_snapshot != snapshot)
            return;
        const auto graph = watcher->future().takeResult();
        if (!graph)
        {
            m_graphStatus->setText(QStringLiteral("无法生成当前关系图。"));
            return;
        }
        m_graphStatus->setText(QStringLiteral("%1 个节点 · %2 条关系 · 隐藏映射 %3%4")
            .arg(graph->nodes.size()).arg(graph->edges.size()).arg(graph->hiddenMappings)
            .arg(graph->limited ? QStringLiteral(" · 达到展示上限") : QString{}));
        m_expandGraph->setText(m_graphExpanded ? QStringLiteral("收起扩展") : QStringLiteral("展开更多"));
        auto* scene = m_graph->scene();
        const auto base = palette().color(QPalette::Base);
        const auto ink = palette().color(QPalette::Text);
        const auto color = base.lightness() > 128 ? ServiceColor(service) : ink;
        QList<QPointF> positions;
        int layers[5]{};
        for (const auto& node : graph->nodes)
        {
            const QPointF position(node.layer * 255, layers[node.layer]++ * 98);
            positions.push_back(position);
            auto* card = scene->addRect(QRectF(position, QSizeF(200, 75)), QPen(color), base);
            card->setZValue(1);
            card->setData(0, node.evidence);
            if (node.member != Scl::NoNode)
            {
                card->setData(1, node.member);
                card->setData(2, node.input);
            }
            const auto role = [kind = node.role] {
                switch (kind)
                {
                case SclGraphRole::Sender: return QStringLiteral("发送 IED");
                case SclGraphRole::Publication: return QStringLiteral("发布流");
                case SclGraphRole::Member: return QStringLiteral("数据集成员");
                case SclGraphRole::Input: return QStringLiteral("接收声明");
                case SclGraphRole::Receiver: return QStringLiteral("接收 IED");
                case SclGraphRole::Definition: return QStringLiteral("描述依据");
                }
                return QString{};
            }();
            card->setToolTip((role + '\n' + node.label).toHtmlEscaped());
            auto* label = new QGraphicsTextItem(card);
            label->setDefaultTextColor(ink);
            label->setPos(position + QPointF(5, 3));
            label->setTextWidth(190);
            label->setPlainText(role + '\n' + node.label.simplified().left(64));
        }
        for (const auto& edge : graph->edges)
        {
            const auto from = positions[edge.from] + QPointF(200, 37);
            const auto to = positions[edge.to] + QPointF(0, 37);
            const bool candidate = edge.relation == SclGraphRelation::Candidate
                || edge.relation == SclGraphRelation::ContainmentCandidate;
            const bool bound = edge.relation == SclGraphRelation::Bound;
            const QPen pen(color, bound ? 2 : 1, candidate ? Qt::DashLine
                : bound ? Qt::SolidLine : Qt::DotLine);
            auto* line = scene->addLine(QLineF(from, to), pen);
            line->setData(0, edge.evidence);
            const auto delta = to - from;
            const auto length = std::hypot(delta.x(), delta.y());
            if (length > 0)
            {
                const auto axis = delta / length;
                const QPointF normal(-axis.y(), axis.x());
                scene->addPolygon(QPolygonF{to, to - 9 * axis + 4 * normal, to - 9 * axis - 4 * normal},
                    Qt::NoPen, color)->setData(0, edge.evidence);
            }
            if (candidate)
                line->setToolTip(edge.relation == SclGraphRelation::ContainmentCandidate
                    ? QStringLiteral("对象包含候选") : QStringLiteral("成员匹配候选"));
            else if (bound)
                line->setToolTip(QStringLiteral("明确绑定"));
        }
        scene->setSceneRect(scene->itemsBoundingRect().adjusted(-12, -12, 12, 12));
        static_cast<ConnectionGraph*>(m_graph)->Fit();
    });
    const int depth = m_graphExpanded ? 1 : 0;
    watcher->setFuture(QtConcurrent::run([snapshot, connection, member, depth, task]() -> std::optional<SclRelationGraph> {
        try { return ProjectSclRelation(*snapshot, connection, member, depth, task.get()); }
        catch (const Scl::TaskCancelled&) { return {}; }
        catch (const std::exception&) { return {}; }
    }));
}
SclConnectionView::Location SclConnectionView::CurrentLocation() const
{
    Location location;
    if (m_current >= 0 && m_current < int(m_list.rows.size()))
    {
        const auto& row = m_list.rows[m_current];
        location.control = row.control; location.receiver = row.receiver; location.input = row.unresolvedInput;
    }
    location.filter = m_filter->text(); location.service = m_service->currentData().toString();
    location.offset = m_offset; location.member = m_members->currentRow();
    location.mappedOnly = m_mappedOnly->isChecked();
    if (m_publication)
    {
        location.control = m_control;
        if (location.member >= 0 && location.member < int(m_detail.members.size()))
            location.memberNode = m_detail.members[location.member].member;
    }
    return location;
}

void SclConnectionView::RestoreLocation(const Location& location)
{
    { const QSignalBlocker filter(m_filter), service(m_service), mapped(m_mappedOnly);
      m_filter->setText(location.filter); SetService(location.service); m_mappedOnly->setChecked(location.mappedOnly); }
    Schedule();
    m_restore = location;
}
}
