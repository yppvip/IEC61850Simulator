#pragma once
#include "SclAnalysisService.hpp"
#include <QWidget>
#include <QTimer>
#include <optional>

class QTableView; class QLabel; class QLineEdit; class QComboBox;
class QPushButton; class QGraphicsView; class QGraphicsScene;
class QToolButton;
namespace iec61850sim {
class SclMmsTableModel;
class SclMmsView final : public QWidget {
    Q_OBJECT
public:
    struct Location {
        std::shared_ptr<const SclSnapshot> snapshot;
        SclMmsRequest request;
        std::optional<SclMmsKey> selected, top;
        int column = 0, topOffset = 0;
        QString field, topField;
        int fieldOffset = 0;
    };
    explicit SclMmsView(QWidget* parent = nullptr);
    ~SclMmsView() override;
    QWidget* Inspector() const { return m_inspector; }
    void SetContext(std::shared_ptr<const SclSnapshot>, Scl::NodeId);
    void Open(SclMmsQueryKind);
    void OpenNode(Scl::NodeId);
    Location CurrentLocation() const;
    void RestoreLocation(const Location&);
    void Cancel();
    QTableView* Table() const { return m_table; }
    QTableView* Fields() const { return m_fields; }
    std::shared_ptr<const SclMmsResult> Result() const { return m_result; }
    std::optional<SclMmsKey> Selected() const { return m_selected; }
signals:
    void BeforeNavigate();
    void Navigated();
    void LocationUpdated();
    void SelectedNode(int node);
    void OpenEvidence(int node, const QString& label);
    void ScopeRequested(int ied);
    void ShowInspector();
    void ExportIssues();
private:
    void hideEvent(QHideEvent*) override;
    void showEvent(QShowEvent*) override;
    void Navigate(SclMmsRequest);
    void Run();
    void ApplyList();
    void Select(int row, bool navigate);
    void Detail();
    void ApplyDetail();
    void UpdateActions();
    void OpenSource();
    void Continue(SclMmsQueryKind);
    void RenderGraph();
    void Coverage();
    bool m_applying = false;
    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::NodeId m_ied = Scl::NoNode;
    SclMmsRequest m_request;
    std::optional<SclMmsKey> m_selected;
    std::optional<Location> m_restore;
    std::shared_ptr<const SclMmsResult> m_result, m_detail;
    SclMmsQuerySession m_list, m_details;
    QTimer m_delay;
    SclMmsTableModel *m_model, *m_fieldModel;
    QTableView *m_table, *m_fields;
    QWidget* m_inspector;
    QLabel *m_title, *m_status, *m_fieldTitle;
    QComboBox* m_route;
    QLineEdit* m_filter;
    QLineEdit* m_global;
    QPushButton *m_previous, *m_next, *m_open, *m_source;
    QToolButton* m_actions;
    QGraphicsView* m_graph;
    QGraphicsScene* m_scene;
};
}
