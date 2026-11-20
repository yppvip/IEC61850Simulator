#include "SclMmsView.hpp"
#include <QApplication>
#include <QClipboard>
#include <QLabel>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTableView>
#include <QToolButton>
#include <algorithm>

namespace iec61850sim {
namespace {
QString PageName(SclMmsQueryKind kind) {
    switch (kind) {
    case SclMmsQueryKind::Reports: return QStringLiteral("报告列表");
    case SclMmsQueryKind::Members: return QStringLiteral("数据集成员");
    case SclMmsQueryKind::Signals: return QStringLiteral("信号对象");
    case SclMmsQueryKind::Addresses: return QStringLiteral("属性与地址");
    case SclMmsQueryKind::Search: return QStringLiteral("全工程搜索");
    case SclMmsQueryKind::Instances: return QStringLiteral("预期实例");
    case SclMmsQueryKind::Clients: return QStringLiteral("客户端声明");
    case SclMmsQueryKind::ReverseClients: return QStringLiteral("客户端反查");
    case SclMmsQueryKind::ReverseSignal: return QStringLiteral("数据集与报告使用");
    case SclMmsQueryKind::Graph: return QStringLiteral("局部关系图");
    case SclMmsQueryKind::Endpoints: return QStringLiteral("连接配置");
    case SclMmsQueryKind::Servers: return QStringLiteral("访问点");
    case SclMmsQueryKind::Capabilities: return QStringLiteral("服务声明");
    case SclMmsQueryKind::Findings: return QStringLiteral("文件检查");
    default: return QStringLiteral("字段与依据");
    }
}
bool SameKey(const SclMmsKey& a, const SclMmsKey& b) {
    return a.kind == b.kind && a.node == b.node && a.record == b.record
        && a.ordinal == b.ordinal && a.field == b.field;
}
}

void SclMmsView::SetHistoryEnabled(bool back, bool forward) {
    m_back->setEnabled(back); m_forward->setEnabled(forward);
    m_back->setToolTip(QStringLiteral("回到刚才查阅的位置（Alt+Left）"));
    m_forward->setToolTip(QStringLiteral("恢复后退前的位置（Alt+Right）"));
}

void SclMmsView::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_pathMenu) UpdateNavigation();
    emit GeometryChanged();
}

void SclMmsView::Commit(Location location) {
    if (!m_snapshot || location.snapshot != m_snapshot) return;
    emit BeforeNavigate();
    if (location.viewIed != m_ied) emit ScopeRequested(location.viewIed);
    if (location.snapshot != m_snapshot) return;
    RestoreLocation(location);
    emit RestoreInspector(location.inspectorVisible);
    emit Navigated();
}

void SclMmsView::OpenAncestor(std::shared_ptr<const Location> location) {
    // Copy before scope changes clear the old route; callbacks never retain a display row.
    if (location && location->snapshot == m_snapshot) Commit(*location);
}

void SclMmsView::ChangePage(int offset) {
    auto location = CurrentLocation(); location.request.offset = offset; location.request.locate.reset();
    location.selected.reset(); location.top.reset(); location.topOffset = 0;
    location.field.clear(); location.topField.clear(); location.fieldOffset = 0;
    RestoreLocation(location); // Pagination updates this history entry; it is not a business parent.
    emit LocationUpdated();
}

void SclMmsView::EnsureParent() {
    if (!m_snapshot || m_parent) return;
    const auto& s = *m_snapshot;
    const auto reportParent = [&](Scl::NodeId node) -> std::shared_ptr<const Location> {
        const auto found = s.mmsIndex.reports.constFind(node);
        if (found == s.mmsIndex.reports.cend()) return {};
        Location l; l.snapshot = m_snapshot; l.request.scope = m_request.scope;
        l.viewIed = m_ied; l.inspectorVisible = m_restore ? m_restore->inspectorVisible : m_inspectorVisible;
        l.selected = SclMmsKey{SclMmsRecordKind::Report, node, *found}; l.request.locate = l.selected;
        l.searchOrigin = m_searchOrigin;
        return std::make_shared<const Location>(std::move(l));
    };
    const auto memberParent = [&]() -> std::shared_ptr<const Location> {
        const auto found = s.mmsIndex.memberReferences.constFind(m_request.target);
        if (found == s.mmsIndex.memberReferences.cend()) return {};
        const auto& ref = s.analysis.references[*found];
        Location l; l.snapshot = m_snapshot; l.request.scope = m_request.scope;
        l.request.kind = SclMmsQueryKind::Members; l.request.report = m_request.report;
        l.request.target = s.analysis.document->nodes[ref.source].parent;
        l.selected = SclMmsKey{SclMmsRecordKind::Member, ref.source, *found, ref.memberOrdinal};
        l.request.locate = l.selected; l.viewIed = m_ied; l.inspectorVisible = m_inspectorVisible;
        l.parent = reportParent(m_request.report); l.searchOrigin = m_searchOrigin;
        return std::make_shared<const Location>(std::move(l));
    };
    switch (m_request.kind) {
    case SclMmsQueryKind::Members: case SclMmsQueryKind::Instances:
    case SclMmsQueryKind::Clients: case SclMmsQueryKind::Graph:
        m_parent = reportParent(m_request.report); break;
    case SclMmsQueryKind::Signals:
        m_parent = memberParent(); break;
    case SclMmsQueryKind::Addresses: case SclMmsQueryKind::ReverseSignal:
        if (m_request.signal >= 0 && m_request.signal < int(s.tasks.catalog.instances.size())) {
            const auto& signal = s.tasks.catalog.instances[m_request.signal];
            const auto& definition = s.tasks.catalog.definitions[signal.definition];
            Location l; l.snapshot = m_snapshot; l.request.scope = m_request.scope;
            l.request.kind = SclMmsQueryKind::Signals; l.request.target = m_request.target; l.request.report = m_request.report;
            l.selected = SclMmsKey{SclMmsRecordKind::Signal, signal.logicalNode, m_request.signal, 0, definition.path};
            l.request.locate = l.selected; l.viewIed = m_ied; l.inspectorVisible = m_inspectorVisible;
            l.parent = memberParent(); l.searchOrigin = m_searchOrigin;
            m_parent = std::make_shared<const Location>(std::move(l));
        }
        break;
    default: break;
    }
}

QString SclMmsView::PageLabel(const Location& location, bool selection) const {
    QString name = PageName(location.request.kind);
    if (!m_snapshot || location.snapshot != m_snapshot) return name;
    const auto& s = *m_snapshot;
    const auto attr = [&](int node) { return node >= 0 && node < int(s.analysis.document->nodes.size())
        ? s.analysis.document->attribute(node, "name") : QString{}; };
    if (location.request.kind == SclMmsQueryKind::Members) {
        auto ds = location.request.target;
        const auto report = s.mmsIndex.reports.constFind(location.request.report);
        if (report != s.mmsIndex.reports.cend()) ds = s.mms.reports[*report].dataSet.target();
        name += " · " + attr(ds);
        if (ds >= 0) name += " [" + SclTaskPath(*s.analysis.document, ds) + ']';
    }
    if (selection && location.selected) {
        const auto& key = *location.selected;
        if (key.kind == SclMmsRecordKind::Report) name += QStringLiteral(" · 报告 %1 [%2]").arg(attr(key.node), SclTaskPath(*s.analysis.document, key.node));
        else if (key.kind == SclMmsRecordKind::Member) name += QStringLiteral(" · 成员 #%1").arg(key.ordinal);
        else if (key.kind == SclMmsRecordKind::Signal || key.kind == SclMmsRecordKind::Address) name += " · " + key.field;
    }
    return name;
}

void SclMmsView::UpdateNavigation() {
    const bool shortWindow = window()->height() < 650;
    const bool narrow = width() < 600 || shortWindow;
    if (m_controlsLayout && m_pathLayout && shortWindow != m_compactPaths) {
        m_compactPaths = shortWindow;
        m_pathLayout->removeWidget(m_searchReturn); m_pathLayout->removeWidget(m_pathMenu);
        m_controlsLayout->removeWidget(m_searchReturn); m_controlsLayout->removeWidget(m_pathMenu);
        if (shortWindow) {
            m_controlsLayout->insertWidget(2, m_pathMenu); m_controlsLayout->insertWidget(3, m_searchReturn);
        } else { m_pathLayout->addWidget(m_searchReturn); m_pathLayout->addWidget(m_pathMenu); }
    }
    m_title->setVisible(!shortWindow);
    m_global->setVisible(!narrow); if (m_compactSearch) m_compactSearch->setVisible(narrow);
    m_back->setText(narrow ? QString() : QStringLiteral("后退")); m_back->setAccessibleName(QStringLiteral("后退"));
    m_forward->setText(narrow ? QString() : QStringLiteral("前进")); m_forward->setAccessibleName(QStringLiteral("前进"));
    m_pathMenu->setText(shortWindow ? QStringLiteral("查阅路径") : narrow ? QStringLiteral("路径") : QStringLiteral("完整查阅路径"));
    m_pathMenu->setToolTip(QStringLiteral("跳转到任一上层，或复制完整查阅路径"));
    // A stable path band prevents each long/short label from resizing both ADS panes.
    m_title->setFixedHeight(fontMetrics().lineSpacing() * (shortWindow ? 1 : 2) + 4);
    const auto current = PageLabel(CurrentLocation());
    const auto signature = QStringLiteral("%1/%2/%3/%4/%5/%6/").arg(m_ied).arg(m_request.scope.accessPoint)
        .arg(m_request.scope.server).arg(narrow).arg(shortWindow).arg(bool(m_snapshot)) + current;
    if (m_navigationSignature == signature && m_renderedParent == m_parent.get() && m_renderedOrigin == m_searchOrigin.get()) return;
    m_navigationSignature = signature; m_renderedParent = m_parent.get(); m_renderedOrigin = m_searchOrigin.get();
    ++m_pathEpoch; m_ancestors.clear();
    auto ancestor = m_parent;
    for (; ancestor && ancestor->snapshot == m_snapshot && m_ancestors.size() < 12; ancestor = ancestor->parent)
        m_ancestors.push_back(ancestor);
    const bool truncated = ancestor && ancestor->snapshot == m_snapshot;
    std::reverse(m_ancestors.begin(), m_ancestors.end());
    m_up->setEnabled(bool(m_snapshot && m_parent));
    const auto up = m_parent ? PageName(m_parent->request.kind) : QString{};
    m_up->setText(up.isEmpty() ? QStringLiteral("上一级") : QStringLiteral("上一级：%1").arg(up));
    m_up->setToolTip(up.isEmpty() ? QStringLiteral("已在当前 MMS 入口") : QStringLiteral("返回业务父级：%1").arg(PageLabel(*m_parent)));
    m_up->setAccessibleName(m_up->text());
    m_searchReturn->setVisible(bool(m_snapshot && m_searchOrigin));
    auto* menu = m_pathMenu->menu(); menu->clear();
    QStringList full, visible;
    QString scope = m_snapshot && m_ied >= 0 ? m_snapshot->analysis.document->attribute(m_ied, "name") : QStringLiteral("整个工程");
    if (m_snapshot && m_request.scope.accessPoint >= 0) scope += " / AP " + m_snapshot->analysis.document->attribute(m_request.scope.accessPoint, "name");
    if (m_request.scope.server >= 0) scope += QStringLiteral(" / Server");
    full << scope; if (!narrow) visible << scope.left(48).toHtmlEscaped();
    if (truncated) {
        const auto hint = QStringLiteral("仅展示最近 12 级；较早路径可逐级返回");
        full << hint; visible << QStringLiteral("…"); menu->addAction(hint)->setEnabled(false);
        m_pathMenu->setText(QStringLiteral("查阅路径"));
    }
    const auto epoch = m_pathEpoch;
    for (int i = 0; i < int(m_ancestors.size()); ++i) {
        const auto l = m_ancestors[i]; const auto label = PageLabel(*l); full << label;
        auto* action = menu->addAction(label.left(256), this, [this, l, epoch] { if (epoch == m_pathEpoch) OpenAncestor(l); });
        action->setToolTip(label.toHtmlEscaped());
        if (i >= int(m_ancestors.size()) - (narrow ? 1 : 2))
            visible << QStringLiteral("<a href=\"%1:%2\">%3</a>").arg(epoch).arg(i).arg(label.left(narrow ? 20 : 48).toHtmlEscaped());
    }
    full << current; visible << current.left(narrow ? 24 : 64).toHtmlEscaped();
    menu->addAction(current.left(256))->setEnabled(false);
    m_title->setText(visible.join(QStringLiteral(" › "))); m_title->setToolTip(full.join(QStringLiteral(" › ")).toHtmlEscaped());
    menu->addSeparator();
    menu->addAction(truncated ? QStringLiteral("复制已展示的查阅路径") : QStringLiteral("复制完整查阅路径"), this,
        [text = full.join(QStringLiteral(" › "))] { QApplication::clipboard()->setText(text); });
    m_pathMenu->setEnabled(bool(m_snapshot));
}

void SclMmsView::CompleteRestore() {
    const auto epoch = m_epoch;
    QTimer::singleShot(0, this, [this, epoch] {
        if (epoch != m_epoch || !m_snapshot || !isVisible()) return;
        const QScopedValueRollback<bool> guard(m_applying, true);
        if (m_restore) {
            const auto restore = *m_restore;
            if (restore.top && m_result) for (int i = 0; i < int(m_result->rows.size()); ++i)
                if (SameKey(m_result->rows[i].key, *restore.top)) {
                    m_table->scrollTo(m_table->model()->index(i, 0), QAbstractItemView::PositionAtTop);
                    m_table->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value() + restore.topOffset); break;
                }
            if (!restore.top && m_table->currentIndex().isValid()) m_table->scrollTo(m_table->currentIndex());
            if (m_detail && !restore.topField.isEmpty()) for (int i = 0; i < int(m_detail->fields.size()); ++i)
                if (m_detail->fields[i].key == restore.topField) {
                    m_fields->scrollTo(m_fields->model()->index(i, 0), QAbstractItemView::PositionAtTop);
                    m_fields->verticalScrollBar()->setValue(m_fields->verticalScrollBar()->value() + restore.fieldOffset); break;
                }
            m_table->horizontalScrollBar()->setValue(restore.horizontal);
            m_fields->horizontalScrollBar()->setValue(restore.fieldHorizontal);
            m_restore.reset();
        }
        UpdateNavigation(); emit LocationUpdated();
    });
}
}
