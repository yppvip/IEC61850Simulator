#pragma once

void MainWindowBaselineTest::SclTask10EvidenceWorkflow_data()
{
    QTest::addColumn<QSize>("size");
    QTest::newRow("normal") << QSize(1280,800);
    QTest::newRow("narrow") << QSize(620,560);
}

void MainWindowBaselineTest::SclTask10EvidenceWorkflow()
{
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask10EvidenceWorkflow")))
        QSKIP("Run as the separate object evidence workflow");
    using namespace iec61850sim;
    using namespace Scl;
    using namespace Task8Acceptance;
    QFETCH(QSize,size);
    QTemporaryDir directory;
    QVERIFY(directory.isValid()); IsolateSettings(directory.path());
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"IEC61850Simulator","IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView",true);
    const auto oldClipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([oldClipboard] { QApplication::clipboard()->setText(oldClipboard); });
    const auto external = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    const auto path = external.isEmpty() ? QStringLiteral(SCL_FIXTURE_DIR "/Task10Evidence.scd") : external;
    const auto digest = FileDigest(path);
    MainWindow window; window.resize(size); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>();
    auto* view = window.findChild<SclMmsView*>();
    auto* pages = window.findChild<SclTaskPages*>();
    auto* scope = window.findChild<QComboBox*>("SclTaskIed");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* toggle = window.findChild<QToolButton*>("SclToggleDetails");
    auto* summary = window.findChild<QLabel*>("SclMmsObjectSummary");
    auto* dataSource = window.findChild<QToolButton*>("SclMmsDataSetSource");
    auto* instanceSource = window.findChild<QToolButton*>("SclMmsInstanceSource");
    auto* typeSource = window.findChild<QToolButton*>("SclMmsTypeSource");
    auto* composition = window.findChild<QToolButton*>("SclMmsComposition");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* back = window.findChild<QPushButton*>("SclBack");
    QVERIFY(service && view && pages && scope && inspector && toggle && summary && dataSource && instanceSource
        && typeSource && composition && sourceLabel && source && back);
    service->Open(path); QTRY_COMPARE_WITH_TIMEOUT(service->state(),SclAnalysisService::State::Ready,90000);
    auto snapshot = service->snapshot();
    auto ied = snapshot->browse.ieds.front();
    const auto wantedIed = qEnvironmentVariable("IEC61850_T100_IED");
    if (!wantedIed.isEmpty()) for (const auto n : snapshot->browse.ieds)
        if (snapshot->analysis.document->attribute(n,"name") == wantedIed) ied = n;
    scope->setCurrentIndex(scope->findData(ied));
    QVERIFY(QMetaObject::invokeMethod(scope,"activated",Q_ARG(int,scope->currentIndex())));
    pages->SetPage(SclTaskPages::Mms);
    if (!toggle->isChecked()) toggle->click();
    inspector->setCurrentWidget(view->Inspector());
    const auto query = [snapshot](SclMmsRequest request) {
        return QtConcurrent::run([snapshot,request] { return QuerySclMms(*snapshot,request,nullptr); });
    };
    SclMmsRequest signalRequest; signalRequest.kind = SclMmsQueryKind::Signals; signalRequest.scope.ied = ied;
    signalRequest.text = "PhV"; // Query the target subset in the worker, beyond the catalogue's first page.
    auto catalogue = query(signalRequest); QTRY_VERIFY(catalogue.isFinished());
    int checked = 0;
    QSignalSpy opened(view,&SclMmsView::OpenEvidence);
    for (const auto& object : catalogue.result().rows) {
        if (object.key.field != "PhV.phsA" && object.key.field != "PhV.res") continue;
        const auto& doc = *snapshot->analysis.document;
        if (doc.attribute(object.key.node,"lnClass") != "MMXU" || doc.attribute(object.key.node,"inst") != "1") continue;
        SclMmsRequest addresses; addresses.kind = SclMmsQueryKind::Addresses;
        addresses.scope.ied = ied; addresses.signal = object.key.record;
        auto attributes = query(addresses); QTRY_VERIFY(attributes.isFinished());
        for (const auto& attribute : attributes.result().rows) {
            if (attribute.key.field != "cVal.mag.f" && attribute.key.field != "cVal.ang.f") continue;
            SclMmsView::Location location; location.snapshot = snapshot; location.viewIed = ied;
            location.request = addresses; location.selected = attribute.key; location.inspectorVisible = true;
            view->RestoreLocation(location);
            QTRY_VERIFY(view->Result() && view->Selected() == attribute.key && view->Fields()->isEnabled());
            QTRY_VERIFY(!summary->text().contains(QStringLiteral("查询中")) && !summary->text().isEmpty());
            QVERIFY(summary->text().contains(QStringLiteral("类型展开")));
            QVERIFY(summary->text().contains("CMV / FLOAT32"));
            const auto screenshot = qEnvironmentVariable("IEC61850_T102_SCREENSHOT");
            if (!screenshot.isEmpty() && checked == 0) {
                QTest::qWait(100);
                QVERIFY(window.grab().save(screenshot + QStringLiteral("-%1x%2.png").arg(size.width()).arg(size.height())));
            }
            const auto findField = [view](const QString& label) {
                for (int i = 0; i < view->Fields()->model()->rowCount(); ++i)
                    if (view->Fields()->model()->index(i,0).data().toString() == label)
                        return view->Fields()->model()->index(i,1);
                return QModelIndex{};
            };
            QCOMPARE(findField(QStringLiteral("DO / SDO 路径")).data().toString(),object.key.field);
            QCOMPARE(findField(QStringLiteral("DA / BDA 属性路径")).data().toString(),attribute.key.field);
            QCOMPARE(findField(QStringLiteral("MMS Item")).data().toString(),attribute.item);
            // Each source family actually navigates to its own snapshot anchor.
            for (auto* button : {dataSource,instanceSource,typeSource}) {
                QAction* action = nullptr;
                for (auto* candidate : button->menu()->actions())
                    if (candidate->isEnabled() && candidate->text().contains(QStringLiteral("标签起始行"))) { action = candidate; break; }
                if (!action) { QVERIFY(button == dataSource); continue; }
                const auto expectedLine = action->text().section(QStringLiteral("标签起始行 "),-1).toInt();
                QVERIFY(expectedLine > 0);
                opened.clear(); action->trigger();
                QTRY_COMPARE(opened.size(),1);
                const auto node = opened.front()[0].toInt();
                QCOMPARE(int(doc.nodes[node].source.line),expectedLine);
                QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(expectedLine)));
                QTRY_VERIFY(SclNavigationProbe::VisibleRect(source->viewport()).intersects(source->cursorRect()));
                back->click();
                QTRY_VERIFY(view->Table()->isEnabled() && view->Fields()->isEnabled() && view->Selected() == attribute.key
                    && inspector->currentWidget() == view->Inspector());
                QTRY_VERIFY(SclNavigationProbe::VisibleRect(view->Table()->viewport()).intersects(view->Table()->visualRect(view->Table()->currentIndex())));
                // Observe after pending wrap/ADS layout events as well.
                QTest::qWait(80);
                QVERIFY(SclNavigationProbe::VisibleRect(view->Table()->viewport()).intersects(view->Table()->visualRect(view->Table()->currentIndex())));
                QTRY_VERIFY(!summary->text().isEmpty() && !summary->text().contains(QStringLiteral("查询中")));
            }
            if (object.key.field == "PhV.res" && attribute.key.field == "cVal.ang.f") {
                QVERIFY(summary->text().contains(QStringLiteral("未发现数据集引用")));
                QVERIFY(instanceSource->menu()->actions().front()->text().contains(QStringLiteral("回退")));
                QVERIFY(instanceSource->menu()->actions().front()->text().contains(QStringLiteral("未声明")));
            }
            // Full-value copy remains distinct from explicit original-source actions.
            const auto item = findField(QStringLiteral("MMS Item"));
            view->Fields()->setCurrentIndex(item); view->Fields()->scrollTo(item);
            QTRY_VERIFY(SclNavigationProbe::VisibleRect(view->Fields()->viewport()).intersects(view->Fields()->visualRect(item)));
            QTest::mouseDClick(view->Fields()->viewport(),Qt::LeftButton,Qt::NoModifier,view->Fields()->visualRect(item).center());
            QCOMPARE(QApplication::clipboard()->text(),attribute.item);
            if (checked == 0) {
                QAction* itemComposition = nullptr;
                for (auto* action : composition->menu()->actions()) if (action->text().startsWith("MMS Item")) itemComposition = action;
                QVERIFY(itemComposition); itemComposition->trigger();
                auto* trace = window.findChild<QWidget*>("SclReferenceTrace");
                auto* evidenceTree = trace->findChild<QTreeWidget*>("SclTraceTree");
                QTRY_VERIFY(trace->isVisible() && evidenceTree->topLevelItemCount() == 1);
                QCOMPARE(evidenceTree->topLevelItem(0)->text(1),attribute.item);
                back->click();
                QTRY_VERIFY(view->Table()->isVisible() && view->Table()->isEnabled() && view->Selected() == attribute.key);
                QTRY_VERIFY(!summary->text().isEmpty() && !summary->text().contains(QStringLiteral("查询中")));
            }
            // The dataset table preserves independent occurrences and all user kinds.
            auto* all = dataSource->menu()->findChild<QAction*>("SclMmsAllDataSets"); QVERIFY(all); all->trigger();
            QTRY_VERIFY(view->Result() && view->Result()->request.kind == SclMmsQueryKind::DataSetRelations);
            QVERIFY(view->Result()->request.key == attribute.key);
            for (const auto& relation : view->Result()->rows) QVERIFY(relation.dataSetRelation);
            back->click();
            QTRY_VERIFY(view->Result() && view->Result()->request.kind == SclMmsQueryKind::Addresses && view->Selected() == attribute.key);
            ++checked;
        }
    }
    QVERIFY(checked >= 4);
    const auto& doc = *snapshot->analysis.document;
    NodeId member = NoNode;
    for (int n = 0; n < int(doc.nodes.size()); ++n) {
        if (doc.nodes[n].name == "FCDA" && snapshot->browse.iedOwners[n] == ied
            && doc.attribute(n,"doName") == "PhV.phsA" && doc.attribute(n,"lnClass") == "MMXU"
            && (doc.attribute(n,"daName").isEmpty() || doc.attribute(n,"daName") == "cVal.mag.f")) { member = n; break; }
    }
    QVERIFY(member != NoNode);
    SclMmsRequest members; members.kind = SclMmsQueryKind::Members; members.target = doc.nodes[member].parent;
    members.scope.ied = ied;
    SclMmsView::Location memberLocation; memberLocation.snapshot = snapshot; memberLocation.viewIed = ied;
    memberLocation.request = members; memberLocation.inspectorVisible = true;
    const auto referenceIndex = snapshot->mmsIndex.memberReferences.value(member,-1); QVERIFY(referenceIndex >= 0);
    memberLocation.selected = SclMmsKey{SclMmsRecordKind::Member,member,referenceIndex,snapshot->analysis.references[referenceIndex].memberOrdinal};
    memberLocation.request.locate = memberLocation.selected;
    view->RestoreLocation(memberLocation);
    QTRY_VERIFY(view->Result() && view->Result()->request.kind == SclMmsQueryKind::Members && view->Fields()->isEnabled());
    const auto* model = view->Table()->model();
    QCOMPARE(model->headerData(1,Qt::Horizontal).toString(),QStringLiteral("原始 doName"));
    QCOMPARE(model->headerData(2,Qt::Horizontal).toString(),QStringLiteral("原始 daName"));
    const auto displayed = view->Table()->currentIndex().row(); QVERIFY(displayed >= 0);
    QCOMPARE(model->index(displayed,1).data().toString(),doc.attribute(member,"doName"));
    const auto rawDa = doc.attribute(member,"daName");
    QCOMPARE(model->index(displayed,2).data().toString(),rawDa.isNull() ? QStringLiteral("未声明") : rawDa.isEmpty() ? QStringLiteral("已声明空值") : rawDa);
    QVERIFY(typeSource->isEnabled() && instanceSource->isEnabled());
    auto* proceed = window.findChild<QPushButton*>("SclMmsOpen"); QVERIFY(proceed); proceed->click();
    QTRY_VERIFY(view->Result() && view->Result()->request.kind == SclMmsQueryKind::Signals && view->Table()->isEnabled());
    proceed->click();
    QTRY_VERIFY(view->Result() && view->Result()->request.kind == SclMmsQueryKind::Addresses && view->Table()->isEnabled());
    QTRY_VERIFY(summary->text().contains(QStringLiteral("当前来源数据集")) && !summary->text().contains(QStringLiteral("查询中")));
    // An action retained from a previous selection cannot navigate a replacement document.
    QPointer<QAction> stale = typeSource->menu()->actions().isEmpty() ? nullptr : typeSource->menu()->actions().front();
    opened.clear(); service->Close();
    if (stale) stale->trigger();
    QCOMPARE(opened.size(),0);
    QCOMPARE(FileDigest(path),digest);
    qInfo() << "T10.2 evidence/source/copy/history workflow" << size << "attributes" << checked << "source unchanged";
}
