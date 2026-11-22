#pragma once

#include "SclHierarchyQuery.hpp"
#include <QWidget>

class QComboBox;
class QDialog;
class QLabel;
class QListWidget;
class QPushButton;
class QTreeView;

namespace iec61850sim
{
class SclDataSetModel;

class SclDataSetView final : public QWidget
{
    Q_OBJECT
public:
    struct UsersLocation
    {
        Scl::NodeId dataSet = Scl::NoNode;
        Scl::NodeId selected = Scl::NoNode;
        int offset = 0;
        int kind = 0;
    };

    struct Location
    {
        SclHierarchyRequest filter;
        bool flat = false;
        int offset = 0;
        int column = 0;
        Scl::NodeId anchor = Scl::NoNode;
        Scl::NodeId anchorParent = Scl::NoNode;
        int anchorDelta = 0;
        std::vector<Scl::NodeId> expanded;
    };

    explicit SclDataSetView(QWidget* parent);
    QTreeView* Tree() const { return m_tree; }
    void SetContext(std::shared_ptr<const SclSnapshot> snapshot, Scl::NodeId ied);
    void SetFilter(const SclHierarchyRequest& filter);
    void SyncSelection(Scl::NodeId node, Scl::NodeId dataSet);
    Location CurrentLocation() const;
    void Restore(const Location& location);
    void OpenUsers(Scl::NodeId dataSet);
    void CloseUsers();
    UsersLocation CurrentUsers() const;
    void RestoreUsers(const UsersLocation& location);

signals:
    void Selected(Scl::NodeId node);
    void OpenSource(Scl::NodeId node);
    void OpenMembers(Scl::NodeId dataSet);
    void OpenTarget(Scl::NodeId node);
    void OpenUser(Scl::NodeId node);
    void OpenTopology(Scl::NodeId node);
    void LocationUpdated();
    void FilterHidesSelection(bool hidden);

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void LoadList(bool locate);
    void ReceiveList(SclHierarchyQuerySession& session);
    void LoadBranch(Scl::NodeId dataSet, int offset, Scl::NodeId locate = Scl::NoNode);
    void ReceiveBranch();
    void ApplySelection(bool locate);
    void RestoreAnchor(const Location& location);
    void ContinueRestore();
    void UpdateActions();
    void Activate(const QModelIndex& index);
    void LoadUsers(int offset);
    void ActivateUser();

    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::NodeId m_ied = Scl::NoNode;
    Scl::NodeId m_node = Scl::NoNode, m_selectedSet = Scl::NoNode;
    SclHierarchyRequest m_filter;
    SclHierarchyQuerySession m_list, m_locate, m_branch, m_users;
    SclDataSetModel* m_model;
    QTreeView* m_tree;
    QComboBox* m_mode;
    QComboBox* m_order;
    QLabel* m_status;
    QPushButton* m_previous;
    QPushButton* m_next;
    QPushButton* m_openMembers;
    QPushButton* m_source;
    QPushButton* m_showUsers;
    QPushButton* m_target;
    QDialog* m_userDialog;
    QListWidget* m_userList;
    QLabel* m_userStatus;
    QPushButton* m_userPrevious;
    QPushButton* m_userNext;
    QPushButton* m_userOpen;
    QComboBox* m_userKind;
    Scl::NodeId m_userSelection = Scl::NoNode;
    int m_userOffset = 0;
    Scl::NodeId m_userSet = Scl::NoNode;
    Scl::NodeId m_pendingBranch = Scl::NoNode;
    quint64 m_listId = 0, m_locateId = 0, m_branchId = 0, m_usersId = 0;
    int m_offset = 0;
    bool m_listReady = false;
    bool m_loadingList = false;
    bool m_locatingSelection = false;
    bool m_locateMiss = false;
    bool m_locateLimited = false;
    bool m_applying = false;
    std::optional<Location> m_restore;
    bool m_triedAnchor = false;
    size_t m_restoreBranch = 0;
};
} // namespace iec61850sim
