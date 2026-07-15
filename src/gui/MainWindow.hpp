#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>

class QListWidget;

namespace ads
{
class CDockManager;
class CDockWidget;
}

namespace iec61850sim
{
class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void CreateMenus();
    void CreateToolBar();
    void CreateDockLayout();
    void RestoreDockLayout();
    void OpenSclFile();
    void CreateServer();
    void CreateClient();
    void ShowDetail(const QString& identifier, const QString& title, const QString& type);

    ads::CDockManager* m_dockManager = nullptr;
    QListWidget* m_serverList = nullptr;
    QListWidget* m_clientList = nullptr;
    QHash<QString, QPointer<ads::CDockWidget>> m_detailDocks;
    int m_serverCount = 0;
    int m_clientCount = 0;
};
} // namespace iec61850sim
