#pragma once

#include <QHash>
#include <QModelIndex>
#include <QSet>
#include <QObject>
#include <QTimer>

class QAbstractItemDelegate;
class QTableView;

namespace iec61850sim
{
class SclTableInteraction final : public QObject
{
    Q_OBJECT
public:
    enum { FullValueRole = Qt::UserRole + 40 };
    static constexpr int PreviewLimit = 4096;
    explicit SclTableInteraction(QTableView* view);
    void SetModel(QAbstractItemModel* model);
    void SetEnabled(bool enabled);

signals:
    void OpenSource();

protected:
    bool eventFilter(QObject* receiver, QEvent* event) override;

private:
    void Schedule();
    void Invalidate();
    void Measure();
    void Copy(const QModelIndex& index, bool row);
    void ShowFull(const QModelIndex& index);

    QTableView* m_view;
    QAbstractItemDelegate* m_defaultDelegate;
    QAbstractItemDelegate* m_wrapDelegate;
    QTimer m_timer;
    QHash<int, int> m_heights;
    QSet<int> m_measured;
    QList<QMetaObject::Connection> m_connections;
    int m_cursor = -1;
    bool m_enabled = false;
    bool m_measuring = false;
    bool m_stale = false;
    bool m_swallowRelease = false;
};
}
