#pragma once

#include "SclAnalysisService.hpp"
#include "SclSearch.hpp"
#include <QWidget>
#include <optional>

class QAbstractItemView;
class QLabel;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QTimer;
class QVBoxLayout;
class QTextBrowser;
class QToolButton;
class QPushButton;
class QTabWidget;
class QTableView;
class QTreeView;
namespace ads { class CDockManager; class CDockWidget; }

namespace iec61850sim
{
class SclTreeModel;
class SclTableModel;
class SclSearchSession;
class SclResultsModel;
class SclSourcePreview;
class SclTopologyFiles;

class SclWorkspace final : public QWidget
{
public:
    explicit SclWorkspace(QWidget* parent = nullptr);
    ~SclWorkspace() override;
    bool UpdateSession(std::shared_ptr<const SclSnapshot> snapshot, const Scl::Result& failure);

protected:
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    struct Location
    {
        Scl::NodeId node;
        Scl::NodeId dataSet;
        int table;
        int inspector;
        int finding;
        Scl::NodeId ied;
    };
    void Select(Scl::NodeId id, bool route = false, int finding = -1, bool openSource = false);
    void SelectFinding(int finding, bool openSource, bool route = false);
    void RestoreWorkspaceLayout();
    void ResetWorkspaceLayout();
    void LocateTableRow();
    void ChangeTable();
    void UpdateTabVisibility();
    void SetIedScope(Scl::NodeId ied);
    void UpdateOverview();
    void SetMemberScope(Scl::NodeId dataSet);
    void RememberView();
    void Navigate(int direction);
    void UpdateHistoryButtons();
    void Preview(const Scl::SourceIndex* source, Scl::SourceLocation location, const QString& path);
    void RefreshSourcePreview();
    void UpdatePath();
    void UpdateReferencePath(const QModelIndex& index);
    void UpdateDetails();
    void CreateSearchBar(QVBoxLayout* layout);
    void AttachSearchResults();
    void ScheduleSearch();
    void BeginSearch();
    void CancelSearch();
    void ResetSearch(bool clearInputs = false);
    void UpdateSearchStatus();
    void ApplyTableFilter(QTableView* table, std::optional<std::vector<int>> rows);
    void SaveIssues();
    void CreateTopologyControls(QVBoxLayout* layout);
    void AddTopologyAction(QAbstractItemView* view);
    void UpdateTopologyControls();
    void OpenTopology(Scl::NodeId id);

    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::Result m_failure;
    Scl::NodeId m_selected = Scl::NoNode;
    int m_finding = -1;
    bool m_showingFailure = false;
    bool m_sourceDirty = true;
    Scl::NodeId m_dataSet = Scl::NoNode;
    Scl::NodeId m_ied = Scl::NoNode;
    QComboBox* m_browseScope;
    QCheckBox* m_allTemplates;
    QHash<int, Scl::NodeId> m_tabNodes;
    int m_tabFinding = -1;
    std::vector<Location> m_history;
    int m_historyIndex = -1;
    bool m_restoring = false;
    ads::CDockManager* m_docks;
    ads::CDockWidget* m_treeDock;
    ads::CDockWidget* m_tableDock;
    ads::CDockWidget* m_inspectorDock;
    QByteArray m_defaultLayout;
    SclTreeModel* m_treeModel;
    QTreeView* m_tree;
    QTabWidget* m_tables;
    QTabWidget* m_inspector;
    QLabel* m_summary;
    QLabel* m_scope;
    QWidget* m_scopeActions;
    QPushButton* m_clearNavigationFilter;
    QLabel* m_breadcrumb;
    QToolButton* m_pathMenu;
    QLabel* m_referencePath;
    QPushButton* m_copyDeclared;
    QPushButton* m_copyDerived;
    QPushButton* m_topology;
    QPushButton* m_copyTopologyPath;
    QLabel* m_topologyStatus;
    std::shared_ptr<SclTopologyFiles> m_topologyFiles;
    QString m_topologyPath;
    bool m_buildingTopology = false;
    QLabel* m_sourceLabel;
    QTextBrowser* m_properties;
    SclSourcePreview* m_source;
    QPushButton* m_back;
    QPushButton* m_forward;
    QPushButton* m_failedSource;
    QTableView* m_members;
    QTableView* m_findings;
    QTableView* m_links;
    QTableView* m_candidates;
    std::vector<SclTableModel*> m_models;
    std::vector<QTableView*> m_routeTables;
    std::vector<QPushButton*> m_scopeButtons;
    QLineEdit* m_query;
    QLineEdit* m_iedFilter;
    QLineEdit* m_typeFilter;
    QLineEdit* m_fcFilter;
    QComboBox* m_searchMode;
    QComboBox* m_severityFilter;
    QCheckBox* m_exact;
    QCheckBox* m_motion;
    QLabel* m_searchStatus;
    QLabel* m_exportStatus;
    QPushButton* m_cancelSearch;
    QPushButton* m_moreResults;
    QPushButton* m_export;
    QTableView* m_results;
    SclResultsModel* m_resultsModel;
    SclSearchSession* m_search;
    QTimer* m_searchDelay;
    QTableView* m_filteredTable = nullptr;
    std::vector<int> m_filteredRows;
    bool m_searching = false;
    bool m_searchCancelled = false;
    bool m_exporting = false;
    QString m_searchError;
};
} // namespace iec61850sim
