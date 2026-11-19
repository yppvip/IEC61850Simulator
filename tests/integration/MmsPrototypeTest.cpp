#include "MmsPrototypeWindow.hpp"
#include "SclTableInteraction.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QTreeWidget>
#include <QtTest>
#include <DockManager.h>
#include <DockWidget.h>

using namespace iec61850sim::prototype;
namespace {
template<class T> T* Find(MmsPrototypeWindow& window, const char* name) { return window.findChild<T*>(name); }
QModelIndex Row(QTableView* table, const QString& id, int column = 0)
{
    for (int i = 0; i < table->model()->rowCount(); ++i)
        if (table->model()->index(i, 0).data(Qt::UserRole).toString() == id) return table->model()->index(i, column);
    return {};
}
void Task(MmsPrototypeWindow& window, Page page)
{
    auto* tasks = Find<QTreeWidget>(window, "PrototypeTasks");
    if (!tasks->isVisible()) {
        auto* picker = Find<QComboBox>(window, "PrototypeTaskPicker");
        QVERIFY(picker->isVisible());
        picker->setCurrentIndex(picker->findData(int(page)));
        QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, picker->currentIndex())));
        QTRY_COMPARE(window.Session()->Current().page, page);
        return;
    }
    for (int i = 0; i < tasks->topLevelItemCount(); ++i) {
        auto* item = tasks->topLevelItem(i);
        if (Page(item->data(0, Qt::UserRole).toInt()) == page) {
            tasks->scrollToItem(item);
            QTest::mouseClick(tasks->viewport(), Qt::LeftButton, Qt::NoModifier, tasks->visualItemRect(item).center());
            QTRY_COMPARE(window.Session()->Current().page, page);
            return;
        }
    }
    QFAIL("Task is missing");
}
void Select(MmsPrototypeWindow& window, const QString& id, int column = 0)
{
    auto* table = Find<QTableView>(window, "PrototypeTable");
    const auto index = Row(table, id, column);
    QVERIFY(index.isValid());
    table->scrollTo(index);
    QTRY_VERIFY(!table->visualRect(index).intersected(table->viewport()->rect()).isEmpty());
    QTest::mouseClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(index).intersected(table->viewport()->rect()).center());
    QTRY_COMPARE(window.Session()->Current().selected, id);
}
void Click(MmsPrototypeWindow& window, const char* name)
{
    auto* button = Find<QPushButton>(window, name);
    QVERIFY(button && button->isEnabled() && button->isVisible());
    QTest::mouseClick(button, Qt::LeftButton);
}
void Capture(MmsPrototypeWindow& window, const QString& name)
{
    const auto directory = qEnvironmentVariable("MMS_PROTOTYPE_ARTIFACT_DIR");
    if (directory.isEmpty()) return;
    QVERIFY(QDir().mkpath(directory));
    QApplication::processEvents();
    QVERIFY(window.grab().save(QDir(directory).filePath(name + ".png")));
}
}

class MmsPrototypeTest final : public QObject {
    Q_OBJECT
private slots:
    void FourRoutes();
    void StatesAndHistory();
    void CopyAndFieldEvidence();
    void NarrowAndKeyboard();
    void FullValuesAndAnchors();
};

void MmsPrototypeTest::FourRoutes()
{
    MmsPrototypeWindow window; window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    qInfo() << "Prototype native size / DPR:" << window.size() << window.devicePixelRatioF();
    auto* session = window.Session();
    auto* table = Find<QTableView>(window, "PrototypeTable");
    Task(window, Page::Reports);
    QApplication::processEvents();
    qInfo() << "Prototype report viewport / panels:" << table->viewport()->size()
            << window.findChild<ads::CDockManager*>()->splitterSizes(window.findChild<ads::CDockWidget*>("PrototypeContentDock")->dockAreaWidget());
    QVERIFY(table->viewport()->height() >= table->verticalHeader()->defaultSectionSize());
    Select(window, "report");
    Capture(window, "Reports");
    Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().page, Page::Members);
    QCOMPARE(session->Current().parent, QStringLiteral("report"));
    QVERIFY(Find<QLabel>(window, "PrototypeBreadcrumb")->text().contains(QStringLiteral("数据集 Events")));
    QCOMPARE(table->model()->rowCount(), 2);
    Select(window, "member-2");
    QCOMPARE(Row(table, "member-2").data().toString(), QStringLiteral("2"));
    Click(window, "PrototypeSourceAction");
    QVERIFY(Find<QPlainTextEdit>(window, "PrototypeSource")->toPlainText().contains("<FCDA"));
    QCOMPARE(session->Current().selected, QStringLiteral("member-2"));
    Click(window, "PrototypeReturnSource");
    QCOMPARE(session->Current().parent, QStringLiteral("report"));
    QCOMPARE(table->currentIndex().data(Qt::UserRole).toString(), QStringLiteral("member-2"));
    Capture(window, "Members");
    Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().page, Page::Attributes);
    QCOMPARE(session->Current().selected, QStringLiteral("attribute"));

    auto* search = Find<QLineEdit>(window, "PrototypeSearch");
    search->setText("Unlisted"); QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(session->Current().page, Page::Search);
    Select(window, "unlisted"); Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().page, Page::Signals);
    QCOMPARE(session->Current().ied, QStringLiteral("DemoIED"));
    QVERIFY(Row(table, "unlisted", 2).data().toString().contains(QStringLiteral("未入数据集")));
    Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().selected, QStringLiteral("unlisted-attribute"));
    Find<QCheckBox>(window, "PrototypeTechnical")->setChecked(true);
    QVERIFY(!table->isColumnHidden(7));
    QCOMPARE(Row(table, "unlisted-attribute", 7).data().toString(), QStringLiteral("GGIO1$ST$Unlisted$stVal"));
    Capture(window, "Address");
    Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().page, Page::Signals);
    QCOMPARE(session->Current().selected, QStringLiteral("unlisted"));

    Task(window, Page::Clients); Select(window, "client-a");
    QVERIFY(Row(table, "client-a", 3).data().toString().contains(QStringLiteral("非在线订阅")));
    Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().selected, QStringLiteral("report"));
    Click(window, "PrototypeClients");
    QCOMPARE(session->Current().page, Page::Clients);
    QCOMPARE(table->model()->rowCount(), 2);

    Task(window, Page::Checks); Select(window, "issue"); Click(window, "PrototypeSourceAction");
    QVERIFY(Find<QPlainTextEdit>(window, "PrototypeSource")->toPlainText().contains("datSet=\"Absent\""));
    Click(window, "PrototypeReturnSource"); Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().selected, QStringLiteral("broken"));
    QVERIFY(!Find<QPushButton>(window, "PrototypeDrill")->isEnabled());
    Capture(window, "Error");
    Task(window, Page::Connections); Select(window, "endpoint");
    QCOMPARE(Row(table, "endpoint", 2).data(iec61850sim::SclTableInteraction::FullValueRole).toString(), QString{});
    QVERIFY(Find<QLabel>(window, "PrototypeBanner")->text().contains(QStringLiteral("合成示例")));
}

void MmsPrototypeTest::StatesAndHistory()
{
    MmsPrototypeWindow window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* session = window.Session();
    auto* table = Find<QTableView>(window, "PrototypeTable");
    Task(window, Page::Reports); Select(window, "unknown"); Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().parent, QStringLiteral("unknown"));
    QCOMPARE(table->model()->rowCount(), 2); // Shared dataset; report context must stay urUnknown.
    Click(window, "PrototypeBack"); Click(window, "PrototypeClients");
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(Find<QLabel>(window, "PrototypeSummary")->text().contains(QStringLiteral("未知")));
    Click(window, "PrototypeBack");
    auto* filter = Find<QLineEdit>(window, "PrototypeFilter");
    filter->setText("not-present");
    QVERIFY(!table->currentIndex().isValid());
    QVERIFY(Find<QLabel>(window, "PrototypeDetailTitle")->text().contains(QStringLiteral("筛选遮挡")));
    Task(window, Page::Checks); Click(window, "PrototypeBack");
    QCOMPARE(filter->text(), QStringLiteral("not-present"));
    QCOMPARE(session->Current().selected, QStringLiteral("unknown"));
    filter->clear();
    QCOMPARE(table->currentIndex().data(Qt::UserRole).toString(), QStringLiteral("unknown"));
    auto* scope = Find<QComboBox>(window, "PrototypeScope");
    scope->setCurrentIndex(scope->findData("EmptyIED"));
    QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, scope->currentIndex())));
    QCOMPARE(table->model()->rowCount(), 0);
    QVERIFY(session->Current().selected.isEmpty());
    QCOMPARE(session->Current().filter, QString{});
    QVERIFY(Find<QLabel>(window, "PrototypeSummary")->text().contains(QStringLiteral("不表示不支持 MMS")));
    Capture(window, "Empty");
    // Global search ignores EmptyIED, then explicit navigation changes scope.
    auto* search = Find<QLineEdit>(window, "PrototypeSearch"); search->setText("brStatus"); QTest::keyClick(search, Qt::Key_Return);
    Select(window, "report"); Click(window, "PrototypeDrill");
    QCOMPARE(session->Current().ied, QStringLiteral("DemoIED"));
    const auto oldSnapshot = session->Current().snapshot;
    session->SetSnapshot(Snapshot::Demo("demo-2"));
    QVERIFY(session->Current().snapshot != oldSnapshot);
    QVERIFY(!session->CanBack() && !session->CanForward());
    QVERIFY(session->Current().selected.isEmpty() && session->Current().source.isEmpty());
}

void MmsPrototypeTest::CopyAndFieldEvidence()
{
    MmsPrototypeWindow window; window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    Task(window, Page::Reports); Select(window, "report");
    auto* table = Find<QTableView>(window, "PrototypeTable");
    Find<QCheckBox>(window, "PrototypeTechnical")->setChecked(true);
    const auto index = Row(table, "report", 6); table->scrollTo(index);
    QApplication::clipboard()->setText(QStringLiteral("prototype-copy-sentinel"));
    QApplication::processEvents();
    QSignalSpy copied(QApplication::clipboard(), &QClipboard::dataChanged);
    QSignalSpy activated(table, &QTableView::activated);
    const auto point = table->visualRect(index).intersected(table->viewport()->rect()).center();
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QTRY_COMPARE(QApplication::clipboard()->text(), QStringLiteral("DemoStatus"));
    QTRY_COMPARE(copied.size(), 1); QCOMPARE(activated.size(), 0);
    QVERIFY(window.Session()->Current().source.isEmpty());
    Select(window, "unknown", 6);
    QTest::keyClick(table, Qt::Key_C, Qt::ControlModifier);
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("DemoStatus")); // Missing is not copied as a prompt.
    Select(window, "report");
    auto* fields = Find<QTableView>(window, "PrototypeFields");
    const auto field = Row(fields, QStringLiteral("触发配置"));
    fields->scrollTo(field);
    QTRY_VERIFY(fields->viewport()->height() >= fields->verticalHeader()->defaultSectionSize());
    QTest::mouseClick(fields->viewport(), Qt::LeftButton, Qt::NoModifier, fields->visualRect(field).intersected(fields->viewport()->rect()).center());
    Click(window, "PrototypeFieldSource");
    QCOMPARE(window.Session()->Current().source, QStringLiteral("trigger"));
    const auto preview = Find<QPlainTextEdit>(window, "PrototypeSource")->toPlainText();
    QVERIFY(preview.contains("<TrgOps"));
    QVERIFY(!preview.startsWith(QStringLiteral("第"))); // Line labels stay outside copied XML.
    QVERIFY(preview.split('\n').size() <= 7);
    QVERIFY(Find<QPlainTextEdit>(window, "PrototypeSource")->viewport()->height() >= window.fontMetrics().height() * 2);
    QTRY_VERIFY(Find<QPlainTextEdit>(window, "PrototypeSource")->viewport()->rect().intersects(
        Find<QPlainTextEdit>(window, "PrototypeSource")->cursorRect()));
    Capture(window, "Source");
    Click(window, "PrototypeReturnSource");
    QCOMPARE(fields->currentIndex().data(Qt::UserRole).toString(), QStringLiteral("触发配置"));
    QTest::keyClick(fields, Qt::Key_Return);
    QCOMPARE(window.Session()->Current().source, QStringLiteral("trigger"));
}

void MmsPrototypeTest::NarrowAndKeyboard()
{
    MmsPrototypeWindow window; window.resize(620, 560); window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    auto* picker = Find<QComboBox>(window, "PrototypeTaskPicker");
    qInfo() << "Prototype narrow size / DPR:" << window.size() << window.devicePixelRatioF();
    QVERIFY(picker->isVisible());
    QVERIFY(!Find<QTreeWidget>(window, "PrototypeTasks")->isVisible());
    picker->setCurrentIndex(picker->findData(int(Page::Reports)));
    QVERIFY(QMetaObject::invokeMethod(picker, "activated", Q_ARG(int, picker->currentIndex())));
    auto* table = Find<QTableView>(window, "PrototypeTable");
    table->setFocus(); QTest::keyClick(table, Qt::Key_Down);
    QVERIFY(!window.Session()->Current().selected.isEmpty());
    auto* drill = Find<QPushButton>(window, "PrototypeDrill");
    drill->setFocus(); QTest::keyClick(drill, Qt::Key_Space);
    QCOMPARE(window.Session()->Current().page, Page::Members);
    QCOMPARE(picker->currentText(), QStringLiteral("数据集成员"));
    auto* details = Find<QCheckBox>(window, "PrototypeDetails");
    details->setFocus(); QTest::keyClick(details, Qt::Key_Space);
    QVERIFY(!Find<QTableView>(window, "PrototypeFields")->isVisible());
    Capture(window, "NarrowCollapsed");
    QTest::keyClick(details, Qt::Key_Space);
    QVERIFY(Find<QTableView>(window, "PrototypeFields")->isVisible());
    Click(window, "PrototypeBack");
    QCOMPARE(window.Session()->Current().page, Page::Reports);
    QVERIFY(window.width() <= 620);
    auto* search = Find<QLineEdit>(window, "PrototypeSearch");
    search->setFocus(); QTest::keyClicks(search, "Unlisted"); QTest::keyClick(search, Qt::Key_Return);
    QCOMPARE(window.Session()->Current().page, Page::Search);
    Capture(window, "NarrowSearch");
    Click(window, "PrototypeResetLayout");
    QVERIFY(details->isChecked() && picker->isVisible());
    QVERIFY(!Find<QTreeWidget>(window, "PrototypeTasks")->isVisible());
}

void MmsPrototypeTest::FullValuesAndAnchors()
{
    MmsPrototypeWindow window;
    auto snapshot = std::make_shared<Snapshot>(*Snapshot::Demo("long-values"));
    const auto value = QStringLiteral("0001\n中文说明 😀 ") + QString(6000, 'W') + QStringLiteral("末尾");
    snapshot->records[0].cells[1] = Cell(value);
    const auto seed = snapshot->records[0];
    for (int i = 0; i < 80; ++i) {
        auto row = seed; row.id = QStringLiteral("extra-%1").arg(i); row.cells[1] = Cell(QStringLiteral("普通说明，多行换行验证 ") + QString(100, 'x'));
        snapshot->records.push_back(row);
    }
    window.Session()->SetSnapshot(snapshot);
    window.show(); QVERIFY(QTest::qWaitForWindowExposed(&window));
    Task(window, Page::Reports);
    auto* table = Find<QTableView>(window, "PrototypeTable");
    Select(window, "report", 1);
    const auto index = Row(table, "report", 1);
    QVERIFY(index.data().toString().contains(QStringLiteral("完整值")));
    QCOMPARE(index.data(iec61850sim::SclTableInteraction::FullValueRole).toString(), value);
    QTest::mouseDClick(table->viewport(), Qt::LeftButton, Qt::NoModifier, table->visualRect(index).center());
    QTRY_COMPARE(QApplication::clipboard()->text(), value);
    QVERIFY(window.Session()->Current().source.isEmpty());
    QCOMPARE(index.data(Qt::UserRole + 1).toString(), QStringLiteral("long-values"));
    const auto anchor = Row(table, "extra-35");
    table->scrollTo(anchor, QAbstractItemView::PositionAtTop);
    QTRY_COMPARE(table->rowAt(0), anchor.row());
    table->verticalScrollBar()->setValue(table->verticalScrollBar()->value() + 5);
    QTRY_COMPARE(window.Session()->Current().topId, QStringLiteral("extra-35"));
    const auto topId = window.Session()->Current().topId;
    Task(window, Page::Checks); Click(window, "PrototypeBack");
    QTRY_COMPARE(table->model()->index(table->rowAt(0), 0).data(Qt::UserRole).toString(), topId);
    QCOMPARE(window.Session()->Current().selected, QStringLiteral("report"));
}

QTEST_MAIN(MmsPrototypeTest)
#include "MmsPrototypeTest.moc"
