#include "MainWindow.hpp"
#include "SclAnalysisService.hpp"
#include "SclIssueExport.hpp"
#include "SclSourcePreview.hpp"
#include "SclTopology.hpp"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDir>
#include <QDesktopServices>
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
#include <QScrollBar>
#include <QStatusBar>
#include <QTabWidget>
#include <QTableView>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextLayout>
#include <QThreadPool>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QUrl>
#include <QtTest>
#include <DockManager.h>
#include <DockAreaWidget.h>
#include <DockWidget.h>
#include <algorithm>
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
    auto* dataSets = window.findChild<QTableView*>("SclDataSets");
    auto* members = window.findChild<QTableView*>("SclMembers");
    auto* links = window.findChild<QTableView*>("SclLinks");
    auto* findings = window.findChild<QTableView*>("SclFindings");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* properties = window.findChild<QTextBrowser*>("SclProperties");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    auto* back = window.findChild<QPushButton*>("SclBack");
    auto* simulation = window.findChild<QAction*>("ShowSimulation");
    auto* showAnalysis = window.findChild<QAction*>("ShowSclWorkspace");
    auto* manager = window.findChild<ads::CDockManager*>();
    QVERIFY(service && workspace && reports && dataSets && members && links && findings && tree && tabs
        && inspector && source && sourceLabel && properties && back && simulation && showAnalysis && manager);
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
    QCOMPARE(tabs->currentWidget(), dataSets);
    QCOMPARE(dataSets->currentIndex().data(Qt::UserRole).toInt(), dataSet);
    const auto dataSetTree = tree->currentIndex();
    tabs->setCurrentWidget(members);
    QVERIFY(members->model()->rowCount() > 0);
    const auto memberIndex = members->model()->index(0, 0);
    const auto member = memberIndex.data(Qt::UserRole).toInt();
    QCOMPARE(memberIndex.data().toString(), QStringLiteral("1"));
    members->setCurrentIndex(memberIndex);
    QCOMPARE(selected(), member);

    const auto target = links->model()->index(0, 0).data(Qt::UserRole).toInt();
    QVERIFY(target > 0 && target != NoNode);
    auto* referencePath = window.findChild<QLabel*>("SclReferencePath");
    QVERIFY(referencePath && referencePath->text().contains("doName"));
    QVERIFY(referencePath->text().contains(QStringLiteral("href='%1'").arg(target)));
    QVERIFY(QMetaObject::invokeMethod(referencePath, "linkActivated", Q_ARG(QString, QString::number(target))));
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
    QCOMPARE(reports->currentIndex(), reportIndex);

    tree->setCurrentIndex(dataSetTree.parent());
    QCOMPARE(tabs->currentIndex(), 0);
    auto* scope = window.findChild<QLabel*>("SclNodeOverview");
    QVERIFY(scope && scope->isVisible() && scope->text().contains("LN"));
    tree->setCurrentIndex(dataSetTree);
    QCOMPARE(tabs->currentWidget(), dataSets);
    QCOMPARE(analysis.document->nodes[member].parent, dataSet);
    const auto memberTree = tree->model()->index(snapshot->browse.branches[member].row, 0, dataSetTree);
    QVERIFY(memberTree.isValid());
    tree->setCurrentIndex(memberTree);
    QCOMPARE(tabs->currentWidget(), members);
    QCOMPARE(members->currentIndex().data(Qt::UserRole).toInt(), member);

    QVERIFY(findings->model()->rowCount() > 0);
    tabs->setCurrentWidget(findings);
    const auto issue = findings->model()->index(0, 0);
    QVERIFY(QMetaObject::invokeMethod(findings, "activated", Q_ARG(QModelIndex, issue)));
    QCOMPARE(selected(), issue.data(Qt::UserRole).toInt());
    QCOMPARE(inspector->currentIndex(), 2);
    QCOMPARE(source->extraSelections().size(), 1);
    const auto issueLine = issue.siblingAtColumn(2).data().toInt();
    QVERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行").arg(issueLine)));
    QCOMPARE(source->extraSelections().front().cursor.block().text().trimmed(),
             analysis.source->line(issueLine).text.trimmed().left(1024).trimmed());
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

class TopologyUrlReceiver final : public QObject
{
    Q_OBJECT
public:
    TopologyUrlReceiver() { QDesktopServices::setUrlHandler(QStringLiteral("file"), this, "Capture"); }
    ~TopologyUrlReceiver() override { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); }
    QUrl url;

public slots:
    void Capture(const QUrl& value) { url = value; }
};

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
    void SclReferenceDetails();
    void SclSourceHighlighting();
    void SclTopologyFlow();
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
    if (QTest::currentTestFailed())
        return;
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* reports = window.findChild<QTableView*>("SclReports");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    QVERIFY(reports->model()->rowCount() >= 2);
    tabs->setCurrentWidget(reports);
    reports->setCurrentIndex(reports->model()->index(0, 0));
    const auto first = tree->currentIndex();
    reports->setCurrentIndex(reports->model()->index(1, 0));
    const auto second = tree->currentIndex();
    QVERIFY(first.isValid() && second.isValid() && first != second);
    inspector->setCurrentIndex(2);
    const auto overview = tabs->widget(0);
    tabs->setCurrentWidget(overview);
    tree->setCurrentIndex(first);
    QCOMPARE(tabs->currentWidget(), reports);
    QCOMPARE(reports->currentIndex().row(), 0);
    QApplication::processEvents();
    QList<qint64> samples;
    for (int i = 0; i < 40; ++i)
    {
        elapsed.start();
        tree->setCurrentIndex(i % 2 == 0 ? second : first);
        QApplication::processEvents();
        // Force an offscreen raster update of the visible window.
        const auto frame = window.grab();
        samples.push_back(elapsed.nsecsElapsed());
        QVERIFY(!frame.isNull());
    }
    std::sort(samples.begin(), samples.end());
    qInfo() << "Tree navigation P95 ms (40 offscreen frames):" << samples[37] / 1e6;
    for (auto* view : {static_cast<QAbstractItemView*>(tree), static_cast<QAbstractItemView*>(reports)})
    {
        auto* scroll = view->verticalScrollBar();
        const int previous = scroll->value();
        QVERIFY(scroll->maximum() > scroll->minimum());
        samples.clear();
        for (int i = 0; i < 40; ++i)
        {
            elapsed.start();
            scroll->setValue(scroll->minimum() + (i + 1) * (scroll->maximum() - scroll->minimum()) / 40);
            QApplication::processEvents();
            const auto frame = view->viewport()->grab();
            samples.push_back(elapsed.nsecsElapsed());
            QVERIFY(!frame.isNull());
        }
        std::sort(samples.begin(), samples.end());
        qInfo() << view->objectName() << "scroll P95 ms (40 offscreen frames):" << samples[37] / 1e6;
        scroll->setValue(previous);
    }
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

    const auto reportWithId = std::find_if(analysis.reports.cbegin(), analysis.reports.cend(),
        [](const auto& report) { return !report.rptId.isEmpty(); });
    QVERIFY(reportWithId != analysis.reports.cend());
    query->setText(reportWithId->rptId);
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(results->model()->rowCount() > 0);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("共")));
    qInfo() << "Exact raw rptID search returned results";

    elapsed.restart();
    const auto topology = iec61850sim::BuildSclTopology(*service->snapshot(), analysis.reports.front().source);
    QVERIFY(!topology.isEmpty());
    QCOMPARE(topology.value("sha256").toString(), QString::fromLatin1(analysis.sha256));
    const auto topologyViews = topology.value("views").toObject();
    QVERIFY(topologyViews.size() > 1);
    qInfo() << "Report topology ms, nodes, views:" << elapsed.elapsed() << topology.value("nodes").toArray().size()
            << topologyViews.size();

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
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    QVERIFY(source && source->toPlainText().isEmpty());
    CheckWorkspaceFlow(window);
    QDir().mkpath(QString::fromUtf8(BASELINE_ARTIFACT_DIR));
    QVERIFY(window.grab().save(QString::fromUtf8(BASELINE_ARTIFACT_DIR) + QStringLiteral("/SclWorkspace.png")));
    auto* details = window.findChild<QToolButton*>("SclToggleDetails");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* tables = window.findChild<QTabWidget*>("SclTables");
    QVERIFY(details && inspector && tables && inspector->isVisible());
    window.resize(760, 640);
    QCoreApplication::processEvents();
    const int width = tables->width();
    const int page = inspector->currentIndex();
    details->setFocus();
    QTest::keyClick(details, Qt::Key_Space);
    QTRY_VERIFY(!inspector->isVisible() && tables->width() > width);
    QTest::keyClick(details, Qt::Key_Space);
    QTRY_VERIFY(inspector->isVisible());
    QCOMPARE(inspector->currentIndex(), page);
}

void MainWindowBaselineTest::SclReferenceDetails()
{
    using Service = iec61850sim::SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    iec61850sim::MainWindow window;
    window.show();
    auto* service = window.findChild<Service*>();
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* reports = window.findChild<QTableView*>("SclReports");
    auto* candidates = window.findChild<QTableView*>("SclCandidates");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* path = window.findChild<QLabel*>("SclBreadcrumb");
    auto* reference = window.findChild<QLabel*>("SclReferencePath");
    auto* properties = window.findChild<QTextBrowser*>("SclProperties");
    auto* copy = window.findChild<QPushButton*>("SclCopyDeclared");
    auto* copyDerived = window.findChild<QPushButton*>("SclCopyDerived");
    auto* failed = window.findChild<QPushButton*>("SclFailedSource");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    QVERIFY(service && tabs && inspector && reports && candidates && tree && path && reference && properties
        && copy && copyDerived && failed && sourceLabel && source);

    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
    QTRY_COMPARE(service->state(), Service::State::Ready);
    auto* overview = window.findChild<QLabel*>("SclOverview");
    QVERIFY(overview && overview->text().contains(QStringLiteral("完整 XSD：未执行")));
    tabs->setCurrentWidget(reports);
    reports->setCurrentIndex(reports->model()->index(1, 0));
    QVERIFY(properties->toPlainText().contains(QStringLiteral("原始 rptID\n未声明")));
    reports->setCurrentIndex(reports->model()->index(0, 0));
    const auto owner = tree->currentIndex().parent();
    QVERIFY(owner.isValid());
    QVERIFY(path->text().contains(QStringLiteral("href='%1'").arg(owner.data(Qt::UserRole).toInt())));
    QVERIFY(QMetaObject::invokeMethod(path, "linkActivated", Q_ARG(QString, owner.data(Qt::UserRole).toString())));
    QCOMPARE(tree->currentIndex(), owner);
    tabs->setCurrentWidget(reports);
    reports->setCurrentIndex(reports->model()->index(0, 0));
    inspector->setCurrentIndex(1);
    QVERIFY(reference->text().contains(QStringLiteral("已解析")));
    const auto* links = window.findChild<QTableView*>("SclLinks");
    const auto* dataSets = window.findChild<QTableView*>("SclDataSets");
    QVERIFY(links && dataSets);
    const auto lastColumn = [](const QTableView* view) {
        return view->model()->index(0, view->model()->columnCount() - 1);
    };
    const auto description = QStringLiteral("有序成员，含 DO 引用及重复成员");
    QCOMPARE(lastColumn(dataSets).data().toString(), description);
    QCOMPARE(lastColumn(links).data().toString(), description);
    copy->click();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("dsEvents"));
    QVERIFY(copyDerived->isEnabled());
    copyDerived->click();
    QCOMPARE(QApplication::clipboard()->text(), service->snapshot()->analysis.reports.front().dataSetReference);

    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/DanglingDataSet.scd"));
    QTRY_COMPARE(service->state(), Service::State::Ready);
    tabs->setCurrentWidget(reports);
    reports->setCurrentIndex(reports->model()->index(0, 0));
    QVERIFY(reference->text().contains(QStringLiteral("找不到目标")));
    QVERIFY(reference->text().contains("missingDataSet"));

    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/DuplicateDataSet.scd"));
    QTRY_COMPARE(service->state(), Service::State::Ready);
    tabs->setCurrentWidget(reports);
    reports->setCurrentIndex(reports->model()->index(0, 0));
    inspector->setCurrentIndex(1);
    QVERIFY(reference->text().contains(QStringLiteral("有歧义")));
    QCOMPARE(candidates->model()->rowCount(), 2);
    QVERIFY(lastColumn(links).data().toString().isEmpty());
    QVERIFY(candidates->isVisible());

    const auto retained = service->snapshot();
    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Truncated.scd"));
    QTRY_COMPARE(service->state(), Service::State::Failed);
    QCOMPARE(service->snapshot(), retained);
    failed->click();
    QVERIFY(sourceLabel->text().contains(QStringLiteral("失败的加载请求")));
    QVERIFY(!source->toPlainText().isEmpty());
    QVERIFY(source->toPlainText().contains(service->failure().source->line(service->failure().diagnostic.line).text.trimmed()));
    tree->setCurrentIndex(tree->model()->index(0, 0));
    QVERIFY(sourceLabel->text().contains("DuplicateDataSet.scd"));
}

void MainWindowBaselineTest::SclSourceHighlighting()
{
    const auto xml = QStringLiteral("<SCL xmlns='http://www.iec.ch/61850/2003/SCL'>\r\n"
        "<!-- comment\r\n<DO> fake -->\r\n"
        "<Header id='值 &amp;\r\n继续'/>\r\n"
        "<Private><![CDATA[内容\r\n<fake>]]></Private>\r\n"
        "<Private>前\u2029后</Private>\r\n</SCL>");
    iec61850sim::SclSourcePreview preview(nullptr);
    const auto color = [&](int block, int column) {
        for (const auto& range : preview.document()->findBlockByNumber(block).layout()->formats())
            if (column >= range.start && column < range.start + range.length)
                return range.format.foreground().color();
        return QColor();
    };
    for (const bool utf16 : {false, true})
    {
        QByteArray bytes = xml.toUtf8();
        if (utf16)
        {
            bytes = QByteArray("\xff\xfe", 2);
            for (const auto ch : xml)
            {
                bytes.append(char(ch.unicode() & 0xff));
                bytes.append(char(ch.unicode() >> 8));
            }
        }
        const auto result = Scl::Reader::readBytes(bytes);
        QVERIFY(result.document);
        const auto location = result.document->nodes[1].source;
        QVERIFY(preview.Show(result.source.get(), location, result.document.get()).contains(QStringLiteral("第 4 行")));
        QCOMPARE(preview.extraSelections().front().cursor.block().text(), QStringLiteral("<Header id='值 &amp;"));
        QVERIFY(color(0, 0).isValid());
        QVERIFY(color(0, 0) != color(3, 8)); // Tag and attribute
        QCOMPARE(color(1, 0), color(2, 0)); // Multiline comment
        QCOMPARE(color(3, 12), color(4, 0)); // Multiline value
        QVERIFY(color(3, 14) != color(3, 12)); // Entity and value
        QVERIFY(color(6, 0).isValid() && color(6, 0) != color(0, 0)); // CDATA
        preview.selectAll();
        preview.copy();
        auto expected = xml;
        QCOMPARE(QApplication::clipboard()->text(), expected.replace(QStringLiteral("\r\n"), QStringLiteral("\n")));
    }

    const auto bounded = Scl::Reader::readBytes("<SCL xmlns='http://www.iec.ch/61850/2003/SCL'>\n<!--\n"
        + QByteArray("  <DO>\n").repeated(20) + "-->\n" + QByteArray(1100, ' ')
        + "<Header id='late'/>\n<Private/>\n</SCL>");
    QVERIFY(bounded.document);
    const auto location = bounded.document->nodes[1].source;
    const auto status = preview.Show(bounded.source.get(), location, bounded.document.get());
    QVERIFY(status.contains(QStringLiteral("已截断")) && status.contains(QStringLiteral("仅定位到行")));
    QVERIFY(!color(0, 2).isValid()); // The window starts inside a comment, with no trusted opening tag yet.
    const auto line = preview.extraSelections().front().cursor.block();
    QCOMPARE(line.text(), QString(1024, ' '));
    QVERIFY(color(line.blockNumber() + 1, 0).isValid()); // Resume at a verified opening tag.
}

void MainWindowBaselineTest::SclTopologyFlow()
{
    using namespace iec61850sim;
    using Service = SclAnalysisService;
    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    const auto input = temp.filePath("Topology.scd");
    const auto bytes = QByteArray(R"(<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
<IED name="Synthetic"><AccessPoint name="A"><Server>
<LDevice inst="Owner"><LN0 lnType="L"><DataSet name="d">
<FCDA lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="0"/>
<FCDA lnClass="LLN0" doName="Obj.Sub" daName="values(0).leaf" fc="ST"/>
<FCDA lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="1"/>
<FCDA ldInst="Other" lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="0"/>
<FCDA lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="9"/>
)") + QByteArray("<FCDA lnClass='LLN0' doName='Obj.Sub' daName='values.leaf' fc='ST' ix='1'/>\n").repeated(26)
        + R"(</DataSet><ReportControl name="r&lt;/script&gt;&quot;&apos;" datSet="d"/></LN0></LDevice>
<LDevice inst="Other"><LN0 lnType="L"/></LDevice>
</Server></AccessPoint></IED>
<DataTypeTemplates>
<LNodeType id="L" lnClass="LLN0"><DO name="Obj" type="D"/></LNodeType>
<DOType id="D" cdc="SPS"><SDO name="Sub" type="S"/></DOType>
<DOType id="S" cdc="SPS"><DA name="values" fc="ST" bType="Struct" type="A" count="3"/></DOType>
<DAType id="A"><BDA name="leaf" bType="BOOLEAN"/></DAType>
</DataTypeTemplates></SCL>)";
    QVERIFY(WriteFile(input, bytes));
    TopologyUrlReceiver browser;
    QString pagePath;
    {
        MainWindow window;
        window.show();
        auto* service = window.findChild<Service*>();
        auto* reports = window.findChild<QTableView*>("SclReports");
        auto* tabs = window.findChild<QTabWidget*>("SclTables");
        auto* open = window.findChild<QPushButton*>("SclOpenTopology");
        QVERIFY(service && reports && tabs && open && !open->isEnabled());
        service->Open(input);
        QTRY_COMPARE(service->state(), Service::State::Ready);
        const auto snapshot = service->snapshot();
        QVERIFY(snapshot && snapshot->analysis.reports.size() == 1);
        const auto report = snapshot->analysis.reports.front().source;
        const auto graph = BuildSclTopology(*snapshot, report);
        QCOMPARE(graph.value("version").toInt(), 1);
        const auto nodes = graph.value("nodes").toArray();
        QVERIFY(nodes.size() <= 768);
        const auto views = graph.value("views").toObject();
        const auto group = [&views](int id, const QString& prefix) {
            for (const auto item : views.value(QString::number(id)).toArray())
                if (item.toObject().value("label").toString().startsWith(prefix))
                    return item.toObject();
            return QJsonObject{};
        };
        const auto edges = [](const QJsonObject& data) { return data.value("edges").toArray(); };
        const auto target = [](const QJsonArray& list, int row) { return list.at(row).toObject().value("to").toInt(-1); };
        const int root = graph.value("focus").toInt(-1);
        const int dataSet = target(edges(group(root, QStringLiteral("控制块引用数据集"))), 0);
        QVERIFY(dataSet >= 0);
        const auto members = group(dataSet, QStringLiteral("数据集成员"));
        QCOMPARE(members.value("total").toInt(), 31);
        const auto memberEdges = edges(members);
        QCOMPARE(memberEdges.size(), 24);
        for (int row = 0; row < memberEdges.size(); ++row)
            QCOMPARE(memberEdges.at(row).toObject().value("label").toString(), QStringLiteral("FCDA #%1").arg(row + 1));

        const auto instance = [&](int row) {
            return target(edges(group(target(memberEdges, row), QStringLiteral("对象实例和派生映射"))), 0);
        };
        const int first = instance(0);
        QVERIFY(first >= 0);
        QCOMPARE(instance(1), first); // inline and explicit selectors for the same access
        QVERIFY(instance(2) != first && instance(3) != first);
        QCOMPARE(group(first, QStringLiteral("引用同一对象实例")).value("total").toInt(), 2);
        const int definition = target(edges(group(first, QStringLiteral("对象实例和模板定义"))), 0);
        QVERIFY(definition >= 0);
        QCOMPARE(group(definition, QStringLiteral("引用同一模板定义")).value("total").toInt(), 30);
        const auto doSteps = edges(group(target(memberEdges, 0), "doName"));
        QCOMPARE(nodes.at(target(doSteps, 1)).toObject().value("kind").toString(), QStringLiteral("DO"));
        QCOMPARE(nodes.at(target(doSteps, 2)).toObject().value("kind").toString(), QStringLiteral("SDO"));
        const auto daSteps = edges(group(target(memberEdges, 0), "daName"));
        QCOMPARE(nodes.at(target(daSteps, 1)).toObject().value("kind").toString(), QStringLiteral("DA"));
        QCOMPARE(nodes.at(target(daSteps, 2)).toObject().value("kind").toString(), QStringLiteral("BDA"));
        const auto arraySteps = edges(group(target(memberEdges, 0), "ix"));
        QCOMPARE(nodes.at(target(arraySteps, 1)).toObject().value("kind").toString(), QStringLiteral("DA"));
        QVERIFY(!nodes.at(target(doSteps, 1)).toObject().value("excerpt").toArray().isEmpty());
        const auto invalid = target(memberEdges, 4);
        QVERIFY(group(invalid, QStringLiteral("对象实例和派生映射")).isEmpty());
        const auto stopped = edges(group(invalid, "ix"));
        QCOMPARE(stopped.last().toObject().value("kind").toString(), QStringLiteral("status"));
        QCOMPARE(stopped.last().toObject().value("label").toString(), QStringLiteral("缺失"));

        tabs->setCurrentWidget(reports);
        reports->setCurrentIndex(reports->model()->index(0, 0));
        QVERIFY(open->isEnabled());
        open->click();
        QTRY_VERIFY(browser.url.isLocalFile());
        pagePath = browser.url.toLocalFile();
        QByteArray pageBytes;
        {
            QFile page(pagePath);
            QVERIFY(page.open(QIODevice::ReadOnly));
            pageBytes = page.readAll();
        }
        QCOMPARE(pageBytes.count("</script>"), 2);
        const QByteArray marker("id=\"graph-data\">");
        const auto offset = pageBytes.indexOf(marker) + marker.size();
        QVERIFY(offset >= marker.size());
        const auto data = QByteArray::fromBase64(pageBytes.mid(offset, pageBytes.indexOf("</script>", offset) - offset));
        QVERIFY(!data.contains(input.toUtf8()));
        QVERIFY(data.contains("</script>"));
        QCOMPARE(QJsonDocument::fromJson(data).object(), graph);

        const std::pair<const char*, int> fixtures[] = {{"DanglingDataSet.scd", 1}, {"DuplicateDataSet.scd", 3}};
        for (const auto& fixture : fixtures)
        {
            service->Open(QStringLiteral(SCL_FIXTURE_DIR "/") + QString::fromLatin1(fixture.first));
            QTRY_COMPARE(service->state(), Service::State::Ready);
            const auto next = service->snapshot();
            const auto nextGraph = BuildSclTopology(*next, next->analysis.reports.front().source);
            const auto nextView = nextGraph.value("views").toObject().value(QString::number(nextGraph.value("focus").toInt())).toArray();
            QVERIFY(!nextView.isEmpty());
            const auto relation = edges(nextView.at(0).toObject());
            QCOMPARE(relation.at(0).toObject().value("kind").toString(), QStringLiteral("status"));
            QCOMPARE(relation.size(), fixture.second);
        }
        QFile stillOpen(pagePath);
        QVERIFY(stillOpen.open(QIODevice::ReadOnly));
        QCOMPARE(stillOpen.readAll(), pageBytes);
    }
    QVERIFY(!QFile::exists(pagePath));
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
    QCOMPARE(results->model()->index(0, results->model()->columnCount() - 1).data().toString(),
             QStringLiteral("合成遥信"));
    const auto hit = results->model()->index(0, 0);
    const auto node = hit.data(Qt::UserRole).toInt();
    QVERIFY(QMetaObject::invokeMethod(results, "activated", Q_ARG(QModelIndex, hit)));
    QCOMPARE(tree->currentIndex().data(Qt::UserRole).toInt(), node);
    QVERIFY(tabs->currentWidget() != results);
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
    query->setText("TEST_EVENTS");
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(results->model()->rowCount() > 0);
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
    reports->setCurrentIndex(reports->model()->index(0, 0));
    const auto firstReport = tree->currentIndex();
    reports->setCurrentIndex(reports->model()->index(1, 0));
    const auto secondReport = tree->currentIndex();
    QVERIFY(firstReport != secondReport);
    auto* clearFilter = window.findChild<QPushButton*>("SclClearNavigationFilter");
    QVERIFY(clearFilter);
    ied->setText("no-such-ied");
    QTRY_COMPARE(reports->model()->rowCount(), 0);
    tree->setCurrentIndex(firstReport);
    QCOMPARE(tabs->currentWidget(), reports);
    QVERIFY(!reports->currentIndex().isValid());
    QVERIFY(clearFilter->isVisible());
    back->click();
    QCOMPARE(tree->currentIndex(), secondReport);
    QVERIFY(!reports->currentIndex().isValid());
    forward->click();
    QCOMPARE(tree->currentIndex(), firstReport);
    clearFilter->click();
    QCOMPARE(reports->model()->rowCount(), 2);
    QCOMPARE(reports->currentIndex().row(), 0);
    QVERIFY(!clearFilter->isVisible());
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
