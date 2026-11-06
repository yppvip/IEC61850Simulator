#include "MainWindow.hpp"
#include "SclAnalysisService.hpp"
#include "SclIssueExport.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QToolBar>
#include <QTreeView>
#include <QtTest>
#include <DockManager.h>
#include <DockAreaWidget.h>
#include <DockWidget.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
void IsolateSettings(const QString& path)
{
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, path);
    QSettings::setPath(QSettings::IniFormat, QSettings::SystemScope, path);
}

bool WriteFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QByteArray SclXml(const QByteArray& id)
{
    return "<SCL xmlns='http://www.iec.ch/61850/2003/SCL'><Header id='" + id + "'/></SCL>";
}

void CheckWorkspaceFlow(iec61850sim::MainWindow& window)
{
    using Scl::NoNode;
    auto* service = window.findChild<iec61850sim::SclAnalysisService*>();
    auto* workspace = window.findChild<QWidget*>("SclWorkspace");
    auto* reports = window.findChild<QTableView*>("SclReports");
    auto* members = window.findChild<QTableView*>("SclMembers");
    auto* links = window.findChild<QTableView*>("SclLinks");
    auto* findings = window.findChild<QTableView*>("SclFindings");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* properties = window.findChild<QPlainTextEdit*>("SclProperties");
    auto* back = window.findChild<QPushButton*>("SclBack");
    auto* simulation = window.findChild<QAction*>("ShowSimulation");
    auto* showAnalysis = window.findChild<QAction*>("ShowSclWorkspace");
    auto* manager = window.findChild<ads::CDockManager*>();
    QVERIFY(service && workspace && reports && members && links && findings && tree && tabs
        && inspector && source && properties && back && simulation && showAnalysis && manager);
    auto* serverDock = manager->findDockWidget(QStringLiteral("服务器"));
    auto* clientDock = manager->findDockWidget(QStringLiteral("客户端"));
    QVERIFY(serverDock && clientDock);
    QVERIFY(serverDock->isClosed() && clientDock->isClosed());
    QVERIFY(workspace->isVisible());
    const auto snapshot = service->snapshot();
    QVERIFY(snapshot && !snapshot->analysis.reports.empty());
    const auto& analysis = snapshot->analysis;
    QCOMPARE(reports->model()->rowCount(), int(analysis.reports.size()));
    const auto selected = [&] { return tree->currentIndex().data(Qt::UserRole).toInt(); };
    const auto jump = [&](int row) {
        const auto index = links->model()->index(row, 0);
        QVERIFY(index.isValid());
        links->setCurrentIndex(index);
        QVERIFY(QMetaObject::invokeMethod(links, "activated", Q_ARG(QModelIndex, index)));
    };
    tabs->setCurrentWidget(reports);
    const auto reportIndex = reports->model()->index(0, 0);
    const auto report = analysis.reports.front().source;
    reports->setCurrentIndex(reportIndex);
    QCOMPARE(selected(), report);
    QVERIFY(properties->toPlainText().contains(QStringLiteral("rptID")));
    inspector->setCurrentIndex(1);

    const auto dataSet = links->model()->index(0, 0).data(Qt::UserRole).toInt();
    QVERIFY(dataSet > 0);
    jump(0);
    QCOMPARE(selected(), dataSet);
    QCOMPARE(analysis.document->nodes[dataSet].name, QStringLiteral("DataSet"));
    QCOMPARE(tabs->currentWidget(), members);
    QVERIFY(members->model()->rowCount() > 0);
    const auto memberIndex = members->model()->index(0, 0);
    const auto member = memberIndex.data(Qt::UserRole).toInt();
    QCOMPARE(memberIndex.data().toString(), QStringLiteral("1"));
    members->setCurrentIndex(memberIndex);
    QCOMPARE(selected(), member);

    const auto target = links->model()->index(0, 0).data(Qt::UserRole).toInt();
    QVERIFY(target > 0 && target != NoNode);
    jump(0);
    QCOMPARE(selected(), target);
    QVERIFY(tree->visualRect(tree->currentIndex()).isValid());
    QCOMPARE(links->model()->index(links->model()->rowCount() - 1, 0).data().toString(), QStringLiteral("使用方"));
    auto type = analysis.document->nodes[target].parent;
    const auto outgoing = analysis.outgoing.constFind(target);
    if (outgoing != analysis.outgoing.cend()
        && analysis.references[*outgoing].kind == Scl::LinkKind::MemberType
        && analysis.references[*outgoing].state == Scl::LinkState::Resolved)
    {
        type = analysis.references[*outgoing].candidates.front();
        jump(0);
    }
    else
        tree->setCurrentIndex(tree->currentIndex().parent());
    QCOMPARE(selected(), type);
    QVERIFY(analysis.document->nodes[type].name.endsWith("Type"));
    inspector->setCurrentIndex(2);
    const auto& node = analysis.document->nodes[type];
    const auto original = analysis.source->line(node.source.line).text.trimmed();
    QVERIFY(!original.isEmpty() && source->toPlainText().contains(original.left(900)));
    QCOMPARE(source->extraSelections().size(), 1);

    back->click();
    QCOMPARE(selected(), target);
    back->click();
    QCOMPARE(selected(), member);
    back->click();
    QCOMPARE(selected(), dataSet);
    back->click();
    QCOMPARE(selected(), report);
    QCOMPARE(tabs->currentWidget(), reports);

    QVERIFY(findings->model()->rowCount() > 0);
    tabs->setCurrentWidget(findings);
    const auto issue = findings->model()->index(0, 0);
    QVERIFY(QMetaObject::invokeMethod(findings, "activated", Q_ARG(QModelIndex, issue)));
    QCOMPARE(selected(), issue.data(Qt::UserRole).toInt());
    QCOMPARE(inspector->currentIndex(), 2);
    simulation->trigger();
    QVERIFY(!workspace->isVisible());
    QVERIFY(!serverDock->isClosed() && !clientDock->isClosed());
    showAnalysis->trigger();
    QVERIFY(workspace->isVisible());
    QCOMPARE(selected(), issue.data(Qt::UserRole).toInt());
    auto* toolbar = window.findChild<QToolBar*>("MainToolBar");
    QVERIFY(toolbar && !toolbar->actions().isEmpty());
    toolbar->actions().front()->trigger();
    QVERIFY(!workspace->isVisible());
    QCOMPARE(qobject_cast<QListWidget*>(serverDock->widget())->count(), 1);
    showAnalysis->trigger();
    QVERIFY(workspace->isVisible());
    QCOMPARE(selected(), issue.data(Qt::UserRole).toInt());
}

int LayoutProbe(const QString& mode)
{
    iec61850sim::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto* manager = window.findChild<ads::CDockManager*>();
    if (!manager)
        return 10;
    auto* server = manager->findDockWidget(QStringLiteral("服务器"));
    auto* client = manager->findDockWidget(QStringLiteral("客户端"));
    if (!server || !client)
        return 11;
    if (mode == "--write-layout" || mode == "--legacy-layout")
    {
        if (mode == "--write-layout" && server->dockAreaWidget() == client->dockAreaWidget())
            return 12;
        if (server->dockAreaWidget() != client->dockAreaWidget())
            manager->addDockWidget(ads::CenterDockWidgetArea, client, server->dockAreaWidget());
        client->raise();
        QApplication::processEvents();
        auto* toolbar = window.findChild<QToolBar*>("MainToolBar");
        if (!toolbar || toolbar->actions().isEmpty())
            return 18;
        toolbar->actions().front()->trigger();
        if (mode == "--legacy-layout")
        {
            QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
                QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
            settings.setValue("MainWindow/DockLayout", manager->saveState(0));
            settings.remove("MainWindow/DockLayoutVersion");
            settings.remove("MainWindow/DockLayoutSha256");
            settings.sync();
            return settings.status() == QSettings::NoError ? 0 : 17;
        }
        auto* service = window.findChild<iec61850sim::SclAnalysisService*>();
        service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
        QElapsedTimer timer;
        timer.start();
        while (!service->snapshot() && timer.elapsed() < 4000)
            QTest::qWait(10);
        if (!service->snapshot() || manager->dockWidgetsMap().size() != 4)
            return 19;
    }
    const bool grouped = server->dockAreaWidget() == client->dockAreaWidget();
    if (grouped == (mode == "--default-layout"))
        return 13;
    if (mode != "--write-layout")
    {
        auto* service = window.findChild<iec61850sim::SclAnalysisService*>();
        auto* workspace = window.findChild<QWidget*>("SclWorkspace");
        if (manager->dockWidgetsMap().size() != 3 || server->isClosed() || client->isClosed()
            || service->state() != iec61850sim::SclAnalysisService::State::Idle || workspace->isVisible())
            return 14;
    }
    window.close();
    QSettings settings(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    if (settings.format() != QSettings::IniFormat || settings.status() != QSettings::NoError)
        return 15;
    const auto layout = settings.value("MainWindow/DockLayout").toByteArray();
    return layout.isEmpty() || settings.value("MainWindow/DockLayoutVersion").toInt() != 1
        || settings.value("MainWindow/DockLayoutSha256").toByteArray()
            != QCryptographicHash::hash(layout, QCryptographicHash::Sha256) ? 16 : 0;
}

struct ProcessMemory { qint64 privateBytes = -1; qint64 peakWorkingSet = -1; };

ProcessMemory Memory()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters), sizeof counters))
        return {qint64(counters.PrivateUsage), qint64(counters.PeakWorkingSetSize)};
#endif
    return {};
}
}

class MainWindowBaselineTest final : public QObject
{
    Q_OBJECT
private slots:
    void ListsAndDetails();
    void LayoutAcrossProcesses();
    void ResetLayoutAndHelp();
    void SclLifecycle();
    void SclResponsiveAndClose();
    void SclWorkspaceFlow();
    void SclSearchFlow();
    void SclIssueExport();
    void SclLocalAcceptance();
    void SclLocalCycles();
};

void MainWindowBaselineTest::SclLocalCycles()
{
    using Service = iec61850sim::SclAnalysisService;
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty() || !QCoreApplication::arguments().contains("SclLocalCycles"))
        QSKIP("Run SclLocalCycles explicitly with IEC61850_LOCAL_SCL for ten-cycle acceptance");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    auto* close = window.findChild<QAction*>("CloseScl");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* reports = window.findChild<QTableView*>("SclReports");
    auto* overview = window.findChild<QLabel*>("SclOverview");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    QVERIFY(service && close && tree && reports && overview && source);
    QVERIFY(!close->isEnabled());
    QApplication::processEvents();
    qInfo() << "Baseline private bytes and peak working set:" << Memory().privateBytes << Memory().peakWorkingSet;
    QByteArray hash;
    size_t nodes = 0;
    size_t references = 0;
    for (int i = 1; i <= 10; ++i)
    {
        QElapsedTimer timer;
        timer.start();
        service->Open(path);
        QTRY_VERIFY_WITH_TIMEOUT(service->state() != Service::State::Loading, 25000);
        QVERIFY2(service->snapshot(), qPrintable(service->failure().diagnostic.detail));
        const auto loaded = timer.elapsed();
        std::weak_ptr<const iec61850sim::SclSnapshot> released;
        {
            const auto snapshot = service->snapshot();
            released = snapshot;
            if (i == 1)
            {
                hash = snapshot->analysis.sha256;
                nodes = snapshot->analysis.document->nodes.size();
                references = snapshot->analysis.references.size();
            }
            QCOMPARE(snapshot->analysis.sha256, hash);
            QCOMPARE(snapshot->analysis.document->nodes.size(), nodes);
            QCOMPARE(snapshot->analysis.references.size(), references);
            QVERIFY(nodes > 0);
        }
        timer.restart();
        close->trigger();
        QCOMPARE(service->state(), Service::State::Idle);
        QVERIFY(!service->snapshot() && !close->isEnabled());
        QCOMPARE(tree->model()->rowCount(), 0);
        QCOMPARE(reports->model()->rowCount(), 0);
        QVERIFY(source->toPlainText().isEmpty());
        QCOMPARE(overview->text(), QStringLiteral("尚无可浏览的文档"));
        QTRY_VERIFY(released.expired());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto memory = Memory();
        qInfo() << "Cycle" << i << "load ms" << loaded << "close ms" << timer.elapsed()
                << "private bytes after close" << memory.privateBytes << "peak working set" << memory.peakWorkingSet;
    }
    qInfo() << "Verified SHA-256, nodes, references:" << hash << nodes << references;
}

void MainWindowBaselineTest::SclLifecycle()
{
    using Service = iec61850sim::SclAnalysisService;
    using State = Service::State;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    const auto a = temp.filePath("A.SCD");
    const auto b = temp.filePath("B.cid");
    const auto bad = temp.filePath("bad.scd");
    QVERIFY(WriteFile(a, SclXml("A")));
    QVERIFY(WriteFile(b, SclXml("B")));
    QVERIFY(WriteFile(bad, "<SCL"));

    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    auto* status = window.findChild<QLabel*>("SclStatus");
    auto* reload = window.findChild<QAction*>("ReloadScl");
    auto* cancel = window.findChild<QAction*>("CancelScl");
    QVERIFY(service && status && reload && cancel);
    QVERIFY(!reload->isEnabled());
    QStringList published;
    connect(service, &Service::Changed, &window, [&] {
        if (service->state() == State::Ready && !service->sourceChanged())
            published.push_back(service->snapshot()->path);
    });
    window.statusBar()->showMessage(QStringLiteral("Redis 数据库不可用"));
    service->Open(a);
    service->Open(b);
    QVERIFY(cancel->isEnabled());
    QTRY_VERIFY(service->state() == State::Ready);
    QCOMPARE(published, QStringList{b});
    QVERIFY(status->text().contains("B.cid"));
    QCOMPARE(window.statusBar()->currentMessage(), QStringLiteral("Redis 数据库不可用"));
    QVERIFY(status->geometry().left() > window.statusBar()->width() / 3);
    const auto first = service->snapshot();
    QVERIFY(first && first->analysis.source);
    QCOMPARE(first->analysis.source->bytes(), SclXml("B"));

    service->Open(bad);
    QTRY_VERIFY(service->state() == State::Failed);
    QVERIFY(service->snapshot() == first);
    QCOMPARE(service->failure().diagnostic.code, Scl::Error::Xml);
    QCOMPARE(service->failure().source->bytes(), QByteArray("<SCL"));
    QVERIFY(status->text().contains("bad.scd") && status->text().contains("B.cid"));
    service->Open(temp.filePath("not-scl.txt"));
    QCOMPARE(service->state(), State::Failed);
    QVERIFY(service->snapshot() == first);

    QVERIFY(WriteFile(b, SclXml("updated")));
    QTRY_VERIFY(service->sourceChanged());
    QVERIFY(status->text().contains(QStringLiteral("重新加载")));
    reload->trigger();
    QTRY_VERIFY(service->state() == State::Ready);
    QVERIFY(service->snapshot() != first);
    QCOMPARE(service->snapshot()->path, b);
    QCOMPARE(service->snapshot()->analysis.source->bytes(), SclXml("updated"));
    QCOMPARE(first->analysis.source->bytes(), SclXml("B"));
    QVERIFY(!service->sourceChanged());

    service->Open(a);
    cancel->trigger();
    QCOMPARE(service->state(), State::Cancelled);
    QVERIFY(!cancel->isEnabled());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QCOMPARE(service->state(), State::Cancelled);
    QCOMPARE(published, (QStringList{b, b}));

    auto* close = window.findChild<QAction*>("CloseScl");
    QVERIFY(close);
    service->Open(a);
    close->trigger();
    QCOMPARE(service->state(), State::Idle);
    QVERIFY(!service->snapshot());
    QVERIFY(QThreadPool::globalInstance()->waitForDone(5000));
    QCoreApplication::processEvents();
    QCOMPARE(service->state(), State::Idle);
    QVERIFY(!close->isEnabled());
}

void MainWindowBaselineTest::SclResponsiveAndClose()
{
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    const auto path = temp.filePath("large.scd");
    QByteArray bytes("<SCL xmlns='http://www.iec.ch/61850/2003/SCL'>\n");
    bytes += QByteArray("<Private type='synthetic'/>\n").repeated(480000);
    bytes += "</SCL>";
    QVERIFY(WriteFile(path, bytes));

    auto window = std::make_unique<iec61850sim::MainWindow>();
    window->show();
    auto* service = window->findChild<Service*>();
    int loadingTicks = 0;
    QTimer heartbeat;
    heartbeat.setInterval(5);
    connect(&heartbeat, &QTimer::timeout, window.get(), [&] {
        if (service->state() == Service::State::Loading)
            ++loadingTicks;
    });
    service->Open(path);
    heartbeat.start();
    QTRY_VERIFY(service->stage() == Scl::TaskStage::Xml && loadingTicks > 2);
    QElapsedTimer elapsed;
    elapsed.start();
    service->Cancel();
    QCOMPARE(service->state(), Service::State::Cancelled);
    QVERIFY(elapsed.elapsed() < 500);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(3000));
    QCoreApplication::processEvents();
    QVERIFY(!service->snapshot());

    const auto next = temp.filePath("next.scd");
    QVERIFY(WriteFile(next, SclXml("next")));
    service->Open(path);
    QTRY_VERIFY(service->stage() == Scl::TaskStage::Xml);
    service->Open(next);
    QTRY_VERIFY(service->state() == Service::State::Ready);
    QCOMPARE(service->snapshot()->path, next);

    service->Open(path);
    QTRY_VERIFY(service->stage() == Scl::TaskStage::Xml);
    elapsed.restart();
    QVERIFY(window->close());
    window.reset();
    QVERIFY(elapsed.elapsed() < 500);
    QVERIFY(QThreadPool::globalInstance()->waitForDone(3000));
    QCoreApplication::processEvents();
}

void MainWindowBaselineTest::SclLocalAcceptance()
{
    using Service = iec61850sim::SclAnalysisService;
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty())
        QSKIP("Set IEC61850_LOCAL_SCL for optional local SCD acceptance");
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    QElapsedTimer elapsed;
    elapsed.start();
    service->Open(path);
    QTRY_VERIFY_WITH_TIMEOUT(service->state() != Service::State::Loading, 20000);
    QVERIFY2(service->snapshot(), qPrintable(service->failure().diagnostic.detail));
    const auto& analysis = service->snapshot()->analysis;
    qInfo() << "SCL ready in" << elapsed.elapsed() << "ms,"
            << analysis.document->nodes.size() << "nodes," << analysis.references.size() << "references,"
            << analysis.sha256;
    QVERIFY(!analysis.document->nodes.empty());
    CheckWorkspaceFlow(window);
    auto* query = window.findChild<QLineEdit*>("SclQuery");
    auto* exact = window.findChild<QCheckBox*>("SclExact");
    auto* results = window.findChild<QTableView*>("SclSearchResults");
    auto* status = window.findChild<QLabel*>("SclSearchStatus");
    auto* more = window.findChild<QPushButton*>("SclMoreResults");
    QVERIFY(query && exact && results && status && more);
    query->setFocus();
    QTRY_VERIFY(query->hasFocus());
    exact->setChecked(true);
    query->setText(analysis.reports.front().definitionReference);
    elapsed.restart();
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(results->model()->rowCount() > 0);
    qInfo() << "First report match in" << elapsed.elapsed() << "ms (debounce excluded)";
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("共")));

    query->setText("DAI");
    elapsed.restart();
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(results->model()->rowCount() > 0);
    qInfo() << "First DAI match in" << elapsed.elapsed() << "ms (debounce excluded)";
    QTRY_VERIFY_WITH_TIMEOUT(status->text().startsWith(QStringLiteral("共")), 15000);
    QCOMPARE(results->model()->rowCount(), 512);
    QVERIFY(more->isVisible());
    more->click();
    QCOMPARE(results->model()->rowCount(), 1024);
    qInfo() << "DAI search completed in" << elapsed.elapsed() << "ms:" << status->text();
    query->setText("LN");
    QTest::keyClick(query, Qt::Key_Return);
    QVERIFY(status->text().startsWith(QStringLiteral("正在搜索")));
    QTest::keyClick(query, Qt::Key_Escape);
    QVERIFY(status->text().contains(QStringLiteral("已取消")));
    QVERIFY(QThreadPool::globalInstance()->waitForDone(3000));
    QCoreApplication::processEvents();
    QCOMPARE(results->model()->rowCount(), 0);
}

void MainWindowBaselineTest::SclWorkspaceFlow()
{
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    QVERIFY(service);
    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/NamespaceCollision.scd"));
    QTRY_VERIFY(service->state() == Service::State::Ready);
    CheckWorkspaceFlow(window);
    QDir().mkpath(QString::fromUtf8(BASELINE_ARTIFACT_DIR));
    QVERIFY(window.grab().save(QString::fromUtf8(BASELINE_ARTIFACT_DIR) + QStringLiteral("/SclWorkspace.png")));
}

void MainWindowBaselineTest::SclSearchFlow()
{
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    QVERIFY(service);
    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/NamespaceCollision.scd"));
    QTRY_VERIFY(service->state() == Service::State::Ready);

    auto* workspace = window.findChild<QWidget*>("SclWorkspace");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* query = window.findChild<QLineEdit*>("SclQuery");
    auto* ied = window.findChild<QLineEdit*>("SclIedFilter");
    auto* exact = window.findChild<QCheckBox*>("SclExact");
    auto* mode = window.findChild<QComboBox*>("SclSearchMode");
    auto* severity = window.findChild<QComboBox*>("SclSeverityFilter");
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* reports = window.findChild<QTableView*>("SclReports");
    auto* results = window.findChild<QTableView*>("SclSearchResults");
    auto* status = window.findChild<QLabel*>("SclSearchStatus");
    auto* cancel = window.findChild<QPushButton*>("SclCancelSearch");
    auto* back = window.findChild<QPushButton*>("SclBack");
    auto* forward = window.findChild<QPushButton*>("SclForward");
    QVERIFY(workspace && tree && query && ied && exact && mode && severity && tabs && reports && results
        && status && cancel && back && forward);
    window.activateWindow();
    tree->setFocus();
    QTRY_VERIFY(tree->hasFocus());
    QTest::keyClick(tree, Qt::Key_F, Qt::ControlModifier);
    QTRY_VERIFY(query->hasFocus());
    const auto root = tree->model()->index(0, 0);
    QModelIndex device;
    for (int i = 0; i < tree->model()->rowCount(root); ++i)
    {
        const auto index = tree->model()->index(i, 0, root);
        if (index.data().toString().startsWith("IED"))
            device = index;
    }
    QVERIFY(device.isValid() && !tree->isExpanded(device));
    query->setText(QStringLiteral("合成遥信"));
    QTRY_COMPARE(results->model()->rowCount(), 1);
    QCOMPARE(tabs->currentWidget(), results);
    const auto hit = results->model()->index(0, 0);
    const auto node = hit.data(Qt::UserRole).toInt();
    QVERIFY(QMetaObject::invokeMethod(results, "activated", Q_ARG(QModelIndex, hit)));
    QCOMPARE(tree->currentIndex().data(Qt::UserRole).toInt(), node);
    QVERIFY(tree->isExpanded(device));
    QVERIFY(back->isEnabled());
    back->click();
    QCOMPARE(tree->currentIndex(), root);
    QVERIFY(forward->isEnabled());
    forward->click();
    QCOMPARE(tree->currentIndex().data(Qt::UserRole).toInt(), node);

    exact->setChecked(true);
    query->setText("EXPLICIT_LD/LLN0.BR.brcbEvents");
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_COMPARE(results->model()->rowCount(), 1);
    QCOMPARE(results->model()->index(0, 1).data().toString(), QStringLiteral("报告映射"));
    query->setText("[no+matches]$");
    QTRY_VERIFY(status->text().contains(QStringLiteral("没有匹配")));
    QCOMPARE(results->model()->rowCount(), 0);
    query->setText("IED");
    QVERIFY(cancel->isEnabled());
    QTest::keyClick(query, Qt::Key_Escape);
    QVERIFY(status->text().contains(QStringLiteral("已取消")));
    QTest::qWait(260);
    QCOMPARE(results->model()->rowCount(), 0);

    query->setText("EXT001");
    severity->setCurrentIndex(3);
    QTRY_COMPARE(results->model()->rowCount(), 1);
    QCOMPARE(results->model()->index(0, 1).data().toString(), QStringLiteral("EXT001"));
    severity->setCurrentIndex(0);
    query->clear();
    mode->setCurrentIndex(1);
    tabs->setCurrentWidget(reports);
    ied->setText("no-such-ied");
    QTRY_COMPARE(reports->model()->rowCount(), 0);
    ied->setText("IED_TEST");
    QTRY_VERIFY(status->text() == QStringLiteral("筛选后 2 行"));
    QCOMPARE(reports->model()->rowCount(), 2);
    const auto replacement = temp.filePath("replacement.scd");
    QVERIFY(WriteFile(replacement, SclXml("replacement")));
    mode->setCurrentIndex(0);
    query->setText("IED");
    QTest::keyClick(query, Qt::Key_Return);
    service->Open(replacement);
    QTRY_VERIFY(service->state() == Service::State::Ready);
    QCOMPARE(service->snapshot()->path, replacement);
    QVERIFY(query->text().isEmpty());
    QCOMPARE(results->model()->rowCount(), 0);
}

void MainWindowBaselineTest::SclIssueExport()
{
    using namespace iec61850sim;
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    const auto source = temp.filePath("source.scd");
    QVERIFY(WriteFile(source, SclXml("original")));
    Service service;
    service.Open(source);
    QTRY_VERIFY(service.state() == Service::State::Ready);
    auto snapshot = *service.snapshot();
    snapshot.analysis.findings.push_back({"TEST", Scl::Severity::Warning, 0, {0, 1},
        QStringLiteral("  =1+1"), QStringLiteral("中文,\"引号\"\n下一行"),
        QStringLiteral("\t+cmd"), QStringLiteral("@formula")});
    const auto jsonPath = temp.filePath("issues.json");
    const auto csvPath = temp.filePath("issues.csv");
    QCOMPARE(ExportSclIssues(snapshot, jsonPath, IssueFormat::Json), QString());
    QCOMPARE(ExportSclIssues(snapshot, csvPath, IssueFormat::Csv), QString());
    QFile json(jsonPath);
    QVERIFY(json.open(QIODevice::ReadOnly));
    const auto report = QJsonDocument::fromJson(json.readAll()).object();
    QCOMPARE(report.value("sha256").toString().toLatin1(), snapshot.analysis.sha256);
    QCOMPARE(report.value("ruleSetVersion").toString(), snapshot.analysis.ruleSetVersion);
    QCOMPARE(report.value("coverage").toArray().size(), int(snapshot.analysis.coverage.size()));
    const auto issue = report.value("findings").toArray().last().toObject();
    QCOMPARE(issue.value("detail").toString(), QStringLiteral("中文,\"引号\"\n下一行"));
    QCOMPARE(issue.value("objectPath").toString(), QStringLiteral("  =1+1"));
    QCOMPARE(issue.value("line").toInt(), 1);
    QCOMPARE(issue.value("byteOffset").toInt(), 0);
    QFile csv(csvPath);
    QVERIFY(csv.open(QIODevice::ReadOnly));
    const auto bytes = csv.readAll();
    QVERIFY(bytes.contains("\"coverage\",\"source.scd\""));
    QVERIFY(bytes.contains(snapshot.analysis.sha256));
    QVERIFY(bytes.contains(QStringLiteral("\"中文,\"\"引号\"\"\n下一行\"").toUtf8()));
    QVERIFY(bytes.contains("\"'  =1+1\""));
    QVERIFY(bytes.contains("\"'\t+cmd\""));
    QVERIFY(bytes.contains("\"'@formula\""));
    QVERIFY(!ExportSclIssues(snapshot, source, IssueFormat::Json).isEmpty());
    QFile original(source);
    QVERIFY(original.open(QIODevice::ReadOnly));
    QCOMPARE(original.readAll(), SclXml("original"));
}

void MainWindowBaselineTest::ListsAndDetails()
{
    QTemporaryDir settings;
    QVERIFY(settings.isValid());
    IsolateSettings(settings.path());
    iec61850sim::MainWindow window;
    window.show();
    QApplication::processEvents();
    auto* manager = window.findChild<ads::CDockManager*>();
    QVERIFY(manager);
    QCOMPARE(manager->dockWidgetsMap().size(), 3);
    auto* serverDock = manager->findDockWidget(QStringLiteral("服务器"));
    auto* clientDock = manager->findDockWidget(QStringLiteral("客户端"));
    QVERIFY(serverDock && clientDock);
    auto* servers = qobject_cast<QListWidget*>(serverDock->widget());
    auto* clients = qobject_cast<QListWidget*>(clientDock->widget());
    QVERIFY(servers && clients);
    auto* toolbar = window.findChild<QToolBar*>(QStringLiteral("MainToolBar"));
    QVERIFY(toolbar);
    const auto trigger = [&](const QString& text) {
        for (auto* action : toolbar->actions())
            if (action->text() == text)
            {
                action->trigger();
                return true;
            }
        return false;
    };
    QVERIFY(trigger(QStringLiteral("新建服务器")));
    QVERIFY(trigger(QStringLiteral("新建客户端")));
    QCOMPARE(servers->count(), 1);
    QCOMPARE(clients->count(), 1);
    QCOMPARE(servers->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("server-1"));
    QCOMPARE(clients->currentItem()->data(Qt::UserRole).toString(), QStringLiteral("client-1"));
    auto* detail = manager->findDockWidget(QStringLiteral("客户端 1"));
    QVERIFY(detail);
    auto* label = qobject_cast<QLabel*>(detail->widget());
    QVERIFY(label);
    QVERIFY(label->text().contains(QStringLiteral("名称：客户端 1")));
    QCOMPARE(manager->dockWidgetsMap().size(), 5);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("客户端 1")));
    // Selection activates the existing detail instead of adding a duplicate.
    QTest::mouseClick(servers->viewport(), Qt::LeftButton, Qt::NoModifier,
        servers->visualItemRect(servers->item(0)).center());
    QApplication::processEvents();
    auto* serverDetail = manager->findDockWidget(QStringLiteral("服务器 1"));
    QVERIFY(serverDetail);
    QVERIFY(serverDetail->isVisible());
    QCOMPARE(serverDetail->dockAreaWidget()->currentDockWidget(), serverDetail);
    QCOMPARE(manager->dockWidgetsMap().size(), 5);
    QVERIFY(trigger(QStringLiteral("新建服务器")));
    QCOMPARE(servers->count(), 2);
    QCOMPARE(clients->count(), 1);
    QCOMPARE(manager->dockWidgetsMap().size(), 6);
    QDir().mkpath(QString::fromUtf8(BASELINE_ARTIFACT_DIR));
    QVERIFY(window.grab().save(QString::fromUtf8(BASELINE_ARTIFACT_DIR) + QStringLiteral("/MainWindow.png")));
    window.close();
}

void MainWindowBaselineTest::LayoutAcrossProcesses()
{
    QTemporaryDir settings;
    QVERIFY(settings.isValid());
    const auto run = [&](const QString& mode) {
        QProcess process;
        process.start(QCoreApplication::applicationFilePath(), {mode, settings.path()});
        if (!process.waitForStarted() || !process.waitForFinished(10000))
            return mode + ' ' + process.errorString();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
            return mode + QStringLiteral(" probe exit=%1 ").arg(process.exitCode())
                + QString::fromUtf8(process.readAllStandardError());
        return QString();
    };
    QCOMPARE(run("--write-layout"), QString());
    QCOMPARE(run("--read-layout"), QString());
    QCOMPARE(run("--legacy-layout"), QString());
    QCOMPARE(run("--read-layout"), QString());

    IsolateSettings(settings.path());
    QSettings stored(QSettings::defaultFormat(), QSettings::UserScope,
        QStringLiteral("IEC61850Simulator"), QStringLiteral("IEC61850Simulator"));
    const auto custom = stored.value("MainWindow/DockLayout").toByteArray();
    const auto digest = stored.value("MainWindow/DockLayoutSha256").toByteArray();
    QVERIFY(!custom.isEmpty() && !digest.isEmpty());
    stored.setValue("MainWindow/DockLayoutVersion", 999);
    stored.sync();
    QCOMPARE(run("--default-layout"), QString());

    stored.sync();
    stored.setValue("MainWindow/DockLayout", QByteArray("damaged state"));
    stored.setValue("MainWindow/DockLayoutSha256", digest);
    stored.sync();
    QCOMPARE(run("--default-layout"), QString());

    stored.sync();
    const QByteArray corrupt("valid checksum but invalid ADS data");
    stored.setValue("MainWindow/DockLayout", corrupt);
    stored.setValue("MainWindow/DockLayoutSha256", QCryptographicHash::hash(corrupt, QCryptographicHash::Sha256));
    stored.sync();
    QCOMPARE(run("--default-layout"), QString());
}

void MainWindowBaselineTest::ResetLayoutAndHelp()
{
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* manager = window.findChild<ads::CDockManager*>();
    auto* reset = window.findChild<QAction*>("ResetDockLayout");
    auto* help = window.findChild<QAction*>("SclHelp");
    auto* toolbar = window.findChild<QToolBar*>("MainToolBar");
    auto* service = window.findChild<Service*>();
    auto* simulation = window.findChild<QAction*>("ShowSimulation");
    QVERIFY(manager && reset && help && toolbar && service && simulation);
    auto* server = manager->findDockWidget(QStringLiteral("服务器"));
    auto* client = manager->findDockWidget(QStringLiteral("客户端"));
    QVERIFY(server && client && server->dockAreaWidget() != client->dockAreaWidget());
    manager->addDockWidget(ads::CenterDockWidgetArea, client, server->dockAreaWidget());
    toolbar->actions().front()->trigger();
    auto* detail = manager->findDockWidget(QStringLiteral("服务器 1"));
    QVERIFY(detail);
    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
    QTRY_VERIFY(service->state() == Service::State::Ready);
    const auto snapshot = service->snapshot();
    reset->trigger();
    QVERIFY(service->snapshot() == snapshot);
    QVERIFY(server->dockAreaWidget() != client->dockAreaWidget());
    QVERIFY(server->isClosed() && client->isClosed());
    QCOMPARE(manager->findDockWidget(QStringLiteral("服务器 1")), detail);
    QVERIFY(detail->dockAreaWidget());
    simulation->trigger();
    QVERIFY(!server->isClosed() && !client->isClosed());

    QString helpText;
    QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
        {
            helpText = dialog->text();
            dialog->accept();
        }
    });
    help->trigger();
    QVERIFY(helpText.contains("Ctrl+F") && helpText.contains(QStringLiteral("关闭 SCL 文档")));
}

int main(int argc, char** argv)
{
    QApplication application(argc, argv);
    const auto arguments = application.arguments();
    if (arguments.size() == 3 && arguments[1].endsWith(QStringLiteral("-layout")))
    {
        IsolateSettings(arguments[2]);
        return LayoutProbe(arguments[1]);
    }
    MainWindowBaselineTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "MainWindowBaselineTest.moc"
