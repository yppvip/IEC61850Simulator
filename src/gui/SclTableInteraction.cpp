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
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTextLayout>
#include <QToolTip>
#include <QVBoxLayout>
#include <QtMath>
#include <utility>

namespace iec61850sim
{
namespace
{
class WrappedCell final : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

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
    : QObject(view), m_view(view), m_defaultDelegate(view->itemDelegate()),
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
    connect(view->horizontalHeader(), &QHeaderView::geometriesChanged, this, &SclTableInteraction::Schedule);
    connect(view->horizontalHeader(), &QHeaderView::sectionResized, this, &SclTableInteraction::Invalidate);
    SetModel(view->model());
}

void SclTableInteraction::SetModel(QAbstractItemModel* model)
{
    for (const auto& connection : std::as_const(m_connections))
        disconnect(connection);
    m_connections.clear();
    if (!model)
        return;
    m_connections << connect(model, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        for (auto it = m_heights.cbegin(); it != m_heights.cend(); ++it)
            m_view->setRowHeight(it.key(), m_view->verticalHeader()->defaultSectionSize());
        m_heights.clear();
        m_measured.clear();
        m_cursor = -1;
        m_swallowRelease = false;
    });
    m_connections << connect(model, &QAbstractItemModel::modelReset, this, &SclTableInteraction::Invalidate);
    const auto clearRows = [this] {
        for (auto it = m_heights.cbegin(); it != m_heights.cend(); ++it)
            m_view->setRowHeight(it.key(), m_view->verticalHeader()->defaultSectionSize());
        m_heights.clear();
        m_measured.clear();
        m_cursor = -1;
    };
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
    m_view->setWordWrap(enabled);
    m_view->setTextElideMode(enabled ? Qt::ElideNone : Qt::ElideRight);
    m_view->setVerticalScrollMode(enabled ? QAbstractItemView::ScrollPerPixel : QAbstractItemView::ScrollPerItem);
    if (enabled)
        Invalidate();
    else
    {
        m_timer.stop();
        m_swallowRelease = false;
        for (auto it = m_heights.cbegin(); it != m_heights.cend(); ++it)
            m_view->setRowHeight(it.key(), m_view->verticalHeader()->defaultSectionSize());
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

void SclTableInteraction::Measure()
{
    if (!m_enabled || !m_view->isVisible() || !m_view->model() || m_view->model()->rowCount() == 0)
        return;
    const int top = m_view->rowAt(0);
    if (top < 0)
        return;
    const int offset = -m_view->rowViewportPosition(top);
    const auto visibleEnd = [this, top] {
        const int bottom = m_view->rowAt(m_view->viewport()->height() - 1);
        return qMin(m_view->model()->rowCount() - 1, (bottom < 0 ? top : bottom) + 4);
    };
    const int last = visibleEnd();
    const int first = qMax(0, top - 2);
    m_measuring = true;
    for (auto it = m_heights.begin(); it != m_heights.end();)
    {
        if (it.key() < first || it.key() > last)
        {
            m_view->setRowHeight(it.key(), m_view->verticalHeader()->defaultSectionSize());
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
    auto* header = m_view->verticalHeader();
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
            if (m_view->isColumnHidden(col))
                continue;
            option.rect.setWidth(m_view->columnWidth(col) - (m_view->showGrid() ? 1 : 0));
            height = qMax(height, static_cast<WrappedCell*>(m_wrapDelegate)->Height(option,
                m_view->model()->index(row, col)) + (m_view->showGrid() ? 1 : 0));
        }
        if (m_view->rowHeight(row) != height)
            m_view->setRowHeight(row, height);
        if (height != header->defaultSectionSize())
            m_heights.insert(row, height);
        else
            m_heights.remove(row);
        m_measured.insert(row);
        if (budget.elapsed() >= 6)
            break;
    }
    auto* scroll = m_view->verticalScrollBar();
    scroll->setValue(header->sectionPosition(top) + qBound(0, offset, m_view->rowHeight(top) - 1));
    m_measuring = false;
    if (m_cursor <= visibleEnd())
        Schedule();
}

void SclTableInteraction::Copy(const QModelIndex& index, bool row)
{
    if (!index.isValid())
        return;
    auto value = index.data(FullValueRole).toString();
    if (row)
    {
        QStringList fields;
        auto* header = m_view->horizontalHeader();
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

bool SclTableInteraction::eventFilter(QObject* receiver, QEvent* event)
{
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
            if (index.isValid() && !key->isAutoRepeat())
                emit OpenSource();
            return true;
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
    case QEvent::ContextMenu:
    {
        const auto* context = static_cast<QContextMenuEvent*>(event);
        const QPersistentModelIndex index(context->reason() == QContextMenuEvent::Keyboard
            ? m_view->currentIndex() : m_view->indexAt(m_view->viewport()->mapFromGlobal(context->globalPos())));
        if (!index.isValid())
            break;
        m_view->setCurrentIndex(index);
        QMenu menu(m_view);
        const auto* cell = menu.addAction(tr("复制单元格"));
        const auto* row = menu.addAction(tr("复制行"));
        const auto* full = menu.addAction(tr("查看完整值"));
        const auto* selected = menu.exec(context->globalPos());
        if (selected == cell || selected == row)
            Copy(index, selected == row);
        else if (selected == full)
            ShowFull(index);
        return true;
    }
    default:
        break;
    }
    return false;
}
}
