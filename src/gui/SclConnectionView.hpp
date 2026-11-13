#pragma once

#include "SclConnections.hpp"
#include <QTimer>
#include <QWidget>
#include <optional>

class QCheckBox;
class QComboBox;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QTreeWidget;

namespace iec61850sim
{
class SclConnectionView final : public QWidget
{
    Q_OBJECT
public:
    struct Location
    {
        Scl::NodeId control = Scl::NoNode, receiver = Scl::NoNode, input = Scl::NoNode;
        QString filter, service;
        int offset = 0, member = 0;
        bool mappedOnly = false;
    };
    explicit SclConnectionView(QWidget* parent = nullptr);
    ~SclConnectionView() override;
    // The enclosing scroll area keeps hidden-page minima out of the ADS layout
    // while preserving access to actions in narrow windows and at high DPI.
    QSize minimumSizeHint() const override { return {660, 360}; }
    void SetContext(std::shared_ptr<const SclSnapshot> snapshot, Scl::NodeId scope);
    void SetService(const QString& service);
    Location CurrentLocation() const;
    void RestoreLocation(const Location& location);

signals:
    void OpenEvidence(int node);
    void ScopeChanged(int node);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void Schedule();
    void Reload();
    void SelectConnection(int index);
    void LoadMembers();
    void SelectMember(int row);
    void Cancel();
    void ClearDetail();
    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::NodeId m_scope = Scl::NoNode;
    std::shared_ptr<Scl::TaskControl> m_query, m_selection;
    SclConnectionList m_list;
    SclConnectionDetail m_detail;
    int m_current = -1, m_offset = 0;
    bool m_dirty = true;
    std::optional<Location> m_restore;
    QTimer m_delay;
    QLineEdit* m_filter;
    QComboBox* m_service;
    QLabel *m_stats, *m_breadcrumb, *m_summary, *m_paging;
    QTreeWidget* m_tree;
    QTableWidget* m_members;
    QGraphicsView* m_graph;
    QTextBrowser* m_inspector;
    QCheckBox* m_mappedOnly;
    QPushButton *m_previous, *m_next, *m_source, *m_input;
    QComboBox* m_evidence;
    QComboBox* m_graphMember;
};
}
