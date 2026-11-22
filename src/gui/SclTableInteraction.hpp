#pragma once

#include <QHash>
#include <QModelIndex>
#include <QSet>
#include <QObject>
#include <QTimer>
#include <functional>
#include <memory>

class QAbstractItemDelegate;
class QAbstractItemView;
class QMenu;
class QTableView;
class QTreeView;

namespace iec61850sim
{
struct SclReferenceTraceSelection;

class SclTableInteraction final : public QObject
{
    Q_OBJECT
public:
    enum { FullValueRole = Qt::UserRole + 40 };
    static constexpr int PreviewLimit = 4096;
    explicit SclTableInteraction(QTableView* view);
    explicit SclTableInteraction(QTreeView* view);
    explicit SclTableInteraction(QAbstractItemView* view);
    void SetModel(QAbstractItemModel* model);
    void SetEnabled(bool enabled);
    void EnableReferenceMenus() { m_referenceMenus = true; }
    using ReferenceTraceHandler = std::function<void(std::shared_ptr<const SclReferenceTraceSelection>)>;
    void SetReferenceTraceHandler(ReferenceTraceHandler handler) { m_traceHandler = std::move(handler); }
    const ReferenceTraceHandler& TraceHandler() const { return m_traceHandler; }
    QAbstractItemView* View() const { return m_view; }
    quint64 ReferenceEpoch() const { return m_referenceEpoch; }

signals:
    void OpenSource();
    void PopulateContextMenu(QMenu& menu, const QModelIndex& index);

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override;

private:
    SclTableInteraction(QAbstractItemView* view, QTableView* table, QTreeView* tree);
    void Schedule();
    void Invalidate();
    void Measure();
    void MeasureTree();
    void Copy(const QModelIndex& index, bool row);
    void ShowFull(const QModelIndex& index);
    bool OpenContextMenu(const QPoint& global, bool keyboard);

    QAbstractItemView* m_view;
    QTableView* m_table;
    QTreeView* m_tree;
    QAbstractItemDelegate* m_defaultDelegate;
    QAbstractItemDelegate* m_wrapDelegate;
    QTimer m_timer;
    QHash<int, int> m_heights;
    QSet<int> m_measured;
    QHash<QPersistentModelIndex, int> m_treeHeights;
    QList<QMetaObject::Connection> m_connections;
    int m_cursor = -1;
    bool m_enabled = false;
    bool m_referenceMenus = false;
    quint64 m_referenceEpoch = 0;
    bool m_measuring = false;
    bool m_stale = false;
    bool m_swallowRelease = false;
    ReferenceTraceHandler m_traceHandler;
};
}
