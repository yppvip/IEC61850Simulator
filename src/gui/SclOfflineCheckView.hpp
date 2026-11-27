#pragma once
#include "SclAnalysisService.hpp"
#include <QWidget>

class QComboBox; class QLineEdit; class QTextBrowser; class QPushButton; class QScrollArea;
namespace iec61850sim {
struct SclCheckLocation {
    std::shared_ptr<const SclSnapshot> snapshot;
    SclCheckInput input;
    std::shared_ptr<const SclMmsResult> result;
    int scroll = 0;
    Scl::NodeId source = Scl::NoNode;
};
class SclOfflineCheckView final : public QWidget {
    Q_OBJECT
public:
    explicit SclOfflineCheckView(QWidget* = nullptr);
    void SetContext(std::shared_ptr<const SclSnapshot>, SclMmsScope);
    void Open(const SclCheckInput& = {});
    std::shared_ptr<SclCheckLocation> Location() const;
    bool Restore(std::shared_ptr<SclCheckLocation>);
    void Cancel();
    std::shared_ptr<const SclMmsResult> Result() const { return m_result; }
    Scl::NodeId OpenedSource() const { return m_sourceNode; }
signals:
    void Changed();
    void OpenTarget(const SclMmsRow&);
    void OpenSource(int);
    void InspectConfirmed(int);
    void Back();
private:
    void Invalidate();
    void Run();
    void Render();
    void hideEvent(QHideEvent*) override;
    void showEvent(QShowEvent*) override;
    SclCheckInput Input() const;
    std::shared_ptr<const SclSnapshot> m_snapshot;
    SclMmsScope m_scope;
    SclMmsQuerySession m_query;
    quint64 m_request = 0, m_epoch = 0;
    bool m_applying = false;
    Scl::NodeId m_device = Scl::NoNode;
    Scl::NodeId m_sourceNode = Scl::NoNode;
    std::shared_ptr<const SclMmsResult> m_result;
    QComboBox *m_kind, *m_candidates;
    QLineEdit *m_original, *m_domain, *m_fc;
    QTextBrowser* m_evidence;
    QPushButton *m_open, *m_inspect, *m_choose;
    QScrollArea* m_scroll;
};
}
