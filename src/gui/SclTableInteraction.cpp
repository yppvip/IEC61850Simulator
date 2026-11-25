#include "SclTableInteraction.hpp"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialog>
#include <QElapsedTimer>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextLayout>
#include <QToolTip>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtMath>
#include <functional>
#include <utility>

namespace iec61850sim
{
namespace
{
class WrappedCell final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    std::function<int(const QModelIndex&)> treeHeight;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (treeHeight)
            return {0, treeHeight(index)};
        return QStyledItemDelegate::sizeHint(option, index);
    }

    int Height(QStyleOptionViewItem option, const QModelIndex& index) const
    {
        initStyleOption(&option, index);
        return Layout(option, nullptr, qMax(1, option.rect.width() - Inset(option) - 5));
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        auto cell = option;
        initStyleOption(&cell, index);
        const auto style = cell.widget ? cell.widget->style() : QApplication::style();
        auto background = cell;
        background.text.clear();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, cell.widget);
        const int left = Inset(cell);
        painter->save();
        painter->setClipRect(cell.rect, Qt::IntersectClip);
        painter->translate(cell.rect.left() + left, cell.rect.top());
        painter->setPen(cell.palette.color(cell.state.testFlag(QStyle::State_Selected)
            ? QPalette::HighlightedText : QPalette::Text));
        Layout(cell, painter, qMax(1, cell.rect.width() - left - 5));
        painter->restore();
    }

private:
    static int Inset(const QStyleOptionViewItem& cell)
    {
        return cell.icon.isNull() ? 5 : cell.decorationSize.width() + 9;
    }

    static int Layout(const QStyleOptionViewItem& option, QPainter* painter, int width)
    {
        auto text = option.text;
        text.replace(QChar::LineFeed, QChar::LineSeparator);
        QTextLayout layout(text, option.font);
        QTextOption format;
        format.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        layout.setTextOption(format);
        qreal y = 4;
        layout.beginLayout();
        for (auto line = layout.createLine(); line.isValid(); line = layout.createLine())
        {
            line.setLineWidth(width);
            line.setPosition(QPointF(0, y));
            y += line.height();
        }
        layout.endLayout();
        if (painter)
            layout.draw(painter, {});
        return qCeil(y) + 4;
    }
};
}

SclTableInteraction::SclTableInteraction(QTableView* view)
    : SclTableInteraction(view, view, nullptr)
{
}

SclTableInteraction::SclTableInteraction(QTreeView* view)
    : SclTableInteraction(view, nullptr, view)
{
}

SclTableInteraction::SclTableInteraction(QAbstractItemView* view)
    : SclTableInteraction(view, qobject_cast<QTableView*>(view), qobject_cast<QTreeView*>(view))
{
}

SclTableInteraction::SclTableInteraction(QAbstractItemView* view, QTableView* table, QTreeView* tree)
    : QObject(view), m_view(view), m_table(table), m_tree(tree), m_defaultDelegate(view->itemDelegate()),
      m_wrapDelegate(new WrappedCell(view))
{
    view->installEventFilter(this);
    view->viewport()->installEventFilter(this);
    m_timer.setSingleShot(true);
    connect(&m_timer, &QTimer::timeout, this, &SclTableInteraction::Measure);
    connect(view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
        if (!m_measuring)
        {
            m_cursor = -1;
            Schedule();
        }
    });
    auto* header = table ? table->horizontalHeader() : tree->header();
    connect(header, &QHeaderView::geometriesChanged, this, &SclTableInteraction::Schedule);
    connect(header, &QHeaderView::sectionResized, this, &SclTableInteraction::Invalidate);
    if (tree)
    {
        static_cast<WrappedCell*>(m_wrapDelegate)->treeHeight = [this](const QModelIndex& index) {
            return m_treeHeights.value(index.siblingAtColumn(0), m_view->fontMetrics().height() + 8);
        };
        connect(tree, &QTreeView::expanded, this, &SclTableInteraction::Schedule);
        connect(tree, &QTreeView::collapsed, this, &SclTableInteraction::Schedule);
    }
    SetModel(view->model());
}

void SclTableInteraction::SetModel(QAbstractItemModel* model)
{
    for (const auto& connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    if (!model)
        return;
    const auto clearRows = [this] {
        CancelRestore();
        for (auto it = m_heights.cbegin(); it != m_heights.cend(); ++it)
            m_table->setRowHeight(it.key(), m_table->verticalHeader()->defaultSectionSize());
        m_heights.clear();
        m_measured.clear();
        m_treeHeights.clear();
        m_cursor = -1;
    };
    m_connections << connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this, clearRows] {
        clearRows();
        m_swallowRelease = false;
    });
    m_connections << connect(model, &QAbstractItemModel::modelReset, this, &SclTableInteraction::Invalidate);
    m_connections << connect(model, &QAbstractItemModel::rowsAboutToBeInserted, this, clearRows);
    m_connections << connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, this, clearRows);
    m_connections << connect(model, &QAbstractItemModel::layoutAboutToBeChanged, this, clearRows);
    m_connections << connect(model, &QAbstractItemModel::rowsInserted, this, &SclTableInteraction::Invalidate);
    m_connections << connect(model, &QAbstractItemModel::rowsRemoved, this, &SclTableInteraction::Invalidate);
    m_connections << connect(model, &QAbstractItemModel::dataChanged, this, &SclTableInteraction::Invalidate);
    m_connections << connect(model, &QAbstractItemModel::layoutChanged, this, &SclTableInteraction::Invalidate);
}

void SclTableInteraction::SetEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    m_view->setItemDelegate(enabled ? m_wrapDelegate : m_defaultDelegate);
    if (m_table)
        m_table->setWordWrap(enabled);
    else
        m_tree->setWordWrap(enabled);
    m_view->setTextElideMode(enabled ? Qt::ElideNone : Qt::ElideRight);
    m_view->setVerticalScrollMode(enabled ? QAbstractItemView::ScrollPerPixel : QAbstractItemView::ScrollPerItem);
    if (enabled)
        Invalidate();
    else
    {
        m_timer.stop();
        m_swallowRelease = false;
        for (auto it = m_heights.cbegin(); it != m_heights.cend(); ++it)
            m_table->setRowHeight(it.key(), m_table->verticalHeader()->defaultSectionSize());
        m_heights.clear();
        m_measured.clear();
    }
}

void SclTableInteraction::Schedule()
{
    if (m_enabled && !m_measuring && !m_timer.isActive())
        m_timer.start(0);
}

void SclTableInteraction::Invalidate()
{
    m_stale = true;
    m_cursor = -1;
    Schedule();
}

void SclTableInteraction::CancelRestore()
{
    ++m_restoreGeneration;
    m_restoreCell = QPersistentModelIndex{};
}

void SclTableInteraction::RestorePosition(const QModelIndex& selected, const QModelIndex& top, int offset, int horizontal)
{
    CancelRestore();
    if (!m_table || !m_enabled) return;
    m_restoreCell = selected;
    if (top.isValid()) {
        m_table->scrollTo(top, QAbstractItemView::PositionAtTop);
        m_table->verticalScrollBar()->setValue(m_table->verticalScrollBar()->value() + offset);
    }
    m_table->horizontalScrollBar()->setValue(horizontal);
    if (selected.isValid() && !m_table->viewport()->visibleRegion().intersects(m_table->visualRect(selected)))
        m_table->scrollTo(selected, QAbstractItemView::EnsureVisible);
    Schedule();
}

void SclTableInteraction::Measure()
{
    if (!m_enabled || !m_view->isVisible() || !m_view->model() || m_view->model()->rowCount() == 0)
        return;
    if (m_tree)
    {
        MeasureTree();
        return;
    }
    const int top = m_table->rowAt(0);
    if (top < 0)
        return;
    const int offset = -m_table->rowViewportPosition(top);
    const auto visibleEnd = [this, top] {
        const int bottom = m_table->rowAt(m_view->viewport()->height() - 1);
        return qMin(m_view->model()->rowCount() - 1, (bottom < 0 ? top : bottom) + 4);
    };
    const int last = visibleEnd();
    const int first = qMax(0, top - 2);
    m_measuring = true;
    for (auto it = m_heights.begin(); it != m_heights.end();)
    {
        if (it.key() < first || it.key() > last)
        {
            m_table->setRowHeight(it.key(), m_table->verticalHeader()->defaultSectionSize());
            it = m_heights.erase(it);
        }
        else
            ++it;
    }
    if (m_stale)
    {
        m_stale = false;
        m_cursor = first;
        m_measured.clear();
    }
    for (auto it = m_measured.begin(); it != m_measured.end();)
        if (*it < first || *it > last)
            it = m_measured.erase(it);
        else
            ++it;
    QElapsedTimer budget;
    budget.start();
    auto* header = m_table->verticalHeader();
    QStyleOptionViewItem option;
    option.initFrom(m_view);
    option.widget = m_view;
    option.font = m_view->font();
    option.decorationSize = m_view->iconSize().isValid() ? m_view->iconSize() : QSize(16, 16);
    for (int row = m_cursor < first ? first : m_cursor; row <= last; ++row)
    {
        m_cursor = row + 1;
        if (m_measured.contains(row))
            continue;
        int height = header->defaultSectionSize();
        for (int col = 0; col < m_view->model()->columnCount(); ++col)
        {
            if (m_table->isColumnHidden(col))
                continue;
            option.rect.setWidth(m_table->columnWidth(col) - (m_table->showGrid() ? 1 : 0));
            height = qMax(height, static_cast<WrappedCell*>(m_wrapDelegate)->Height(option,
                m_view->model()->index(row, col)) + (m_table->showGrid() ? 1 : 0));
        }
        if (m_table->rowHeight(row) != height)
            m_table->setRowHeight(row, height);
        if (height != header->defaultSectionSize())
            m_heights.insert(row, height);
        else
            m_heights.remove(row);
        m_measured.insert(row);
        if (budget.elapsed() >= 6)
            break;
    }
    auto* scroll = m_view->verticalScrollBar();
    scroll->setValue(header->sectionPosition(top) + qBound(0, offset, m_table->rowHeight(top) - 1));
    if (m_restoreCell.isValid() && !m_table->viewport()->visibleRegion().intersects(m_table->visualRect(m_restoreCell)))
        m_table->scrollTo(m_restoreCell, QAbstractItemView::EnsureVisible);
    m_measuring = false;
    const auto elapsedUs = budget.nsecsElapsed() / 1000;
    m_view->setProperty("SclMaxMeasureUs", qMax(m_view->property("SclMaxMeasureUs").toLongLong(), elapsedUs));
    const int restoredTop = m_table->rowAt(0);
    bool pendingVisible = false;
    for (int row = qMax(0, restoredTop - 2); row <= visibleEnd(); ++row)
        if (!m_measured.contains(row)) { pendingVisible = true; break; }
    if (pendingVisible) m_cursor = -1;
    if (m_cursor <= visibleEnd() || pendingVisible)
        Schedule();
    else if (m_restoreCell.isValid()) {
        const auto generation = m_restoreGeneration;
        QTimer::singleShot(0, this, [this, generation] {
            if (generation == m_restoreGeneration && !m_timer.isActive() && !m_stale) CancelRestore();
        });
    }
}

void SclTableInteraction::MeasureTree()
{
    const auto top = m_tree->indexAt(QPoint(0, 0)).siblingAtColumn(0);
    if (!top.isValid())
        return;
    const auto offset = -m_tree->visualRect(top).top();
    const bool stale = std::exchange(m_stale, false);
    if (stale)
        m_treeHeights.clear();
    bool changed = stale;
    bool measured = false;
    QElapsedTimer timer;
    timer.start();
    m_measuring = true;
    QStyleOptionViewItem option;
    option.initFrom(m_tree);
    option.widget = m_tree;
    option.font = m_tree->font();
    for (auto index = top; index.isValid() && m_tree->visualRect(index).top() < m_tree->viewport()->height();
         index = m_tree->indexBelow(index))
    {
        if (m_treeHeights.contains(index))
            continue;
        measured = true;
        int height = m_tree->fontMetrics().height() + 8;
        for (int col = 0; col < m_tree->model()->columnCount(); ++col)
        {
            if (m_tree->isColumnHidden(col))
                continue;
            const auto cell = index.siblingAtColumn(col);
            option.rect = m_tree->visualRect(cell);
            height = qMax(height, static_cast<WrappedCell*>(m_wrapDelegate)->Height(option, cell));
        }
        m_treeHeights.insert(index, height);
        changed |= height != m_tree->fontMetrics().height() + 8;
    }
    if (changed)
    {
        const auto horizontal = m_tree->horizontalScrollBar()->value();
        m_tree->doItemsLayout();
        m_tree->scrollTo(top, QAbstractItemView::PositionAtTop);
        m_tree->horizontalScrollBar()->setValue(horizontal);
        m_tree->verticalScrollBar()->setValue(m_tree->verticalScrollBar()->value()
            + qBound(0, offset, qMax(0, m_tree->visualRect(top).height() - 1)));
    }
    m_measuring = false;
    if (measured)
        m_tree->setProperty("SclTreeMeasureUs", qMax(qint64(1), timer.nsecsElapsed() / 1000));
    if (changed)
        Schedule();
}

void SclTableInteraction::Copy(const QModelIndex& index, bool row)
{
    if (!index.flags().testFlag(Qt::ItemIsSelectable))
        return;
    auto value = index.data(FullValueRole).toString();
    if (row)
    {
        QStringList fields;
        auto* header = m_table ? m_table->horizontalHeader() : m_tree->header();
        for (int visual = 0; visual < header->count(); ++visual)
        {
            const int col = header->logicalIndex(visual);
            if (!header->isSectionHidden(col))
                fields << index.siblingAtColumn(col).data(FullValueRole).toString();
        }
        value = fields.join('\t');
    }
    if (!value.isEmpty())
        QApplication::clipboard()->setText(value);
    QToolTip::showText(m_view->viewport()->mapToGlobal(m_view->visualRect(index).bottomLeft()),
        value.isEmpty() ? tr("无可复制内容") : tr("已复制 %1 字符").arg(value.size()), m_view, {}, 900);
}

void SclTableInteraction::ShowFull(const QModelIndex& index)
{
    if (!index.isValid())
        return;
    auto* dialog = new QDialog(m_view);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("完整单元格内容"));
    auto* layout = new QVBoxLayout(dialog);
    auto* text = new QPlainTextEdit(dialog);
    text->setReadOnly(true);
    text->setPlainText(index.data(FullValueRole).toString());
    layout->addWidget(text);
    dialog->resize(720, 420);
    dialog->open();
}

bool SclTableInteraction::OpenContextMenu(const QPoint& global, bool keyboard)
{
    const QPersistentModelIndex index(keyboard ? m_view->currentIndex()
        : m_view->indexAt(m_view->viewport()->mapFromGlobal(global)));
    if (!index.flags().testFlag(Qt::ItemIsSelectable)) return false;

    if (!m_referenceMenus) m_view->setCurrentIndex(index);
    const QPointer<SclTableInteraction> alive(this);
    QMenu menu;
    const auto* cell = m_enabled ? menu.addAction(tr("复制单元格")) : nullptr;
    const auto* row = m_enabled ? menu.addAction(tr("复制行")) : nullptr;
    const auto* full = m_enabled ? menu.addAction(tr("查看完整值")) : nullptr;
    const auto* source = m_enabled ? menu.addAction(tr("查看原文")) : nullptr;
    for (auto* action : m_view->actions()) {
        if (!m_referenceMenus) { menu.addAction(action); continue; }
        auto* copy = menu.addAction(action->icon(), action->text(), this, [this, index, original = QPointer<QAction>(action)] {
            if (original && index.isValid()) {
                m_view->setCurrentIndex(index);
                original->trigger();
            }
        });
        copy->setEnabled(action->isEnabled());
    }
    emit PopulateContextMenu(menu, index);
    if (menu.isEmpty()) return false;

    const auto* selected = menu.exec(global);
    if (!alive || !selected || !index.isValid()) return true;
    if (selected == cell || selected == row) Copy(index, selected == row);
    else if (selected == full) ShowFull(index);
    else if (selected == source) {
        m_view->setCurrentIndex(index);
        emit OpenSource();
    }
    return true;
}

bool SclTableInteraction::eventFilter(QObject* receiver, QEvent* event)
{
    if (event->type() == QEvent::Wheel || event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::KeyPress || event->type() == QEvent::Hide) CancelRestore();
    if (event->type() == QEvent::ContextMenu && (m_enabled || m_referenceMenus)) {
        const auto* context = static_cast<QContextMenuEvent*>(event);
        return OpenContextMenu(context->globalPos(), context->reason() == QContextMenuEvent::Keyboard);
    }
    if (m_referenceMenus) {
        if (event->type() == QEvent::Hide || event->type() == QEvent::EnabledChange) ++m_referenceEpoch;
        // A reference menu uses the hit row without selecting it or moving history.
        if (receiver == m_view->viewport() && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonRelease)
            && static_cast<QMouseEvent*>(event)->button() == Qt::RightButton) return true;
        if (event->type() == QEvent::KeyPress) {
            const auto* key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Menu || (key->key() == Qt::Key_F10 && key->modifiers() == Qt::ShiftModifier)) {
                const auto rect = m_view->visualRect(m_view->currentIndex());
                return OpenContextMenu(m_view->viewport()->mapToGlobal(rect.center()), true);
            }
        }
    }
    if (!m_enabled)
        return false;
    switch (event->type())
    {
    case QEvent::Show:
        Schedule();
        break;
    case QEvent::Resize:
        m_cursor = -1;
        Schedule();
        break;
    case QEvent::FontChange:
    case QEvent::DevicePixelRatioChange:
    case QEvent::StyleChange:
        Invalidate();
        break;
    case QEvent::MouseButtonPress:
        m_swallowRelease = false;
        break;
    case QEvent::MouseButtonDblClick:
    {
        if (receiver != m_view->viewport())
            break;
        const auto* mouse = static_cast<QMouseEvent*>(event);
        const auto index = m_view->indexAt(mouse->position().toPoint());
        if (mouse->button() == Qt::LeftButton && index.isValid())
        {
            if (m_table)
                m_view->setCurrentIndex(index);
            Copy(index, false);
            m_swallowRelease = true;
            return true;
        }
        break;
    }
    case QEvent::MouseButtonRelease:
        if (m_swallowRelease && static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton)
        {
            m_swallowRelease = false;
            return true;
        }
        break;
    case QEvent::KeyPress:
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        const auto index = m_view->currentIndex();
        if (key->matches(QKeySequence::Copy) || (key->key() == Qt::Key_C
            && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)))
        {
            Copy(index, key->modifiers().testFlag(Qt::ShiftModifier));
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)
        {
            if (index.flags().testFlag(Qt::ItemIsSelectable) && !key->isAutoRepeat())
                emit OpenSource();
            return !m_tree || index.flags().testFlag(Qt::ItemIsSelectable);
        }
        break;
    }
    case QEvent::ShortcutOverride:
    {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->matches(QKeySequence::Copy) || (key->key() == Qt::Key_C
            && key->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier)))
        {
            event->accept();
            return true;
        }
        break;
    }
    default:
        break;
    }
    return false;
}
}
