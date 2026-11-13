#include "SclConnectionView.hpp"

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
#include <QShortcut>
#include <QSplitter>
#include <QStyle>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>
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
        if (transform().m11() * factor < .25 || transform().m11() * factor > 3.) return;
        m_autoFit = false;
        scale(factor, factor);
    }
protected:
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
        QStringLiteral("描述"), QStringLiteral("接收 IED"), QStringLiteral("接收内部地址 intAddr"), QStringLiteral("证据")});
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
    auto* copy = new QShortcut(QKeySequence::Copy, m_members);
    connect(copy, &QShortcut::activated, this, [this] {
        QStringList cells;
        for (auto* item : m_members->selectedItems()) cells << item->text();
        QApplication::clipboard()->setText(cells.join('\t'));
    });
    tabs->addTab(mapping, style()->standardIcon(QStyle::SP_FileDialogDetailedView), QStringLiteral("成员映射"));

    auto* graphPanel = new QWidget(tabs);
    auto* graphLayout = new QVBoxLayout(graphPanel);
    auto* graphActions = new QHBoxLayout;
    auto* legend = new QLabel(QStringLiteral("实线：源控制块绑定  ·  虚线：候选  ·  双击节点：原文"), graphPanel);
    legend->setWordWrap(true);
    graphActions->addWidget(legend, 1);
    auto* fit = new QPushButton(QStringLiteral("适应"), graphPanel);
    auto* zoomIn = new QPushButton(QStringLiteral("放大 +"), graphPanel);
    auto* zoomOut = new QPushButton(QStringLiteral("缩小 −"), graphPanel);
    graphActions->addWidget(zoomOut);
    graphActions->addWidget(zoomIn);
    graphActions->addWidget(fit);
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
    graphLayout->addWidget(graph, 1);
    tabs->addTab(graphPanel, style()->standardIcon(QStyle::SP_ComputerIcon), QStringLiteral("关系图"));

    auto* inspector = new QWidget(tabs);
    auto* inspectorLayout = new QVBoxLayout(inspector);
    m_inspector = new QTextBrowser(inspector);
    m_inspector->setObjectName("SclConnectionInspector");
    inspectorLayout->addWidget(m_inspector);
    tabs->addTab(inspector, QStringLiteral("成员详情"));
    auto* actions = new QHBoxLayout;
    m_source = new QPushButton(QStringLiteral("发送成员原文"), body);
    m_input = new QPushButton(QStringLiteral("接收声明原文"), body);
    m_evidence = new QComboBox(body);
    m_evidence->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_evidence->setMinimumContentsLength(7);
    auto* evidence = new QPushButton(QStringLiteral("配置依据"), body);
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
    Schedule();
}

void SclConnectionView::SetService(const QString& service)
{
    m_service->setCurrentIndex(qMax(0, m_service->findData(service)));
}

void SclConnectionView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
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
    if (m_current < 0 || m_current >= int(m_list.rows.size()) || !m_snapshot) return;
    if (m_selection) m_selection->cancelled = true;
    const auto snapshot = m_snapshot;
    const auto connection = m_list.rows[m_current];
    const auto offset = m_offset;
    const bool mapped = m_mappedOnly->isChecked();
    const auto task = m_selection = std::make_shared<Scl::TaskControl>();
    m_detail = {};
    m_members->setRowCount(0);
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
        for (const auto& item : m_detail.evidence) m_evidence->addItem(item.label, item.node);
        m_members->setRowCount(int(m_detail.members.size()));
        for (int row = 0; row < int(m_detail.members.size()); ++row)
        {
            const auto& member = m_detail.members[row];
            for (int col = 0; col < member.cells.size(); ++col)
            {
                auto* cell = new QTableWidgetItem(member.cells[col]);
                cell->setToolTip(member.cells[col].toHtmlEscaped());
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
        const int selected = m_restore ? m_restore->member : 0;
        m_restore.reset();
        if (!m_detail.members.empty()) m_members->setCurrentCell(qBound(0, selected, int(m_detail.members.size()) - 1), 0);
        else SelectMember(-1);
    });
    watcher->setFuture(QtConcurrent::run([snapshot, connection, offset, mapped, task]() -> std::optional<SclConnectionDetail> {
        try { return DescribeSclConnection(*snapshot, connection, offset, mapped, task.get()); }
        catch (const Scl::TaskCancelled&) { return {}; }
        catch (const std::exception&) { return {}; }
    }));
}

void SclConnectionView::SelectMember(int row)
{
    if (row < 0 || row >= int(m_detail.members.size()) || m_current < 0) return;
    const auto& member = m_detail.members[row];
    const auto& connection = m_list.rows[m_current];
    { const QSignalBlocker blocker(m_graphMember); m_graphMember->setCurrentIndex(row); }
    m_source->setEnabled(member.member != Scl::NoNode);
    m_input->setEnabled(member.input != Scl::NoNode);
    m_inspector->setPlainText(QStringLiteral("发送装置：%1\n发送成员：%2\n描述：%3\n接收装置：%4\n内部地址：%5\n证据：%6\n\n%7")
        .arg(connection.sourceName, member.cells.value(1), member.cells.value(2), member.cells.value(3),
            member.cells.value(4), member.cells.value(5), m_detail.description));
    auto* scene = m_graph->scene();
    scene->clear();
    const auto base = palette().color(QPalette::Base);
    const auto color = base.lightness() > 128 ? ServiceColor(connection.service) : palette().color(QPalette::Text);
    const auto ink = palette().color(QPalette::Text);
    const auto card = [&](int x, const QString& heading, const QString& body, int node) {
        auto* box = scene->addRect(x, 12, 260, 145, QPen(color, 2), base);
        box->setData(0, node);
        box->setFlag(QGraphicsItem::ItemIsSelectable);
        box->setToolTip((heading + '\n' + body).toHtmlEscaped());
        auto* title = new QGraphicsTextItem(box);
        title->setDefaultTextColor(ink);
        auto font = title->font(); font.setBold(true); title->setFont(font);
        title->setPlainText(heading.left(40));
        title->setPos(x + 12, 22);
        title->setTextWidth(234);
        auto* text = new QGraphicsTextItem(box);
        text->setDefaultTextColor(ink);
        text->setPlainText(body.left(120));
        text->setTextWidth(234);
        text->setPos(x + 12, 55);
    };
    card(0, QStringLiteral("↑ 发端 · ") + connection.sourceName, member.cells.value(1), member.member);
    card(320, connection.service + QStringLiteral(" · 发布流"), connection.identifier + '\n' + connection.addresses, connection.control);
    card(640, QStringLiteral("↓ 收端 · ") + connection.targetName,
        member.cells.value(4).isEmpty() ? QStringLiteral("本文件未见该成员的接收内部地址") : member.cells.value(4), member.input);
    for (int x : {260, 580})
    {
        QPainterPath path;
        path.moveTo(x, 85); path.lineTo(x + 60, 85);
        const bool uncertain = (x == 580 && (member.candidate || member.input == Scl::NoNode))
            || connection.control == Scl::NoNode;
        QPen pen(color, 2, uncertain ? Qt::DashLine : Qt::SolidLine);
        scene->addPath(path, pen);
        scene->addPolygon(QPolygonF({QPointF(x + 60, 85), QPointF(x + 51, 80), QPointF(x + 51, 90)}), QPen(color), color);
    }
    auto* status = scene->addText(member.cells.value(5));
    status->setDefaultTextColor(ink);
    status->setPos(320, 175);
    scene->setSceneRect(-15, 0, 930, 230);
    static_cast<ConnectionGraph*>(m_graph)->Fit();
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
