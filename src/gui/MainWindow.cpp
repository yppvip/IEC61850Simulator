#include "MainWindow.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QKeySequence>
#include <QLoggingCategory>
#include <QSettings>
#include <QStatusBar>
#include <QToolBar>

#include <DockManager.h>
#include <DockWidget.h>

namespace
{
constexpr auto SclFileFilter = "SCL 文件 (*.icd *.scd *.cid *.ccd);;所有文件 (*.*)";
Q_LOGGING_CATEGORY(UiLog, "iec61850sim.ui")

QListWidget* CreateEndpointList(QWidget* parent)
{
    auto* list = new QListWidget(parent);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setAlternatingRowColors(true);
    return list;
}
} // namespace

namespace iec61850sim
{
MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("IEC61850Simulator"));
    resize(1280, 800);

    CreateMenus();
    CreateToolBar();
    CreateDockLayout();
    RestoreDockLayout();
    statusBar()->showMessage(QStringLiteral("就绪"));
}

MainWindow::~MainWindow()
{
    // Dock destruction callbacks use m_detailDocks. Destroy the manager while
    // derived-class members are still alive, before QObject deletes children.
    delete m_dockManager;
}

void MainWindow::CreateMenus()
{
    QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("文件"));
    QAction* openAction = fileMenu->addAction(QStringLiteral("打开文件"));
    openAction->setShortcut(QKeySequence::Open);
    connect(openAction, &QAction::triggered, this, &MainWindow::OpenSclFile);
}

void MainWindow::CreateToolBar()
{
    QToolBar* toolBar = addToolBar(QStringLiteral("主工具栏"));
    toolBar->setObjectName(QStringLiteral("MainToolBar"));

    QAction* newServerAction = toolBar->addAction(QStringLiteral("新建服务器"));
    connect(newServerAction, &QAction::triggered, this, &MainWindow::CreateServer);

    QAction* newClientAction = toolBar->addAction(QStringLiteral("新建客户端"));
    connect(newClientAction, &QAction::triggered, this, &MainWindow::CreateClient);
}

void MainWindow::CreateDockLayout()
{
    m_dockManager = new ads::CDockManager(this);
    setCentralWidget(m_dockManager);

    auto* welcomeLabel = new QLabel(QStringLiteral("欢迎使用 IEC61850Simulator\n\n"
                                                   "可通过工具栏创建服务器或客户端，\n"
                                                   "也可从“文件”菜单选择 SCL 文件。"));
    welcomeLabel->setAlignment(Qt::AlignCenter);
    auto* welcomeDock = new ads::CDockWidget(m_dockManager, QStringLiteral("工作区"));
    welcomeDock->setWidget(welcomeLabel);
    welcomeDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, welcomeDock);

    m_serverList = CreateEndpointList(this);
    auto* serverDock = new ads::CDockWidget(m_dockManager, QStringLiteral("服务器"));
    serverDock->setWidget(m_serverList);
    serverDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* serverArea = m_dockManager->addDockWidget(ads::LeftDockWidgetArea, serverDock);

    m_clientList = CreateEndpointList(this);
    auto* clientDock = new ads::CDockWidget(m_dockManager, QStringLiteral("客户端"));
    clientDock->setWidget(m_clientList);
    clientDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::BottomDockWidgetArea, clientDock, serverArea);

    connect(m_serverList, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        ShowDetail(item->data(Qt::UserRole).toString(), item->text(), QStringLiteral("服务器"));
    });
    connect(m_clientList, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        ShowDetail(item->data(Qt::UserRole).toString(), item->text(), QStringLiteral("客户端"));
    });
}

void MainWindow::RestoreDockLayout()
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    const QByteArray layout = settings.value(QStringLiteral("MainWindow/DockLayout")).toByteArray();
    if (!layout.isEmpty() && !m_dockManager->restoreState(layout))
    {
        qWarning(UiLog) << "无法恢复 ADS 停靠布局。";
    }
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    settings.setValue(QStringLiteral("MainWindow/DockLayout"), m_dockManager->saveState());
    settings.sync();
    if (settings.status() == QSettings::NoError)
    {
        qInfo(UiLog) << "已保存 ADS 停靠布局。";
    }
    else
    {
        qWarning(UiLog) << "无法保存 ADS 停靠布局。";
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::OpenSclFile()
{
    const QString fileName = QFileDialog::getOpenFileName(this, QStringLiteral("打开 SCL 文件"), {}, QString::fromLatin1(SclFileFilter));
    if (fileName.isEmpty())
    {
        return;
    }

    const QString suffix = QFileInfo(fileName).suffix().toLower();
    if (suffix != QStringLiteral("icd") && suffix != QStringLiteral("scd") && suffix != QStringLiteral("cid") && suffix != QStringLiteral("ccd"))
    {
        QMessageBox::warning(this, QStringLiteral("文件类型不支持"), QStringLiteral("仅支持 ICD、SCD、CID 和 CCD 文件。"));
        return;
    }

    statusBar()->showMessage(QStringLiteral("已选择 SCL 文件：%1（尚未解析）").arg(fileName));
    qInfo(UiLog) << "已选择 SCL 文件：" << fileName;
}

void MainWindow::CreateServer()
{
    const QString name = QStringLiteral("服务器 %1").arg(++m_serverCount);
    const QString identifier = QStringLiteral("server-%1").arg(m_serverCount);
    auto* item = new QListWidgetItem(QStringLiteral("%1  [已创建]").arg(name), m_serverList);
    item->setData(Qt::UserRole, identifier);
    m_serverList->setCurrentItem(item);
    ShowDetail(identifier, name, QStringLiteral("服务器"));
    statusBar()->showMessage(QStringLiteral("已创建 %1。").arg(name));
    qInfo(UiLog) << "已创建服务器：" << identifier;
}

void MainWindow::CreateClient()
{
    const QString name = QStringLiteral("客户端 %1").arg(++m_clientCount);
    const QString identifier = QStringLiteral("client-%1").arg(m_clientCount);
    auto* item = new QListWidgetItem(QStringLiteral("%1  [已创建]").arg(name), m_clientList);
    item->setData(Qt::UserRole, identifier);
    m_clientList->setCurrentItem(item);
    ShowDetail(identifier, name, QStringLiteral("客户端"));
    statusBar()->showMessage(QStringLiteral("已创建 %1。").arg(name));
    qInfo(UiLog) << "已创建客户端：" << identifier;
}

void MainWindow::ShowDetail(const QString& identifier, const QString& title, const QString& type)
{
    if (const QPointer<ads::CDockWidget> existingDock = m_detailDocks.value(identifier); !existingDock.isNull())
    {
        existingDock->toggleView(true);
        existingDock->raise();
        return;
    }

    auto* detailLabel = new QLabel(QStringLiteral("%1\n\n名称：%2\n状态：已创建\n\n变量和参数将在后续阶段接入。")
                                       .arg(type, title));
    detailLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    detailLabel->setMargin(16);

    auto* detailDock = new ads::CDockWidget(m_dockManager, title);
    detailDock->setWidget(detailLabel);
    detailDock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, detailDock);
    m_detailDocks.insert(identifier, detailDock);
    connect(detailDock, &QObject::destroyed, this, [this, identifier]() {
        m_detailDocks.remove(identifier);
    });
}
} // namespace iec61850sim
