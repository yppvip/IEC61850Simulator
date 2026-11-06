#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QListWidget;
class QLabel;
class QAction;
class QStackedWidget;

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace iec61850sim
{
class SclAnalysisService;
class SclWorkspace;
class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void CreateMenus();
    void CreateToolBar();
    void CreateDockLayout();
    void RestoreDockLayout();
    void ResetDockLayout();
    void ShowSclHelp();
    void OpenSclFile();
    void UpdateSclStatus();
    void ShowWorkspace(bool analysis);
    void RestoreSimulationLists();
    void CreateServer();
    void CreateClient();
    void ShowDetail(const QString& identifier, const QString& title, const QString& type);

    ads::CDockManager* m_dockManager = nullptr;
    ads::CDockWidget* m_workspaceDock = nullptr;
    ads::CDockWidget* m_serverDock = nullptr;
    ads::CDockWidget* m_clientDock = nullptr;
    bool m_serverWasOpen = true;
    bool m_clientWasOpen = true;
    QListWidget* m_serverList = nullptr;
    QListWidget* m_clientList = nullptr;
    SclAnalysisService* m_sclService = nullptr;
    SclWorkspace* m_sclWorkspace = nullptr;
    QStackedWidget* m_workspacePages = nullptr;
    QLabel* m_sclStatus = nullptr;
    QAction* m_reloadScl = nullptr;
    QAction* m_cancelScl = nullptr;
    QAction* m_closeScl = nullptr;
    QByteArray m_defaultDockLayout;
    QHash<QString, QPointer<ads::CDockWidget>> m_detailDocks;
    int m_serverCount = 0;
    int m_clientCount = 0;
};
} // namespace iec61850sim
