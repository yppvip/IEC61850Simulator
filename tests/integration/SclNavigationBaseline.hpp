#pragma once

// T7.0 test-only probes. No production navigation or settings are changed.
#include "SclReader.hpp"
#include <QStackedWidget>
#include <functional>

namespace SclNavigationProbe {
using namespace iec61850sim;

inline bool Await(const std::function<bool()>& ready, int timeout = 15000)
{
    QElapsedTimer timer; timer.start();
    while (!ready() && timer.elapsed() < timeout) {
        QApplication::processEvents(QEventLoop::AllEvents, 5);
        QTest::qWait(1);
    }
    QApplication::processEvents();
    return ready();
}

inline QJsonObject Key(const std::optional<SclMmsKey>& key)
{
    if (!key) return {};
    return {{"kind", int(key->kind)}, {"node", key->node}, {"record", key->record},
        {"ordinal", key->ordinal}, {"field", key->field}};
}

inline bool Same(const std::optional<SclMmsKey>& a, const std::optional<SclMmsKey>& b)
{ return Key(a) == Key(b); }

inline QRect VisibleRect(QWidget* viewport)
{
    if (!viewport->isVisible()) return {};
    auto visible = viewport->rect();
    for (auto* parent = viewport->parentWidget(); parent; parent = parent->parentWidget())
        visible = visible.intersected(QRect(viewport->mapFrom(parent, QPoint(0, 0)), parent->size()));
    return visible;
}
inline bool Visible(QTableView* table)
{
    if (!table->isVisible() || !table->currentIndex().isValid()) return false;
    const auto cell = table->visualRect(table->currentIndex());
    return VisibleRect(table->viewport()).intersected(cell).height() >= std::min(cell.height(), table->fontMetrics().height() + 2);
}

inline bool Settled(SclMmsView* view, SclMmsQueryKind kind, bool allowEmpty = false)
{
    const auto result = view->Result();
    if (!result || result->request.kind != kind || !view->Table()->isEnabled()) return false;
    if (result->rows.empty()) return allowEmpty;
    const auto* details = view->findChild<SclMmsQuerySession*>("SclMmsDetailsSession");
    return view->Selected() && details && details->result()
        && Same(view->Selected(), details->result()->request.key)
        && view->Fields()->model()->rowCount() > 0 && Visible(view->Table()) && Visible(view->Fields());
}

inline QJsonObject Location(SclMmsView* view)
{
    const auto l = view->CurrentLocation();
    return {{"kind", int(l.request.kind)}, {"ied", l.request.scope.ied},
        {"ap", l.request.scope.accessPoint}, {"server", l.request.scope.server},
        {"report", l.request.report}, {"target", l.request.target}, {"signal", l.request.signal},
        {"offset", l.request.offset}, {"filter", l.request.text}, {"fc", l.request.fc},
        {"selected", Key(l.selected)}, {"top", Key(l.top)}, {"topOffset", l.topOffset},
        {"field", l.field}, {"topField", l.topField}, {"fieldOffset", l.fieldOffset},
        {"rowVisible", Visible(view->Table())}, {"fieldVisible", Visible(view->Fields())}};
}

inline QJsonObject Restored(const SclMmsView::Location& expected, SclMmsView* view)
{
    const auto actual = view->CurrentLocation();
    return {{"request", expected.request.kind == actual.request.kind
        && expected.request.scope == actual.request.scope && expected.request.report == actual.request.report
        && expected.request.target == actual.request.target && expected.request.signal == actual.request.signal
        && expected.request.text == actual.request.text && expected.request.offset == actual.request.offset},
        {"selected", Same(expected.selected, actual.selected)},
        {"top", Same(expected.top, actual.top) && expected.topOffset == actual.topOffset},
        {"field", expected.field == actual.field},
        {"fieldTop", expected.topField == actual.topField && expected.fieldOffset == actual.fieldOffset},
        {"rowVisible", Visible(view->Table())}, {"fieldVisible", Visible(view->Fields())}};
}

inline QJsonObject Stats(QList<qint64> values)
{
    if (values.isEmpty()) return {{"count", 0}};
    const auto original = values;
    std::sort(values.begin(), values.end());
    QJsonArray raw;
    for (const auto value : original) raw.append(value);
    return {{"count", values.size()}, {"unit", "us"}, {"raw", raw},
        {"p50", values[(values.size() + 1) / 2 - 1]},
        {"p95", values[(values.size() * 95 + 99) / 100 - 1]}, {"max", values.back()}};
}

inline QString Input()
{
    const auto local = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    return local.isEmpty() ? QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd") : local;
}

inline bool Save(const QString& name, const QJsonObject& data)
{
    const auto dir = qEnvironmentVariable("IEC61850_TASK7_ARTIFACTS");
    if (dir.isEmpty()) return true;
    if (!QDir().mkpath(dir)) return false;
    QFile file(QDir(dir).filePath(name));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const auto bytes = QJsonDocument(data).toJson();
    return file.write(bytes) == bytes.size();
}

inline bool Screenshot(QWidget& window, const QString& name)
{
    const auto dir = qEnvironmentVariable("IEC61850_TASK7_ARTIFACTS");
    if (dir.isEmpty()) return true;
    QDir().mkpath(dir); QApplication::processEvents();
    return window.grab().save(QDir(dir).filePath(name + ".png"));
}
}

void MainWindowBaselineTest::SclNavigationFixture()
{
    using namespace iec61850sim;
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    SclAnalysisService service;
    service.Open(QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd"));
    QTRY_COMPARE(service.state(), SclAnalysisService::State::Ready);
    const auto snapshot = service.snapshot(); QVERIFY(snapshot);
    QCOMPARE(snapshot->analysis.document->standardCounts.value("IED"), 2);
    QCOMPARE(snapshot->analysis.document->standardCounts.value("DataSet"), 2);
    QCOMPARE(snapshot->analysis.document->standardCounts.value("FCDA"), 5);
    QCOMPARE(snapshot->mms.reports.size(), size_t(3));
    const auto& a = snapshot->mms.reports[0]; const auto& b = snapshot->mms.reports[1];
    const auto& other = snapshot->mms.reports[2];
    QVERIFY(a.dataSet.target() >= 0); QCOMPARE(a.dataSet.target(), b.dataSet.target());
    QVERIFY(a.dataSet.target() != other.dataSet.target());
    QCOMPARE(a.memberCount, 4); QCOMPARE(other.memberCount, 1);
    QCOMPARE(snapshot->mmsIndex.reportsByDataSet.value(a.dataSet.target()).size(), size_t(2));
    int repeated = 0, unresolved = 0;
    QSet<Scl::NodeId> occurrences;
    for (const auto& ref : snapshot->analysis.references) {
        if (ref.kind != Scl::LinkKind::Fcda || snapshot->analysis.document->nodes[ref.source].parent != a.dataSet.target()) continue;
        occurrences.insert(ref.source);
        if (ref.state == Scl::LinkState::Resolved) ++repeated; else ++unresolved;
    }
    QCOMPARE(occurrences.size(), 4); QCOMPARE(repeated, 3); QCOMPARE(unresolved, 1);
}

void MainWindowBaselineTest::SclNavigationBaseline()
{
    using namespace SclNavigationProbe;
    if (!QCoreApplication::arguments().contains("SclNavigationBaseline"))
        QSKIP("Explicit T7.0 native/local baseline only");
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    QFile source(Input()); QVERIFY(source.open(QIODevice::ReadOnly));
    const auto hash = QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex(); source.close();
    MainWindow window; window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>(); auto* pages = window.findChild<SclTaskPages*>();
    auto* view = window.findChild<SclMmsView*>(); auto* open = window.findChild<QPushButton*>("SclMmsOpen");
    auto* back = window.findChild<QPushButton*>("SclBack"); auto* forward = window.findChild<QPushButton*>("SclForward");
    auto* sourceButton = window.findChild<QPushButton*>("SclMmsSource");
    auto* sourceText = window.findChild<QPlainTextEdit*>("SclSource");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* filter = window.findChild<QLineEdit*>("SclMmsFilter");
    QVERIFY(service && pages && view && open && back && forward && sourceButton && sourceText && inspector && filter);
    QElapsedTimer load; load.start(); qint64 last = 0, heartbeatMax = 0;
    QTimer heartbeat; heartbeat.setTimerType(Qt::PreciseTimer); heartbeat.setInterval(16);
    connect(&heartbeat, &QTimer::timeout, &window, [&] { const auto now = load.elapsed(); heartbeatMax = std::max(heartbeatMax, now - last); last = now; });
    heartbeat.start(); service->Open(Input());
    QVERIFY(Await([&] { return service->state() != SclAnalysisService::State::Loading; }, 180000));
    QCOMPARE(service->state(), SclAnalysisService::State::Ready);
    const auto readyMs = load.elapsed(); QCOMPARE(service->snapshot()->analysis.sha256, hash);
    pages->SetPage(SclTaskPages::Mms); view->Open(SclMmsQueryKind::Reports);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
    const auto firstReportMs = load.elapsed();
    const auto reports = view->CurrentLocation();
    QVERIFY(Screenshot(window, "01-Reports"));
    QTest::mouseClick(open, Qt::LeftButton);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Members); }));
    const auto firstMemberMs = load.elapsed();
    const auto members = view->CurrentLocation();
    QVERIFY(Screenshot(window, "02-Members"));
    QTest::mouseClick(open, Qt::LeftButton);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Signals); }));
    const auto signalLocation = view->CurrentLocation();
    QVERIFY(Screenshot(window, "03-Signal"));
    QTest::mouseClick(open, Qt::LeftButton);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Addresses); }));
    const auto addresses = view->CurrentLocation();
    QVERIFY(Screenshot(window, "04-Addresses"));
    QVERIFY(sourceButton->isEnabled()); QTest::mouseClick(sourceButton, Qt::LeftButton);
    QVERIFY(Await([&] { return inspector->currentIndex() == 2 && !sourceText->toPlainText().isEmpty()
        && sourceText->viewport()->rect().intersects(sourceText->cursorRect()); }));
    QVERIFY(Screenshot(window, "05-Source"));
    QTest::mouseClick(back, Qt::LeftButton);
    QVERIFY(Await([&] { return inspector->currentWidget() == view->Inspector() && Settled(view, SclMmsQueryKind::Addresses); }));
    QJsonObject restoration{{"sourceBack", Restored(addresses, view)}};
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Signals); }));
    restoration["addressBack"] = Restored(signalLocation, view);
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Members); }));
    restoration["signalBack"] = Restored(members, view);
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
    restoration["memberBack"] = Restored(reports, view);
    QTest::mouseClick(forward, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Members); }));
    restoration["forward"] = Restored(members, view);

    // Observe filter retention separately; the 160 ms debounce is not hidden in navigation timings.
    view->Open(SclMmsQueryKind::Reports); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
    const auto name = service->snapshot()->analysis.document->attribute(view->Selected()->node, "name");
    filter->setText(name);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports) && view->Result()->request.text == name; }));
    const auto filtered = view->CurrentLocation();
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Members); }));
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
    restoration["filteredReportBack"] = Restored(filtered, view);

    // Warm-up once, then record 30 complete cycles. Samples include query delivery and paint opportunity.
    QHash<QString, QList<qint64>> timings;
    QJsonArray route;
    qint64 listApply = 0, detailApply = 0;
    for (int round = -1; round < 30; ++round) {
        view->Open(SclMmsQueryKind::Reports); QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
        const auto step = [&](const QString& label, const std::function<void()>& action, SclMmsQueryKind kind, bool isSource = false) {
            QElapsedTimer timer; timer.start(); action();
            const bool success = Await([&] { return isSource ? inspector->currentIndex() == 2 && !sourceText->toPlainText().isEmpty()
                && sourceText->viewport()->rect().intersects(sourceText->cursorRect())
                : inspector->currentWidget() == view->Inspector() && Settled(view, kind); });
            if (round >= 0) timings[label].append(timer.nsecsElapsed() / 1000);
            listApply = std::max(listApply, view->property("MmsListApplyUs").toLongLong());
            detailApply = std::max(detailApply, view->property("MmsDetailApplyUs").toLongLong());
            if (round == 0) { auto state = Location(view); state["step"] = label; route.append(state); }
            return success;
        };
        QVERIFY(step("reportToMembers", [&] { QTest::mouseClick(open, Qt::LeftButton); }, SclMmsQueryKind::Members));
        QVERIFY(step("memberToSignal", [&] { QTest::mouseClick(open, Qt::LeftButton); }, SclMmsQueryKind::Signals));
        QVERIFY(step("signalToAddresses", [&] { QTest::mouseClick(open, Qt::LeftButton); }, SclMmsQueryKind::Addresses));
        QVERIFY(step("addressToSource", [&] { QTest::mouseClick(sourceButton, Qt::LeftButton); }, SclMmsQueryKind::Addresses, true));
        QVERIFY(step("sourceBack", [&] { QTest::mouseClick(back, Qt::LeftButton); }, SclMmsQueryKind::Addresses));
        QVERIFY(step("addressBack", [&] { QTest::mouseClick(back, Qt::LeftButton); }, SclMmsQueryKind::Signals));
        QVERIFY(step("signalBack", [&] { QTest::mouseClick(back, Qt::LeftButton); }, SclMmsQueryKind::Members));
        QVERIFY(step("memberBack", [&] { QTest::mouseClick(back, Qt::LeftButton); }, SclMmsQueryKind::Reports));
    }
    QJsonObject statistics;
    for (auto i = timings.cbegin(); i != timings.cend(); ++i) statistics[i.key()] = Stats(i.value());
    const auto mainMemory = Memory();
    QJsonObject data{{"schema", 1}, {"sha256", QString::fromLatin1(hash)}, {"qt", qVersion()},
        {"platform", QGuiApplication::platformName()}, {"readyMs", readyMs}, {"firstReportMs", firstReportMs},
        {"firstMemberMs", firstMemberMs}, {"workingSet", mainMemory.workingSet}, {"peakWorkingSet", mainMemory.peakWorkingSet},
        {"heartbeatMaxMs", heartbeatMax}, {"listApplyMaxUs", listApply}, {"detailApplyMaxUs", detailApply},
        {"rowMeasureMaxUs", view->Table()->property("SclMaxMeasureUs").toLongLong()},
        {"width", window.width()}, {"height", window.height()}, {"dpr", window.devicePixelRatioF()},
        {"restore", restoration}, {"timings", statistics}, {"route", route}};
    QVERIFY(Save("MainChain.json", data));
    // Exercise the advanced path separately from the MMS timing samples.
    auto* switchView = window.findChild<QPushButton*>("SclTaskSwitch");
    if (!switchView) for (auto* button : window.findChildren<QPushButton*>())
        if (button->text() == QStringLiteral("高级视图")) switchView = button;
    QVERIFY(switchView); QTest::mouseClick(switchView, Qt::LeftButton);
    auto* sets = window.findChild<QTreeView*>("SclDataSets"); auto* memberTable = window.findChild<QTableView*>("SclMembers");
    auto* tabs = window.findChild<QTabWidget*>("SclTables"); auto* tree = window.findChild<QTreeView*>("SclTree");
    QVERIFY(sets && memberTable && tabs && tree);
    const auto snapshot = service->snapshot();
    const auto& browse = snapshot->browse;
    const auto& dataSets = browse.categories[size_t(SclCategory::DataSets)];
    const auto found = std::find_if(dataSets.begin(), dataSets.end(),
        [&](Scl::NodeId node) { return browse.dataSetMembers.value(node).count > 0; });
    QVERIFY(found != dataSets.end());
    const auto dataSet = *found;
    std::vector<Scl::NodeId> ancestors;
    for (auto node = dataSet; node != Scl::NoNode; node = snapshot->analysis.document->nodes[node].parent)
        ancestors.push_back(node);
    QModelIndex dataSetInStructure;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
        dataSetInStructure = tree->model()->index(*it == 0 ? 0 : browse.branches[*it].row, 0, dataSetInStructure);
    QVERIFY(dataSetInStructure.isValid());
    tree->setCurrentIndex(dataSetInStructure);
    QVERIFY(Await([&] { return sets->isVisible() && sets->currentIndex().isValid()
        && sets->currentIndex().data(Qt::UserRole).toInt() == dataSet; }));
    QVERIFY(Screenshot(window, "06-AdvancedDataSets"));
    tabs->setCurrentWidget(memberTable);
    QVERIFY(Await([&] { return memberTable->isVisible() && memberTable->model()->rowCount() > 0; }));
    const auto firstMember = memberTable->model()->index(0, 0);
    memberTable->setCurrentIndex(firstMember);
    memberTable->scrollTo(firstMember);
    QVERIFY(Await([&] { return Visible(memberTable); }));
    const int memberCount = memberTable->model()->rowCount();
    QVERIFY(Screenshot(window, "07-AdvancedMembers"));
    const auto usage = Memory(); heartbeat.stop();
    data["workingSet"] = usage.workingSet;
    data["peakWorkingSet"] = usage.peakWorkingSet;
    data["heartbeatMaxMs"] = heartbeatMax;
    data["advancedDataSet"] = dataSet;
    data["advancedMemberCount"] = memberCount;
    QVERIFY(Save("NavigationBaseline.json", data));
    qInfo().noquote() << "TASK7_BASELINE" << QJsonDocument(QJsonObject{{"readyMs", readyMs},
        {"peakWorkingSet", usage.peakWorkingSet}, {"heartbeatMaxMs", heartbeatMax}, {"restoration", restoration}}).toJson(QJsonDocument::Compact);
    service->Close(); QVERIFY(Await([&] { return !service->snapshot(); }));
    QVERIFY(source.open(QIODevice::ReadOnly)); QCOMPARE(QCryptographicHash::hash(source.readAll(), QCryptographicHash::Sha256).toHex(), hash);
}

void MainWindowBaselineTest::SclNavigationReview()
{
    using namespace SclNavigationProbe;
    if (!QCoreApplication::arguments().contains("SclNavigationReview")) QSKIP("Explicit native local review only");
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    MainWindow window; window.setWindowTitle(QStringLiteral("Task7 T7.0 导航基线（隔离设置）")); window.resize(1280, 800); window.show();
    auto* service = window.findChild<SclAnalysisService*>(); service->Open(Input());
    QVERIFY(Await([&] { return service->state() != SclAnalysisService::State::Loading; }, 180000));
    QCOMPARE(service->state(), SclAnalysisService::State::Ready);
    auto* pages = window.findChild<SclTaskPages*>(); auto* view = window.findChild<SclMmsView*>();
    pages->SetPage(SclTaskPages::Mms); view->Open(SclMmsQueryKind::Reports);
    QVERIFY(Await([&] { return Settled(view, SclMmsQueryKind::Reports); }));
    int sequence = 0;
    QTimer capture; capture.setSingleShot(true); capture.setInterval(350);
    connect(view, &SclMmsView::Navigated, &capture, qOverload<>(&QTimer::start));
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    connect(inspector, &QTabWidget::currentChanged, &capture, qOverload<>(&QTimer::start));
    connect(window.findChild<QTabWidget*>("SclTables"), &QTabWidget::currentChanged, &capture, qOverload<>(&QTimer::start));
    connect(&capture, &QTimer::timeout, &window, [&] {
        const auto name = QStringLiteral("Review-%1").arg(++sequence, 2, 10, QLatin1Char('0'));
        Screenshot(window, name); Save(name + ".json", Location(view));
    });
    QVERIFY(Screenshot(window, "Review-00"));
    QEventLoop loop; connect(qApp, &QGuiApplication::lastWindowClosed, &loop, &QEventLoop::quit);
    QTimer::singleShot(600000, &loop, &QEventLoop::quit); loop.exec();
}
