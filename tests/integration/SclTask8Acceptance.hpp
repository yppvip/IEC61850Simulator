#pragma once

namespace Task8Acceptance
{
enum class Gesture { Mouse, Keyboard, Hover };
struct MenuTest {
    bool opened = false, expanded = false, dispatched = false;
    QString preview;
    QStringList heads;
};
inline MenuTest ChooseMenu(QAbstractItemView* view, const QModelIndex& index, const QString& label,
    Gesture gesture, const std::function<void()>& invalidate = {}, const QString& leafName = {}) {
    MenuTest result;
    QPointer<QMenu> root, submenu;
    QTimer drive;
    int ticks = 0;
    QObject::connect(&drive, &QTimer::timeout, view, [&] {
        if (!root) root = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!root) return;
        result.opened = true;
        if (++ticks > 240) { root->close(); drive.stop(); return; }
        if (!submenu) {
            result.heads.clear();
            for (auto* action : root->actions()) {
                result.heads << action->text();
                if (action->text() != label) continue;
                submenu = action->menu();
                if (!submenu) {
                    drive.stop();
                    result.dispatched = true;
                    action->trigger();
                    if (root) root->close();
                    return;
                }
            }
            if (!submenu) return;
            auto* action = submenu->menuAction();
            if (gesture == Gesture::Hover) {
                const auto point = root->actionGeometry(action).center();
                QMouseEvent move(QEvent::MouseMove, point, root->mapToGlobal(point),
                    Qt::NoButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(root, &move);
            } else {
                root->setActiveAction(action);
                QTest::keyClick(root, Qt::Key_Right);
            }
            return;
        }
        if (!submenu->isVisible() || submenu->actions().isEmpty()) return;
        result.expanded = true;
        QPointer<QAction> leaf;
        for (auto* action : submenu->actions()) {
            if (!leafName.isEmpty() && action->text() != leafName) continue;
            leaf = action;
            break;
        }
        if (!leaf) { drive.stop(); root->close(); return; }
        result.preview = leaf->text();
        drive.stop();
        if (invalidate) {
            invalidate();
            if (leaf) { result.dispatched = true; leaf->trigger(); }
            if (root) root->close();
        } else if (gesture == Gesture::Keyboard) {
            submenu->setActiveAction(leaf);
            result.dispatched = true;
            QTest::keyClick(submenu, Qt::Key_Return);
        } else {
            result.dispatched = true;
            QTest::mouseClick(submenu, Qt::LeftButton, Qt::NoModifier, submenu->actionGeometry(leaf).center());
        }
        if (root) root->close();
    });
    drive.start(10);
    if (gesture == Gesture::Keyboard) {
        view->setFocus();
        QTest::keyClick(view, Qt::Key_F10, Qt::ShiftModifier);
    } else {
        const auto point = view->visualRect(index).center();
        QContextMenuEvent event(QContextMenuEvent::Mouse, point, view->viewport()->mapToGlobal(point));
        QApplication::sendEvent(view->viewport(), &event);
    }
    return result;
}
}

namespace Task8Acceptance
{
using namespace iec61850sim;
using Scl::NodeId;
using Scl::NoNode;

struct PhaseCase
{
    NodeId member = NoNode;
    NodeId ied = NoNode;
    SclMmsKey magnitude, angle;
};

std::optional<PhaseCase> FindPhaseCase(const SclSnapshot& snapshot)
{
    const auto& doc = *snapshot.analysis.document;
    int eligible = 0, limited = 0, delivered = 0;
    QHash<NodeId, SclMmsResult> scopes;
    for (const auto& ref : snapshot.analysis.references) {
        if (ref.kind != Scl::LinkKind::Fcda || ref.state != Scl::LinkState::Resolved
            || ref.logicalNode == NoNode || ref.arrayIndex >= 0 || ref.memberOrdinal > 200)
            continue;
        const auto node = ref.source;
        if (doc.attribute(node, "doName") != "PhV.phsA" || !doc.attribute(node, "daName").isEmpty()
            || doc.attribute(node, "fc") != "MX" || !snapshot.mmsIndex.reportsByDataSet.contains(doc.nodes[node].parent))
            continue;

        const auto owner = snapshot.browse.iedOwners[ref.logicalNode];
        if (owner == NoNode) continue;
        ++eligible;
        auto scoped = scopes.find(owner);
        if (scoped == scopes.end()) {
            SclMmsRequest request;
            request.kind = SclMmsQueryKind::Attributes;
            request.scope.ied = owner;
            request.text = QStringLiteral("A 相电压");
            scoped = scopes.insert(owner, QuerySclMms(snapshot, request, nullptr));
            limited += scoped->limited;
            delivered += int(scoped->rows.size());
        }
        const auto& result = *scoped;
        const auto leaf = [&](const QString& field) {
            return std::find_if(result.rows.cbegin(), result.rows.cend(), [&](const auto& row) {
                return row.key.node == ref.logicalNode && row.objectPath == "PhV.phsA" && row.key.field == field
                    && row.fc == "MX" && row.direct == SclMmsDirectState::Mapped;
            });
        };
        const auto magnitude = leaf(QStringLiteral("cVal.mag.f"));
        const auto angle = leaf(QStringLiteral("cVal.ang.f"));
        if (magnitude != result.rows.cend() && angle != result.rows.cend())
            return PhaseCase{node, owner, magnitude->key, angle->key};
    }
    qInfo() << "TASK8_LOCAL_PROBE eligible members / queried IEDs / limited scopes / delivered rows:"
        << eligible << scopes.size() << limited << delivered;
    return std::nullopt;
}

QByteArray FileDigest(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
}

void MainWindowBaselineTest::SclTask8LocalAcceptance()
{
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask8LocalAcceptance")))
        QSKIP("Run explicitly with a local read-only SCD");
    RunPhaseLocalAcceptance(false);
}

void MainWindowBaselineTest::SclTask9LocalAcceptance()
{
    if (!QCoreApplication::arguments().contains(QStringLiteral("SclTask9LocalAcceptance")))
        QSKIP("Run explicitly with a local read-only SCD");
    RunPhaseLocalAcceptance(true);
}

void MainWindowBaselineTest::RunPhaseLocalAcceptance(bool verifyTrace)
{
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty()) QSKIP("Set IEC61850_LOCAL_SCL to a local read-only SCD");
    using namespace Task8Acceptance;

    QTemporaryDir temp;
    QVERIFY(temp.isValid());
    IsolateSettings(temp.path());
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, "IEC61850Simulator", "IEC61850Simulator");
    settings.setValue("SclWorkspace/TaskView", true);
    const auto oldClipboard = QApplication::clipboard()->text();
    const auto restoreClipboard = qScopeGuard([oldClipboard] { QApplication::clipboard()->setText(oldClipboard); });
    const auto before = FileDigest(path);
    QVERIFY(!before.isEmpty());

    MainWindow window;
    window.resize(1280, 800);
    window.show();
    auto* service = window.findChild<SclAnalysisService*>();
    auto* mms = window.findChild<SclMmsView*>();
    auto* tasks = window.findChild<QTreeWidget*>("SclTaskTree");
    auto* scope = window.findChild<QComboBox*>("SclTaskIed");
    auto* filter = window.findChild<QLineEdit*>("SclMmsFilter");
    auto* status = window.findChild<QLabel*>("SclMmsStatus");
    auto* detailToggle = window.findChild<QToolButton*>("SclToggleDetails");
    auto* inspector = window.findChild<QTabWidget*>("SclInspector");
    auto* source = window.findChild<QPlainTextEdit*>("SclSource");
    auto* sourceLabel = window.findChild<QLabel*>("SclSourceLabel");
    auto* sourceButton = window.findChild<QPushButton*>("SclMmsSource");
    auto* open = window.findChild<QPushButton*>("SclMmsOpen");
    auto* back = window.findChild<QPushButton*>("SclMmsBack");
    auto* taskSwitch = window.findChild<QPushButton*>("SclTaskViewSwitch");
    auto* structure = window.findChild<QTreeView*>("SclTree");
    auto* sets = window.findChild<QTreeView*>("SclDataSets");
    auto* advancedScope = window.findChild<QComboBox*>("SclBrowseScope");
    auto* trace = window.findChild<QWidget*>("SclReferenceTrace");
    auto* evidence = window.findChild<QTreeWidget*>("SclTraceTree");
    auto* switchEvidence = window.findChild<QPushButton*>("SclTraceSwitch");
    auto* original = window.findChild<QPushButton*>("SclTraceSource");
    auto* traceBack = window.findChild<QPushButton*>("SclTraceBack");
    auto* choices = window.findChild<QComboBox*>("SclTraceChoices");
    QVERIFY(service && mms && tasks && scope && filter && status && detailToggle && inspector && source && sourceLabel
        && sourceButton && open && back && taskSwitch && structure && sets && advancedScope);
    if (verifyTrace) QVERIFY(trace && evidence && switchEvidence && original && traceBack && choices);
    const auto showTrace = [](auto* view, const auto& index, const QString& group) {
        const auto item = ChooseMenu(view, index, group, Gesture::Mouse, {}, QStringLiteral("查看关系"));
        return item.expanded && item.dispatched;
    };
    const auto branch = [&](const QString& label) -> QTreeWidgetItem* {
        if (evidence->topLevelItemCount() != 1) return nullptr;
        auto* root = evidence->topLevelItem(0);
        for (int i = 0; i < root->childCount(); ++i)
            if (root->child(i)->text(0) == label) return root->child(i);
        return nullptr;
    };

    QElapsedTimer elapsed;
    elapsed.start();
    service->Open(path);
    QTRY_COMPARE_WITH_TIMEOUT(service->state(), SclAnalysisService::State::Ready, 90000);
    const auto readyMs = elapsed.elapsed();
    auto snapshot = service->snapshot();
    QVERIFY(snapshot && snapshot->analysis.sha256 == before);
    auto fixture = QtConcurrent::run([snapshot] { return FindPhaseCase(*snapshot); });
    QTRY_VERIFY_WITH_TIMEOUT(fixture.isFinished(), 20000);
    const auto test = fixture.result();
    QVERIFY2(test, "No resolved A-phase report member with both scalar attributes on one IED query page");

    const auto& doc = *snapshot->analysis.document;
    const auto attr = [&](NodeId node, const char* key) { return doc.attribute(node, QString::fromLatin1(key)); };
    const auto ln = test->angle.node;
    const auto ld = doc.nodes[ln].parent;
    const auto lnName = attr(ln, "prefix") + attr(ln, "lnClass") + attr(ln, "inst");
    const auto rawDomain = attr(ld, "ldName");
    const auto domain = rawDomain.isEmpty() ? attr(test->ied, "name") + attr(ld, "inst") : rawDomain;
    const auto object = domain + '/' + lnName + QStringLiteral(".PhV.phsA");
    const auto memberItem = lnName + QStringLiteral("$MX$PhV$phsA");
    const auto iedIndex = scope->findData(test->ied);
    QVERIFY(iedIndex >= 0);
    scope->setCurrentIndex(iedIndex);
    QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, iedIndex)));
    const auto route = tasks->findItems(QStringLiteral("查找属性"), Qt::MatchExactly | Qt::MatchRecursive);
    QCOMPARE(route.size(), 1);
    tasks->setCurrentItem(route.front());
    const auto term = QStringLiteral("A 相电压");
    filter->setText(term);
    const auto page = [&](SclMmsQueryKind kind) {
        return mms->Result() && mms->Result()->request.kind == kind && mms->Table()->isEnabled();
    };
    QTRY_VERIFY_WITH_TIMEOUT(page(SclMmsQueryKind::Attributes) && mms->Result()->request.text == term, 10000);
    auto* table = mms->Table();
    const auto rowFor = [&](const SclMmsKey& key) {
        if (!mms->Result()) return -1;
        const auto& rows = mms->Result()->rows;
        const auto found = std::find_if(rows.cbegin(), rows.cend(), [&](const auto& row) { return row.key == key; });
        return found == rows.cend() ? -1 : int(found - rows.cbegin());
    };
    const auto visible = [](QAbstractItemView* view, const QModelIndex& index) {
        const auto cell = view->visualRect(index);
        return index.isValid() && SclNavigationProbe::VisibleRect(view->viewport()).intersected(cell).height()
            >= std::min(cell.height(), view->fontMetrics().height()) && cell.height() > 0;
    };
    const auto field = [&](const QString& text) {
        auto* model = mms->Fields()->model();
        for (int row = 0; row < model->rowCount(); ++row)
            if (model->index(row, 0).data().toString() == text) return model->index(row, 1);
        return QModelIndex{};
    };
    const auto copy = [](QAbstractItemView* view, const QModelIndex& index, const QString& name, const QString& expected) {
        QApplication::clipboard()->setText(QStringLiteral("Task8 T8.4 clipboard marker"));
        const auto menu = ChooseMenu(view, index, name, Gesture::Mouse);
        return menu.expanded && menu.dispatched && QApplication::clipboard()->text() == expected;
    };
    const auto count = mms->Result()->total;
    const bool searchLimited = mms->Result()->limited;
    const bool searchWorkBudget = mms->Result()->limitation.contains(QStringLiteral("查询工作预算"));
    QCOMPARE(mms->Result()->request.scope.ied, test->ied);
    if (searchLimited) QVERIFY(status->text().contains(QStringLiteral("结果不完整")));
    if (!detailToggle->isChecked()) detailToggle->click();
    inspector->setCurrentWidget(mms->Inspector());

    for (const auto& key : {test->magnitude, test->angle}) {
        const int row = rowFor(key);
        QVERIFY(row >= 0);
        const auto index = table->model()->index(row, 0);
        const auto expectedItem = memberItem + '$' + QString(key.field).replace('.', '$');
        const auto expectedObject = object + '.' + key.field;
        const auto entry = mms->Result()->rows[row];
        QVERIFY(entry.domain == domain && entry.item == expectedItem && entry.objectReference == expectedObject);
        QVERIFY(entry.fc == "MX" && entry.online == SclMmsOnlineState::Unverified);
        QVERIFY(doc.nodes[key.declaration].name == "BDA" && attr(key.declaration, "name") == "f");
        QVERIFY(doc.nodes[entry.fcEvidence.node].name == "DA" && attr(entry.fcEvidence.node, "fc") == "MX");
        table->setCurrentIndex(index);
        table->scrollTo(index);
        QTRY_VERIFY(visible(table, index) && field(QStringLiteral("MMS Item")).data().toString() == expectedItem);
        QVERIFY(field(QStringLiteral("在线读取")).data().toString() == QStringLiteral("未验证"));
        QVERIFY(copy(table, index, QStringLiteral("对象引用"), expectedObject));
        QVERIFY(copy(table, index, QStringLiteral("MMS Item"), expectedItem));
        if (key == test->angle) {
            if (verifyTrace) {
                QVERIFY(showTrace(table, index, QStringLiteral("MMS Item")));
                QTRY_VERIFY(trace->isVisible() && branch(QStringLiteral("LNName")) && branch(QStringLiteral("功能约束 FC")));
                QVERIFY(evidence->topLevelItem(0)->text(1) == QApplication::clipboard()->text());
                QVERIFY(branch(QStringLiteral("LNName"))->text(1) == lnName);
                if (!switchEvidence->isChecked()) switchEvidence->click();
                evidence->setCurrentItem(branch(QStringLiteral("LNName")));
                original->click();
                QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[ln].source.line)));
                evidence->expandItem(branch(QStringLiteral("功能约束 FC")));
                QTRY_VERIFY(branch(QStringLiteral("功能约束 FC"))->childCount() == 1);
                evidence->setCurrentItem(branch(QStringLiteral("功能约束 FC"))->child(0));
                original->click();
                QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[entry.fcEvidence.node].source.line)));
                QTRY_VERIFY(SclNavigationProbe::VisibleRect(source->viewport()).intersects(source->cursorRect()));
                traceBack->click();
                QTRY_VERIFY(page(SclMmsQueryKind::Attributes) && mms->Selected() == test->angle);

                auto angleIndex = table->model()->index(rowFor(test->angle), 0);
                QVERIFY(showTrace(table, angleIndex, QStringLiteral("MMS Domain")));
                QTRY_VERIFY(branch(QStringLiteral("LDName")));
                QVERIFY(branch(QStringLiteral("LDName"))->text(1) == domain);
                if (!switchEvidence->isChecked()) switchEvidence->click();
                evidence->expandItem(branch(QStringLiteral("LDName")));
                QTRY_VERIFY(branch(QStringLiteral("LDName"))->childCount() >= 2);
                const auto* nameSource = branch(QStringLiteral("LDName"))->child(0);
                QVERIFY(nameSource->text(1) == (rawDomain.isEmpty() ? attr(test->ied, "name") : rawDomain));
                traceBack->click();
                QTRY_VERIFY(page(SclMmsQueryKind::Attributes) && mms->Selected() == test->angle);

                angleIndex = table->model()->index(rowFor(test->angle), 0);
                QVERIFY(showTrace(table, angleIndex, QStringLiteral("类型引用")));
                QTRY_VERIFY(trace->isVisible());
                if (choices->isVisible()) choices->setCurrentIndex(1);
                QTRY_VERIFY(branch(QStringLiteral("类型声明")));
                QVERIFY(evidence->topLevelItem(0)->childCount() >= 2);
                QVERIFY(evidence->topLevelItem(0)->child(1)->text(2) == QStringLiteral("引用定义"));
                traceBack->click();
                QTRY_VERIFY(page(SclMmsQueryKind::Attributes) && mms->Selected() == test->angle);
            }
            const auto detail = field(QStringLiteral("MMS Item"));
            mms->Fields()->setCurrentIndex(detail);
            mms->Fields()->scrollTo(detail);
            QTRY_VERIFY(visible(mms->Fields(), detail) && sourceButton->isEnabled());
            sourceButton->click();
            const auto line = doc.nodes[key.declaration].source.line;
            QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(line)));
            QTRY_VERIFY(SclNavigationProbe::VisibleRect(source->viewport()).intersects(source->cursorRect()));
        }
    }

    open->click();
    QTRY_VERIFY_WITH_TIMEOUT(page(SclMmsQueryKind::AttributeReports), 10000);
    const auto reportRow = [&] {
        for (int i = 0; i < int(mms->Result()->rows.size()); ++i) {
            const auto& row = mms->Result()->rows[i];
            if (row.key.node == test->member && row.reportMatch == SclMmsReportMatch::ObjectCoverage) return i;
        }
        return -1;
    }();
    QVERIFY(reportRow >= 0 && !mms->Result()->limited);
    const auto reportUses = mms->Result()->total;
    const auto reportEvidence = mms->Result()->rows[reportRow];
    QVERIFY(reportEvidence.report != NoNode && reportEvidence.dataSet == doc.nodes[test->member].parent);
    QVERIFY(reportEvidence.item == memberItem && reportEvidence.domain == domain);
    const auto reportIndex = table->model()->index(reportRow, 0);
    table->setCurrentIndex(reportIndex);
    table->scrollTo(reportIndex);
    QTRY_VERIFY(visible(table, reportIndex));
    QTRY_VERIFY(field(QStringLiteral("报告匹配")).data().toString() == QStringLiteral("DO 级覆盖"));
    QVERIFY(field(QStringLiteral("在线报告")).data().toString() == QStringLiteral("未验证"));
    QVERIFY(copy(table, reportIndex, QStringLiteral("对象引用"), object));
    QVERIFY(copy(table, reportIndex, QStringLiteral("MMS Item"), memberItem));
    if (verifyTrace) {
        QVERIFY(showTrace(table, reportIndex, QStringLiteral("功能约束")));
        QTRY_VERIFY(branch(QStringLiteral("FCDA 声明的 FC")));
        QVERIFY(branch(QStringLiteral("FCDA 声明的 FC"))->text(1) == "MX");
        traceBack->click();
        QTRY_VERIFY(page(SclMmsQueryKind::AttributeReports));
    }
    back->click();
    QTRY_VERIFY(page(SclMmsQueryKind::Attributes) && mms->Selected() == test->angle);
    QCOMPARE(scope->currentData().toInt(), test->ied);

    taskSwitch->click();
    const auto ds = doc.nodes[test->member].parent;
    structure->setCurrentIndex(Task7Acceptance::StructureIndex(structure, *snapshot, ds));
    QTRY_VERIFY(sets->isVisible() && sets->currentIndex().isValid() && sets->currentIndex().data(Qt::UserRole).toInt() == ds);
    const auto root = sets->currentIndex().siblingAtColumn(0);
    sets->expand(root);
    const auto memberIndex = [&] {
        for (int row = 0; row < sets->model()->rowCount(root); ++row) {
            const auto index = sets->model()->index(row, 0, root);
            if (index.data(Qt::UserRole).toInt() == test->member) return index;
        }
        return QModelIndex{};
    };
    QTRY_VERIFY(memberIndex().isValid());
    auto member = memberIndex();
    sets->scrollTo(member);
    QTRY_VERIFY(visible(sets, member));
    QVERIFY(copy(sets, member, QStringLiteral("对象引用"), object));
    QVERIFY(copy(sets, member, QStringLiteral("MMS Item"), memberItem));
    QVERIFY(copy(sets, member, QStringLiteral("原始 FCDA 声明"), snapshot->analysis.source->openingTag(doc.nodes[test->member].source)));
    if (verifyTrace) {
        QVERIFY(showTrace(sets, member, QStringLiteral("对象引用")));
        QTRY_VERIFY(branch(QStringLiteral("FCDA 成员声明")));
        QVERIFY(evidence->topLevelItem(0)->text(1) == object);
        QVERIFY(!branch(QStringLiteral("属性 DA")));
        if (!switchEvidence->isChecked()) switchEvidence->click();
        evidence->setCurrentItem(branch(QStringLiteral("FCDA 成员声明")));
        original->click();
        QTRY_VERIFY(sourceLabel->text().contains(QStringLiteral("第 %1 行（").arg(doc.nodes[test->member].source.line)));
        traceBack->click();
        QTRY_VERIFY(sets->isVisible() && memberIndex().isValid());
        member = memberIndex();
    }
    QVERIFY(ChooseMenu(sets, member, QStringLiteral("在 MMS 中查阅属性"), Gesture::Mouse).dispatched);
    QTRY_VERIFY(page(SclMmsQueryKind::Attributes) && mms->Result()->request.target == test->member);
    const auto angleRow = rowFor(test->angle);
    QVERIFY(angleRow >= 0);
    const auto aggregateAttributes = mms->Result()->total;
    QVERIFY(!mms->Result()->limited);
    const auto angleIndex = table->model()->index(angleRow, 0);
    table->setCurrentIndex(angleIndex);
    table->scrollTo(angleIndex);
    if (!SclNavigationProbe::Await([&] { return visible(table, angleIndex); }, 4000)) {
        qInfo() << "Reopened attribute grid geometry" << table->isVisible() << table->viewport()->size()
            << SclNavigationProbe::VisibleRect(table->viewport()) << table->visualRect(angleIndex);
        for (auto* widget = table->viewport(); widget && widget != &window; widget = widget->parentWidget())
            qInfo() << "Attribute ancestor" << widget->metaObject()->className() << widget->objectName() << widget->geometry();
    }
    QVERIFY(visible(table, angleIndex));
    QVERIFY(copy(table, angleIndex, QStringLiteral("对象引用"), object + '.' + test->angle.field));
    QVERIFY(copy(table, angleIndex, QStringLiteral("MMS Item"), memberItem + QStringLiteral("$cVal$ang$f")));
    back->click();
    QTRY_VERIFY(sets->isVisible() && sets->currentIndex().data(Qt::UserRole).toInt() == test->member);
    QCOMPARE(advancedScope->currentData().toInt(), test->ied);

    const auto ieds = snapshot->browse.ieds.size();
    std::weak_ptr<const SclSnapshot> released = snapshot;
    snapshot.reset();
    service->Close();
    QTRY_VERIFY_WITH_TIMEOUT(released.expired(), 10000);
    QCOMPARE(FileDigest(path), before);
    qInfo().noquote() << (verifyTrace ? "TASK9_LOCAL" : "TASK8_LOCAL") << QJsonDocument(QJsonObject{
        {"sha256", QString::fromLatin1(before)}, {"ieds", qint64(ieds)}, {"readyMs", readyMs},
        {"scopedPhaseHits", count}, {"searchLimited", searchLimited}, {"searchWorkBudget", searchWorkBudget},
        {"reportUsesForAngle", reportUses},
        {"aggregateMemberAttributes", aggregateAttributes},
        {"explicitDomain", !rawDomain.isEmpty()}, {"sameLeafAndMemberCopies", true}, {"snapshotReleased", true},
        {"referenceTraceVerified", verifyTrace},
        {"width", window.width()}, {"height", window.height()}, {"dpr", window.devicePixelRatioF()}
    }).toJson(QJsonDocument::Compact);
}
