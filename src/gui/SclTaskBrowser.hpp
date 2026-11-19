#pragma once

#include "SclAnalysisService.hpp"
#include "SclTaskPages.hpp"
#include "SclConnectionView.hpp"
#include "SclMmsView.hpp"
#include <QTimer>
#include <optional>

class QComboBox;
class QAbstractItemModel;

namespace iec61850sim
{
class SclTaskTableModel;

class SclTaskBrowser final : public QObject
{
    Q_OBJECT
public:
    struct Location
    {
        SclTaskPages::Page page = SclTaskPages::Overview;
        QString filter;
        bool inputs = false;
        std::optional<SclTaskRow> row;
        int scroll = 0;
        SclConnectionView::Location connection;
        std::optional<SclTaskRow> top;
        int topOffset = 0;
        SclConnectionView::Location publication;
        SclMmsView::Location mms;
    };

    explicit SclTaskBrowser(QWidget* parent);
    ~SclTaskBrowser() override;
    SclTaskPages* View() const { return m_view; }
    void SetSnapshot(std::shared_ptr<const SclSnapshot> snapshot);
    void SetScope(Scl::NodeId ied);
    Location CurrentLocation() const;
    void RestoreLocation(const Location& location);
    bool IsSignalPage() const;
    bool IsPublicationPage() const;
    bool IsMmsPage() const { return m_page == SclTaskPages::Mms; }
    QString SignalPath() const;
    void LocateSignal();
    void OpenSignalEvidence(int index);

signals:
    void ScopeChanged(int ied);
    void OpenEvidence(int node, int finding);
    void OpenSignalSource(int node, const QString& label);
    void SignalDetailChanged(const QString& path, const QString& html);
    void SignalPageChanged();
    void SignalSelectionChanged();

private:
    void Schedule();
    void RunFilter();
    void SelectRow(const QModelIndex& row);
    void UpdateOverview();
    void Cancel();
    void PresentSignal(const QString& message = {});
    SclTaskKind Kind() const;

    SclTaskPages* m_view;
    SclTaskTableModel* m_model;
    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::NodeId m_ied = Scl::NoNode;
    SclTaskPages::Page m_page = SclTaskPages::Overview;
    bool m_inputs = false;
    QString m_filter;
    SclTaskDetail m_detail;
    std::optional<SclTaskRow> m_signal;
    std::optional<Location> m_restore;
    std::shared_ptr<Scl::TaskControl> m_query;
    std::shared_ptr<Scl::TaskControl> m_selection;
    QTimer m_delay;
};
}
