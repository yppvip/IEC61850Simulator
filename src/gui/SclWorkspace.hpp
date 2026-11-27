#pragma once

#include "SclAnalysisService.hpp"
#include "SclDataSetView.hpp"
#include "SclSearch.hpp"
#include "SclTaskBrowser.hpp"
#include <QWidget>
#include <optional>

class QAbstractItemView;
class QLabel;
class QMenu;
class QComboBox;
class QLineEdit;
class QCheckBox;
class QTimer;
class QVBoxLayout;
class QTextBrowser;
class QToolButton;
class QPushButton;
class QTabWidget;
class QStackedWidget;
class QTableView;
class QTreeView;
class QTreeWidget;
class QTreeWidgetItem;
namespace ads { class CDockManager; class CDockWidget; }

namespace iec61850sim
{
class SclTreeModel;
class SclTableModel;
class SclSearchSession;
class SclResultsModel;
class SclSourcePreview;
class SclTopologyFiles;
class SclTableInteraction;
class SclReferenceTraceView;
struct SclReferenceTraceSelection;
struct SclReferenceTraceLocation;
struct SclReferenceMenuContext;
struct SclReferenceRequest;

class SclWorkspace final : public QWidget
{
public:
    explicit SclWorkspace(QWidget* parent = nullptr);
    ~SclWorkspace() override;
    bool UpdateSession(std::shared_ptr<const SclSnapshot> snapshot, const Scl::Result& failure);

protected:
    bool eventFilter(QObject*, QEvent*) override;
    void hideEvent(QHideEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    struct Relationship
    {
        Scl::NodeId origin = Scl::NoNode;
        QString action;
    };
    struct Location
    {
        Scl::NodeId node;
        Scl::NodeId dataSet;
        int table;
        int inspector;
        int finding;
        Scl::NodeId ied;
        bool taskView = false;
        SclTaskBrowser::Location task;
        bool inspectorVisible = true;
        QString signalEvidence;
        SclDataSetView::Location dataSets;
        SclDataSetView::UsersLocation users;
        Relationship relationship;
        int reference = -1;
        Scl::NodeId candidate = Scl::NoNode;
        std::shared_ptr<SclReferenceTraceLocation> trace;
        SclQuery filter;
        int searchMode = 0, column = 0, horizontal = 0, topOffset = 0;
        Scl::NodeId top = Scl::NoNode;
        bool allTemplates = false, selectionVisible = true;
    };
    void Select(Scl::NodeId id, bool route = false, int finding = -1, bool openSource = false, bool keepScope = false,
        std::optional<Relationship> relationship = {});
    void FollowRelationship(Scl::NodeId target, const QString& action);
    void ShowRelationshipHelp();
    void UpdateRelationshipSummary();
    void SelectFinding(int finding, bool openSource, bool route = false);
    void RestoreWorkspaceLayout();
    void ResetWorkspaceLayout();
    void LocateTableRow();
    void ChangeTable();
    void UpdateTabVisibility();
    void SetIedScope(Scl::NodeId ied);
    void UpdateOverview();
    void OpenAdvancedPage(QWidget* page);
    void OpenMemberPage(Scl::NodeId dataSet);
    void RefreshOverviewSummary();
    void SetMemberScope(Scl::NodeId dataSet);
    void RememberView();
    void Navigate(int direction);
    void UpdateHistoryButtons();
    void Preview(const Scl::SourceIndex* source, Scl::SourceLocation location, const QString& path);
    void RefreshSourcePreview();
    void BalanceTraceSource();
    QList<int> TraceSplitterSizes() const;
    void UpdatePath();
    void UpdateReferencePath(const QModelIndex& index);
    void UpdateDetails();
    void QueryAdvancedEvidence();
    void ApplyAdvancedEvidence();
    void RestoreAdvancedPosition(const Location&);
    void CancelAdvancedRestore();
    void CreateSearchBar(QVBoxLayout* layout);
    void AttachSearchResults();
    void ScheduleSearch();
    void BeginSearch();
    void CancelSearch();
    void ResetSearch(bool clearInputs = false);
    void UpdateSearchStatus();
    void ApplyTableFilter(QTableView* table, std::optional<std::vector<int>> rows);
    void SaveIssues();
    void ShowTaskView(bool show, bool remember = false);
    void BalanceMmsPanels();
    void BalanceAdvancedPanels();
    void PushTaskHistory();
    void CreateTopologyControls(QVBoxLayout* layout);
    void AddTopologyAction(QAbstractItemView* view);
    void AddMmsAttributeAction(QMenu& menu, Scl::NodeId node, QAbstractItemView* view, const QModelIndex& index);
    void AddNodeReferenceMenu(SclTableInteraction*, bool candidate = false);
    SclReferenceMenuContext ReferenceMenu(SclReferenceRequest);
    void OpenMmsAttributeTarget(Scl::NodeId node);
    void UpdateTopologyControls();
    void OpenTopology(Scl::NodeId id);
    void OpenReferenceTrace(std::shared_ptr<const SclReferenceTraceSelection>);
    bool IsReferenceTrace() const;
    void LeaveReferenceTrace();

    std::shared_ptr<const SclSnapshot> m_snapshot;
    bool m_closing = false;
    quint64 m_referenceGeneration = 0;
    SclMmsQuerySession* m_advancedEvidence;
    quint64 m_advancedEvidenceId = 0;
    QString m_propertyHtml;
    std::optional<Location> m_advancedRestore;
    Scl::Result m_failure;
    Scl::NodeId m_selected = Scl::NoNode;
    int m_finding = -1;
    bool m_showingFailure = false;
    bool m_sourceDirty = true;
    Scl::NodeId m_dataSet = Scl::NoNode;
    Scl::NodeId m_ied = Scl::NoNode;
    QComboBox* m_browseScope;
    QWidget* m_advancedScope;
    QWidget* m_advancedSearch;
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
    QStackedWidget* m_navigationModes;
    QComboBox* m_navigationKind;
    QTreeWidget* m_tasks;
    QTreeWidgetItem* m_mmsAttributeTask;
    QTabWidget* m_tables;
    SclDataSetView* m_dataSets;
    SclHierarchyQuerySession* m_overviewQuery;
    QLabel* m_overviewCounts;
    QPushButton* m_overviewUsers;
    QPushButton* m_overviewRelation;
    QPushButton* m_overviewDataSets;
    QPushButton* m_overviewStructure;
    QPushButton* m_overviewTopology;
    QWidget* m_memberContext;
    QLabel* m_memberPath;
    Scl::NodeId m_relationTarget = Scl::NoNode;
    Relationship m_relationship;
    QLabel* m_relationshipSummary;
    QLabel* m_linksLimit;
    QLabel* m_candidatesLimit;
    QPushButton* m_relationshipParent;
    QPushButton* m_relationshipUsers;
    QStackedWidget* m_tableModes;
    QStackedWidget* m_centerPages;
    SclReferenceTraceView* m_referenceTrace;
    SclTaskBrowser* m_taskBrowser;
    QPushButton* m_taskSwitch;
    bool m_preferTaskView = true;
    bool m_advancedInspectorVisible = true;
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
    QLabel* m_signalTitle;
    QLabel* m_structureTitle;
    QPushButton* m_locateSignal;
    QWidget* m_signalInspector;
    QString m_signalEvidence;
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
