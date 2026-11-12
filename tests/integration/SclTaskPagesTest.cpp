#include "SclTaskPages.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTableView>
#include <QTextBrowser>
#include <QtTest>

using iec61850sim::SclTaskPages;

class SclTaskPagesTest : public QObject
{
    Q_OBJECT
private slots:
    void Walkthrough();
};

void SclTaskPagesTest::Walkthrough()
{
    // Deliberately synthetic. This walkthrough validates the navigation and
    // presentation contract before any product index populates these pages.
    const QStringList titles = {QStringLiteral("断路器位置"), QStringLiteral("A 相电流"),
        QStringLiteral("EventsGo"), QStringLiteral("SampleBus"), QStringLiteral("源对象缺失")};
    const QStringList details = {
        QStringLiteral("遥信建议 · Source/L/XCBR1.Pos\n用于 Events 的第 1、2 条成员"),
        QStringLiteral("遥测建议 · MMXU.A.phsA\n单位未声明，可查类型定义"),
        QStringLiteral("Events 数据集 · 第 1 条 Pos，第 2 条 Pos.stVal\nSink 只解析到对象，发布流关系未确认"),
        QStringLiteral("smpRate = 80\n采样模式和频率依据未声明"),
        QStringLiteral("此引用在工程内无法解析\n完整 XSD 未执行")};
    SclTaskPages widget;
    QStandardItemModel rows;
    QSortFilterProxyModel filter;
    filter.setSourceModel(&rows);
    filter.setFilterCaseSensitivity(Qt::CaseInsensitive);
    widget.SetModel(&filter);
    widget.SetScopeOptions({{-1, QStringLiteral("整个工程")}, {0, QStringLiteral("Source · 测试装置")},
                            {1, QStringLiteral("Empty · 无业务声明")}}, -1);
    widget.SetOverview(QStringLiteral("正在读取工程…"));
    auto* tabs = widget.findChild<QTabBar*>("SclTaskNavigation");
    auto* query = widget.findChild<QLineEdit*>("SclTaskFilter");
    auto* detail = widget.findChild<QTextBrowser*>("SclTaskDetail");
    auto* source = widget.findChild<QPushButton*>("SclTaskSource");
    auto* scope = widget.findChild<QComboBox*>("SclTaskIed");
    auto* status = widget.findChild<QLabel*>("SclTaskStatus");
    QVERIFY(tabs && query && detail && source && scope && status);
    int selectedScope = -1;
    const auto populate = [&](SclTaskPages::Page page) {
        rows.clear();
        widget.SetDetail({}, false);
        rows.setHorizontalHeaderLabels({QStringLiteral("对象"), QStringLiteral("解释")});
        if (selectedScope != 1 && page != SclTaskPages::Overview)
        {
            const int first = page == SclTaskPages::Signals ? 0 : page;
            const int last = page == SclTaskPages::Signals ? 1 : first;
            for (int i = first; i <= last; ++i)
                rows.appendRow({new QStandardItem(titles[i]), new QStandardItem(details[i])});
        }
        widget.SetStatus(rows.rowCount() ? QStringLiteral("请选择一行") : QStringLiteral("当前装置未声明"));
    };
    connect(&widget, &SclTaskPages::PageChanged, &widget, populate);
    connect(&widget, &SclTaskPages::ScopeChanged, &widget, [&](int id) {
        selectedScope = id;
        populate(static_cast<SclTaskPages::Page>(tabs->currentIndex()));
    });
    connect(&widget, &SclTaskPages::FilterChanged, &filter, &QSortFilterProxyModel::setFilterFixedString);
    connect(&widget, &SclTaskPages::Selected, &widget, [&](const QModelIndex& index) {
        widget.SetDetail(index.siblingAtColumn(1).data().toString(), index.isValid());
    });
    QSignalSpy open(&widget, &SclTaskPages::OpenSource);
    widget.resize(620, 500);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));
    tabs->setFocus();
    QTest::keyClick(tabs, Qt::Key_Right);
    QCOMPARE(tabs->currentIndex(), int(SclTaskPages::Signals));
    QCOMPARE(filter.rowCount(), 2);
    widget.Table()->setFocus();
    widget.Table()->setCurrentIndex(filter.index(0, 0));
    QVERIFY(detail->toPlainText().contains("XCBR1.Pos"));
    detail->selectAll();
    detail->copy();
    QTest::keyClick(widget.Table(), Qt::Key_Down);
    QVERIFY(detail->toPlainText().contains(QStringLiteral("单位未声明")));
    query->setText(QStringLiteral("不存在"));
    QCOMPARE(filter.rowCount(), 0);
    query->clear();
    for (int page = SclTaskPages::Goose; page <= SclTaskPages::Checks; ++page)
    {
        tabs->setFocus();
        QTest::keyClick(tabs, Qt::Key_Right);
        QCOMPARE(tabs->currentIndex(), page);
        widget.Table()->setCurrentIndex(filter.index(0, 0));
        QCOMPARE(detail->toPlainText(), details[page]);
    }
    QVERIFY(source->isEnabled());
    QTest::keyClick(source, Qt::Key_Space);
    QCOMPARE(open.size(), 1);
    widget.SetPage(SclTaskPages::Signals);
    scope->setCurrentIndex(2);
    QVERIFY(QMetaObject::invokeMethod(scope, "activated", Q_ARG(int, 2)));
    QCOMPARE(filter.rowCount(), 0);
    QVERIFY(status->text().contains(QStringLiteral("未声明")));
    QVERIFY(!source->isEnabled());
    QCOMPARE(widget.width(), 620);
}

QTEST_MAIN(SclTaskPagesTest)
#include "SclTaskPagesTest.moc"
