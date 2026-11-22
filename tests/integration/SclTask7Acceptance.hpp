#pragma once

namespace Task7Acceptance
{
using namespace iec61850sim;
using namespace SclNavigationProbe;

inline QModelIndex StructureIndex(QTreeView* tree, const SclSnapshot& snapshot, Scl::NodeId node)
{
    std::vector<Scl::NodeId> ancestors;
    for (; node != Scl::NoNode; node = snapshot.analysis.document->nodes[node].parent)
        ancestors.push_back(node);
    QModelIndex index;
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
        index = tree->model()->index(*it == 0 ? 0 : snapshot.browse.branches[*it].row, 0, index);
    return index;
}

inline Scl::NodeId LargestDataSet(const SclSnapshot& snapshot)
{
    Scl::NodeId largest = Scl::NoNode;
    int members = 0;
    for (auto it = snapshot.browse.dataSetMembers.cbegin(); it != snapshot.browse.dataSetMembers.cend(); ++it)
        if (it->count > members || (it->count == members && it.key() < largest))
        {
            members = it->count;
            largest = it.key();
        }
    return largest;
}

inline bool CellVisible(QTreeView* view, const QModelIndex& index)
{
    const auto cell = view->visualRect(index);
    return index.isValid() && VisibleRect(view->viewport()).intersected(cell).height()
        >= std::min(cell.height(), view->fontMetrics().height()) && cell.height() > 0;
}
}

void MainWindowBaselineTest::SclRelationshipNavigation()
{
    using namespace Task7Acceptance;
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QFile fixture(QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    auto xml = fixture.readAll();
    xml.insert(xml.indexOf("<GSEControl"), "<SampledValueControl name='SvA' datSet='Shared'/>");
    xml.insert(xml.indexOf("<ReportControl"),
        "<DataSet name='Clash'/><DataSet name='Clash'/><ReportControl name='CandidateR' datSet='Clash'/>");
    xml.insert(xml.indexOf("</DataSet>"),
        "<FCDA ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Array' daName='stVal' fc='ST' ix='1'/>");
    xml.insert(xml.indexOf("<DO name=\"Unlisted\""), "<DO name='Array' type='Status' count='3'/>");
    QFile file(temp.filePath("Relationships.scd")); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(xml), xml.size()); file.close();

    MainWindow window;
    const auto width = qEnvironmentVariableIntValue("IEC61850_MMS_WIDTH");
    const auto height = qEnvironmentVariableIntValue("IEC61850_MMS_HEIGHT");
    window.resize(width > 0 ? width : 1280, height > 0 ? height : 800);
    const auto paletteMode = qEnvironmentVariable("IEC61850_RELATION_PALETTE");
    if (!paletteMode.isEmpty())
    {
        auto palette = window.palette();
        palette.setColor(QPalette::Window, Qt::black);
        palette.setColor(QPalette::WindowText, Qt::white);
        palette.setColor(QPalette::Base, Qt::black);
        palette.setColor(QPalette::AlternateBase, QColor("#171717"));
        palette.setColor(QPalette::Text, Qt::white);
        palette.setColor(QPalette::Button, QColor("#171717"));
        palette.setColor(QPalette::ButtonText, Qt::white);
        palette.setColor(QPalette::Highlight, paletteMode == "contrast" ? Qt::yellow : QColor("#2355a4"));
        palette.setColor(QPalette::HighlightedText, paletteMode == "contrast" ? Qt::black : Qt::white);
        window.setPalette(palette);
    }
    window.show();
    auto* service = window.findChild<SclAnalysisService*>();
    service->Open(file.fileName());
    QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    const auto snapshot = service->snapshot();
    const auto find = [&](const QString& tag, const QString& name) {
        const auto& doc = *snapshot->analysis.document;
        for (int id = 0; id < int(doc.nodes.size()); ++id)
            if (doc.nodes[id].name == tag && doc.attribute(id, "name") == name)
                return id;
        return Scl::NoNode;
    };
    auto* structure = window.findChild<QTreeView*>("SclTree");
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* links = window.findChild<QTableView*>("SclLinks");
    auto* candidates = window.findChild<QTableView*>("SclCandidates");
    auto* reference = window.findChild<QLabel*>("SclReferencePath");
    auto* summary = window.findChild<QLabel*>("SclRelationshipSummary");
    auto* usersButton = window.findChild<QPushButton*>("SclDataSetUsers");
    auto* users = window.findChild<QListWidget*>("SclDataSetUsersList");
    auto* userKind = window.findChild<QComboBox*>("SclDataSetUserKind");
    auto* dialog = window.findChild<QDialog*>("SclDataSetUsersDialog");
    auto* back = window.findChild<QPushButton*>("SclBack");
    auto* forward = window.findChild<QPushButton*>("SclForward");
    const auto selected = [&] { return structure->currentIndex().data(Qt::UserRole).toInt(); };
    const auto select = [&](Scl::NodeId id) { structure->setCurrentIndex(StructureIndex(structure, *snapshot, id)); };
    const auto dataSet = find("DataSet", "Shared");
    const auto goose = find("GSEControl", "GooseA");
    const auto sv = find("SampledValueControl", "SvA");
    select(dataSet);
    QTRY_COMPARE(sets->currentIndex().data(Qt::UserRole).toInt(), dataSet);
    inspector->setCurrentIndex(1);
    QVERIFY(summary->text().contains(QStringLiteral("包含 · 5 条")));
    usersButton->click();
    QTRY_COMPARE(users->count(), 4);
    userKind->setCurrentIndex(2);
    QTRY_COMPARE(users->count(), 1);
    users->setCurrentRow(0);
    QCOMPARE(users->currentItem()->data(Qt::UserRole).toInt(), goose);
    QTest::keyClick(users, Qt::Key_Return);
    QTRY_COMPARE(selected(), goose);
    QVERIFY(!dialog->isVisible());
    QVERIFY(summary->text().contains(QStringLiteral("查阅来源 · 数据集使用方")));
    QVERIFY(reference->text().contains(QStringLiteral("使用的数据集")));
    back->click();
    QTRY_VERIFY(dialog->isVisible() && users->currentItem());
    QCOMPARE(selected(), dataSet);
    QCOMPARE(userKind->currentIndex(), 2);
    QCOMPARE(users->currentItem()->data(Qt::UserRole).toInt(), goose);
    forward->click();
    QCOMPARE(selected(), goose);
    QVERIFY(!dialog->isVisible());
    back->click();
    QTRY_VERIFY(dialog->isVisible());
    userKind->setCurrentIndex(3);
    QTRY_COMPARE(users->count(), 1);
    users->setCurrentRow(0);
    QCOMPARE(users->currentItem()->data(Qt::UserRole).toInt(), sv);
    window.findChild<QPushButton*>("SclDataSetOpenUser")->click();
    QCOMPARE(selected(), sv);
    QVERIFY(reference->text().contains(QStringLiteral("使用的数据集")));
    QVERIFY(QMetaObject::invokeMethod(links, "activated", Q_ARG(QModelIndex, links->currentIndex())));
    QCOMPARE(selected(), dataSet);
    QVERIFY(summary->text().contains(QStringLiteral("查阅来源 · 使用的数据集")));

    const auto range = snapshot->browse.dataSetMembers.value(dataSet);
    const auto member = snapshot->browse.memberRows[range.first + range.count - 1];
    select(member);
    QVERIFY(summary->text().contains(QStringLiteral("所属 ·")));
    QVERIFY(reference->text().contains("doName Array"));
    QVERIFY(reference->text().contains("daName stVal"));
    QVERIFY(reference->text().contains("fc ST"));
    QVERIFY(reference->text().contains("ix 1"));
    window.findChild<QPushButton*>("SclRelationshipOwner")->click();
    QCOMPARE(selected(), dataSet);
    back->click();
    QCOMPARE(selected(), member);

    const auto ambiguous = find("ReportControl", "CandidateR");
    select(ambiguous);
    QVERIFY(reference->text().contains(QStringLiteral("有歧义")));
    QCOMPARE(links->currentIndex().data(Qt::UserRole).toInt(), Scl::NoNode);
    QCOMPARE(candidates->model()->rowCount(), 2);
    QVERIFY(QMetaObject::invokeMethod(links, "activated", Q_ARG(QModelIndex, links->currentIndex())));
    QCOMPARE(selected(), ambiguous);
    const auto candidate = candidates->model()->index(1, 0);
    const auto candidateNode = candidate.data(Qt::UserRole).toInt();
    candidates->setCurrentIndex(candidate);
    QVERIFY(QMetaObject::invokeMethod(candidates, "activated", Q_ARG(QModelIndex, candidate)));
    QCOMPARE(selected(), candidateNode);
    QVERIFY(summary->text().contains(QStringLiteral("查阅来源 · 候选依据")));
    back->click();
    QCOMPARE(selected(), ambiguous);
    QCOMPARE(candidates->currentIndex().data(Qt::UserRole).toInt(), candidateNode);
    QVERIFY(reference->text().contains(QStringLiteral("有歧义")));
    window.findChild<QToolButton*>("SclRelationshipHelp")->click();
    auto* guide = window.findChild<QMessageBox*>("SclRelationshipGuide");
    QVERIFY(guide && guide->isVisible() && guide->text().contains(QStringLiteral("查阅来源")));
    guide->close();
    qInfo() << "T7.4 relation navigation" << QGuiApplication::platformName() << window.size()
        << window.devicePixelRatioF() << "screens" << QGuiApplication::screens().size() << "palette" << paletteMode;
}

void MainWindowBaselineTest::SclTask7Cache()
{
    using namespace Task7Acceptance;
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    QFile fixture(QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    auto xml = fixture.readAll();
    const auto start = xml.indexOf("<FCDA ");
    const auto end = xml.indexOf("/>", start) + 2;
    const auto members = xml.mid(start, end - start).repeated(200);
    QByteArray extra;
    for (int i = 0; i < 11; ++i)
        extra += "<DataSet name='Cache" + QByteArray::number(i) + "'>" + members + "</DataSet>";
    xml.insert(xml.indexOf("<ReportControl"), extra);
    QFile file(temp.filePath("Cache.scd")); QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(xml), xml.size()); file.close();
    MainWindow window; window.resize(1280, 800); window.show();
    auto* service = window.findChild<SclAnalysisService*>();
    service->Open(file.fileName());
    QTRY_COMPARE(service->state(), SclAnalysisService::State::Ready);
    auto* tree = window.findChild<QTreeView*>("SclDataSets");
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* structure = window.findChild<QTreeView*>("SclTree");
    tabs->setCurrentWidget(tree->parentWidget());
    QTRY_COMPARE(tree->model()->rowCount(), 13);
    int largest = 0;
    for (int row = 1; row <= 11; ++row)
    {
        const auto root = tree->model()->index(row, 0);
        tree->expand(root);
        QTRY_COMPARE(tree->model()->rowCount(root), 200);
        largest = qMax(largest, tree->property("SclTreeCacheRows").toInt());
        QVERIFY(largest <= 1800);
        tree->collapse(root);
        QCOMPARE(structure->currentIndex().data(Qt::UserRole).toInt(), 0);
    }
    const auto evicted = tree->model()->index(1, 0);
    QCOMPARE(tree->model()->rowCount(evicted), 1);
    tree->expand(evicted);
    QTRY_COMPARE(tree->model()->rowCount(evicted), 200);
    QVERIFY(tree->property("SclTreeCacheRows").toInt() <= 1800);
    qInfo() << "T7.5 tree cache maximum (includes reserved load rows)" << largest;
}

void MainWindowBaselineTest::SclTask7Cycles()
{
    using namespace Task7Acceptance;
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty() || !QCoreApplication::arguments().contains("SclTask7Cycles"))
        QSKIP("Explicit T7.5 ten-cycle local acceptance");
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    MainWindow window; window.resize(1280, 800); window.show();
    auto* service = window.findChild<SclAnalysisService*>();
    auto* tabs = window.findChild<QTabWidget*>("SclTables");
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* close = window.findChild<QAction*>("CloseScl");
    auto* users = window.findChild<QListWidget*>("SclDataSetUsersList");
    auto* dialog = window.findChild<QDialog*>("SclDataSetUsersDialog");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* raw = window.findChild<QPlainTextEdit*>("SclSource");
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    const auto hash = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(); file.close();
    QJsonArray cycles;
    for (int run = 1; run <= 10; ++run)
    {
        QElapsedTimer elapsed; elapsed.start();
        service->Open(path);
        QVERIFY(Await([&] { return service->state() != SclAnalysisService::State::Loading; }, 120000));
        QCOMPARE(service->state(), SclAnalysisService::State::Ready);
        const auto readyMs = elapsed.elapsed();
        auto snapshot = service->snapshot();
        QCOMPARE(snapshot->analysis.sha256, hash);
        const auto dataSet = LargestDataSet(*snapshot);
        QVERIFY(dataSet != Scl::NoNode);
        tree->setCurrentIndex(StructureIndex(tree, *snapshot, dataSet));
        QTRY_COMPARE(sets->currentIndex().data(Qt::UserRole).toInt(), dataSet);
        const auto root = sets->currentIndex().siblingAtColumn(0);
        sets->expand(root);
        QTRY_COMPARE(sets->model()->rowCount(root), 201);
        const auto first = sets->model()->index(0, 0, root);
        sets->setCurrentIndex(first);
        window.findChild<QPushButton*>("SclDataSetSource")->click();
        QTRY_VERIFY(inspector->currentIndex() == 2 && !raw->toPlainText().isEmpty());
        window.findChild<QPushButton*>("SclDataSetUsers")->click();
        QTRY_VERIFY(dialog->isVisible() && !dialog->findChild<QLabel*>()->text().startsWith(QStringLiteral("正在")));
        if (users->count())
            users->setCurrentRow(0);
        const auto cacheRows = sets->property("SclTreeCacheRows").toInt();
        const auto working = Memory();
        std::weak_ptr<const SclSnapshot> released = snapshot;
        snapshot.reset();
        elapsed.restart();
        close->trigger();
        QVERIFY(!dialog->isVisible());
        QTRY_VERIFY(released.expired());
        QCOMPARE(sets->model()->rowCount(), 0);
        QCOMPARE(tabs->currentIndex(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto after = Memory();
        const QJsonObject cycle{{"cycle", run}, {"readyMs", readyMs}, {"closeMs", elapsed.elapsed()},
            {"treeCacheRows", cacheRows}, {"workingSet", working.workingSet},
            {"workingSetAfterClose", after.workingSet}, {"privateBytesAfterClose", after.privateBytes}};
        cycles.append(cycle);
        qInfo().noquote() << "T7.5 cycle" << QJsonDocument(cycle).toJson(QJsonDocument::Compact);
        QVERIFY(Save("Task7Cycles.json", {{"sha256", QString::fromLatin1(hash)}, {"cycles", cycles}}));
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(), hash);
}

void MainWindowBaselineTest::SclTask7TreeTiming()
{
    using namespace Task7Acceptance;
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty() || !QCoreApplication::arguments().contains("SclTask7TreeTiming"))
        QSKIP("Explicit T7.5 tree timing");
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("windows"));
    QTemporaryDir temp; QVERIFY(temp.isValid()); IsolateSettings(temp.path());
    MainWindow window; window.resize(1280, 800); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* service = window.findChild<SclAnalysisService*>();
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* tree = window.findChild<QTreeView*>("SclTree");
    auto* mode = window.findChild<QComboBox*>("SclDataSetMode");
    QElapsedTimer clock; clock.start();
    QTimer heartbeat;
    qint64 last = 0, loadingGap = 0, navigationGap = 0;
    bool loaded = false;
    heartbeat.setTimerType(Qt::PreciseTimer);
    heartbeat.setInterval(16);
    connect(&heartbeat, &QTimer::timeout, &window, [&] {
        const auto now = clock.elapsed();
        auto& gap = loaded ? navigationGap : loadingGap;
        gap = std::max(gap, now - last);
        last = now;
    });
    heartbeat.start();
    service->Open(path);
    QVERIFY(Await([&] { return service->state() != SclAnalysisService::State::Loading; }, 120000));
    QCOMPARE(service->state(), SclAnalysisService::State::Ready);
    const auto readyMs = clock.elapsed();
    loaded = true;
    auto snapshot = service->snapshot();
    const auto dataSet = LargestDataSet(*snapshot);
    const auto memberCount = snapshot->browse.dataSetMembers.value(dataSet).count;
    QVERIFY(memberCount > 200);
    tree->setCurrentIndex(StructureIndex(tree, *snapshot, dataSet));
    QTRY_COMPARE(sets->currentIndex().data(Qt::UserRole).toInt(), dataSet);
    QHash<QString, QList<qint64>> timings;
    const auto sample = [&](const QString& category, const QElapsedTimer& timer, int run) {
        if (run < 0)
            return;
        timings[category + "EndToEnd"].append(timer.nsecsElapsed() / 1000);
        timings[category + "Worker"].append(sets->property("SclBranchWorkerUs").toLongLong());
        timings[category + "Delivery"].append(sets->property("SclBranchApplyUs").toLongLong());
        timings[category + "Measure"].append(sets->property("SclTreeMeasureUs").toLongLong());
    };
    for (int run = -1; run < 30; ++run)
    {
        mode->setCurrentIndex(1);
        mode->setCurrentIndex(0);
        const auto root = sets->currentIndex().siblingAtColumn(0);
        QCOMPARE(root.data(Qt::UserRole).toInt(), dataSet);
        sets->scrollTo(root, QAbstractItemView::PositionAtTop);
        sets->horizontalScrollBar()->setValue(0);
        QVERIFY(Await([&] { return CellVisible(sets, root); }));
        const auto rect = sets->visualRect(root);
        const QPoint arrow(rect.left() - sets->indentation() / 2, rect.top() + 8);
        QVERIFY(VisibleRect(sets->viewport()).contains(arrow));
        QElapsedTimer timer; timer.start();
        QTest::mouseClick(sets->viewport(), Qt::LeftButton, {}, arrow);
        QVERIFY(Await([&] { return sets->model()->rowCount(root) == 201
            && CellVisible(sets, sets->model()->index(0, 0, root))
            && sets->property("SclTreeMeasureUs").toLongLong() > 0; }));
        sample("first", timer, run);
        const auto more = sets->model()->index(200, 0, root);
        sets->scrollTo(more, QAbstractItemView::PositionAtCenter);
        QVERIFY(Await([&] { return CellVisible(sets, more); }));
        timer.restart();
        QTest::mouseClick(sets->viewport(), Qt::LeftButton, {}, sets->visualRect(more).center());
        QVERIFY(Await([&] { return sets->model()->rowCount(root) == memberCount
            && CellVisible(sets, sets->model()->index(200, 0, root))
            && sets->property("SclBranchOffset").toInt() == 200
            && sets->property("SclTreeMeasureUs").toLongLong() > 0; }));
        sample("next", timer, run);
    }
    heartbeat.stop();
    QJsonObject stats;
    for (auto it = timings.cbegin(); it != timings.cend(); ++it)
        stats[it.key()] = Stats(it.value());
    const auto hash = snapshot->analysis.sha256;
    QVERIFY(Save("Task7TreeTiming.json", {{"sha256", QString::fromLatin1(hash)}, {"readyMs", readyMs},
        {"loadHeartbeatMs", loadingGap}, {"navigationHeartbeatMs", navigationGap}, {"members", memberCount},
        {"peakWorkingSet", Memory().peakWorkingSet}, {"timings", stats}}));
    qInfo().noquote() << "T7.5 tree timing P95 (us)"
        << stats.value("firstEndToEnd").toObject().value("p95").toInt()
        << stats.value("nextEndToEnd").toObject().value("p95").toInt();
    service->Close();
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(), hash);
}
