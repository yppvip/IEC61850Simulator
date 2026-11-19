#include "MmsPrototypeWindow.hpp"
#include "SclTableInteraction.hpp"

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTableView>
#include <QTextBlock>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <DockManager.h>
#include <DockWidget.h>

namespace iec61850sim::prototype {
class PrototypeTableModel final : public QAbstractTableModel {
public:
    struct Row { QString id; QList<Cell> cells; QString evidence; };
    explicit PrototypeTableModel(QObject* parent) : QAbstractTableModel(parent) {}
    void Reset(QStringList headers, QList<Row> rows, QString snapshot) {
        beginResetModel(); m_headers = std::move(headers); m_rows = std::move(rows); m_snapshot = std::move(snapshot); endResetModel();
    }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_rows.size(); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_headers.size(); }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        return orientation == Qt::Horizontal && role == Qt::DisplayRole ? m_headers.value(section) : QVariant{};
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= m_rows.size()) return {};
        const auto& row = m_rows[index.row()];
        if (role == Qt::UserRole) return row.id;
        if (role == Qt::UserRole + 1) return m_snapshot;
        if (role == Qt::UserRole + 2) return row.evidence;
        if (index.column() >= row.cells.size()) return {};
        const auto& cell = row.cells[index.column()];
        if (role == SclTableInteraction::FullValueRole) return cell.value;
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole) {
            if (cell.display.size() > SclTableInteraction::PreviewLimit)
                return QStringLiteral("完整值 %1 字符；右键查看或双击复制").arg(cell.display.size());
            return cell.display;
        }
        return {};
    }
private:
    QStringList m_headers;
    QList<Row> m_rows;
    QString m_snapshot;
};

namespace {
const QList<Page> Tasks{Page::Overview, Page::Reports, Page::Signals, Page::Clients, Page::Connections, Page::Checks, Page::Advanced};
QStringList Headers(Page page)
{
    switch (page) {
    case Page::Reports: case Page::Advanced: return {QStringLiteral("报告定义"), QStringLiteral("说明"), QStringLiteral("报告类型"), QStringLiteral("数据集"), QStringLiteral("成员数"), QStringLiteral("配置状态"), QStringLiteral("原始 rptID"), QStringLiteral("定义引用（派生）")};
    case Page::Members: return {QStringLiteral("原始序号"), QStringLiteral("对象"), QStringLiteral("对象说明"), QStringLiteral("属性"), "FC", QStringLiteral("引用状态"), QStringLiteral("对象引用"), "MMS Item"};
    case Page::Signals: return {QStringLiteral("对象"), QStringLiteral("对象说明"), QStringLiteral("数据集使用"), QStringLiteral("对象状态"), QStringLiteral("对象引用")};
    case Page::Attributes: return {QStringLiteral("属性"), QStringLiteral("所属对象说明"), "FC", QStringLiteral("类型"), QStringLiteral("文件声明值"), QStringLiteral("解释状态"), "MMS Domain", "MMS Item"};
    case Page::Clients: return {QStringLiteral("客户端"), QStringLiteral("声明种类"), QStringLiteral("报告定义"), QStringLiteral("证据状态"), QStringLiteral("目标 IED"), QStringLiteral("实例关联")};
    case Page::Connections: return {QStringLiteral("访问点"), "IP", QStringLiteral("端口"), QStringLiteral("配置状态"), QStringLiteral("服务器位置"), QStringLiteral("子网络")};
    case Page::Checks: return {QStringLiteral("规则"), QStringLiteral("问题或覆盖说明"), QStringLiteral("状态"), QStringLiteral("关联对象")};
    case Page::Search: return {QStringLiteral("对象类别"), QStringLiteral("匹配对象"), QStringLiteral("所属 IED"), QStringLiteral("匹配内容（合成）")};
    case Page::Overview: return {};
    }
    return {};
}
QLabel* Text(const QString& value, QWidget* parent)
{
    auto* label = new QLabel(value, parent);
    label->setTextFormat(Qt::PlainText); label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}
void ConfigureTable(QTableView* table)
{
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors(true);
    table->setShowGrid(false);
    table->verticalHeader()->hide();
    table->horizontalHeader()->setMinimumSectionSize(64);
    table->horizontalHeader()->setStretchLastSection(true);
    table->setMinimumSize(0, 0);
    table->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
}
}

MmsPrototypeWindow::MmsPrototypeWindow(QWidget* parent) : QMainWindow(parent), m_session(new MmsPrototypeSession(this))
{
    setObjectName("MmsPrototypeWindow");
    setWindowTitle(QStringLiteral("MMS 查阅 · 合成数据交互原型 T6.1"));
    resize(1280, 800);
    auto* root = new QWidget(this);
    auto* layout = new QVBoxLayout(root); layout->setContentsMargins(8, 8, 8, 8);
    setCentralWidget(root);
    auto* banner = Text(QStringLiteral("Demo.scd · 合成示例    离线交互原型 · 未接入工程文件或设备"), root);
    banner->setObjectName("PrototypeBanner");
    auto font = banner->font(); font.setBold(true); banner->setFont(font);
    layout->addWidget(banner);
    auto* project = new QHBoxLayout;
    project->addWidget(new QLabel(QStringLiteral("范围"), root));
    m_scope = new QComboBox(root); m_scope->setObjectName("PrototypeScope");
    m_scope->setAccessibleName(QStringLiteral("当前 IED 范围"));
    m_scope->addItem(QStringLiteral("整个工程"), ""); m_scope->addItem("DemoIED", "DemoIED"); m_scope->addItem("EmptyIED", "EmptyIED");
    project->addWidget(m_scope);
    m_global = new QLineEdit(root); m_global->setObjectName("PrototypeSearch");
    m_global->setPlaceholderText(QStringLiteral("全局搜索（整个工程）：报告、地址、说明"));
    m_global->setAccessibleName(QStringLiteral("搜索整个合成工程"));
    project->addWidget(m_global, 1);
    auto* search = new QPushButton(QStringLiteral("搜索"), root); search->setObjectName("PrototypeSearchAction"); project->addWidget(search);
    layout->addLayout(project);
    auto* navigation = new QHBoxLayout;
    m_back = new QPushButton(QStringLiteral("返回"), root); m_back->setObjectName("PrototypeBack"); m_back->setToolTip("Alt+Left");
    m_forward = new QPushButton(QStringLiteral("前进"), root); m_forward->setObjectName("PrototypeForward"); m_forward->setToolTip("Alt+Right");
    navigation->addWidget(m_back); navigation->addWidget(m_forward);
    m_taskPicker = new QComboBox(root); m_taskPicker->setObjectName("PrototypeTaskPicker");
    m_taskPicker->setAccessibleName(QStringLiteral("任务导航（窄窗）"));
    for (auto task : Tasks) m_taskPicker->addItem(MmsPrototypeSession::PageName(task), int(task));
    navigation->addWidget(m_taskPicker);
    navigation->addStretch();
    m_technical = new QCheckBox(QStringLiteral("技术字段"), root); m_technical->setObjectName("PrototypeTechnical"); navigation->addWidget(m_technical);
    m_details = new QCheckBox(QStringLiteral("详情"), root); m_details->setObjectName("PrototypeDetails"); m_details->setChecked(true); navigation->addWidget(m_details);
    auto* reset = new QPushButton(QStringLiteral("重置布局"), root); reset->setObjectName("PrototypeResetLayout"); navigation->addWidget(reset);
    layout->addLayout(navigation);
    m_breadcrumb = Text({}, root); m_breadcrumb->setObjectName("PrototypeBreadcrumb"); layout->addWidget(m_breadcrumb);
    m_docks = new ads::CDockManager(root); layout->addWidget(m_docks, 1);
    auto* content = new QWidget(m_docks);
    auto* contentLayout = new QVBoxLayout(content); contentLayout->setContentsMargins(8, 8, 8, 8);
    m_summary = Text({}, content); m_summary->setObjectName("PrototypeSummary"); contentLayout->addWidget(m_summary);
    m_filter = new QLineEdit(content); m_filter->setObjectName("PrototypeFilter");
    m_filter->setPlaceholderText(QStringLiteral("筛选当前列表（遵守当前范围）")); m_filter->setAccessibleName(QStringLiteral("筛选当前列表")); contentLayout->addWidget(m_filter);
    auto* actions = new QHBoxLayout;
    m_drill = new QPushButton(content); m_drill->setObjectName("PrototypeDrill"); actions->addWidget(m_drill);
    m_clients = new QPushButton(QStringLiteral("客户端配置"), content); m_clients->setObjectName("PrototypeClients"); actions->addWidget(m_clients);
    m_sourceAction = new QPushButton(QStringLiteral("查看原文"), content); m_sourceAction->setObjectName("PrototypeSourceAction"); actions->addWidget(m_sourceAction);
    actions->addStretch(); contentLayout->addLayout(actions);
    m_table = new QTableView(content); m_table->setObjectName("PrototypeTable"); m_table->setAccessibleName(QStringLiteral("当前任务数据"));
    m_rows = new PrototypeTableModel(m_table); m_table->setModel(m_rows); ConfigureTable(m_table); contentLayout->addWidget(m_table, 1);
    auto* tableInteraction = new SclTableInteraction(m_table); tableInteraction->SetEnabled(true);
    auto* contentDock = new ads::CDockWidget(QStringLiteral("MMS 查阅"), m_docks); contentDock->setWidget(content);
    contentDock->setObjectName("PrototypeContentDock");
    m_contentDock = contentDock;
    contentDock->setFeature(ads::CDockWidget::DockWidgetClosable, false); m_docks->setCentralWidget(contentDock);
    m_tasks = new QTreeWidget(m_docks); m_tasks->setObjectName("PrototypeTasks"); m_tasks->setHeaderHidden(true);
    m_tasks->setAccessibleName(QStringLiteral("MMS 任务导航")); m_tasks->setMinimumWidth(0);
    for (auto task : Tasks) {
        auto* item = new QTreeWidgetItem(m_tasks, {MmsPrototypeSession::PageName(task)});
        item->setData(0, Qt::UserRole, int(task));
    }
    m_navigationDock = new ads::CDockWidget(QStringLiteral("任务导航"), m_docks); m_navigationDock->setWidget(m_tasks);
    m_navigationDock->setFeature(ads::CDockWidget::DockWidgetClosable, false); m_docks->addDockWidget(ads::LeftDockWidgetArea, m_navigationDock);
    m_detailStack = new QStackedWidget(m_docks); m_detailStack->setObjectName("PrototypeDetailStack");
    auto* inspector = new QWidget(m_detailStack); auto* inspectorLayout = new QVBoxLayout(inspector); inspectorLayout->setContentsMargins(8, 8, 8, 8);
    m_detailTitle = Text({}, inspector); m_detailTitle->setObjectName("PrototypeDetailTitle"); inspectorLayout->addWidget(m_detailTitle);
    m_fields = new QTableView(inspector); m_fields->setObjectName("PrototypeFields"); m_fields->setAccessibleName(QStringLiteral("所选对象属性与依据"));
    m_fieldRows = new PrototypeTableModel(m_fields); m_fields->setModel(m_fieldRows); ConfigureTable(m_fields); inspectorLayout->addWidget(m_fields, 1);
    auto* fieldInteraction = new SclTableInteraction(m_fields); fieldInteraction->SetEnabled(true);
    m_fieldSource = new QPushButton(QStringLiteral("查看所选字段原文"), inspector); m_fieldSource->setObjectName("PrototypeFieldSource"); inspectorLayout->addWidget(m_fieldSource, 0, Qt::AlignLeft);
    m_detailStack->addWidget(inspector);
    auto* sourcePage = new QWidget(m_detailStack); auto* sourceLayout = new QVBoxLayout(sourcePage); sourceLayout->setContentsMargins(8, 8, 8, 8);
    m_sourceTitle = Text({}, sourcePage); m_sourceTitle->setObjectName("PrototypeSourceTitle"); sourceLayout->addWidget(m_sourceTitle);
    auto* returnSource = new QPushButton(QStringLiteral("返回所选对象"), sourcePage); returnSource->setObjectName("PrototypeReturnSource"); sourceLayout->addWidget(returnSource, 0, Qt::AlignLeft);
    m_sourceLines = Text({}, sourcePage); m_sourceLines->setObjectName("PrototypeSourceLines"); sourceLayout->addWidget(m_sourceLines);
    m_source = new QPlainTextEdit(sourcePage); m_source->setObjectName("PrototypeSource"); m_source->setReadOnly(true); sourceLayout->addWidget(m_source, 1);
    m_source->setMinimumSize(0, 0); m_source->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_detailStack->addWidget(sourcePage);
    m_detailsDock = new ads::CDockWidget(QStringLiteral("属性与依据"), m_docks); m_detailsDock->setWidget(m_detailStack);
    m_detailsDock->setObjectName("PrototypeDetailsDock");
    m_docks->addDockWidget(ads::BottomDockWidgetArea, m_detailsDock, contentDock->dockAreaWidget());
    for (auto* dock : {m_navigationDock, contentDock, m_detailsDock}) {
        dock->setMinimumSize(0, 0); dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromDockWidget);
    }
    m_docks->setSplitterSizes(m_navigationDock->dockAreaWidget(), {180, 1000});
    m_docks->setSplitterSizes(contentDock->dockAreaWidget(), {350, 230});
    m_defaultLayout = m_docks->saveState(1);
    connect(m_session, &MmsPrototypeSession::Changed, this, &MmsPrototypeWindow::Present);
    connect(m_back, &QPushButton::clicked, m_session, &MmsPrototypeSession::Back);
    connect(m_forward, &QPushButton::clicked, m_session, &MmsPrototypeSession::Forward);
    connect(m_scope, &QComboBox::activated, this, [this] { m_session->SetScope(m_scope->currentData().toString()); });
    connect(m_tasks, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) { if (!m_applying && item) m_session->Navigate(Page(item->data(0, Qt::UserRole).toInt())); });
    connect(m_taskPicker, &QComboBox::activated, this, [this] { m_session->Navigate(Page(m_taskPicker->currentData().toInt())); });
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) { if (!m_applying) m_session->SetFilter(text); });
    const auto runSearch = [this] { m_session->Search(m_global->text()); };
    connect(search, &QPushButton::clicked, this, runSearch); connect(m_global, &QLineEdit::returnPressed, this, runSearch);
    connect(m_table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!m_applying && index.isValid()) m_session->Select(index.data(Qt::UserRole).toString(), index.column());
    });
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        const int top = m_table->rowAt(0);
        if (!m_applying && top >= 0)
            m_session->SetAnchor(m_rows->index(top, 0).data(Qt::UserRole).toString(), -m_table->rowViewportPosition(top));
    });
    connect(m_fields->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& index) {
        if (!m_applying && index.isValid()) {
            m_session->SetField(index.data(Qt::UserRole).toString());
            m_fieldSource->setEnabled(!index.data(Qt::UserRole + 2).toString().isEmpty());
        }
    });
    connect(m_drill, &QPushButton::clicked, m_session, &MmsPrototypeSession::Drill);
    connect(m_clients, &QPushButton::clicked, m_session, &MmsPrototypeSession::ShowClients);
    const auto openSource = [this] { m_session->OpenSource(); };
    connect(m_sourceAction, &QPushButton::clicked, this, openSource); connect(tableInteraction, &SclTableInteraction::OpenSource, this, openSource);
    connect(m_fieldSource, &QPushButton::clicked, this, &MmsPrototypeWindow::OpenFieldSource);
    connect(fieldInteraction, &SclTableInteraction::OpenSource, this, &MmsPrototypeWindow::OpenFieldSource);
    connect(returnSource, &QPushButton::clicked, m_session, &MmsPrototypeSession::CloseSource);
    connect(m_technical, &QCheckBox::toggled, this, [this] { Present(true); });
    connect(m_details, &QCheckBox::toggled, this, [this](bool visible) { m_detailsDock->toggleView(visible); BalanceCompactPanels(); });
    connect(m_detailsDock, &ads::CDockWidget::viewToggled, m_details, &QCheckBox::setChecked);
    connect(reset, &QPushButton::clicked, this, &MmsPrototypeWindow::ResetLayout);
    auto* backKey = new QShortcut(QKeySequence("Alt+Left"), this); connect(backKey, &QShortcut::activated, m_session, &MmsPrototypeSession::Back);
    auto* forwardKey = new QShortcut(QKeySequence("Alt+Right"), this); connect(forwardKey, &QShortcut::activated, m_session, &MmsPrototypeSession::Forward);
    auto* searchKey = new QShortcut(QKeySequence::Find, this); connect(searchKey, &QShortcut::activated, this, [this] { m_global->setFocus(); });
    auto* filterKey = new QShortcut(QKeySequence("Ctrl+L"), this); connect(filterKey, &QShortcut::activated, this, [this] { m_filter->setFocus(); });
    setTabOrder(m_scope, m_global); setTabOrder(m_global, search); setTabOrder(search, m_back); setTabOrder(m_back, m_forward);
    statusBar()->showMessage(QStringLiteral("合成数据 · 单击选择；双击 / Ctrl+C 复制完整单元格；Enter 查看原文"));
    Present(true);
}

MmsPrototypeWindow::~MmsPrototypeWindow()
{
    m_applying = true;
    disconnect(m_details, nullptr, this, nullptr);
    disconnect(m_detailsDock, nullptr, m_details, nullptr);
    delete m_docks;
    m_docks = nullptr;
}
void MmsPrototypeWindow::OpenFieldSource()
{
    const auto index = m_fields->currentIndex();
    const auto key = index.data(Qt::UserRole + 2).toString();
    if (!key.isEmpty()) m_session->OpenSource(key);
}
void MmsPrototypeWindow::ResetLayout()
{
    m_docks->restoreState(m_defaultLayout, 1);
    m_navigationDock->toggleView(!m_narrow);
    m_details->setChecked(true);
    BalanceCompactPanels();
}
void MmsPrototypeWindow::BalanceCompactPanels()
{
    if (height() >= 650 || !m_contentDock || !m_details->isChecked()) return;
    const auto sizes = m_docks->splitterSizes(m_contentDock->dockAreaWidget());
    if (sizes.size() != 2) return;
    const int available = sizes[0] + sizes[1];
    // The ADS central pane stretches; the details pane otherwise keeps the
    // requested pixel size. Use the actual remaining height, not ratio-like constants.
    if (available > 0)
        m_docks->setSplitterSizes(m_contentDock->dockAreaWidget(), {available / 2, available - available / 2});
}
void MmsPrototypeWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (!m_navigationDock) return;
    m_narrow = event->size().width() < 820;
    m_navigationDock->toggleView(!m_narrow); m_taskPicker->setVisible(m_narrow);
    // Apply after the window manager has constrained a high-DPI window to the desktop.
    QTimer::singleShot(0, this, &MmsPrototypeWindow::BalanceCompactPanels);
}

void MmsPrototypeWindow::Present(bool rowsChanged)
{
    m_applying = true;
    const auto& location = m_session->Current();
    const auto* selected = m_session->Selected();
    const auto visible = m_session->Rows();
    const auto scope = location.ied.isEmpty() ? QStringLiteral("整个工程") : location.ied;
    m_scope->setCurrentIndex(m_scope->findData(location.ied));
    m_filter->setText(location.filter); m_filter->setEnabled(location.page != Page::Search);
    const auto taskPage = location.page == Page::Members ? Page::Reports : location.page == Page::Attributes ? Page::Signals : location.page;
    m_tasks->setCurrentItem(nullptr);
    for (int i = 0; i < m_tasks->topLevelItemCount(); ++i)
        if (Page(m_tasks->topLevelItem(i)->data(0, Qt::UserRole).toInt()) == taskPage) m_tasks->setCurrentItem(m_tasks->topLevelItem(i));
    while (m_taskPicker->count() > Tasks.size()) m_taskPicker->removeItem(m_taskPicker->count() - 1);
    int pickerIndex = m_taskPicker->findData(int(location.page));
    if (pickerIndex < 0) {
        m_taskPicker->addItem(MmsPrototypeSession::PageName(location.page), int(location.page));
        pickerIndex = m_taskPicker->count() - 1;
        static_cast<QStandardItemModel*>(m_taskPicker->model())->item(pickerIndex)->setEnabled(false);
    }
    m_taskPicker->setCurrentIndex(pickerIndex);
    m_back->setEnabled(m_session->CanBack()); m_forward->setEnabled(m_session->CanForward());
    const auto* parent = m_session->Data().Find(location.parent);
    m_breadcrumb->setText(scope + "  /  MMS 查阅  /  " + MmsPrototypeSession::PageName(location.page)
        + (parent ? "  /  " + parent->title : QString{})
        + (location.page == Page::Members && parent ? QStringLiteral("  /  数据集 ") + parent->cells.value(3).display : QString{})
        + (selected ? "  /  " + selected->title : QString{}));
    if (rowsChanged) {
        QList<PrototypeTableModel::Row> rows;
        for (const auto* record : visible) {
            if (location.page == Page::Search) {
                QStringList values; for (const auto& cell : record->cells) values << cell.display;
                rows.push_back({record->id, {Cell(MmsPrototypeSession::PageName(record->page)), Cell(record->title), Cell(record->ied), Cell(values.join(" · "))}, record->evidence});
            } else rows.push_back({record->id, record->cells, record->evidence});
        }
        m_rows->Reset(Headers(location.page), rows, location.snapshot);
        for (int col = 0; col < m_rows->columnCount(); ++col) {
            const int firstTechnical = location.page == Page::Signals ? 4 : location.page == Page::Clients || location.page == Page::Connections ? 4 : 6;
            m_table->setColumnHidden(col, location.page != Page::Search && location.page != Page::Checks
                && location.page != Page::Advanced && col >= firstTechnical && !m_technical->isChecked());
            m_table->setColumnWidth(col, col == 1 ? 230 : 125);
        }
        if (m_rows->columnCount() > 0) m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    }
    QModelIndex current;
    for (int i = 0; i < visible.size(); ++i)
        if (visible[i]->id == location.selected) current = m_rows->index(i, qBound(0, location.column, qMax(0, m_rows->columnCount() - 1)));
    m_table->setCurrentIndex(current);
    QModelIndex top;
    for (int i = 0; i < visible.size(); ++i)
        if (visible[i]->id == location.topId) top = m_rows->index(i, 0);
    if (top.isValid()) {
        m_table->scrollTo(top, QAbstractItemView::PositionAtTop);
        m_table->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value() + location.topOffset);
    } else m_table->verticalScrollBar()->setValue(0);
    QString summary;
    switch (location.page) {
    case Page::Overview: summary = QStringLiteral("从左侧选择任务：查报告上送内容、查信号地址、查客户端或检查依据。窄窗使用顶部任务选择器。所有内容均为合成示例。"); break;
    case Page::Reports: case Page::Advanced: summary = QStringLiteral("选择报告，查看成员、客户端与原文。定义与实例分开。"); break;
    case Page::Members: summary = QStringLiteral("重复成员保留各自原始序号。选择成员，可查看信号地址与原文。"); break;
    case Page::Signals: summary = QStringLiteral("对象目录也包含未入数据集的信号。选中对象后点「查看属性」，再查看 FC、Domain 和 Item。"); break;
    case Page::Attributes: summary = QStringLiteral("对象属性与 MMS 地址示意。文件声明值与实时值分开；地址为预置展示数据，尚无新解析逻辑。"); break;
    case Page::Clients: summary = QStringLiteral("ClientLN 等文件声明与在线订阅不同。可从客户端反查报告；未声明客户端时，实际订阅情况仍未知。"); break;
    case Page::Connections: summary = QStringLiteral("按访问点查看文件连接配置。未声明端口不填常见值；本原型不会尝试建立网络连接。"); break;
    case Page::Checks: summary = QStringLiteral("示例问题与检查覆盖：本页结果均为预置，未执行文件校验。选问题可定位关联报告或查看原文。"); break;
    case Page::Search: summary = QStringLiteral("搜索整个合成工程，忽略当前 IED。选中结果后点「定位对象」，显式进入目标范围。"); break;
    }
    if (visible.isEmpty() && location.page != Page::Overview) {
        summary += location.filter.isEmpty() ? QStringLiteral("\n当前范围没有此类声明；这不表示不支持 MMS。") : QStringLiteral("\n筛选无结果，可清空筛选。工作对象仍保留。");
    } else if (location.page != Page::Overview) summary += QStringLiteral("  共 %1 项。").arg(visible.size());
    m_summary->setText(summary);
    m_table->setVisible(location.page != Page::Overview); m_filter->setVisible(location.page != Page::Overview);
    m_drill->setText(location.page == Page::Search ? QStringLiteral("定位对象") : location.page == Page::Reports || location.page == Page::Advanced ? QStringLiteral("查看数据集成员")
        : location.page == Page::Signals ? QStringLiteral("查看属性") : location.page == Page::Members ? QStringLiteral("查看信号地址")
        : location.page == Page::Attributes ? QStringLiteral("返回信号对象") : QStringLiteral("定位关联报告"));
    m_drill->setVisible(location.page != Page::Overview && location.page != Page::Connections);
    m_sourceAction->setVisible(location.page != Page::Overview);
    m_drill->setEnabled(selected && (location.page == Page::Search || (selected->page == Page::Reports && selected->id != "broken") || !selected->target.isEmpty()));
    m_clients->setVisible(location.page == Page::Reports); m_clients->setEnabled(selected != nullptr);
    m_sourceAction->setEnabled(selected && !selected->evidence.isEmpty());
    m_detailTitle->setText(selected ? QStringLiteral("所选对象：") + selected->title + (current.isValid() ? QString{} : QStringLiteral("（当前筛选遮挡）")) : QStringLiteral("选择一项后，在这里查看属性与依据。"));
    QList<PrototypeTableModel::Row> fields;
    if (selected) for (const auto& field : selected->fields) fields.push_back({field.name, {Cell(field.name), field.cell, Cell(field.origin)}, field.evidence});
    m_fieldRows->Reset({QStringLiteral("字段"), QStringLiteral("完整值 / 状态"), QStringLiteral("来源与含义")}, fields, location.snapshot);
    m_fields->setColumnWidth(0, 180); m_fields->setColumnWidth(1, 350);
    QModelIndex fieldCurrent;
    for (int i = 0; i < fields.size(); ++i) if (fields[i].id == location.field) fieldCurrent = m_fieldRows->index(i, 0);
    m_fields->setCurrentIndex(fieldCurrent); m_fieldSource->setEnabled(fieldCurrent.isValid() && !fieldCurrent.data(Qt::UserRole + 2).toString().isEmpty());
    const bool showingSource = !location.source.isEmpty();
    m_detailStack->setCurrentIndex(showingSource ? 1 : 0);
    if (showingSource) {
        const int line = m_session->Data().evidenceLines.value(location.source);
        const int first = qMax(1, line - 3), last = qMin(int(m_session->Data().xml.size()), line + 3);
        QString evidenceLabel = QStringLiteral("对象声明");
        if (selected) for (const auto& field : selected->fields)
            if (field.name == location.field && field.evidence == location.source) evidenceLabel = field.name;
        m_sourceTitle->setText((selected ? selected->title : QString{}) + " → " + evidenceLabel + QStringLiteral(" 的原文（同一合成快照）"));
        m_sourceLines->setText(QStringLiteral("Demo.scd · 第 %1–%2 行；所在元素第 %3 行 · 仅上下文可复制").arg(first).arg(last).arg(line));
        m_source->setPlainText(m_session->Data().xml.mid(first - 1, last - first + 1).join('\n'));
        auto cursor = m_source->textCursor(); cursor.setPosition(m_source->document()->findBlockByNumber(line - first).position());
        QTextEdit::ExtraSelection selection; selection.cursor = cursor;
        selection.format.setBackground(palette().alternateBase()); selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        m_source->setExtraSelections({selection});
        m_source->setTextCursor(cursor);
        m_source->centerCursor();
        m_details->setChecked(true);
    }
    m_taskPicker->setVisible(m_narrow);
    m_applying = false;
    if (rowsChanged) QTimer::singleShot(0, this, &MmsPrototypeWindow::BalanceCompactPanels);
}
}
