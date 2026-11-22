#pragma once
#include <QMenu>
#include <QInputDialog>
#include <QRegularExpression>
#include <QSplitter>

namespace SclMmsNavigationTest {
using namespace iec61850sim;
using namespace SclNavigationProbe;
inline bool Ready(SclMmsView* view, SclMmsQueryKind kind, bool empty = false) {
    return Await([&] { return view->Result() && view->Result()->request.kind == kind && view->Table()->isEnabled()
        && (empty || (view->Selected() && view->Fields()->isEnabled() && view->Fields()->model()->rowCount() > 0)); });
}
inline void Size(QWidget& window) {
    const auto w = qEnvironmentVariableIntValue("IEC61850_MMS_WIDTH"), h = qEnvironmentVariableIntValue("IEC61850_MMS_HEIGHT");
    window.resize(w > 0 ? w : 1280, h > 0 ? h : 800);
}
inline QByteArray Fixture() {
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("Task7 fixture unavailable");
    return file.readAll();
}
inline bool At(SclMmsView* view, const SclMmsView::Location& expected) {
    const auto actual = view->CurrentLocation();
    return actual.request.kind == expected.request.kind && actual.request.scope == expected.request.scope
        && actual.request.report == expected.request.report && actual.request.target == expected.request.target
        && actual.request.offset == expected.request.offset && actual.request.text == expected.request.text
        && Same(actual.selected, expected.selected);
}
}

void MainWindowBaselineTest::SclMmsNavigation()
{
    using namespace SclMmsNavigationTest;
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    auto bytes = Fixture(); bytes.replace("ReportOtherScope", "ReportA");
    bytes.replace("name=\"ReportB\"", "name=\"ReportB&lt;a href='forged'&gt;\"");
    const auto path = temp.filePath("Navigation.scd"); QVERIFY(WriteFile(path, bytes));
    MainWindow window; Size(window); window.show();
    auto* service = window.findChild<SclAnalysisService*>(); auto* pages = window.findChild<SclTaskPages*>();
    auto* view = window.findChild<SclMmsView*>();
    auto* open = window.findChild<QPushButton*>("SclMmsOpen"); auto* up = window.findChild<QPushButton*>("SclMmsUp");
    auto* back = window.findChild<QPushButton*>("SclMmsBack"); auto* forward = window.findChild<QPushButton*>("SclMmsForward");
    auto* globalBack = window.findChild<QPushButton*>("SclBack");
    auto* title = window.findChild<QLabel*>("SclMmsBreadcrumb"); auto* menu = window.findChild<QToolButton*>("SclMmsPathMenu");
    QVERIFY(service && pages && view && open && up && back && forward && title && menu);
    service->Open(path); QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    auto snapshot = service->snapshot(); const auto reportA = snapshot->mms.reports[0].node, reportB = snapshot->mms.reports[1].node;
    const auto otherReport = snapshot->mms.reports[2].node;
    pages->SetPage(SclTaskPages::Mms); view->Open(SclMmsQueryKind::Reports); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QCOMPARE(view->Result()->rows.size(), size_t(3)); QVERIFY(!up->isEnabled());
    QCOMPARE(back->isEnabled(), globalBack->isEnabled());
    QSignalSpy navigated(view, &SclMmsView::Navigated);
    view->Table()->setCurrentIndex(view->Table()->model()->index(1, 0));
    QVERIFY(Await([&] { return view->Selected() && view->Selected()->node == reportB && view->Fields()->model()->rowCount() > 0; }));
    QCOMPARE(navigated.count(), 0); QVERIFY(title->text().contains("&lt;a")); QVERIFY(!title->text().contains("<a href='forged'"));
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QCOMPARE(view->CurrentLocation().request.report, reportB); QCOMPARE(navigated.count(), 1);
    view->Table()->setCurrentIndex(view->Table()->model()->index(1, 0));
    QVERIFY(Await([&] { return view->Selected() && view->Selected()->ordinal == 2; }));
    QTest::qWait(20); auto members = view->CurrentLocation();
    QCOMPARE(navigated.count(), 1); QCOMPARE(open->text(), QStringLiteral("查看信号对象"));
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
    QCOMPARE(open->text(), QStringLiteral("查看属性与地址"));
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Addresses));
    if (!qgetenv("IEC61850_TASK7_ARTIFACTS").isEmpty()) QVERIFY(Screenshot(window, "Navigation-BeforeVisibility"));
    QVERIFY(Await([&] { return Visible(view->Table()) && Visible(view->Fields()); }));
    QVERIFY(up->isVisible()); QVERIFY(window.rect().contains(up->mapTo(&window, up->rect().center())));
    qInfo() << "T71_NATIVE geometry/available/DPR" << window.size() << window.screen()->availableGeometry().size() << window.devicePixelRatioF();
    const auto oldLink = QRegularExpression("href=\"([^\"]+)\"").match(title->text()).captured(1); QVERIFY(!oldLink.isEmpty());
    auto* screenshotDir = qgetenv("IEC61850_TASK7_ARTIFACTS").isEmpty() ? nullptr : &window;
    if (screenshotDir) QVERIFY(Screenshot(window, "Navigation-Addresses"));
    auto* sourceButton = window.findChild<QPushButton*>("SclMmsSource"); auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    QTest::mouseClick(sourceButton, Qt::LeftButton);
    if (screenshotDir) QVERIFY(Screenshot(window, "Navigation-Source"));
    const bool sourceVisible = Await([&] { return source->isVisible() && VisibleRect(source->viewport()).intersected(source->cursorRect()).height() >= source->fontMetrics().height(); });
    if (!sourceVisible) for (auto* widget = source->viewport(); widget; widget = widget->parentWidget())
        qInfo() << "T71_SOURCE_CLIP" << widget->metaObject()->className() << widget->objectName() << widget->geometry() << widget->minimumSizeHint();
    QVERIFY(sourceVisible);
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Addresses));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QVERIFY(Await([&] { return At(view, members); }));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QCOMPARE(view->Selected()->node, reportB);
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QCOMPARE(view->Selected()->node, members.selected->node);
    QTest::mouseClick(forward, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QCOMPARE(back->isEnabled(), globalBack->isEnabled());
    // The single workspace shortcut works from either the table or the page toolbar.
    window.activateWindow();
    if (QGuiApplication::platformName() == QStringLiteral("windows")) QVERIFY(QTest::qWaitForWindowActive(&window));
    view->Table()->setFocus(); QTRY_VERIFY(view->Table()->hasFocus());
    QTest::keyClick(view->Table(), Qt::Key_Left, Qt::AltModifier);
    QVERIFY(Ready(view, SclMmsQueryKind::Members));
    view->Open(SclMmsQueryKind::Endpoints); QVERIFY(Ready(view, SclMmsQueryKind::Endpoints, true));
    QVERIFY(!forward->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(title, "linkActivated", Q_ARG(QString, oldLink)));
    QCOMPARE(view->CurrentLocation().request.kind, SclMmsQueryKind::Endpoints);
    // Same dataset, different report origins must remain distinct.
    for (const auto report : {reportA, reportB}) {
        view->OpenNode(report); QVERIFY(Ready(view, SclMmsQueryKind::Reports)); QCOMPARE(view->Selected()->node, report);
        QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
        QCOMPARE(view->CurrentLocation().request.report, report);
        QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
        QVERIFY(!menu->menu()->actions().isEmpty()); menu->menu()->actions().front()->trigger();
        QVERIFY(Ready(view, SclMmsQueryKind::Reports)); QCOMPARE(view->Selected()->node, report);
    }
    view->OpenNode(otherReport); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QCOMPARE(view->Selected()->node, otherReport); QCOMPARE(view->Table()->model()->rowCount(), 3); // Same name, different LD.
    // Search has its own return origin while Up follows the business parent.
    auto* scope = window.findChild<QComboBox*>("SclTaskIed"); const auto sink = snapshot->browse.ieds.back();
    const auto setScope = [&](int ied) { const int index = scope->findData(ied); scope->setCurrentIndex(index);
        return QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, index)); };
    QVERIFY(setScope(sink)); auto* search = window.findChild<QLineEdit*>("SclMmsGlobalSearch");
    auto* compactSearch = window.findChild<QToolButton*>("SclMmsCompactSearch");
    if (compactSearch->isVisible()) {
        QTimer::singleShot(0, &window, [] { if (auto* dialog = qobject_cast<QInputDialog*>(QApplication::activeModalWidget()))
            { dialog->setTextValue("SyntheticA"); dialog->accept(); } });
        QTest::mouseClick(compactSearch, Qt::LeftButton);
    } else { search->setText("SyntheticA"); QTest::keyClick(search, Qt::Key_Return); }
    QVERIFY(Ready(view, SclMmsQueryKind::Search));
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QCOMPARE(view->CurrentLocation().request.report, reportA); QVERIFY(scope->currentData().toInt() != sink);
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Reports)); QCOMPARE(view->Selected()->node, reportA);
    auto* searchReturn = window.findChild<QPushButton*>("SclMmsSearchReturn"); QVERIFY(searchReturn->isVisible());
    QTest::mouseClick(searchReturn, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Search)); QCOMPARE(scope->currentData().toInt(), sink);
    view->Open(SclMmsQueryKind::ReverseClients); QVERIFY(Ready(view, SclMmsQueryKind::ReverseClients));
    const auto client = view->Selected(); QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::ReverseClients));
    QVERIFY(Same(view->Selected(), client)); QCOMPARE(scope->currentData().toInt(), sink);
    // Direct entry uses the member's real dataset and never selects its first report user.
    const auto ds = snapshot->mms.reports[0].dataSet.target();
    const auto member = snapshot->browse.memberRows[snapshot->browse.dataSetMembers.value(ds).first + 1];
    view->OpenNode(member); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QCOMPARE(view->CurrentLocation().request.report, Scl::NoNode); QCOMPARE(view->CurrentLocation().request.target, ds);
    QCOMPARE(view->Selected()->node, member); QVERIFY(!up->isEnabled());
    view->Table()->setCurrentIndex(view->Table()->model()->index(3, 0)); QTest::qWait(20);
    QCOMPARE(open->text(), QStringLiteral("查看候选与依据")); const int before = navigated.count();
    QTest::mouseClick(open, Qt::LeftButton); QCOMPARE(navigated.count(), before);
    auto* switchView = window.findChild<QPushButton*>("SclTaskViewSwitch");
    auto* sets = window.findChild<QTreeView*>("SclDataSets"); auto* tabs = window.findChild<QTabWidget*>("SclTables");
    QTest::mouseClick(switchView, Qt::LeftButton); tabs->setCurrentWidget(sets->parentWidget());
    QTRY_VERIFY(sets->model()->index(0, 0).isValid());
    sets->setCurrentIndex(sets->model()->index(0, 0));
    QTRY_COMPARE(sets->currentIndex().data(Qt::UserRole).toInt(), ds);
    QTest::mouseClick(switchView, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QCOMPARE(view->CurrentLocation().request.target, ds); QCOMPARE(view->CurrentLocation().request.report, Scl::NoNode);
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Await([&] { return sets->isVisible(); }));
    QTRY_COMPARE(sets->currentIndex().data(Qt::UserRole).toInt(), ds);
    QTest::mouseClick(switchView, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    auto* docks = window.findChild<ads::CDockManager*>("SclDockManager"); auto* details = docks->findDockWidget("SclInspectorDock");
    view->OpenNode(reportA); QVERIFY(Ready(view, SclMmsQueryKind::Reports)); details->toggleView(false);
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Reports)); QVERIFY(details->isClosed()); details->toggleView(true);
    // A user's horizontal arrangement must not be resized by the short-window helper.
    auto* splitter = qobject_cast<QSplitter*>(details->dockAreaWidget()->parentWidget()); QVERIFY(splitter);
    splitter->setOrientation(Qt::Horizontal); window.resize(window.width(), 530); QTest::qWait(30);
    const auto customSizes = splitter->sizes();
    QVERIFY(QMetaObject::invokeMethod(view, "GeometryChanged")); QTest::qWait(30);
    QCOMPARE(splitter->sizes(), customSizes); splitter->setOrientation(Qt::Vertical);
    // Hide/scope/close discard delayed list/detail/anchor callbacks and old snapshot ancestry.
    view->OpenNode(member); pages->SetPage(SclTaskPages::Overview); service->Close();
    std::weak_ptr<const SclSnapshot> weak = snapshot; snapshot.reset(); members = {};
    QTRY_VERIFY_WITH_TIMEOUT(weak.expired(), 10000);
    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd")); QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    pages->SetPage(SclTaskPages::Mms); view->Open(SclMmsQueryKind::Reports); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QVERIFY(!up->isEnabled()); QVERIFY(!view->CurrentLocation().parent);
}

void MainWindowBaselineTest::SclMmsNavigationPaging()
{
    using namespace SclMmsNavigationTest;
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    auto bytes = Fixture(); const int begin = bytes.indexOf("<FCDA "), end = bytes.indexOf("/>", begin) + 2;
    const auto member = bytes.mid(begin, end - begin); bytes.insert(end, member.repeated(249));
    const auto path = temp.filePath("Paging.scd"); QVERIFY(WriteFile(path, bytes));
    MainWindow window; Size(window); window.show();
    auto* service = window.findChild<SclAnalysisService*>(); auto* pages = window.findChild<SclTaskPages*>(); auto* view = window.findChild<SclMmsView*>();
    auto* open = window.findChild<QPushButton*>("SclMmsOpen"); auto* up = window.findChild<QPushButton*>("SclMmsUp");
    auto* next = window.findChild<QPushButton*>("SclMmsNext"); auto* filter = window.findChild<QLineEdit*>("SclMmsFilter");
    auto* back = window.findChild<QPushButton*>("SclMmsBack");
    service->Open(path); QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    pages->SetPage(SclTaskPages::Mms); view->Open(SclMmsQueryKind::Reports); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QSignalSpy navigated(view, &SclMmsView::Navigated);
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members)); QCOMPARE(view->Result()->total, 253);
    QTest::mouseClick(next, Qt::LeftButton);
    QVERIFY(Await([&] { return view->Result() && view->Result()->request.offset == 200 && view->Table()->isEnabled(); }));
    QCOMPARE(view->Table()->model()->rowCount(), 53); QCOMPARE(navigated.count(), 1);
    auto* table = view->Table(); table->setCurrentIndex(table->model()->index(15, 0)); QTest::qWait(20);
    table->scrollTo(table->currentIndex(), QAbstractItemView::PositionAtTop); table->verticalScrollBar()->setValue(table->verticalScrollBar()->value() + 7);
    auto* fields = view->Fields(); fields->setCurrentIndex(fields->model()->index(3, 1)); fields->scrollTo(fields->currentIndex(), QAbstractItemView::PositionAtTop);
    fields->verticalScrollBar()->setValue(fields->verticalScrollBar()->value() + 2); QTest::qWait(30);
    const auto location = view->CurrentLocation(); QVERIFY(location.top); QVERIFY(location.topOffset > 0); QVERIFY(Visible(table)); QVERIFY(Visible(fields));
    QCOMPARE(navigated.count(), 1);
    QTest::mouseClick(open, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    QVERIFY(Await([&] { return At(view, location); })); QTest::qWait(30);
    const auto restored = view->CurrentLocation();
    QVERIFY(Same(restored.top, location.top)); QCOMPARE(restored.topOffset, location.topOffset);
    QCOMPARE(restored.field, location.field); QCOMPARE(restored.topField, location.topField); QCOMPARE(restored.fieldOffset, location.fieldOffset);
    QVERIFY(Visible(table)); QVERIFY(Visible(fields));
    if (!qgetenv("IEC61850_TASK7_ARTIFACTS").isEmpty()) QVERIFY(Screenshot(window, "Navigation-PagingRestored"));
    filter->setText("NO_MATCH_T7"); QVERIFY(Await([&] { return table->isEnabled() && table->model()->rowCount() == 0; }));
    QVERIFY(!table->currentIndex().isValid()); QVERIFY(Same(view->Selected(), location.selected));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Reports));
    QTest::mouseClick(back, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Members, true));
    QCOMPARE(filter->text(), QStringLiteral("NO_MATCH_T7")); QVERIFY(!table->currentIndex().isValid());
    filter->clear(); QVERIFY(Ready(view, SclMmsQueryKind::Members));
    // Direct attribute-page entry constructs only its uniquely identified signal parent.
    SclMmsView::Location direct; direct.snapshot = service->snapshot(); direct.request.kind = SclMmsQueryKind::Addresses;
    direct.request.signal = 0; direct.request.scope.ied = direct.snapshot->browse.ieds.front(); direct.viewIed = direct.request.scope.ied;
    view->RestoreLocation(direct); QVERIFY(Ready(view, SclMmsQueryKind::Addresses));
    QTest::mouseClick(up, Qt::LeftButton); QVERIFY(Ready(view, SclMmsQueryKind::Signals));
    QCOMPARE(view->Selected()->record, 0); QCOMPARE(view->CurrentLocation().request.report, Scl::NoNode); QVERIFY(!up->isEnabled());
}
