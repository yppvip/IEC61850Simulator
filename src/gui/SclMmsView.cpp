#include "SclMmsView.hpp"
#include "SclTableInteraction.hpp"
#include <QAbstractTableModel>
#include <QClipboard>
#include <QComboBox>
#include <QGraphicsView>
#include <QElapsedTimer>
#include <QGraphicsScene>
#include <QGraphicsRectItem>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainterPath>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QStyle>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTableView>
#include <QVBoxLayout>
#include <cmath>

namespace iec61850sim {
namespace {
bool Same(const SclMmsKey& a, const SclMmsKey& b) {
    return a.kind == b.kind && a.node == b.node && a.record == b.record && a.ordinal == b.ordinal && a.field == b.field;
}
QString Route(SclMmsQueryKind kind) {
    switch (kind) {
    case SclMmsQueryKind::Reports: return QStringLiteral("报告上送");
    case SclMmsQueryKind::Members: return QStringLiteral("数据集成员");
    case SclMmsQueryKind::Instances: return QStringLiteral("预期实例");
    case SclMmsQueryKind::Clients: return QStringLiteral("客户端文件声明（发布侧）");
    case SclMmsQueryKind::ReverseClients: return QStringLiteral("作为客户端的文件声明");
    case SclMmsQueryKind::Signals: return QStringLiteral("信号地址 · 对象目录");
    case SclMmsQueryKind::Addresses: return QStringLiteral("信号属性地址");
    case SclMmsQueryKind::ReverseSignal: return QStringLiteral("数据集与报告使用");
    case SclMmsQueryKind::Endpoints: return QStringLiteral("连接配置");
    case SclMmsQueryKind::Servers: return QStringLiteral("访问点 / Server");
    case SclMmsQueryKind::Capabilities: return QStringLiteral("服务能力原始声明");
    case SclMmsQueryKind::Search: return QStringLiteral("MMS 全局搜索 · 整个工程");
    case SclMmsQueryKind::Graph: return QStringLiteral("局部配置关系图");
    case SclMmsQueryKind::Findings: return QStringLiteral("文件检查 · 基础与 MMS");
    default: return {};
    }
}
}
class SclMmsTableModel final : public QAbstractTableModel {
public:
    using QAbstractTableModel::QAbstractTableModel;
    QStringList headers;
    std::vector<QStringList> rows;
    void Reset(QStringList h, std::vector<QStringList> r = {}) {
        beginResetModel(); headers = std::move(h); rows = std::move(r); endResetModel();
    }
    int rowCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : int(rows.size()); }
    int columnCount(const QModelIndex& p = {}) const override { return p.isValid() ? 0 : headers.size(); }
    QVariant headerData(int c, Qt::Orientation o, int role) const override {
        return o == Qt::Horizontal && role == Qt::DisplayRole ? headers.value(c) : QVariant();
    }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= int(rows.size())) return {};
        const auto value = rows[index.row()].value(index.column());
        if (role == SclTableInteraction::FullValueRole) return value;
        if (role == Qt::DisplayRole) return value.size() > SclTableInteraction::PreviewLimit
            ? QStringLiteral("完整值 %1 字符（右键查看全值）").arg(value.size()) : value;
        if (role == Qt::ToolTipRole) return value.left(4096).toHtmlEscaped();
        return {};
    }
};

SclMmsView::SclMmsView(QWidget* parent) : QWidget(parent), m_list(this), m_details(this) {
    setObjectName("SclMmsView"); setMinimumSize(0, 0);
    m_list.setObjectName("SclMmsListSession"); m_details.setObjectName("SclMmsDetailsSession");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* nav = new QHBoxLayout;
    m_route = new QComboBox(this); m_route->setObjectName("SclMmsRoute");
    m_route->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_route->setMinimumContentsLength(12);
    for (const auto kind : {SclMmsQueryKind::Reports, SclMmsQueryKind::Signals, SclMmsQueryKind::Endpoints,
        SclMmsQueryKind::Servers, SclMmsQueryKind::Capabilities, SclMmsQueryKind::Clients,
        SclMmsQueryKind::ReverseClients, SclMmsQueryKind::Findings}) m_route->addItem(Route(kind), int(kind));
    nav->addWidget(m_route);
    m_global = new QLineEdit(this); m_global->setObjectName("SclMmsGlobalSearch");
    m_global->setPlaceholderText(QStringLiteral("MMS 全局搜索（整个工程，回车执行）"));
    m_global->setAccessibleName(QStringLiteral("MMS 全局搜索整个工程"));
    nav->addWidget(m_global, 1); layout->addLayout(nav);
    m_back = new QPushButton(style()->standardIcon(QStyle::SP_ArrowBack), QStringLiteral("后退"), this);
    m_forward = new QPushButton(style()->standardIcon(QStyle::SP_ArrowForward), QStringLiteral("前进"), this);
    m_up = new QPushButton(QStringLiteral("上一级"), this);
    m_searchReturn = new QPushButton(QStringLiteral("返回搜索结果"), this);
    m_back->setObjectName("SclMmsBack"); m_forward->setObjectName("SclMmsForward");
    m_up->setObjectName("SclMmsUp"); m_searchReturn->setObjectName("SclMmsSearchReturn");
    m_back->setEnabled(false); m_forward->setEnabled(false); m_up->setEnabled(false); m_searchReturn->hide();
    for (auto* button : {m_back, m_forward, m_up, m_searchReturn}) {
        button->setMinimumWidth(0); button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    }
    m_route->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    nav->insertWidget(0, m_back); nav->insertWidget(1, m_forward); nav->insertWidget(2, m_up);
    m_compactSearch = new QToolButton(this); m_compactSearch->setObjectName("SclMmsCompactSearch");
    m_compactSearch->setText(QStringLiteral("搜索")); m_compactSearch->setToolTip(QStringLiteral("MMS 全工程搜索"));
    m_compactSearch->setAccessibleName(QStringLiteral("MMS 全工程搜索")); nav->addWidget(m_compactSearch); m_compactSearch->hide();
    connect(m_compactSearch, &QToolButton::clicked, this, [this] {
        bool ok = false;
        const auto text = QInputDialog::getText(this, QStringLiteral("MMS 全工程搜索"), QStringLiteral("搜索整个工程"),
            QLineEdit::Normal, m_global->text(), &ok);
        if (ok) { m_global->setText(text); SclMmsRequest r; r.kind = SclMmsQueryKind::Search; r.text = text; Navigate(r); }
    });
    connect(m_back, &QPushButton::clicked, this, [this] { emit HistoryRequested(-1); });
    connect(m_forward, &QPushButton::clicked, this, [this] { emit HistoryRequested(1); });
    connect(m_up, &QPushButton::clicked, this, [this] { OpenAncestor(m_parent); });
    connect(m_searchReturn, &QPushButton::clicked, this, [this] { OpenAncestor(m_searchOrigin); });
    auto* path = new QHBoxLayout; m_pathLayout = path;
    m_title = new QLabel(this); m_title->setObjectName("SclMmsBreadcrumb"); m_title->setTextFormat(Qt::RichText);
    m_title->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); m_title->setWordWrap(true);
    m_pathMenu = new QToolButton(this); m_pathMenu->setObjectName("SclMmsPathMenu"); m_pathMenu->setText(QStringLiteral("完整查阅路径"));
    m_pathMenu->setPopupMode(QToolButton::InstantPopup); m_pathMenu->setMenu(new QMenu(m_pathMenu));
    path->addWidget(m_title, 1); path->addWidget(m_searchReturn); path->addWidget(m_pathMenu); layout->addLayout(path);
    connect(m_title, &QLabel::linkActivated, this, [this](const QString& link) {
        const auto parts = link.split(':'); bool epochOk = false, indexOk = false;
        const auto epoch = parts.value(0).toULongLong(&epochOk); const int index = parts.value(1).toInt(&indexOk);
        if (parts.size() == 2 && epochOk && indexOk && epoch == m_pathEpoch && index >= 0 && index < int(m_ancestors.size()))
            OpenAncestor(m_ancestors[index]);
    });
    auto* controls = new QHBoxLayout; m_controlsLayout = controls;
    m_filter = new QLineEdit(this); m_filter->setObjectName("SclMmsFilter");
    m_filter->setPlaceholderText(QStringLiteral("筛选当前范围与父对象")); m_filter->setClearButtonEnabled(true);
    m_filter->setAccessibleName(QStringLiteral("筛选当前MMS列表")); controls->addWidget(m_filter, 1);
    m_previous = new QPushButton(QStringLiteral("上一页数据"), this); m_previous->setObjectName("SclMmsPrevious");
    m_next = new QPushButton(QStringLiteral("下一页数据"), this); m_next->setObjectName("SclMmsNext");
    m_open = new QPushButton(QStringLiteral("继续查阅"), this); m_open->setObjectName("SclMmsOpen");
    m_actions = new QToolButton(this); m_actions->setText(QStringLiteral("更多")); m_actions->setObjectName("SclMmsActions");
    m_actions->setPopupMode(QToolButton::InstantPopup); m_actions->setMenu(new QMenu(m_actions));
    controls->addWidget(m_open);
    controls->addWidget(m_actions); layout->addLayout(controls);
    m_status = new QLabel(this); m_status->setObjectName("SclMmsStatus"); m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true); m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto* paging = new QHBoxLayout; paging->addWidget(m_status, 1); paging->addWidget(m_previous); paging->addWidget(m_next);
    layout->addLayout(paging);
    m_table = new QTableView(this); m_table->setObjectName("SclMmsResults");
    m_model = new SclMmsTableModel(this);
    m_model->headers = {QStringLiteral("对象"), QStringLiteral("说明 / 原值"), QStringLiteral("类别 / 证据"),
        QStringLiteral("数据集 / 对象引用"), "FC", "MMS Domain", "MMS Item / 引用", QStringLiteral("解释状态 / 规则集")};
    const auto configure = [](QTableView* table, SclMmsTableModel* model) {
        table->setModel(model); table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setAlternatingRowColors(true); table->verticalHeader()->hide(); table->setMinimumSize(0, 0);
        table->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
        table->horizontalHeader()->setDefaultSectionSize(170);
        auto* interaction = new SclTableInteraction(table); interaction->SetModel(model); interaction->SetEnabled(true);
        return interaction;
    };
    auto* interaction = configure(m_table, m_model); m_table->setColumnWidth(1, 260);
    layout->addWidget(m_table, 1);
    m_graph = new QGraphicsView(this); m_graph->setObjectName("SclMmsGraph"); m_graph->setMinimumSize(0, 0);
    m_graph->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_scene = new QGraphicsScene(this); m_graph->setScene(m_scene); m_graph->hide(); layout->addWidget(m_graph, 1);
    m_inspector = new QWidget(this); m_inspector->setObjectName("SclMmsInspector");
    auto* fieldsLayout = new QVBoxLayout(m_inspector); fieldsLayout->setContentsMargins(4, 4, 4, 4);
    auto* fieldActions = new QHBoxLayout;
    m_fieldTitle = new QLabel(QStringLiteral("选择一个 MMS 对象以查看字段与依据"), m_inspector);
    m_fieldTitle->setObjectName("SclMmsSelection"); m_fieldTitle->setTextFormat(Qt::PlainText);
    m_fieldTitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_source = new QPushButton(QStringLiteral("查看所选字段原文"), m_inspector); m_source->setObjectName("SclMmsSource");
    m_source->setEnabled(false); fieldActions->addWidget(m_fieldTitle, 1); fieldActions->addWidget(m_source);
    fieldsLayout->addLayout(fieldActions);
    m_fields = new QTableView(m_inspector); m_fields->setObjectName("SclMmsFields");
    m_fieldModel = new SclMmsTableModel(this); m_fieldModel->headers = {QStringLiteral("字段"), QStringLiteral("完整值"), QStringLiteral("解释 / 来源")};
    auto* fieldInteraction = configure(m_fields, m_fieldModel); m_fields->setColumnWidth(1, 300);
    fieldsLayout->addWidget(m_fields, 1);
    connect(interaction, &SclTableInteraction::OpenSource, this, [this] {
        if (m_selected && m_selected->node >= 0) emit OpenEvidence(m_selected->node, QStringLiteral("所选对象所在元素"));
    });
    connect(fieldInteraction, &SclTableInteraction::OpenSource, this, &SclMmsView::OpenSource);
    connect(m_source, &QPushButton::clicked, this, &SclMmsView::OpenSource);
    connect(m_fields->selectionModel(), &QItemSelectionModel::currentRowChanged, this, [this](const QModelIndex& i) {
        m_source->setEnabled(m_detail && i.isValid() && i.row() < int(m_detail->fields.size()) && m_detail->fields[i.row()].evidence.node >= 0);
        if (!m_applying) emit LocationUpdated();
    });
    connect(m_table->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
        [this](const QModelIndex& i) { if (!m_applying && i.isValid()) Select(i.row(), true); });
    connect(m_table->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (!m_applying) emit LocationUpdated(); });
    connect(m_fields->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { if (!m_applying) emit LocationUpdated(); });
    m_delay.setSingleShot(true); m_delay.setInterval(160);
    connect(&m_delay, &QTimer::timeout, this, &SclMmsView::Run);
    connect(m_filter, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (m_applying) return;
        ++m_epoch;
        auto restore = CurrentLocation(); m_list.Cancel(); m_request.text = text; m_request.offset = 0;
        m_table->setEnabled(false); m_open->setEnabled(false);
        m_request.locate.reset(); restore.request = m_request; m_restore = restore;
        emit LocationUpdated(); m_delay.start();
    });
    connect(m_route, &QComboBox::activated, this, [this](int i) { Open(static_cast<SclMmsQueryKind>(m_route->itemData(i).toInt())); });
    connect(m_global, &QLineEdit::returnPressed, this, [this] {
        SclMmsRequest r; r.kind = SclMmsQueryKind::Search; r.text = m_global->text(); Navigate(r);
    });
    connect(m_previous, &QPushButton::clicked, this, [this] { ChangePage(std::max(0, m_request.offset - 200)); });
    connect(m_next, &QPushButton::clicked, this, [this] { ChangePage(m_request.offset + 200); });
    connect(m_open, &QPushButton::clicked, this, [this] {
        if (!m_selected) return;
        switch (m_selected->kind) {
        case SclMmsRecordKind::Report: Continue(SclMmsQueryKind::Members); break;
        case SclMmsRecordKind::Signal: Continue(SclMmsQueryKind::Addresses); break;
        case SclMmsRecordKind::Member:
            if (m_result) {
                const auto row = std::find_if(m_result->rows.begin(), m_result->rows.end(), [this](const auto& r) { return Same(r.key, *m_selected); });
                if (row != m_result->rows.end() && row->state != Scl::LinkState::Resolved) { emit ShowInspector(); break; }
            }
            Continue(SclMmsQueryKind::Addresses); break;
        case SclMmsRecordKind::Instance: Continue(SclMmsQueryKind::Members); break;
        case SclMmsRecordKind::Client: Continue(SclMmsQueryKind::Members); break;
        default: emit ShowInspector(); break;
        }
    });
    connect(&m_list, &SclMmsQuerySession::Ready, this, &SclMmsView::ApplyList);
    connect(&m_details, &SclMmsQuerySession::Ready, this, &SclMmsView::ApplyDetail);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, [this] {
        if (m_applying || m_scene->selectedItems().isEmpty()) return;
        Select(m_scene->selectedItems().front()->data(0).toInt(), true);
    });
    m_request.kind = SclMmsQueryKind::Reports;
}
SclMmsView::~SclMmsView() { Cancel(); }
void SclMmsView::Cancel() { ++m_epoch; m_delay.stop(); m_list.Cancel(); m_details.Cancel(); }
void SclMmsView::hideEvent(QHideEvent* e) { Cancel(); QWidget::hideEvent(e); }
void SclMmsView::showEvent(QShowEvent* e) { QWidget::showEvent(e); if (m_snapshot) { m_restore = CurrentLocation(); Run(); } }
void SclMmsView::SetContext(std::shared_ptr<const SclSnapshot> snapshot, Scl::NodeId ied) {
    if (snapshot == m_snapshot && ied == m_ied) return;
    Cancel(); m_snapshot = std::move(snapshot); m_ied = ied; m_selected.reset(); m_restore.reset(); m_result.reset(); m_detail.reset();
    m_parent.reset(); m_searchOrigin.reset(); m_ancestors.clear();
    m_navigationSignature.clear(); m_renderedParent = nullptr; m_renderedOrigin = nullptr;
    m_list.SetContext(m_snapshot, {m_ied}); m_details.SetContext(m_snapshot, {m_ied});
    m_request = {}; m_request.scope.ied = ied;
    { QSignalBlocker block(m_filter); m_filter->clear(); }
    m_model->Reset(m_model->headers); m_fieldModel->Reset(m_fieldModel->headers);
    m_fieldTitle->setText(QStringLiteral("选择一个 MMS 对象以查看字段与依据")); m_source->setEnabled(false);
    UpdateNavigation(); if (isVisible()) Run();
}
void SclMmsView::Open(SclMmsQueryKind kind) {
    SclMmsRequest request; request.kind = kind; request.scope = m_request.scope; request.scope.ied = m_ied;
    if (kind == SclMmsQueryKind::ReverseClients) request.target = m_ied;
    Navigate(request);
}
void SclMmsView::OpenNode(Scl::NodeId node) {
    if (!m_snapshot || node < 0 || node >= int(m_snapshot->analysis.document->nodes.size())) return;
    Location l; l.snapshot = m_snapshot; l.viewIed = m_snapshot->browse.iedOwners[node];
    l.request.scope.ied = l.viewIed; l.inspectorVisible = m_inspectorVisible;
    if (m_snapshot->mmsIndex.reports.contains(node)) {
        l.selected = SclMmsKey{SclMmsRecordKind::Report, node, m_snapshot->mmsIndex.reports.value(node)};
        l.request.locate = l.selected;
    } else if (m_snapshot->browse.dataSetMembers.contains(node)) {
        l.request.kind = SclMmsQueryKind::Members; l.request.target = node;
    } else if (m_snapshot->mmsIndex.memberReferences.contains(node)) {
        l.request.kind = SclMmsQueryKind::Signals; l.request.target = node;
    } else return;
    Commit(std::move(l));
}
void SclMmsView::Navigate(SclMmsRequest request) {
    Location location; location.snapshot = m_snapshot; location.request = std::move(request);
    location.viewIed = location.request.kind == SclMmsQueryKind::Search ? m_ied : location.request.scope.ied;
    location.inspectorVisible = m_inspectorVisible; Commit(std::move(location));
}
SclMmsView::Location SclMmsView::CurrentLocation() const {
    if (m_restore) { auto l = *m_restore; l.request = m_request; l.selected = m_selected; return l; }
    Location l; l.snapshot = m_snapshot; l.request = m_request; l.selected = m_selected;
    l.parent = m_parent; l.searchOrigin = m_searchOrigin; l.viewIed = m_ied; l.inspectorVisible = m_inspectorVisible;
    l.horizontal = m_table->horizontalScrollBar()->value(); l.fieldHorizontal = m_fields->horizontalScrollBar()->value();
    l.column = m_table->currentIndex().column();
    const int top = m_table->rowAt(0);
    if (m_result && top >= 0 && top < int(m_result->rows.size())) { l.top = m_result->rows[top].key; l.topOffset = -m_table->rowViewportPosition(top); }
    if (m_detail) {
        const int field = m_fields->currentIndex().row(), topField = m_fields->rowAt(0);
        if (field >= 0 && field < int(m_detail->fields.size())) l.field = m_detail->fields[field].key;
        if (topField >= 0 && topField < int(m_detail->fields.size())) { l.topField = m_detail->fields[topField].key; l.fieldOffset = -m_fields->rowViewportPosition(topField); }
    }
    return l;
}
void SclMmsView::RestoreLocation(const Location& location) {
    if (location.snapshot != m_snapshot) return;
    Cancel(); m_request = location.request; m_selected = location.selected; m_restore = location;
    // Keep the disabled previous page until the complete new page arrives. A model reset
    // here would repeat layout/row measurement before ApplyList resets it again.
    m_result.reset();
    m_source->setEnabled(false);
    m_parent = location.parent && location.parent->snapshot == m_snapshot ? location.parent : nullptr;
    m_searchOrigin = location.searchOrigin && location.searchOrigin->snapshot == m_snapshot ? location.searchOrigin : nullptr;
    m_navigationSignature.clear();
    EnsureParent();
    m_restore->parent = m_parent; m_restore->searchOrigin = m_searchOrigin;
    { QSignalBlocker block(m_filter); m_filter->setText(m_request.text); }
    if (m_request.kind == SclMmsQueryKind::Search) { QSignalBlocker block(m_global); m_global->setText(m_request.text); }
    UpdateNavigation(); if (isVisible()) Run();
}
void SclMmsView::Run() {
    if (!m_snapshot || !isVisible()) return;
    m_request.presentation = true;
    auto scope = m_request.kind == SclMmsQueryKind::Search ? SclMmsScope{} : m_request.scope;
    m_list.SetContext(m_snapshot, scope); m_list.Query(m_request);
    UpdateNavigation();
    m_status->setText(QStringLiteral("正在后台查询…"));
    m_fields->setEnabled(false); m_source->setEnabled(false);
    m_table->setEnabled(false); m_actions->setEnabled(false); m_open->setEnabled(false);
    m_previous->setEnabled(false); m_next->setEnabled(false);
    const QSignalBlocker block(m_route);
    m_route->setPlaceholderText(Route(m_request.kind));
    m_route->setAccessibleName(QStringLiteral("当前 MMS 任务 · ") + Route(m_request.kind));
    m_route->setCurrentIndex(m_route->findData(int(m_request.kind)));
}
void SclMmsView::ApplyList() {
    QElapsedTimer timing; timing.start();
    const auto result = m_list.result();
    if (!result || result->document != m_snapshot->analysis.document) return;
    const QScopedValueRollback<bool> guard(m_applying, true);
    m_result = result; m_request.offset = result->request.offset; m_request.locate.reset();
    if (m_restore) m_restore->request = m_request;
    m_table->setEnabled(true); m_actions->setEnabled(true);
    QStringList headers{QStringLiteral("对象"), QStringLiteral("说明 / 原值"), QStringLiteral("类别 / 证据"),
        QStringLiteral("数据集 / 对象引用"), "FC", "MMS Domain", "MMS Item / 引用", QStringLiteral("解释状态 / 规则集")};
    QList<int> columns{0,1,2,3,4,5,6,7};
    switch (m_request.kind) {
    case SclMmsQueryKind::Reports:
        headers = {QStringLiteral("报告定义"), QStringLiteral("报告类型"), QStringLiteral("数据集 / 成员"), QStringLiteral("说明"), QStringLiteral("解析状态")}; columns = {0,2,3,1,7}; break;
    case SclMmsQueryKind::Signals:
        headers = {QStringLiteral("对象"), QStringLiteral("描述"), QStringLiteral("所属装置 / LN"), "CDC", QStringLiteral("状态")}; columns = {0,1,3,2,7}; break;
    case SclMmsQueryKind::Addresses:
        headers = {QStringLiteral("属性"), "MMS Domain", "MMS Item", "FC", QStringLiteral("访问形态"), QStringLiteral("说明 / 来源"), QStringLiteral("状态")}; columns = {0,5,6,4,2,1,7}; break;
    case SclMmsQueryKind::Clients:
    case SclMmsQueryKind::ReverseClients:
        headers = {QStringLiteral("声明种类"), QStringLiteral("文件原值 / 解析证据"), QStringLiteral("关联报告"), QStringLiteral("解析状态")}; columns = {0,1,3,7}; break;
    case SclMmsQueryKind::Endpoints:
    case SclMmsQueryKind::Servers:
    case SclMmsQueryKind::Capabilities:
        headers = {QStringLiteral("对象 / 访问点"), QStringLiteral("文件声明 / 限制"), QStringLiteral("解析状态")}; columns = {0,1,7}; break;
    case SclMmsQueryKind::Findings:
        headers = {QStringLiteral("规则"), QStringLiteral("级别"), QStringLiteral("发现"), QStringLiteral("对象路径"), QStringLiteral("规则集")}; columns = {0,2,1,3,7}; break;
    default: break;
    }
    std::vector<QStringList> cells;
    for (const auto& row : result->rows) { QStringList values; for (auto c : columns) values << row.cells.value(c); cells.push_back(std::move(values)); }
    m_model->Reset(headers, std::move(cells));
    if (m_request.kind == SclMmsQueryKind::Addresses) { m_table->setColumnWidth(0, 140); m_table->setColumnWidth(1, 160); m_table->setColumnWidth(2, 300); m_table->setColumnWidth(3, 60); }
    m_status->setText(result->rows.empty() ? QStringLiteral("当前范围没有匹配声明。文件缺少声明不等于不支持 MMS 或没有客户端。")
        : QStringLiteral("第 %1–%2 项 · %3%4 项 · 双击复制，Enter 查看原文")
            .arg(m_request.offset + 1).arg(m_request.offset + result->rows.size()).arg(result->limited ? QStringLiteral("至少 ") : QString()).arg(result->total));
    if (result->limited) m_status->setText(m_status->text() + '\n' + result->limitation + QStringLiteral(" 请缩小范围；结果不完整。"));
    else if (!result->limitation.isEmpty()) m_status->setText(m_status->text() + '\n' + result->limitation);
    m_previous->setEnabled(m_request.offset > 0);
    m_next->setEnabled(result->total > m_request.offset + int(result->rows.size()) && !result->rows.empty());
    m_table->setVisible(m_request.kind != SclMmsQueryKind::Graph); m_graph->setVisible(m_request.kind == SclMmsQueryKind::Graph);
    if (m_request.kind == SclMmsQueryKind::Graph) RenderGraph();
    int selected = -1;
    for (int i = 0; i < int(result->rows.size()); ++i) if (m_selected && Same(*m_selected, result->rows[i].key)) selected = i;
    if (!m_selected && !result->rows.empty()) selected = 0;
    if (selected >= 0) {
        m_table->setCurrentIndex(m_model->index(selected, m_restore ? std::max(0, m_restore->column) : 0));
        Select(selected, false);
    } else if (m_selected) {
        m_table->selectionModel()->clear(); m_table->setCurrentIndex({});
        m_status->setText(m_status->text() + QStringLiteral("\n所选对象被筛选或分页遮挡；详情保留。可清除当前筛选。"));
        Detail();
    } else {
        m_detail.reset(); m_fieldModel->Reset(m_fieldModel->headers);
        m_fieldTitle->setText(QStringLiteral("选择当前列表中的对象以查看字段与依据"));
    }
    if (m_restore && m_restore->top) for (int i = 0; i < int(result->rows.size()); ++i) if (Same(*m_restore->top, result->rows[i].key)) {
        m_table->scrollTo(m_model->index(i, 0), QAbstractItemView::PositionAtTop);
        m_table->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value() + m_restore->topOffset); break;
    }
    UpdateActions(); setProperty("MmsListApplyUs", timing.nsecsElapsed() / 1000);
    if (!m_selected) CompleteRestore();
}
void SclMmsView::Select(int row, bool navigate) {
    if (!m_result || row < 0 || row >= int(m_result->rows.size())) return;
    if (navigate) { ++m_epoch; m_restore.reset(); }
    m_selected = m_result->rows[row].key;
    emit SelectedNode(m_selected->node);
    Detail(); UpdateActions();
    if (!m_restore) emit LocationUpdated();
}
void SclMmsView::Detail() {
    if (!m_selected) return;
    if (!m_detail || !Same(m_detail->request.key, *m_selected)) {
        QScopedValueRollback<bool> guard(m_applying, true);
        m_detail.reset(); m_fieldModel->Reset(m_fieldModel->headers); m_source->setEnabled(false);
        m_fieldTitle->setText(QStringLiteral("正在后台准备所选对象的字段…"));
    }
    auto request = m_request; request.kind = SclMmsQueryKind::Detail; request.key = *m_selected;
    m_details.SetContext(m_snapshot, m_request.kind == SclMmsQueryKind::Search ? SclMmsScope{} : m_request.scope);
    m_details.Query(request);
}
void SclMmsView::ApplyDetail() {
    QElapsedTimer timing; timing.start();
    auto result = m_details.result();
    if (!result || !m_selected || !Same(*m_selected, result->request.key)) return;
    const QScopedValueRollback<bool> guard(m_applying, true); m_detail = result;
    m_fields->setEnabled(true);
    m_fieldTitle->setText(result->title); m_fieldTitle->setToolTip((result->summary + '\n' + result->limitation).toHtmlEscaped());
    std::vector<QStringList> rows;
    for (const auto& f : result->fields) rows.push_back({f.label, f.value, (!f.present ? f.key == "runtime" ? QStringLiteral("离线未获取 · ") : QStringLiteral("未声明 · ") : f.value.isEmpty() ? QStringLiteral("已声明空值 · ") : QString()) + f.explanation});
    m_fieldModel->Reset(m_fieldModel->headers, std::move(rows));
    int current = m_fieldModel->rowCount() ? 0 : -1;
    if (m_restore) for (int i = 0; i < int(result->fields.size()); ++i) if (result->fields[i].key == m_restore->field) current = i;
    if (current >= 0) m_fields->setCurrentIndex(m_fieldModel->index(current, 1));
    if (m_restore) for (int i = 0; i < int(result->fields.size()); ++i) if (result->fields[i].key == m_restore->topField) {
        m_fields->scrollTo(m_fieldModel->index(i, 0), QAbstractItemView::PositionAtTop);
        m_fields->verticalScrollBar()->setValue(m_fields->verticalScrollBar()->value() + m_restore->fieldOffset); break;
    }
    setProperty("MmsDetailApplyUs", timing.nsecsElapsed() / 1000); CompleteRestore();
}
void SclMmsView::OpenSource() {
    const int i = m_fields->currentIndex().row();
    if (!m_detail || i < 0 || i >= int(m_detail->fields.size())) return;
    const auto& field = m_detail->fields[i];
    if (field.evidence.node >= 0) emit OpenEvidence(field.evidence.node, field.label + QStringLiteral(" · 所在元素"));
}
void SclMmsView::Continue(SclMmsQueryKind kind) {
    if (!m_selected || !m_snapshot) return;
    auto key = *m_selected;
    auto request = m_request;
    if (m_request.kind == SclMmsQueryKind::Search && key.node >= 0) {
        const auto owner = m_snapshot->browse.iedOwners[key.node];
        if (owner != Scl::NoNode) request.scope.ied = owner;
    }
    request.kind = kind; request.text.clear(); request.offset = 0;
    if (key.kind == SclMmsRecordKind::Report || key.kind == SclMmsRecordKind::Instance) request.report = key.node;
    if (key.kind == SclMmsRecordKind::Client && m_result) {
        const auto row = std::find_if(m_result->rows.begin(), m_result->rows.end(), [&](const auto& row) { return Same(row.key, key); });
        if (row == m_result->rows.end() || row->report == Scl::NoNode) { emit ShowInspector(); return; }
        request.report = row->report; request.scope = {m_snapshot->browse.iedOwners[row->report]};
    }
    if (key.kind == SclMmsRecordKind::Signal || key.kind == SclMmsRecordKind::Address) request.signal = key.record;
    if (key.kind == SclMmsRecordKind::Member && kind == SclMmsQueryKind::Addresses) {
        const auto& ref = m_snapshot->analysis.references[key.record]; request.signal = -1;
        request.kind = SclMmsQueryKind::Signals; request.target = key.node;
        if (ref.logicalNode != Scl::NoNode) request.scope.ied = m_snapshot->browse.iedOwners[ref.logicalNode];
    }
    Location next; next.snapshot = m_snapshot; next.request = request; next.viewIed = request.scope.ied;
    next.inspectorVisible = m_inspectorVisible;
    if (m_request.kind == SclMmsQueryKind::Search) {
        next.searchOrigin = std::make_shared<const Location>(CurrentLocation());
    } else {
        next.parent = std::make_shared<const Location>(CurrentLocation()); next.searchOrigin = m_searchOrigin;
    }
    Commit(std::move(next));
}
void SclMmsView::UpdateActions() {
    m_open->setEnabled(bool(m_selected)); auto* menu = m_actions->menu(); menu->clear();
    QString action = QStringLiteral("查看字段与依据");
    if (m_selected) switch (m_selected->kind) {
    case SclMmsRecordKind::Report: case SclMmsRecordKind::Instance: action = QStringLiteral("查看数据集成员"); break;
    case SclMmsRecordKind::Member: action = QStringLiteral("查看信号对象"); break;
    case SclMmsRecordKind::Signal: action = QStringLiteral("查看属性与地址"); break;
    case SclMmsRecordKind::Client: action = QStringLiteral("查看报告成员"); break;
    default: break;
    }
    if (m_selected && m_selected->kind == SclMmsRecordKind::Member && m_result)
        for (const auto& row : m_result->rows) if (Same(row.key, *m_selected) && row.state != Scl::LinkState::Resolved)
            action = QStringLiteral("查看候选与依据");
    m_open->setText(action); UpdateNavigation();
    const bool report = m_selected && (m_selected->kind == SclMmsRecordKind::Report || m_selected->kind == SclMmsRecordKind::Instance);
    for (const auto kind : {SclMmsQueryKind::Members, SclMmsQueryKind::Instances, SclMmsQueryKind::Clients, SclMmsQueryKind::Graph}) {
        auto* action = menu->addAction(Route(kind), this, [this, kind] { Continue(kind); }); action->setEnabled(report);
    }
    auto* usage = menu->addAction(QStringLiteral("信号被哪些数据集/报告使用"), this, [this] { Continue(SclMmsQueryKind::ReverseSignal); });
    usage->setEnabled(m_selected && (m_selected->kind == SclMmsRecordKind::Signal || m_selected->kind == SclMmsRecordKind::Address));
    menu->addAction(QStringLiteral("查看所选对象原文"), this, [this] { if (m_selected) emit OpenEvidence(m_selected->node, QStringLiteral("所选对象所在元素")); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("切换局部图 / 节点列表（键盘）"), this, [this] {
        if (m_request.kind == SclMmsQueryKind::Graph) { const bool graph = m_graph->isVisible(); m_graph->setVisible(!graph); m_table->setVisible(graph); }
    });
    menu->addAction(QStringLiteral("放大局部图"), this, [this] { if (m_graph->transform().m11() < 4) m_graph->scale(1.25, 1.25); });
    menu->addAction(QStringLiteral("缩小局部图"), this, [this] { if (m_graph->transform().m11() > 0.15) m_graph->scale(0.8, 0.8); });
    menu->addAction(QStringLiteral("适应局部图范围"), this, [this] { m_graph->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio); });
    menu->addAction(QStringLiteral("规则覆盖与未执行项"), this, &SclMmsView::Coverage);
    menu->addAction(QStringLiteral("导出全工程基础与 MMS 问题"), this, &SclMmsView::ExportIssues);
    auto* apScope = menu->addAction(QStringLiteral("仅查看此访问点的报告"), this, [this] {
        if (!m_selected) return;
        auto request = SclMmsRequest{}; request.scope = {m_ied, m_selected->node}; Navigate(request);
    });
    apScope->setEnabled(m_selected && m_selected->kind == SclMmsRecordKind::Server);
    menu->addAction(QStringLiteral("恢复整个 IED 范围"), this, [this] { SclMmsRequest r; r.scope.ied = m_ied; Navigate(r); });
}
void SclMmsView::RenderGraph() {
    m_scene->clear(); if (!m_result) return;
    m_status->setText(m_status->text() + QStringLiteral("\n箭头为离线配置引用方向；虚线为未确认或候选，不表示在线收发。"));
    for (const auto& edge : m_result->edges) {
        const auto pos = [](int i) { return QPointF((i % 4) * 280 + 122.5, (i / 4) * 110 + 37.5); };
        const auto difference = pos(edge.to) - pos(edge.from);
        if (difference.isNull()) continue;
        const auto from = pos(edge.from) + QPointF(122.5, 0), to = pos(edge.to) - QPointF(122.5, 0);
        // Route through the grid gaps. A straight line through intervening
        // nodes would visually invent a chain between unrelated records.
        const qreal channel = pos(edge.to).y() - 37.5 - 12;
        QPainterPath path(from); path.lineTo(from.x() + 12, from.y()); path.lineTo(from.x() + 12, channel);
        path.lineTo(to.x() - 12, channel); path.lineTo(to.x() - 12, to.y()); path.lineTo(to);
        QPen pen(palette().color(QPalette::Text));
        if (edge.state != Scl::LinkState::Resolved) pen.setStyle(Qt::DashLine);
        auto* line = m_scene->addPath(path, pen);
        const QPointF unit(1, 0);
        const QPointF normal(-unit.y(), unit.x());
        QPolygonF arrow; arrow << to << to - unit * 10 + normal * 5 << to - unit * 10 - normal * 5;
        m_scene->addPolygon(arrow, pen, palette().brush(QPalette::Text));
        static const QStringList kinds{QStringLiteral("使用数据集"), QStringLiteral("预期实例（离线推导）"), QStringLiteral("客户端文件声明"),
            QStringLiteral("声明的源报告"), QStringLiteral("数据集成员出现"), QStringLiteral("成员对象引用")};
        line->setToolTip(kinds.value(int(edge.kind)) + QStringLiteral(" · ") + SclMmsStateText(edge.state));
    }
    for (int i = 0; i < int(m_result->rows.size()); ++i) {
        const auto& row = m_result->rows[i];
        auto* rect = m_scene->addRect((i % 4) * 280, (i / 4) * 110, 245, 75, QPen(palette().color(QPalette::Text)), palette().brush(QPalette::Base));
        rect->setFlag(QGraphicsItem::ItemIsSelectable); rect->setData(0, i);
        auto* text = new QGraphicsTextItem(rect); text->setDefaultTextColor(palette().color(QPalette::Text));
        text->setPlainText(row.cells.value(2).left(35) + '\n' + row.cells.value(0).left(70));
        text->setTextWidth(235); text->setPos(rect->rect().topLeft()); text->setAcceptedMouseButtons(Qt::NoButton);
        rect->setToolTip(row.cells.join('\n').left(4096).toHtmlEscaped());
    }
    m_scene->setSceneRect(m_scene->itemsBoundingRect());
}
void SclMmsView::Coverage() {
    if (!m_snapshot) return;
    QStringList text{QStringLiteral("XML 格式：已读取。内部一致性与工程提示按下列规则执行。"),
        QStringLiteral("完整 XSD：尚未实现。在线核实：未执行。工程点表完整性：未执行。")};
    for (int source = 0; source < 2; ++source) {
        const auto& coverage = source ? m_snapshot->mms.coverage : m_snapshot->analysis.coverage;
        text << (source ? m_snapshot->mms.ruleSetVersion : m_snapshot->analysis.ruleSetVersion);
        for (const auto& c : coverage) text << QStringLiteral("%1 · %2 · 已检查 %3 · 受限 %4").arg(c.id)
            .arg(c.state == Scl::Coverage::Executed ? QStringLiteral("已执行") : c.state == Scl::Coverage::NotApplicable ? QStringLiteral("不适用") : c.state == Scl::Coverage::Blocked ? QStringLiteral("被阻断") : QStringLiteral("尚未实现")).arg(c.evaluated).arg(c.limited);
    }
    QMessageBox dialog(QMessageBox::Information, QStringLiteral("检查覆盖"), text.join('\n'), QMessageBox::Ok, this);
    dialog.setTextFormat(Qt::PlainText); dialog.exec();
}
}
