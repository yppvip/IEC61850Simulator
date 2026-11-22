#pragma once

#include "SclReferenceTrace.hpp"

#include <QWidget>

class QComboBox;
class QAbstractItemView;
class QGraphicsScene;
class QGraphicsView;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QSplitter;
class QTreeWidget;

namespace iec61850sim
{
struct SclReferenceTraceSelection;
struct SclReferenceTraceLocation;

class SclReferenceTraceView final : public QWidget
{
    Q_OBJECT
public:
    explicit SclReferenceTraceView(QWidget* parent);
    ~SclReferenceTraceView() override;
    void Open(std::shared_ptr<const SclReferenceTraceSelection>, QList<int> sourceSplitterSizes);
    bool Restore(std::shared_ptr<SclReferenceTraceLocation>, const std::shared_ptr<const SclSnapshot>&);
    void Clear();
    std::shared_ptr<SclReferenceTraceLocation> Location() const { return m_location; }
    QAbstractItemView* SourceView() const;
    Scl::NodeId OpenedEvidence() const;
    QList<int> SourceSplitterSizes() const;

signals:
    void ReturnToSource();
    void OpenEvidence(int node);

protected:
    void hideEvent(QHideEvent*) override;
    void showEvent(QShowEvent*) override;

private:
    struct Job {
        int id;
        int candidate;
        std::shared_ptr<SclReferenceTraceLocation> location;
        std::shared_ptr<Scl::TaskControl> control;
    };
    void Cancel();
    void Queue(int id);
    void StartPending();
    void Receive();
    void PopulateChooser();
    void Select(int id);
    void Toggle(int id);
    void FirstLayer();
    void Refresh();
    void Draw();
    void Fit();
    void RefreshDetail();

    std::shared_ptr<SclReferenceTraceLocation> m_location;
    std::optional<Job> m_running, m_pending;
    QFutureWatcher<SclReferenceTrace>* m_worker;
    QComboBox* m_choices;
    QLabel* m_title;
    QLabel* m_status;
    QPushButton* m_expand;
    QPushButton* m_source;
    QPushButton* m_fit;
    QPushButton* m_treeMode;
    QSplitter* m_split;
    QStackedWidget* m_views;
    QTreeWidget* m_tree;
    QGraphicsView* m_graph;
    QGraphicsScene* m_scene;
    QPlainTextEdit* m_detail;
};
}
