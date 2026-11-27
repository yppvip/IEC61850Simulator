#pragma once

// Explicit local T10.0 probe. No product behavior or engineering files are changed.
void MainWindowBaselineTest::SclTask10Baseline()
{
    using namespace iec61850sim;
    using namespace Scl;
    using namespace Task8Acceptance;
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask10Baseline")))
        QSKIP("Explicit local T10.0 baseline only");
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    const auto iedName = qEnvironmentVariable("IEC61850_T100_IED");
    const auto output = qEnvironmentVariable("IEC61850_T100_OUTPUT");
    QVERIFY(!path.isEmpty() && !iedName.isEmpty() && !output.isEmpty());
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    const auto digest = FileDigest(path);
    QTemporaryDir temporary;
    QVERIFY(temporary.isValid());
    IsolateSettings(temporary.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    const auto clipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([clipboard] { QApplication::clipboard()->setText(clipboard); });
    MainWindow window;
    window.resize(1280, 800);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>();
    auto* mms = window.findChild<SclMmsView*>();
    auto* pages = window.findChild<SclTaskPages*>();
    auto* scope = window.findChild<QComboBox*>("SclTaskIed");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* toggle = window.findChild<QToolButton*>("SclToggleDetails");
    auto* sourceButton = window.findChild<QPushButton*>("SclMmsSource");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    auto* details = mms->findChild<SclMmsQuerySession*>("SclMmsDetailsSession");
    auto* back = window.findChild<QPushButton*>("SclBack");
    QVERIFY(service && mms && pages && scope && inspector && toggle && sourceButton && source && sourceLabel && details && back);
    QElapsedTimer load;
    load.start();
    service->Open(path);
    QTRY_COMPARE_WITH_TIMEOUT(service->state(), SclAnalysisService::State::Ready, 90000);
    const auto readyMs = load.elapsed();
    auto snapshot = service->snapshot();
    QCOMPARE(snapshot->analysis.sha256, digest);
    const auto& doc = *snapshot->analysis.document;
    const auto attr = [&](NodeId n, const char* name) { return doc.attribute(n, QString::fromLatin1(name)); };
    NodeId ied = NoNode;
    for (const auto n : snapshot->browse.ieds) if (attr(n, "name") == iedName) ied = n;
    QVERIFY(ied != NoNode);
    scope->setCurrentIndex(scope->findData(ied));
    QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, scope->currentIndex())));
    pages->SetPage(SclTaskPages::Mms);
    if (!toggle->isChecked()) toggle->click();
    inspector->setCurrentWidget(mms->Inspector());
    const auto fieldIndex = [&](const QString& label) {
        auto* model = mms->Fields()->model();
        for (int i = 0; i < model->rowCount(); ++i)
            if (model->index(i, 0).data().toString() == label) return model->index(i, 1);
        return QModelIndex{};
    };
    const auto headers = [](QAbstractItemModel* model) {
        QJsonArray out;
        for (int c = 0; c < model->columnCount(); ++c) out.append(model->headerData(c, Qt::Horizontal).toString());
        return out;
    };
    QJsonArray cases, returns, detailTimes;
    bool sawPhsA = false, sawRes = false, sawVol = false;
    for (int signal = 0; signal < int(snapshot->tasks.catalog.instances.size()); ++signal) {
        const auto& instance = snapshot->tasks.catalog.instances[signal];
        if (snapshot->browse.iedOwners[instance.logicalNode] != ied || attr(instance.logicalNode, "inst") != "1") continue;
        const auto& definition = snapshot->tasks.catalog.definitions[instance.definition];
        const bool voltage = attr(instance.logicalNode, "lnClass") == "MMXU" && definition.path == "PhV.phsA";
        const bool residual = attr(instance.logicalNode, "lnClass") == "MMXU" && definition.path == "PhV.res";
        const bool magnitude = attr(instance.logicalNode, "lnClass") == "MMXN" && definition.path == "Vol";
        if (!voltage && !residual && !magnitude) continue;
        sawPhsA |= voltage; sawRes |= residual; sawVol |= magnitude;
        SclMmsRequest request; request.kind = SclMmsQueryKind::Addresses;
        request.scope.ied = ied; request.signal = signal; request.presentation = true;
        auto query = QtConcurrent::run([snapshot, request] {
            QElapsedTimer timer; timer.start();
            auto result = QuerySclMms(*snapshot, request, nullptr);
            return std::make_pair(std::move(result), timer.nsecsElapsed() / 1000);
        });
        QTRY_VERIFY_WITH_TIMEOUT(query.isFinished(), 15000);
        const auto queryResult = query.result();
        QVERIFY(!queryResult.first.limited);
        QJsonObject object{{"signal", signal}, {"ln", instance.logicalNode}, {"path", definition.path},
            {"cdc", definition.cdc}, {"definition", definition.declaration}, {"type", definition.type},
            {"typeLine", int(doc.nodes[definition.type].source.line)}, {"addressQueryUs", queryResult.second}};
        QJsonArray attributes;
        bool hasAngle = false, hasMagnitude = false;
        for (const auto& row : queryResult.first.rows) {
            if (row.fc != "MX") continue;
            hasAngle |= row.key.field == "ang.f" || row.key.field == "cVal.ang.f";
            hasMagnitude |= row.key.field == "mag.f" || row.key.field == "cVal.mag.f";
            if (row.key.field != "mag.f" && row.key.field != "cVal.mag.f" && row.key.field != "cVal.ang.f") continue;
            QJsonObject entry{{"field", row.key.field}, {"item", row.item}, {"domain", row.domain},
                {"declaration", row.key.declaration}, {"declarationLine", int(doc.nodes[row.key.declaration].source.line)},
                {"fcLine", int(doc.nodes[row.fcEvidence.node].source.line)}};
            QVERIFY(!row.item.isEmpty());
            SclMmsView::Location location; location.snapshot = snapshot; location.viewIed = ied;
            location.request = request; location.request.locate = row.key;
            location.selected = row.key; location.inspectorVisible = true;
            QElapsedTimer pageTime; pageTime.start();
            mms->RestoreLocation(location);
            QTRY_VERIFY_WITH_TIMEOUT(mms->Result() && mms->Result()->request.signal == signal
                && mms->Selected() == row.key && mms->Table()->isEnabled(), 15000);
            QTRY_COMPARE_WITH_TIMEOUT(fieldIndex(QStringLiteral("MMS Item")).data().toString(), row.item, 15000);
            entry["firstPageAndDetailMs"] = pageTime.elapsed();
            mms->Table()->scrollTo(mms->Table()->currentIndex());
            QTRY_VERIFY(SclNavigationProbe::VisibleRect(mms->Table()->viewport())
                .intersects(mms->Table()->visualRect(mms->Table()->currentIndex())));
            entry["headers"] = headers(mms->Table()->model());
            QJsonArray fields;
            for (int f = 0; f < mms->Fields()->model()->rowCount(); ++f) {
                QJsonArray cells;
                for (int c = 0; c < mms->Fields()->model()->columnCount(); ++c)
                    cells.append(mms->Fields()->model()->index(f, c).data().toString());
                fields.append(cells);
            }
            entry["fields"] = fields;
            const auto artifact = qEnvironmentVariable("IEC61850_T100_SCREENSHOT");
            if (!artifact.isEmpty()) QVERIFY(window.grab().save(artifact + '-' + definition.path + '-' + row.key.field + ".png"));
            auto index = fieldIndex(QStringLiteral("MMS Item"));
            mms->Fields()->setCurrentIndex(index); mms->Fields()->scrollTo(index);
            QTRY_VERIFY(sourceButton->isEnabled());
            QTRY_VERIFY(SclNavigationProbe::VisibleRect(mms->Fields()->viewport()).intersects(mms->Fields()->visualRect(index)));
            const bool evidenceDetails = qEnvironmentVariableIsSet("IEC61850_T101_EVIDENCE")
                && !qEnvironmentVariable("IEC61850_T101_EVIDENCE").isEmpty();
            const auto navigationState = [&] {
                auto* table = mms->Table();
                const auto location = mms->CurrentLocation();
                QJsonArray heights;
                for (int n = 0; n < std::min(10,table->model()->rowCount()); ++n) heights.append(table->rowHeight(n));
                return QJsonObject{{"selected",location.selected ? location.selected->field : QString()},
                    {"top",location.top ? location.top->field : QString()}, {"topOffset",location.topOffset},
                    {"scroll",table->verticalScrollBar()->value()}, {"viewportHeight",table->viewport()->height()},
                    {"selectedY",table->visualRect(table->currentIndex()).y()}, {"rowHeights",heights},
                    {"field",location.field}, {"snapshotMatches",location.snapshot == snapshot}};
            };
            QJsonArray resizeEvents;
            QElapsedTimer navigationTimer; navigationTimer.start();
            const auto resizeConnection = evidenceDetails ? connect(mms->Table()->verticalHeader(),&QHeaderView::sectionResized,&window,
                [&](int section, int oldSize, int newSize) { resizeEvents.append(QJsonObject{{"ms",navigationTimer.elapsed()},
                    {"section",section},{"old",oldSize},{"new",newSize}}); }) : QMetaObject::Connection{};
            const auto disconnectResize = qScopeGuard([resizeConnection] { QObject::disconnect(resizeConnection); });
            if (evidenceDetails) entry["navigationBefore"] = navigationState();
            sourceButton->click();
            QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[row.key.declaration].source.line)));
            QTRY_VERIFY(SclNavigationProbe::VisibleRect(source->viewport()).intersects(source->cursorRect()));
            entry["sourceLabel"] = sourceLabel->text();
            QElapsedTimer returnTime; returnTime.start(); back->click();
            QTRY_VERIFY(mms->Table()->isEnabled() && inspector->currentWidget() == mms->Inspector() && mms->Selected() == row.key);
            returns.append(returnTime.nsecsElapsed() / 1000);
            const auto currentVisible = [&] {
                const auto index = mms->Table()->currentIndex();
                return index.isValid() && SclNavigationProbe::VisibleRect(mms->Table()->viewport())
                    .intersects(mms->Table()->visualRect(index));
            };
            while (!currentVisible() && returnTime.elapsed() < 1000) QTest::qWait(10);
            const auto current = mms->Table()->currentIndex();
            entry["returnCurrentValid"] = current.isValid();
            entry["returnCurrentVisible"] = currentVisible();
            entry["returnVisibilityUs"] = returnTime.nsecsElapsed() / 1000;
            if (evidenceDetails) {
                entry["navigationAfter"] = navigationState(); entry["resizeEvents"] = resizeEvents;
            }
            if (!artifact.isEmpty()) QVERIFY(window.grab().save(artifact + '-' + definition.path + '-' + row.key.field + "-return.png"));
            SclMmsRequest uses; uses.kind = SclMmsQueryKind::AttributeReports; uses.scope = row.addressScope; uses.key = row.key;
            auto usage = QtConcurrent::run([snapshot, uses] {
                QElapsedTimer timer; timer.start(); auto r = QuerySclMms(*snapshot, uses, nullptr);
                return std::make_pair(std::move(r), timer.nsecsElapsed() / 1000);
            });
            QTRY_VERIFY_WITH_TIMEOUT(usage.isFinished(), 15000);
            const auto evidence = usage.result();
            entry["reportQueryUs"] = evidence.second; entry["reportLimited"] = evidence.first.limited;
            QJsonArray matches;
            for (const auto& use : evidence.first.rows) matches.append(QJsonObject{
                {"member", use.key.node}, {"ordinal", use.key.ordinal}, {"memberLine", int(doc.nodes[use.key.node].source.line)},
                {"dataSet", use.dataSet}, {"dataSetLine", int(doc.nodes[use.dataSet].source.line)},
                {"match", SclMmsReportMatchText(use.reportMatch)}, {"rawDoName", attr(use.key.node, "doName")},
                {"rawDaName", attr(use.key.node, "daName")}, {"declaredItem", use.item}});
            entry["reportMatches"] = matches;
            if (evidenceDetails) {
                SclMmsRequest requestEvidence; requestEvidence.kind = SclMmsQueryKind::ObjectEvidence;
                requestEvidence.scope = row.addressScope; requestEvidence.key = row.key;
                requestEvidence.identityDocument = snapshot->analysis.document;
                auto projected = QtConcurrent::run([snapshot,requestEvidence] { return QuerySclMms(*snapshot,requestEvidence,nullptr); });
                QTRY_VERIFY_WITH_TIMEOUT(projected.isFinished(),15000);
                const auto objectResult = projected.result(); QVERIFY(objectResult.objectEvidence && !objectResult.limited);
                const auto& object = *objectResult.objectEvidence;
                QCOMPARE(object.objectPath,definition.path); QCOMPARE(object.attributePath,row.key.field);
                const auto projectedItem = std::find_if(object.values.begin(),object.values.end(),[](const auto& value) {
                    return value.kind == SclReferenceValueKind::MmsItem;
                });
                QVERIFY(projectedItem != object.values.end()); QCOMPARE(projectedItem->value,row.item);
                entry["objectEvidence"] = QJsonObject{{"cdc",object.cdc},{"basicType",object.basicType},{"fc",object.fc},
                    {"object",object.objectPath},{"attribute",object.attributePath},
                    {"instanceState",int(object.instance.state)},{"instanceLine",object.instance.target.location.line},
                    {"stoppedAt",object.instance.stoppedAt},{"reason",object.instance.reason}};
                requestEvidence.kind = SclMmsQueryKind::DataSetRelations;
                auto dataSets = QtConcurrent::run([snapshot,requestEvidence] { return QuerySclMms(*snapshot,requestEvidence,nullptr); });
                QTRY_VERIFY_WITH_TIMEOUT(dataSets.isFinished(),15000);
                const auto relations = dataSets.result();
                QJsonArray dataSetRows;
                for (const auto& relation : relations.rows) {
                    QVERIFY(relation.dataSetRelation);
                    dataSetRows.append(QJsonObject{{"member",relation.key.node},{"ordinal",relation.key.ordinal},
                        {"line",relation.dataSetRelation->member.location.line},{"dataSet",relation.dataSet},
                        {"match",SclMmsReportMatchText(relation.reportMatch)},
                        {"users",relation.dataSetRelation->usage->users.size()}});
                }
                entry["dataSetEvidence"] = QJsonObject{{"total",relations.total},{"scanComplete",relations.dataSetCoverage.scanComplete},
                    {"uncertain",relations.dataSetCoverage.uncertainOccurrences},{"conclusion",int(relations.dataSetCoverage.conclusion)},
                    {"limited",relations.limited},{"rows",dataSetRows}};
            }
            if (residual && row.key.field == "cVal.ang.f") QVERIFY(evidence.first.rows.empty() && !evidence.first.limited);
            attributes.append(entry);
        }
        QVERIFY(hasMagnitude);
        if (voltage || residual) QVERIFY(hasAngle);
        if (magnitude) QVERIFY(!hasAngle);
        // Repeated selection measures the existing GUI detail path without a query scan.
        const auto count = std::min(20, int(queryResult.first.rows.size()));
        for (int r = 0; r < count; ++r) {
            const auto index = mms->Table()->model()->index(r, 0);
            if (!index.isValid() || index == mms->Table()->currentIndex()) continue;
            QSignalSpy done(details, &SclMmsQuerySession::Ready);
            QElapsedTimer timer; timer.start(); mms->Table()->setCurrentIndex(index);
            QVERIFY(!done.isEmpty() || done.wait(10000));
            QApplication::processEvents(); detailTimes.append(timer.nsecsElapsed() / 1000);
        }
        object["attributes"] = attributes; object["noInventedAngle"] = magnitude && !hasAngle;
        cases.append(object);
    }
    QVERIFY(sawPhsA && sawRes);
    if (iedName == "TEMPLATE") QVERIFY(sawVol);
    NodeId member = NoNode, dataSet = NoNode;
    for (int n = 0; n < int(doc.nodes.size()); ++n) {
        if (doc.nodes[n].name != "FCDA" || snapshot->browse.iedOwners[n] != ied
            || attr(n, "doName") != "PhV.phsA" || attr(n, "lnClass") != "MMXU"
            || attr(n, "lnInst") != "1" || (attr(n, "daName") != "cVal.mag.f" && !attr(n, "daName").isEmpty())) continue;
        member = n; dataSet = doc.nodes[n].parent; break;
    }
    QVERIFY(member != NoNode);
    SclMmsRequest members; members.kind = SclMmsQueryKind::Members; members.target = dataSet; members.scope.ied = ied;
    auto memberQuery = QtConcurrent::run([snapshot, members] { return QuerySclMms(*snapshot, members, nullptr); });
    QTRY_VERIFY_WITH_TIMEOUT(memberQuery.isFinished(), 15000);
    std::optional<SclMmsRow> memberRow;
    for (const auto& row : memberQuery.result().rows) if (row.key.node == member) memberRow = row;
    QVERIFY(memberRow);
    SclMmsView::Location memberLocation; memberLocation.snapshot = snapshot; memberLocation.viewIed = ied;
    memberLocation.request = members; memberLocation.selected = memberRow->key; memberLocation.inspectorVisible = true;
    mms->RestoreLocation(memberLocation);
    QTRY_VERIFY(mms->Result() && mms->Result()->request.kind == SclMmsQueryKind::Members && mms->Selected() == memberRow->key);
    QTRY_COMPARE(fieldIndex(QStringLiteral("MMS Item")).data().toString(), memberRow->item);
    QJsonArray memberCells, memberFields;
    const auto selectedMember = mms->Table()->currentIndex();
    for (int c = 0; c < mms->Table()->model()->columnCount(); ++c)
        memberCells.append(selectedMember.siblingAtColumn(c).data().toString());
    for (int r = 0; r < mms->Fields()->model()->rowCount(); ++r) {
        QJsonArray cells;
        for (int c = 0; c < mms->Fields()->model()->columnCount(); ++c) cells.append(mms->Fields()->model()->index(r,c).data().toString());
        memberFields.append(cells);
    }
    auto memberField = fieldIndex(QStringLiteral("@doName")); QVERIFY(memberField.isValid());
    mms->Fields()->setCurrentIndex(memberField); mms->Fields()->scrollTo(memberField);
    QTRY_VERIFY(sourceButton->isEnabled()); sourceButton->click();
    QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[member].source.line)));
    QJsonObject memberUi{{"member", member}, {"ordinal", memberRow->key.ordinal}, {"line", int(doc.nodes[member].source.line)},
        {"dataSet", dataSet}, {"dataSetLine", int(doc.nodes[dataSet].source.line)}, {"rawDoName", attr(member,"doName")},
        {"rawDaName", attr(member,"daName")}, {"item",memberRow->item}, {"cells",memberCells}, {"fields",memberFields},
        {"sourceLabel", sourceLabel->text()}};
    back->click(); QTRY_VERIFY(mms->Table()->isEnabled() && inspector->currentWidget() == mms->Inspector());
    const auto memberArtifact = qEnvironmentVariable("IEC61850_T100_SCREENSHOT");
    if (!memberArtifact.isEmpty()) QVERIFY(window.grab().save(memberArtifact + "-member.png"));
    // Inspect actual advanced data-set and object table column contracts.
    auto* taskSwitch = window.findChild<QPushButton*>("SclTaskViewSwitch");
    QVERIFY(taskSwitch); taskSwitch->click();
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* objects = window.findChild<QTableView*>("SclObjects");
    QVERIFY(sets && objects);
    auto* structure = window.findChild<QTreeView*>("SclTree"); QVERIFY(structure);
    structure->setCurrentIndex(Task7Acceptance::StructureIndex(structure, *snapshot, dataSet));
    QTRY_VERIFY(sets->isVisible() && sets->currentIndex().data(Qt::UserRole).toInt() == dataSet);
    const auto root = sets->currentIndex().siblingAtColumn(0); sets->expand(root);
    QTRY_VERIFY(sets->model()->rowCount(root) > 1);
    sets->scrollTo(root);
    const QJsonObject advanced{{"dataSetHeaders", headers(sets->model())}, {"objectHeaders", headers(objects->model())}};
    const auto artifact = qEnvironmentVariable("IEC61850_T100_SCREENSHOT");
    if (!artifact.isEmpty()) QVERIFY(window.grab().save(artifact + "-advanced.png"));
    QJsonObject result{{"version", 1}, {"readyMs", readyMs}, {"sha256", QString::fromLatin1(digest)},
        {"qt", qVersion()}, {"width", window.width()}, {"height", window.height()}, {"dpr", window.devicePixelRatioF()},
        {"workingSet", Memory().workingSet}, {"peakWorkingSet", Memory().peakWorkingSet},
        {"structureIndexUs", snapshot->browse.objectsBuildUs}, {"structureIndexRecords", snapshot->browse.objects.size()},
        {"structureIndexLimited", snapshot->browse.objectsLimited},
        {"cases", cases}, {"memberUi",memberUi}, {"advanced", advanced}, {"returnUs", returns}, {"detailUs", detailTimes}};
    QFile out(output); QVERIFY(out.open(QIODevice::WriteOnly));
    QVERIFY(out.write(QJsonDocument(result).toJson()) > 0); out.close();
    QCOMPARE(FileDigest(path), digest);
    std::weak_ptr<const SclSnapshot> weak = snapshot;
    memberLocation.snapshot.reset(); memberRow.reset(); memberQuery = {};
    snapshot.reset(); service->Close();
    QTRY_VERIFY_WITH_TIMEOUT(weak.expired(), 15000);
    qInfo() << "T10.0 local baseline written; source unchanged; snapshot released";
}
