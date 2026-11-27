#pragma once
void MainWindowBaselineTest::SclTask10OfflineWorkflow_data() {
    QTest::addColumn<QSize>("size");
    QTest::newRow("normal") << QSize(1280,800);
    QTest::newRow("narrow") << QSize(620,560);
}
void MainWindowBaselineTest::SclTask10OfflineWorkflow() {
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask10OfflineWorkflow"))) QSKIP("Separate offline check workflow");
    using namespace iec61850sim; using namespace Scl; using namespace Task8Acceptance;
    QFETCH(QSize,size);
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat,QSettings::UserScope,"IEC61850Simulator","IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView",true);
    const auto external = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    const auto path = external.isEmpty() ? QStringLiteral(SCL_FIXTURE_DIR "/Task10Evidence.scd") : external;
    const auto digest = FileDigest(path);
    MainWindow window; window.resize(size); window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>(); auto* check = window.findChild<SclOfflineCheckView*>();
    auto* mms = window.findChild<SclMmsView*>(); auto* scope = window.findChild<QComboBox*>("SclTaskIed");
    auto* entry = window.findChild<QPushButton*>("SclOpenOfflineCheck");
    auto* input = window.findChild<QLineEdit*>("SclCheckInput"); auto* fc = window.findChild<QLineEdit*>("SclCheckFc");
    auto* domain = window.findChild<QLineEdit*>("SclCheckDomain"); auto* kind = window.findChild<QComboBox*>("SclCheckKind");
    auto* run = window.findChild<QPushButton*>("SclCheckRun"); auto* open = window.findChild<QPushButton*>("SclCheckOpenTarget");
    auto* evidence = window.findChild<QTextBrowser*>("SclCheckEvidence"); auto* back = window.findChild<QPushButton*>("SclMmsBack");
    auto* candidates = window.findChild<QComboBox*>("SclCheckCandidates"); auto* choose = window.findChild<QPushButton*>("SclCheckChoose");
    QVERIFY(service && check && mms && scope && entry && input && fc && domain && kind && run && open && evidence && back && candidates && choose);
    service->Open(path); QTRY_COMPARE_WITH_TIMEOUT(service->state(),SclAnalysisService::State::Ready,90000);
    const auto snapshot = service->snapshot(); const auto& doc = *snapshot->analysis.document;
    auto ied = snapshot->browse.ieds.front();
    const auto wanted = qEnvironmentVariable("IEC61850_T100_IED");
    for (auto n : snapshot->browse.ieds) if (!wanted.isEmpty() && doc.attribute(n,"name") == wanted) ied = n;
    scope->setCurrentIndex(scope->findData(ied)); QVERIFY(QMetaObject::invokeMethod(scope,"activated",Q_ARG(int,scope->currentIndex())));
    SclMmsRequest r; r.kind = SclMmsQueryKind::Signals; r.scope.ied = ied; r.text = "PhV";
    auto worker = QtConcurrent::run([snapshot,r] { return QuerySclMms(*snapshot,r,nullptr); }); QTRY_VERIFY(worker.isFinished());
    SclMmsRow target;
    for (const auto& row : worker.result().rows) {
        if (row.key.field != "PhV.phsA" || doc.attribute(row.key.node,"lnClass") != "MMXU" || doc.attribute(row.key.node,"inst") != "1") continue;
        r.kind = SclMmsQueryKind::Addresses; r.signal = row.key.record;
        auto addresses = QtConcurrent::run([snapshot,r] { return QuerySclMms(*snapshot,r,nullptr); }); QTRY_VERIFY(addresses.isFinished());
        for (const auto& address : addresses.result().rows) if (address.key.field == "cVal.ang.f" && address.fc == "MX") { target = address; break; }
        if (target.key.record >= 0) break;
    }
    QVERIFY(target.key.record >= 0);
    entry->click(); QTRY_VERIFY(check->isVisible());
    input->setText(target.objectReference); fc->setText(target.fc); run->click();
    QTRY_VERIFY(check->Result() && check->Result()->check);
    QVERIFY2(check->Result()->check->mappingPassed,qPrintable(check->Result()->check->model + check->Result()->limitation));
    QCOMPARE(check->Result()->check->target->key,target.key); QVERIFY(evidence->toPlainText().contains(QStringLiteral("离线映射通过")));
    qInfo() << "T10.4 successful check us/work:" << check->Result()->check->elapsedUs << check->Result()->check->workUsed;
    const auto screenshot = qEnvironmentVariable("IEC61850_T104_SCREENSHOT");
    if (external.isEmpty() && !screenshot.isEmpty()) QVERIFY(window.grab().save(screenshot + QStringLiteral("-%1x%2.png").arg(size.width()).arg(size.height())));
    auto* scroll = check->findChild<QScrollArea*>(); QVERIFY(scroll); scroll->ensureWidgetVisible(open);
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(open).width() > 0 && SclNavigationProbe::VisibleRect(open).height() > 0);
    open->click(); QTRY_VERIFY(mms->isVisible() && mms->Result() && mms->Selected() == std::optional<SclMmsKey>(target.key));
    QTRY_VERIFY(mms->Table()->currentIndex().isValid());
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(mms->Table()->viewport()).intersects(mms->Table()->visualRect(mms->Table()->currentIndex())));
    back->click(); QTRY_VERIFY(check->isVisible() && check->Result() && open->isEnabled());
    QCOMPARE(input->text(),target.objectReference); QCOMPARE(fc->text(),target.fc);
    auto* forward = window.findChild<QPushButton*>("SclForward"); QVERIFY(forward && forward->isEnabled());
    forward->click(); QTRY_VERIFY(mms->isVisible() && mms->Result() && mms->Selected() == std::optional<SclMmsKey>(target.key));
    QTRY_VERIFY(SclNavigationProbe::VisibleRect(mms->Table()->viewport()).intersects(mms->Table()->visualRect(mms->Table()->currentIndex())));
    back->click(); QTRY_VERIFY(check->isVisible() && check->Result());
    // Old result and old successful action are revoked immediately on text editing.
    input->setText(target.objectReference + ".missing"); QVERIFY(!check->Result() && !open->isEnabled());
    open->click(); QVERIFY(check->isVisible()); run->click(); QTRY_VERIFY(check->Result() && check->Result()->check);
    QVERIFY(!check->Result()->check->mappingPassed && !check->Result()->check->target);
    // Separate Domain and Item, preserving pasted original text, then explicit candidate choice.
    kind->setCurrentIndex(1); input->setText(target.item); fc->clear(); domain->clear(); run->click();
    QTRY_VERIFY(check->Result() && check->Result()->check);
    QVERIFY(!open->isEnabled()); QVERIFY(candidates->count() > 0); QCOMPARE(candidates->currentIndex(),-1);
    int chosen = -1;
    for (int i = 0; i < check->Result()->check->candidates.size(); ++i)
        if (check->Result()->check->candidates[i].logicalDevice == target.logicalDevice) chosen = i;
    QVERIFY(chosen >= 0); candidates->setCurrentIndex(chosen); choose->click();
    QTRY_VERIFY(check->Result() && check->Result()->check && check->Result()->check->mappingPassed);
    QCOMPARE(check->Result()->request.check.original,target.item);
    if (!external.isEmpty()) {
        // Explicit local acceptance of an actual magnitude-only type, without filename/vendor rules.
        auto magnitudeOnly = QtConcurrent::run([snapshot,ied]() -> std::optional<std::pair<SclMmsResult,SclMmsResult>> {
            const auto& d = *snapshot->analysis.document;
            for (const auto& instance : snapshot->tasks.catalog.instances) {
                if (snapshot->browse.iedOwners[instance.logicalNode] != ied
                    || d.attribute(instance.logicalNode,"lnClass") != "MMXN" || d.attribute(instance.logicalNode,"inst") != "1"
                    || snapshot->tasks.catalog.definitions[instance.definition].path != "Vol") continue;
                auto ld = instance.logicalNode;
                while (ld != NoNode && d.nodes[ld].name != "LDevice") ld = d.nodes[ld].parent;
                if (ld == NoNode) continue;
                SclMmsRequest request; request.kind = SclMmsQueryKind::OfflineCheck;
                request.identityDocument = snapshot->analysis.document; request.scope.ied = ied;
                request.check.fc = "MX";
                const auto root = snapshot->mmsIndex.domainsByDevice.value(ld).name + "/MMXN1.Vol.";
                request.check.original = root + "mag.f";
                auto valid = QuerySclMms(*snapshot,request,nullptr);
                request.check.original = root + "ang.f";
                return std::pair{std::move(valid),QuerySclMms(*snapshot,request,nullptr)};
            }
            return {};
        });
        QTRY_VERIFY(magnitudeOnly.isFinished()); const auto mv = magnitudeOnly.result();
        if (qEnvironmentVariableIsSet("IEC61850_T104_EXPECT_MV")) QVERIFY(mv);
        if (mv) {
            QVERIFY(mv->first.check && mv->first.check->mappingPassed);
            QVERIFY(mv->second.check && !mv->second.check->mappingPassed && !mv->second.check->target);
            QCOMPARE(mv->second.check->stoppedAt,QStringLiteral("ang"));
            QCOMPARE(doc.nodes[mv->second.check->lastConfirmed.node].name,QStringLiteral("DOType"));
            qInfo() << "T10.4 local magnitude-only type verified; missing ang source line:" << mv->second.check->lastConfirmed.location.line;
        }
    }
    if (external.isEmpty()) {
        kind->setCurrentIndex(0); domain->clear(); input->setText("EvidenceDomain/MMXN1.Vol.ang.f"); fc->setText("MX"); run->click();
        QTRY_VERIFY(check->Result() && check->Result()->check);
        const auto failure = check->Result()->check; QCOMPARE(failure->stoppedAt,QStringLiteral("ang"));
        QCOMPARE(doc.nodes[failure->lastConfirmed.node].name,QStringLiteral("DOType")); QVERIFY(!open->isEnabled());
        auto html = evidence->toHtml();
        QRegularExpression link("scl-check-source://request-[0-9]+/" + QString::number(failure->lastConfirmed.node));
        const auto match = link.match(html); QVERIFY(match.hasMatch());
        QVERIFY(QMetaObject::invokeMethod(evidence,"anchorClicked",Q_ARG(QUrl,QUrl(match.captured()))));
        auto* source = window.findChild<QPlainTextEdit*>("SclSource"); QVERIFY(source);
        QTRY_VERIFY(source->isVisible());
        QVERIFY(source->toPlainText().contains("MagnitudeValue"));
        // No-data-set signal remains a successful neutral task target.
        input->setText("EvidenceDomain/MMXU1.NoData.cVal.ang.f"); run->click();
        QTRY_VERIFY(check->Result() && check->Result()->check);
        QVERIFY(check->Result()->check->mappingPassed); QCOMPARE(check->Result()->check->dataSets.conclusion,SclDataSetConclusion::NotReferenced);
        const auto unreferenced = check->Result()->check->target->key;
        open->click(); QTRY_VERIFY(mms->Selected() == std::optional<SclMmsKey>(unreferenced) && mms->Result());
        QTRY_VERIFY(SclNavigationProbe::VisibleRect(mms->Table()->viewport()).intersects(mms->Table()->visualRect(mms->Table()->currentIndex())));
        back->click(); QTRY_VERIFY(check->isVisible() && check->Result());
    }
    // A replacement document with identical names cannot inherit the old result or action.
    service->Open(path); QTRY_VERIFY(service->state() == SclAnalysisService::State::Ready && service->snapshot() != snapshot);
    QVERIFY(!check->Result()); entry->click(); QVERIFY(!open->isEnabled());
    open->click(); QVERIFY(check->isVisible());
    QCOMPARE(FileDigest(path),digest);
    qInfo().noquote() << "T10.4 offline workflow: target visible, Back, candidate selection, invalidation, digest unchanged; external=" << !external.isEmpty();
}
