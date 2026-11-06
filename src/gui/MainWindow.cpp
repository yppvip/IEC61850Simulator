#include "MainWindow.hpp"
#include "SclAnalysisService.hpp"
#include "SclWorkspace.hpp"

#include <QAction>
#include <QCloseEvent>
#include <QCryptographicHash>
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
#include <QStackedWidget>
#include <QToolBar>

#include <DockManager.h>
#include <DockWidget.h>

namespace
{
Q_LOGGING_CATEGORY(UiLog, "iec61850sim.ui")
constexpr int DockLayoutVersion = 1;
constexpr auto DockLayoutKey = "MainWindow/DockLayout";
constexpr auto DockVersionKey = "MainWindow/DockLayoutVersion";
constexpr auto DockDigestKey = "MainWindow/DockLayoutSha256";

QSettings LayoutSettings()
{
    return QSettings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
}

QByteArray LayoutDigest(const QByteArray& layout)
{
    return QCryptographicHash::hash(layout, QCryptographicHash::Sha256);
}

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

    m_sclService = new SclAnalysisService(this);
    CreateMenus();
    CreateToolBar();
    CreateDockLayout();
    m_defaultDockLayout = m_dockManager->saveState(DockLayoutVersion);
    RestoreDockLayout();
    m_sclStatus = new QLabel(statusBar());
    m_sclStatus->setObjectName(QStringLiteral("SclStatus"));
    m_sclStatus->setTextFormat(Qt::PlainText);
    m_sclStatus->setFixedWidth(qMin(width() / 2, 640));
    statusBar()->addPermanentWidget(m_sclStatus);
    connect(m_sclService, &SclAnalysisService::Changed, this, &MainWindow::UpdateSclStatus);
    UpdateSclStatus();
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
    m_reloadScl = fileMenu->addAction(QStringLiteral("重新加载 SCL"));
    m_reloadScl->setObjectName(QStringLiteral("ReloadScl"));
    connect(m_reloadScl, &QAction::triggered, m_sclService, &SclAnalysisService::Reload);
    m_cancelScl = fileMenu->addAction(QStringLiteral("取消 SCL 分析"));
    m_cancelScl->setObjectName(QStringLiteral("CancelScl"));
    connect(m_cancelScl, &QAction::triggered, m_sclService, &SclAnalysisService::Cancel);
    m_closeScl = fileMenu->addAction(QStringLiteral("关闭 SCL 文档"));
    m_closeScl->setObjectName("CloseScl");
    m_closeScl->setShortcut(QKeySequence::Close);
    connect(m_closeScl, &QAction::triggered, this, [this] {
        m_sclService->Close();
        ShowWorkspace(false);
    });
    auto* viewMenu = menuBar()->addMenu(QStringLiteral("视图"));
    auto* simulation = viewMenu->addAction(QStringLiteral("仿真工作区"));
    simulation->setObjectName("ShowSimulation");
    connect(simulation, &QAction::triggered, this, [this] { ShowWorkspace(false); });
    auto* analysis = viewMenu->addAction(QStringLiteral("SCL 分析工作台"));
    analysis->setObjectName("ShowSclWorkspace");
    connect(analysis, &QAction::triggered, this, [this] { ShowWorkspace(true); });
    viewMenu->addSeparator();
    auto* reset = viewMenu->addAction(QStringLiteral("恢复默认布局"));
    reset->setObjectName("ResetDockLayout");
    connect(reset, &QAction::triggered, this, &MainWindow::ResetDockLayout);
    auto* help = menuBar()->addMenu(QStringLiteral("帮助"));
    auto* sclHelp = help->addAction(QStringLiteral("SCL 使用帮助"));
    sclHelp->setObjectName("SclHelp");
    sclHelp->setShortcut(QKeySequence::HelpContents);
    connect(sclHelp, &QAction::triggered, this, &MainWindow::ShowSclHelp);
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
    m_workspacePages = new QStackedWidget;
    m_workspacePages->addWidget(welcomeLabel);
    m_sclWorkspace = new SclWorkspace(m_workspacePages);
    m_workspacePages->addWidget(m_sclWorkspace);
    m_workspaceDock = new ads::CDockWidget(m_dockManager, QStringLiteral("工作区"));
    m_workspaceDock->setWidget(m_workspacePages);
    m_workspaceDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, m_workspaceDock);

    m_serverList = CreateEndpointList(this);
    m_serverDock = new ads::CDockWidget(m_dockManager, QStringLiteral("服务器"));
    m_serverDock->setWidget(m_serverList);
    m_serverDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    auto* serverArea = m_dockManager->addDockWidget(ads::LeftDockWidgetArea, m_serverDock);

    m_clientList = CreateEndpointList(this);
    m_clientDock = new ads::CDockWidget(m_dockManager, QStringLiteral("客户端"));
    m_clientDock->setWidget(m_clientList);
    m_clientDock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    m_dockManager->addDockWidget(ads::BottomDockWidgetArea, m_clientDock, serverArea);

    connect(m_serverList, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        ShowDetail(item->data(Qt::UserRole).toString(), item->text(), QStringLiteral("服务器"));
    });
    connect(m_clientList, &QListWidget::itemClicked, this, [this](const QListWidgetItem* item) {
        ShowDetail(item->data(Qt::UserRole).toString(), item->text(), QStringLiteral("客户端"));
    });
}

void MainWindow::RestoreDockLayout()
{
    const auto settings = LayoutSettings();
    const auto layout = settings.value(DockLayoutKey).toByteArray();
    if (layout.isEmpty() && !settings.contains(DockVersionKey))
        return;

    const bool legacy = !settings.contains(DockVersionKey);
    bool validVersion = false;
    const int version = settings.value(DockVersionKey).toInt(&validVersion);
    const bool compatible = legacy || (validVersion && version == DockLayoutVersion
        && settings.value(DockDigestKey).toByteArray() == LayoutDigest(layout));
    if (compatible && m_dockManager->restoreState(layout, legacy ? 0 : version)
        && m_workspaceDock->dockAreaWidget() && !m_workspaceDock->isClosed()
        && m_serverDock->dockAreaWidget() && m_clientDock->dockAreaWidget())
        return;

    qWarning(UiLog) << "ADS 布局无法恢复，使用默认布局。";
    ResetDockLayout();
}

void MainWindow::ResetDockLayout()
{
    const bool analysis = m_workspacePages->currentIndex() == 1;
    if (analysis)
        ShowWorkspace(false);
    if (!m_dockManager->restoreState(m_defaultDockLayout, DockLayoutVersion))
    {
        qWarning(UiLog) << "无法恢复默认 ADS 布局。";
        if (analysis)
            ShowWorkspace(true);
        return;
    }
    for (const auto& dock : m_detailDocks)
    {
        if (dock)
        {
            m_dockManager->addDockWidget(ads::CenterDockWidgetArea, dock, m_workspaceDock->dockAreaWidget());
            dock->toggleView(true);
        }
    }
    ShowWorkspace(analysis);
}

void MainWindow::ShowSclHelp()
{
    QMessageBox help(QMessageBox::Information, QStringLiteral("SCL 使用帮助"),
        QStringLiteral("在「文件」中打开 ICD、SCD、CID 或 CCD。分析完成后进入工作台。\n\n"
            "在报告表选中报告，通过「引用与使用方」跳到数据集。选择 FCDA 后可继续查看类型和原文。\n"
            "Ctrl+F 聚焦搜索，Enter 立即查询，Esc 取消，Alt+左右方向键浏览历史。\n"
            "使用「导出问题」保存校验摘要。「概览」列出规则执行范围和完整 XSD 的执行状态。\n\n"
            "「文件 → 关闭 SCL 文档」释放当前分析会话。「视图」可切回仿真或恢复默认布局。\n"
            "SCL 分析支持离线运行。"), QMessageBox::Ok, this);
    help.setObjectName("SclHelpDialog");
    help.setTextFormat(Qt::PlainText);
    help.exec();
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    QMainWindow::closeEvent(event);
    if (!event->isAccepted())
        return;
    m_sclService->Close();
    if (m_workspacePages->currentIndex() == 1)
        RestoreSimulationLists();
    for (const auto& dock : m_detailDocks.values())
        if (dock)
            dock->deleteDockWidget();
    m_detailDocks.clear();

    auto settings = LayoutSettings();
    const auto layout = m_dockManager->saveState(DockLayoutVersion);
    settings.setValue(DockLayoutKey, layout);
    settings.setValue(DockVersionKey, DockLayoutVersion);
    settings.setValue(DockDigestKey, LayoutDigest(layout));
    settings.sync();
    if (settings.status() == QSettings::NoError)
    {
        qInfo(UiLog) << "已保存 ADS 停靠布局。";
    }
    else
    {
        qWarning(UiLog) << "无法保存 ADS 停靠布局。";
    }
}

void MainWindow::OpenSclFile()
{
    const auto fileName = QFileDialog::getOpenFileName(this, QStringLiteral("打开 SCL 文件"), {},
        QStringLiteral("SCL 文件 (*.icd *.scd *.cid *.ccd *.ICD *.SCD *.CID *.CCD)"));
    m_sclService->Open(fileName);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    if (m_sclStatus)
        m_sclStatus->setFixedWidth(qMin(width() / 2, 640));
}

void MainWindow::UpdateSclStatus()
{
    using State = SclAnalysisService::State;
    const auto state = m_sclService->state();
    const auto requested = m_sclService->requestedPath();
    const auto snapshot = m_sclService->snapshot();
    const auto name = QFileInfo(requested).fileName();
    QString text;
    QString detail = requested;
    switch (state)
    {
    case State::Idle:
        text = QStringLiteral("SCL：未打开");
        break;
    case State::Loading: {
        QString stage;
        switch (m_sclService->stage())
        {
        case Scl::TaskStage::Reading: stage = QStringLiteral("读取文件"); break;
        case Scl::TaskStage::SourceIndex: stage = QStringLiteral("建立原文索引"); break;
        case Scl::TaskStage::Xml: stage = QStringLiteral("解析 XML"); break;
        case Scl::TaskStage::ReferenceIndex: stage = QStringLiteral("建立引用索引"); break;
        case Scl::TaskStage::Validation: stage = QStringLiteral("解析引用并校验"); break;
        case Scl::TaskStage::BrowseIndex: stage = QStringLiteral("建立浏览索引"); break;
        }
        text = QStringLiteral("SCL：%1 · %2").arg(stage, name);
        break;
    }
    case State::Ready:
        text = QStringLiteral("SCL：已分析 %1").arg(name);
        break;
    case State::Cancelled:
        text = QStringLiteral("SCL：已取消 %1").arg(name);
        break;
    case State::Failed: {
        const auto& failure = m_sclService->failure().diagnostic;
        text = QStringLiteral("SCL：%1 加载失败").arg(name);
        if (failure.line > 0)
            text += QStringLiteral("（第 %1 行）").arg(failure.line);
        detail += '\n' + failure.detail;
        text += QStringLiteral(" · %1").arg(failure.detail);
        break;
    }
    }
    if (snapshot && state != State::Ready)
    {
        text += QStringLiteral(" · 当前文档 %1").arg(QFileInfo(snapshot->path).fileName());
        detail += QStringLiteral("\n当前文档：%1").arg(snapshot->path);
    }
    if (m_sclService->sourceChanged())
        text += QStringLiteral(" · 当前文件已变化，请重新加载");
    m_sclStatus->setText(text);
    m_sclStatus->setToolTip(QStringLiteral("<qt>%1</qt>")
        .arg((detail + '\n' + text).toHtmlEscaped().replace('\n', QStringLiteral("<br>"))));
    m_reloadScl->setEnabled(!requested.isEmpty());
    m_cancelScl->setEnabled(state == State::Loading);
    m_closeScl->setEnabled(state != State::Idle);
    if (m_sclWorkspace->UpdateSession(snapshot, m_sclService->failure())
        || (!snapshot && state == State::Failed && m_sclService->failure().source))
        ShowWorkspace(true);
}

void MainWindow::ShowWorkspace(bool analysis)
{
    if (analysis && m_workspacePages->currentIndex() == 0)
    {
        m_serverWasOpen = !m_serverDock->isClosed();
        m_clientWasOpen = !m_clientDock->isClosed();
        m_serverDock->toggleView(false);
        m_clientDock->toggleView(false);
    }
    else if (!analysis && m_workspacePages->currentIndex() == 1)
        RestoreSimulationLists();
    m_workspacePages->setCurrentIndex(analysis ? 1 : 0);
    m_workspaceDock->toggleView(true);
    m_workspaceDock->raise();
}

void MainWindow::RestoreSimulationLists()
{
    m_serverDock->toggleView(m_serverWasOpen);
    m_clientDock->toggleView(m_clientWasOpen);
}

void MainWindow::CreateServer()
{
    if (m_workspacePages->currentIndex() == 1)
        ShowWorkspace(false);
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
    if (m_workspacePages->currentIndex() == 1)
        ShowWorkspace(false);
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
    m_dockManager->addDockWidget(ads::CenterDockWidgetArea, detailDock, m_workspaceDock->dockAreaWidget());
    m_detailDocks.insert(identifier, detailDock);
    connect(detailDock, &QObject::destroyed, this, [this, identifier]() {
        m_detailDocks.remove(identifier);
    });
}
} // namespace iec61850sim
