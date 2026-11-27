#pragma once

void MainWindowBaselineTest::SclTask10AdvancedWorkflow_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("normal") << QSize(1280,800);
    QTest::newRow("narrow") << QSize(620,560);
}

void MainWindowBaselineTest::SclTask10AdvancedWorkflow()
{
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask10AdvancedWorkflow"))) QSKIP("Separate advanced workflow");
    using namespace iec61850sim; using namespace Scl; using namespace Task8Acceptance;
    QFETCH(QSize,size);
    QTemporaryDir temporary; QVERIFY(temporary.isValid()); IsolateSettings(temporary.path());
    const auto clipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([clipboard] { QApplication::clipboard()->setText(clipboard); });
    const auto external = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    QString path = external;
    if (path.isEmpty()) {
        QFile fixture(QStringLiteral(SCL_FIXTURE_DIR "/Task10Evidence.scd")); QVERIFY(fixture.open(QIODevice::ReadOnly));
        auto xml = fixture.readAll();
        const QByteArray fcda = "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA' daName='cVal.mag.f' fc='MX'/>";
        xml.replace("<ReportControl name=\"ReportA\"", "<DataSet name='PagedDs'>" + fcda.repeated(205) + "</DataSet><ReportControl name=\"ReportA\"");
        path = temporary.filePath("Advanced.scd"); QVERIFY(WriteFile(path,xml));
    }
    const auto digest = FileDigest(path);
    MainWindow window; window.resize(size); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>(); auto* mms = window.findChild<SclMmsView*>();
    auto* objects = window.findChild<QTableView*>("SclObjects"); auto* members = window.findChild<QTableView*>("SclMembers");
    auto* tree = window.findChild<QTreeView*>("SclTree"); auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* tabs = window.findChild<QTabWidget*>("SclTables"); auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* properties = window.findChild<QTextBrowser*>("SclProperties");
    auto* evidence = window.findChild<SclMmsQuerySession*>("SclAdvancedEvidenceSession");
    auto* scope = window.findChild<QComboBox*>("SclBrowseScope");
    auto* mode = window.findChild<QComboBox*>("SclSearchMode"); auto* query = window.findChild<QLineEdit*>("SclQuery");
    auto* status = window.findChild<QLabel*>("SclSearchStatus"); auto* switchView = window.findChild<QPushButton*>("SclTaskViewSwitch");
    auto* back = window.findChild<QPushButton*>("SclBack"); auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    QVERIFY(service && mms && objects && members && tree && sets && tabs && inspector && properties && evidence
        && scope && mode && query && status && switchView && back && sourceLabel);
    service->Open(path); QTRY_COMPARE_WITH_TIMEOUT(service->state(),SclAnalysisService::State::Ready,90000);
    auto snapshot = service->snapshot(); const auto& doc = *snapshot->analysis.document;
    const auto queryWorker = [snapshot](SclMmsRequest r) { return QtConcurrent::run([snapshot,r] { return QuerySclMms(*snapshot,r,nullptr); }); };
    auto ied = snapshot->browse.ieds.front();
    const auto wanted = qEnvironmentVariable("IEC61850_T100_IED");
    for (const auto n : snapshot->browse.ieds) if (!wanted.isEmpty() && doc.attribute(n,"name") == wanted) ied = n;
    SclMmsRequest signalRequest; signalRequest.kind = SclMmsQueryKind::Signals; signalRequest.scope.ied = ied; signalRequest.text = "PhV";
    auto catalogue = queryWorker(signalRequest); QTRY_VERIFY(catalogue.isFinished());
    SclMmsRow attribute;
    for (const auto& signal : catalogue.result().rows) {
        if (signal.key.field != "PhV.phsA" || doc.attribute(signal.key.node,"lnClass") != "MMXU" || doc.attribute(signal.key.node,"inst") != "1") continue;
        SclMmsRequest r; r.kind = SclMmsQueryKind::Addresses; r.scope.ied = ied; r.signal = signal.key.record;
        auto future = queryWorker(r); QTRY_VERIFY(future.isFinished());
        for (const auto& row : future.result().rows) if (row.key.field == "cVal.mag.f" && row.fc == "MX") { attribute = row; break; }
        if (attribute.key.record >= 0) break;
    }
    QVERIFY(attribute.key.record >= 0);
    SclMmsRequest r; r.kind = SclMmsQueryKind::ObjectEvidence; r.key = attribute.key;
    r.scope = attribute.addressScope; r.identityDocument = snapshot->analysis.document;
    auto detail = queryWorker(r); QTRY_VERIFY(detail.isFinished()); const auto expected = detail.result().objectEvidence;
    QVERIFY(expected && expected->instance.state == SclInstanceLocationState::Exact);
    const auto target = expected->instance.target.node;
    if (switchView->text() == QStringLiteral("高级视图")) switchView->click();
    scope->setCurrentIndex(scope->findData(ied));
    QVERIFY(QMetaObject::invokeMethod(scope,"activated",Q_ARG(int,scope->currentIndex())));
    tabs->setCurrentWidget(objects);
    const auto indexFor = [](QAbstractItemView* table, NodeId n, int column = 0) {
        for (int row = 0; row < table->model()->rowCount(); ++row) {
            const auto index = table->model()->index(row,column);
            if (index.data(Qt::UserRole).toInt() == n) return index;
        }
        return QModelIndex{};
    };
    auto index = indexFor(objects,target); QVERIFY(index.isValid()); objects->setCurrentIndex(index); objects->scrollTo(index);
    inspector->setCurrentWidget(properties);
    QTRY_VERIFY(evidence->result() && evidence->result()->request.target == target && evidence->result()->objectEvidence);
    QCOMPARE(index.siblingAtColumn(7).data().toString(),expected->cdc);
    QCOMPARE(index.siblingAtColumn(9).data().toString(),expected->objectPath);
    QCOMPARE(index.siblingAtColumn(10).data().toString(),expected->attributePath);
    QTRY_VERIFY(properties->toPlainText().contains("CMV") && properties->toPlainText().contains("cVal.mag.f"));
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(objects->viewport()).intersects(objects->visualRect(index)));
    const auto screenshot = qEnvironmentVariable("IEC61850_T103_SCREENSHOT");
    if (external.isEmpty() && !screenshot.isEmpty())
        QVERIFY(window.grab().save(screenshot + QStringLiteral("-%1x%2.png").arg(size.width()).arg(size.height())));
    const auto sourceNode = expected->declarations.back().node;
    const auto sourceUrl = QUrl(QStringLiteral("scl-evidence://request-%1/%2").arg(evidence->result()->requestId).arg(sourceNode));
    QVERIFY(QMetaObject::invokeMethod(properties,"anchorClicked",Q_ARG(QUrl,sourceUrl)));
    QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[sourceNode].source.line)));
    back->click();
    QTRY_VERIFY(objects->currentIndex().data(Qt::UserRole).toInt() == target);
    QTRY_VERIFY(evidence->result() && evidence->result()->request.target == target);
    // Copy menu uses a non-selected hit and does not change selection/history/source.
    auto* interaction = objects->findChild<SclTableInteraction*>(); QVERIFY(interaction);
    const auto other = objects->model()->index(0,0); objects->setCurrentIndex(other);
    const auto selected = objects->currentIndex(); const auto beforeSource = sourceLabel->text();
    QMenu copyMenu; interaction->PopulateContextMenu(copyMenu,index);
    const auto pathGroup = [&copyMenu]() -> QMenu* {
        for (auto* action : copyMenu.actions()) if (action->text() == QStringLiteral("工程对象路径")) return action->menu();
        return nullptr;
    };
    QTRY_VERIFY(pathGroup() && !pathGroup()->actions().isEmpty());
    auto* copy = pathGroup()->actions().front(); copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),SclTaskPath(doc,target,true)); QCOMPARE(objects->currentIndex(),selected);
    QCOMPARE(sourceLabel->text(),beforeSource);
    // A filtered original identity survives task navigation and return, including its cell.
    mode->setCurrentIndex(1); query->setText("f");
    QTRY_VERIFY(!status->text().contains(QStringLiteral("搜索中")) && !status->text().contains(QStringLiteral("等待")) && indexFor(objects,target).isValid());
    QTest::qWait(400);
    index = indexFor(objects,target,10); QVERIFY(index.isValid()); objects->setCurrentIndex(index); objects->scrollTo(index);
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(objects->viewport()).intersects(objects->visualRect(index)));
    QMenu navigateMenu; interaction->PopulateContextMenu(navigateMenu,index);
    QAction* open = nullptr;
    for (auto* action : navigateMenu.actions()) if (action->objectName() == "SclOpenMmsAttributes") open = action;
    QVERIFY(open); open->trigger();
    QTRY_VERIFY(mms->isVisible() && mms->Result() && mms->Result()->request.kind == SclMmsQueryKind::Attributes
        && mms->Result()->request.target == target);
    QModelIndex taskRow;
    for (int i = 0; i < int(mms->Result()->rows.size()); ++i)
        if (mms->Result()->rows[i].key == attribute.key) taskRow = mms->Table()->model()->index(i,0);
    QVERIFY(taskRow.isValid()); mms->Table()->setCurrentIndex(taskRow);
    QTRY_VERIFY(mms->ObjectEvidence());
    QCOMPARE(mms->ObjectEvidence()->cdc,expected->cdc);
    switchView->click();
    QTRY_VERIFY(objects->isVisible() && objects->currentIndex().data(Qt::UserRole).toInt() == target);
    back->click();
    QTRY_VERIFY(mms->isVisible() && mms->Selected() == attribute.key);
    back->click();
    QTRY_VERIFY(objects->isVisible() && objects->currentIndex().data(Qt::UserRole).toInt() == target);
    QTRY_COMPARE(query->text(),QStringLiteral("f")); QCOMPARE(mode->currentIndex(),1);
    QTRY_COMPARE(objects->currentIndex().column(),10);
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(objects->viewport()).intersects(objects->visualRect(objects->currentIndex())));
    // Filter invalidation revokes both the original copy menu and attribute action.
    QApplication::clipboard()->setText("T103-stale-sentinel"); copy->trigger();
    QCOMPARE(QApplication::clipboard()->text(),QStringLiteral("T103-stale-sentinel"));
    open->trigger(); QVERIFY(objects->isVisible());
    query->clear(); mode->setCurrentIndex(0); QTest::qWait(400);
    // Exact/structure/object FCDA entries keep their original occurrence and CDC.
    NodeId member = NoNode, paged = NoNode;
    for (const auto n : snapshot->browse.memberRows) {
        if (snapshot->browse.iedOwners[n] != ied || doc.attribute(n,"doName") != "PhV.phsA") continue;
        if (member == NoNode) member = n;
        if (doc.attribute(doc.nodes[n].parent,"name") == "PagedDs") paged = n;
    }
    QVERIFY(member != NoNode);
    const auto selectTree = [&](NodeId n) {
        QList<NodeId> chain;
        for (auto at = n; at != NoNode; at = doc.nodes[at].parent) chain.prepend(at);
        QModelIndex current;
        for (const auto at : chain) current = tree->model()->index(snapshot->browse.branches[at].row,0,current);
        tree->setCurrentIndex(current);
    };
    selectTree(member); tabs->setCurrentWidget(members);
    QTRY_VERIFY(indexFor(members,member).isValid()); const auto flat = indexFor(members,member);
    QCOMPARE(flat.siblingAtColumn(9).data().toString(),doc.attribute(member,"doName"));
    QCOMPARE(flat.siblingAtColumn(11).data().toString(),QStringLiteral("CMV"));
    QTRY_VERIFY(evidence->result() && evidence->result()->request.target == member);
    QCOMPARE(evidence->result()->objectEvidence->cdc,expected->cdc);
    if (paged != NoNode) {
        auto* dataView = window.findChild<SclDataSetView*>(); QVERIFY(dataView);
        tabs->setCurrentWidget(dataView); dataView->SyncSelection(paged,doc.nodes[paged].parent);
        QTRY_VERIFY(sets->currentIndex().data(Qt::UserRole).toInt() == paged);
        const auto current = sets->currentIndex(); QVERIFY(current.parent().isValid());
        QVERIFY(current.siblingAtColumn(0).data().toString().startsWith("#205"));
        QCOMPARE(current.siblingAtColumn(9).data().toString(),expected->cdc);
        QVERIFY(current.siblingAtColumn(11).data().toString().contains(QStringLiteral("精确属性")));
        QTRY_VERIFY(SclNavigationProbe::VisibleRect(sets->viewport()).intersects(sets->visualRect(current)));
    }
    // Templates remain unbound and a replacement document rejects old async results.
    selectTree(attribute.key.declaration); tabs->setCurrentWidget(objects);
    QTRY_VERIFY(evidence->result() && evidence->result()->request.target == attribute.key.declaration);
    QVERIFY(evidence->result()->objectEvidence->values.isEmpty());
    QVERIFY(properties->toPlainText().contains(QStringLiteral("未绑定 LN 实例")));
    selectTree(target); selectTree(attribute.key.declaration); service->Reload();
    QTRY_COMPARE_WITH_TIMEOUT(service->state(),SclAnalysisService::State::Ready,90000);
    QVERIFY(service->snapshot()->analysis.document != snapshot->analysis.document);
    if (evidence->result()) QCOMPARE(evidence->result()->document,service->snapshot()->analysis.document);
    service->Close(); QVERIFY(!evidence->result()); QCOMPARE(FileDigest(path),digest);
    qInfo() << "T10.3 advanced/task/coverage/history/stale workflow" << size << "source unchanged";
}
