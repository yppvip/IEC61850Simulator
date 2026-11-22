#pragma once

#include <QGraphicsView>
#include <QGraphicsScene>

void MainWindowBaselineTest::SclTask9TraceWorkflow_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("normal") << QSize(1280, 800);
    QTest::newRow("narrow") << QSize(620, 560);
}

void MainWindowBaselineTest::SclTask9TraceWorkflow()
{
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask9TraceWorkflow")))
        QSKIP("Run as the separate reference trace test");
    using namespace iec61850sim;
    using namespace Task8Acceptance;
    QFETCH(QSize, size);
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    IsolateSettings(settingsDir.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    const auto oldClipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([oldClipboard] { QApplication::clipboard()->setText(oldClipboard); });
    const auto traceAction = QStringLiteral("查看关系");

    MainWindow window;
    window.resize(size);
    window.show();
    auto* service = window.findChild<SclAnalysisService*>();
    auto* mms = window.findChild<SclMmsView*>();
    auto* tasks = window.findChild<QTreeWidget*>("SclTaskTree");
    auto* scope = window.findChild<QComboBox*>("SclTaskIed");
    auto* filter = window.findChild<QLineEdit*>("SclMmsFilter");
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* structure = window.findChild<QTreeView*>("SclTree");
    auto* taskSwitch = window.findChild<QPushButton*>("SclTaskViewSwitch");
    auto* trace = window.findChild<QWidget*>("SclReferenceTrace");
    auto* tree = trace->findChild<QTreeWidget*>("SclTraceTree");
    auto* graph = trace->findChild<QGraphicsView*>("SclTraceGraph");
    auto* title = trace->findChild<QLabel*>("SclTraceTitle");
    auto* chooser = trace->findChild<QComboBox*>("SclTraceChoices");
    auto* expand = trace->findChild<QPushButton*>("SclTraceExpand");
    auto* mode = trace->findChild<QPushButton*>("SclTraceSwitch");
    auto* openSource = trace->findChild<QPushButton*>("SclTraceSource");
    auto* back = trace->findChild<QPushButton*>("SclTraceBack");
    auto* forward = window.findChild<QPushButton*>("SclForward");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* detailsToggle = window.findChild<QToolButton*>("SclToggleDetails");
    auto* detail = trace->findChild<QPlainTextEdit*>("SclTraceDetail");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    QVERIFY(service && mms && tasks && scope && filter && sets && structure && taskSwitch && tree && graph
        && title && chooser && expand && mode && openSource && back && forward && inspector && detailsToggle
        && detail && source && sourceLabel);

    service->Open(QStringLiteral(SCL_FIXTURE_DIR "/Task8Voltage.scd"));
    QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    auto snapshot = service->snapshot();
    const auto listed = snapshot->browse.ieds.front();
    const auto route = tasks->findItems(QStringLiteral("查找属性"), Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(route.size(), 1);
    scope->setCurrentIndex(scope->findData(listed));
    QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, scope->currentIndex())));
    tasks->setCurrentItem(route.front());
    filter->setText(QStringLiteral("A 相电压"));
    QTRY_VERIFY(mms->Result() && mms->Result()->request.text == filter->text() && mms->Result()->rows.size() == 2);
    if (size.width() >= 1000) {
        if (!detailsToggle->isChecked()) detailsToggle->click();
        inspector->setCurrentWidget(mms->Inspector());
    }
    auto* table = mms->Table();
    auto first = table->model()->index(0, 0);
    auto second = table->model()->index(1, 0);
    table->setCurrentIndex(first);
    table->scrollTo(second);
    const auto selected = mms->Selected();
    const auto expectedItem = mms->Result()->rows[1].item;
    const auto visibleRow = [](QAbstractItemView* view, const QModelIndex& index) {
        return SclNavigationProbe::VisibleRect(view->viewport()).intersects(view->visualRect(index));
    };
    QTRY_VERIFY(visibleRow(table, second));
    const auto initialSource = source->textCursor().position();
    const auto copy = ChooseMenu(table, second, QStringLiteral("MMS Item"), Gesture::Mouse);
    QVERIFY(copy.expanded && copy.dispatched);
    QCOMPARE(QApplication::clipboard()->text(), expectedItem);
    QCOMPARE(mms->Selected(), selected);
    QCOMPARE(source->textCursor().position(), initialSource);
    QVERIFY(!trace->isVisible());
    const auto open = ChooseMenu(table, second, QStringLiteral("MMS Item"), Gesture::Mouse, {}, traceAction);
    QVERIFY(open.expanded && open.dispatched);
    QTRY_VERIFY(trace->isVisible() && tree->topLevelItemCount() == 1);
    QCOMPARE(tree->topLevelItem(0)->text(1), expectedItem);
    QCOMPARE(QApplication::clipboard()->text(), expectedItem);
    QCOMPARE(mms->Selected(), selected);
    if (size.width() >= 1000) QVERIFY(!mode->isChecked());
    if (mode->isChecked()) mode->click();
    QTRY_VERIFY(graph->isVisible() && !graph->scene()->items().isEmpty());
    mode->click();
    QTRY_VERIFY(tree->isVisible());
    auto* fc = tree->topLevelItem(0)->child(1);
    QCOMPARE(fc->text(1), QStringLiteral("MX"));
    tree->setCurrentItem(fc);
    QTest::keyClick(tree, Qt::Key_Right);
    QTRY_VERIFY(tree->currentItem() && tree->currentItem()->childCount() >= 1);
    fc = tree->currentItem();
    tree->setCurrentItem(fc->child(0));
    QVERIFY(detail->toPlainText().contains(QStringLiteral("标签起始行 60")));
    QVERIFY(openSource->isEnabled() && openSource->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(visibleRow(tree, tree->currentIndex()), 1500);
    openSource->click();
    QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 60 行（")));
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(source->viewport()).intersects(source->cursorRect()));
    back->click();
    QTRY_VERIFY(table->isVisible() && table->isEnabled() && mms->Selected() == selected);
    QCOMPARE(scope->currentData().toInt(), listed);
    QCOMPARE(filter->text(), QStringLiteral("A 相电压"));
    QVERIFY(forward->isEnabled());
    forward->click();
    QTRY_VERIFY(trace->isVisible() && tree->currentItem()
        && tree->currentItem()->text(0) == QStringLiteral("DA 定义的有效 FC"));
    QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 60 行（")));
    back->click();
    QTRY_VERIFY(table->isVisible() && table->isEnabled() && mms->Selected() == selected);

    if (size.width() >= 1000) {
        inspector->setCurrentWidget(mms->Inspector());
        auto* fields = mms->Fields();
        const auto itemField = [&] {
            for (int row = 0; row < fields->model()->rowCount(); ++row)
                if (fields->model()->index(row, 0).data().toString() == "MMS Item")
                    return fields->model()->index(row, 1);
            return QModelIndex{};
        };
        const auto expected = mms->Result()->rows.front().item;
        QTRY_VERIFY(fields->isEnabled() && itemField().isValid() && itemField().data().toString() == expected);
        fields->setCurrentIndex(itemField());
        fields->scrollTo(itemField());
        QTRY_VERIFY(visibleRow(fields, itemField()));
        const auto fieldMenu = ChooseMenu(fields, itemField(), QStringLiteral("MMS Item"), Gesture::Mouse, {}, traceAction);
        QVERIFY2(fieldMenu.expanded && fieldMenu.dispatched, qPrintable(fieldMenu.heads.join(QStringLiteral(" | "))));
        QTRY_VERIFY(trace->isVisible() && tree->topLevelItemCount() == 1);
        QCOMPARE(tree->topLevelItem(0)->text(1), expected);
        back->click();
        QTRY_VERIFY(table->isVisible() && table->isEnabled() && fields->isVisible());
    }

    first = table->model()->index(0, 0);
    table->setCurrentIndex(first);
    QVERIFY(ChooseMenu(table, first, QStringLiteral("类型引用"), Gesture::Keyboard, {}, traceAction).dispatched);
    QTRY_VERIFY(chooser->isVisible() && chooser->count() > 2);
    QCOMPARE(chooser->currentIndex(), 0);
    chooser->setCurrentIndex(chooser->count() - 1);
    QTRY_VERIFY(tree->topLevelItemCount() == 1);
    QVERIFY(tree->topLevelItem(0)->childCount() >= 2);
    back->click();
    QTRY_VERIFY(table->isEnabled() && table->isVisible());
    second = table->model()->index(1, 0);
    table->scrollTo(second);
    QVERIFY(ChooseMenu(table, second, QStringLiteral("MMS Item"), Gesture::Mouse,
        [&] { filter->setText(QStringLiteral("相位")); }, traceAction).dispatched);
    QVERIFY(!trace->isVisible());
    QTRY_VERIFY(mms->Result() && mms->Result()->request.text == filter->text());

    taskSwitch->click();
    const auto ds = snapshot->mms.reports.front().dataSet.target();
    QVERIFY(ds != NoNode);
    structure->setCurrentIndex(Task7Acceptance::StructureIndex(structure, *snapshot, ds));
    QTRY_VERIFY(sets->isVisible() && sets->currentIndex().isValid()
        && sets->currentIndex().data(Qt::UserRole).toInt() == ds);
    const auto dataSet = sets->currentIndex().siblingAtColumn(0);
    sets->expand(dataSet);
    QTRY_COMPARE(sets->model()->rowCount(dataSet), 2);
    const auto member = sets->model()->index(1, 0, dataSet);
    sets->scrollTo(member);
    QTRY_VERIFY(visibleRow(sets, member));
    const auto original = sets->currentIndex().data(Qt::UserRole).toInt();
    QVERIFY(ChooseMenu(sets, member, QStringLiteral("对象引用"), Gesture::Mouse, {}, traceAction).dispatched);
    QTRY_VERIFY(trace->isVisible() && tree->topLevelItemCount() == 1);
    QCOMPARE(tree->topLevelItem(0)->text(1), QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA"));
    QVERIFY(tree->topLevelItem(0)->childCount() >= 5);
    back->click();
    QTRY_VERIFY(sets->isVisible() && sets->currentIndex().data(Qt::UserRole).toInt() == original);

    sets->scrollTo(member);
    QVERIFY(ChooseMenu(sets, member, QStringLiteral("对象引用"), Gesture::Mouse, {}, traceAction).dispatched);
    QTRY_VERIFY(trace->isVisible());
    std::weak_ptr<const SclSnapshot> previous = snapshot;
    snapshot.reset();
    service->Close();
    QTRY_VERIFY(!trace->isVisible() && previous.expired());
}
