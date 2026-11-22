#include "SclAnalysisService.hpp"
#include "SclHierarchyQuery.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QtTest>
#include <algorithm>

using namespace iec61850sim;
using namespace Scl;

namespace
{
QByteArray Fixture()
{
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task7Navigation.scd"));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

SclHierarchyRequest Children(SclHierarchyKind kind, NodeId dataSet)
{
    SclHierarchyRequest request;
    request.kind = kind;
    request.dataSet = dataSet;
    return request;
}

NodeId Find(const Document& doc, const QString& tag, const QString& name = {})
{
    for (int id = 0; id < int(doc.nodes.size()); ++id)
        if (doc.nodes[id].isScl() && doc.nodes[id].name == tag
            && (name.isNull() || doc.attribute(id, "name") == name))
            return id;
    return NoNode;
}

std::shared_ptr<const SclSnapshot> OpenFixture(const QByteArray& xml)
{
    QTemporaryFile file(QDir::temp().filePath(QStringLiteral("SclHierarchy-XXXXXX.scd")));
    if (!file.open() || file.write(xml) != xml.size())
        return {};
    file.close();
    SclAnalysisService service;
    QSignalSpy changed(&service, &SclAnalysisService::Changed);
    service.Open(file.fileName());
    while (service.state() == SclAnalysisService::State::Loading)
        if (!changed.wait(10000))
            return {};
    return service.snapshot();
}

QByteArray HashFile(const QString& path)
{
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return file.open(QIODevice::ReadOnly) && hash.addData(&file) ? hash.result().toHex() : QByteArray{};
}
}

class SclHierarchyQueryTest : public QObject
{
    Q_OBJECT

private slots:
    void PagingAndIdentity_data()
    {
        QTest::addColumn<int>("memberCount");
        QTest::newRow("253-members") << 253;
        QTest::newRow("421-members") << 421;
    }

    void PagingAndIdentity()
    {
        QFETCH(int, memberCount);
        auto xml = Fixture();
        const auto start = xml.indexOf("<FCDA ");
        const auto end = xml.indexOf("/>", start) + 2;
        xml.insert(end, xml.mid(start, end - start).repeated(memberCount - 4));
        const auto snapshot = OpenFixture(xml);
        QVERIFY(snapshot);

        SclHierarchyRequest list;
        list.pageSize = 1;
        list.text = "shared";
        const auto first = QuerySclHierarchy(snapshot, list, nullptr);
        QCOMPARE(first.total, 2);
        QVERIFY(first.hasMore);
        QCOMPARE(first.rows.size(), size_t(1));
        QCOMPARE(first.rows.front().declaredMembers, memberCount);
        const auto primary = first.rows.front().node;
        QCOMPARE(first.rows.front().path, QStringLiteral("Publisher/AP1/Server/LD0/LLN0/Shared"));
        list.offset = 1;
        const auto second = QuerySclHierarchy(snapshot, list, nullptr);
        const auto other = second.rows.front().node;
        QVERIFY(other != primary);
        QCOMPARE(second.rows.front().path, QStringLiteral("Publisher/AP1/Server/LD1/LLN0/Shared"));
        list.offset = 0;
        list.locate = other;
        const auto locatedDataSet = QuerySclHierarchy(snapshot, list, nullptr);
        QVERIFY(locatedDataSet.locateFound);
        QCOMPARE(locatedDataSet.offset, 1);
        QCOMPARE(locatedDataSet.rows.front().node, other);

        auto request = Children(SclHierarchyKind::Members, primary);
        request.pageSize = 300;
        const auto page1 = QuerySclHierarchy(snapshot, request, nullptr);
        QCOMPARE(page1.rows.size(), size_t(200));
        QCOMPARE(page1.total, memberCount);
        QVERIFY(page1.hasMore);
        QVERIFY(page1.rows[0].node != page1.rows[1].node);
        QCOMPARE(page1.rows[0].member->target, page1.rows[1].member->target);
        QCOMPARE(page1.rows[0].member->ordinal, 1);
        QCOMPARE(page1.rows[1].member->ordinal, 2);
        request.offset = 200;
        const auto page2 = QuerySclHierarchy(snapshot, request, nullptr);
        QCOMPARE(page2.rows.size(), size_t(std::min(200, memberCount - 200)));
        QCOMPARE(page2.rows.front().member->ordinal, 201);
        QCOMPARE(page2.rows.back().member->ordinal, std::min(400, memberCount));
        QCOMPARE(page2.hasMore, memberCount > 400);
        if (page2.hasMore)
        {
            request.offset = 400;
            const auto page3 = QuerySclHierarchy(snapshot, request, nullptr);
            QCOMPARE(page3.rows.size(), size_t(memberCount - 400));
            QCOMPARE(page3.rows.front().member->ordinal, 401);
            QCOMPARE(page3.rows.back().member->ordinal, memberCount);
            QVERIFY(!page3.hasMore);
        }
        request.locate = page2.rows[17].node;
        request.offset = 0;
        request.text = "IND1.STVAL";
        const auto located = QuerySclHierarchy(snapshot, request, nullptr);
        QVERIFY(located.locateFound);
        QCOMPARE(located.offset, 200);
        QCOMPARE(located.total, memberCount - 2);
        QCOMPARE(located.scopeTotal, memberCount);
        QCOMPARE(located.rows[17].member->ordinal, 218);
        request.dataSet = other;
        const auto wrongParent = QuerySclHierarchy(snapshot, request, nullptr);
        QVERIFY(!wrongParent.locateFound);
        QVERIFY(wrongParent.rows.empty());
        request.dataSet = primary;
        request.text = "none of these fields";
        const auto hidden = QuerySclHierarchy(snapshot, request, nullptr);
        QVERIFY(!hidden.locateFound);
        QVERIFY(hidden.rows.empty());
        QCOMPARE(hidden.scopeTotal, memberCount);
    }

    void EvidenceAndUsers()
    {
        auto xml = Fixture();
        xml.replace("<SCL xmlns=", "<SCL xmlns:x='urn:synthetic-extension' xmlns=");
        xml.replace("<DataSet name=\"Shared\" desc=", "<x:DataSet name='Shared'/><Private><DataSet name='Hidden'/></Private><DataSet name=\"Shared\" desc=");
        xml.replace(xml.indexOf("</DataSet>"), 10, R"(
          <x:FCDA doName="Fake" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Array" daName="stVal" ix="1" fc="ST"/>
          <FCDA ldInst="LD0" lnClass="GGIO" lnInst="1" doName="Ambiguous" daName="stVal" fc="ST"/>
        </DataSet>
        <DataSet name="Clash"/><DataSet name="Clash"/><DataSet name="Empty"/>
        <ReportControl name="AmbiguousUser" datSet="Clash"/>
        <ReportControl name="MissingUser" datSet="Missing"/>
        <x:ReportControl name="FakeUser" datSet="Shared"/>
        <SampledValueControl name="SvA" datSet="Shared"/>)");
        xml.replace("<DO name=\"Unlisted\" type=\"Status\"/>",
            "<DO name='Array' type='Status' count='3'/><DO name='Ambiguous' type='Status'/><DO name='Ambiguous' type='Status'/>");
        const auto snapshot = OpenFixture(xml);
        QVERIFY(snapshot);
        const auto& analysis = snapshot->analysis;
        const auto list = QuerySclHierarchy(snapshot, {}, nullptr);
        QCOMPARE(list.total, 5);
        const auto primary = list.rows.front().node;
        const auto members = QuerySclHierarchy(snapshot, Children(SclHierarchyKind::Members, primary), nullptr);
        QCOMPARE(members.total, 6);
        const auto& da = *members.rows[0].member;
        const auto& object = *members.rows[2].member;
        QCOMPARE(da.state, LinkState::Resolved);
        QCOMPARE(object.state, LinkState::Resolved);
        QCOMPARE(analysis.document->nodes[da.target].name, QStringLiteral("DA"));
        QCOMPARE(analysis.document->nodes[object.target].name, QStringLiteral("DO"));
        QVERIFY(object.daName.isNull());
        QCOMPARE(object.fc, QStringLiteral("ST"));
        QVERIFY(da.description.ObjectTitle());
        QCOMPARE(da.description.ObjectTitle()->value, QStringLiteral("合成位置"));
        QCOMPARE(members.rows[3].member->state, LinkState::Missing);
        QCOMPARE(members.rows[3].member->target, NoNode);
        const auto& array = *members.rows[4].member;
        QCOMPARE(array.ordinal, 5);
        QCOMPARE(array.state, LinkState::Resolved);
        QCOMPARE(array.ix, QStringLiteral("1"));
        QCOMPARE(array.arrayIndex, 1);
        QCOMPARE(array.fc, QStringLiteral("ST"));
        const auto& ambiguous = members.rows[5];
        QCOMPARE(ambiguous.member->state, LinkState::Ambiguous);
        QCOMPARE(ambiguous.member->target, NoNode);
        QCOMPARE(analysis.references.at(ambiguous.reference).candidates.size(), 2);

        auto request = Children(SclHierarchyKind::Users, primary);
        const auto users = QuerySclHierarchy(snapshot, request, nullptr);
        QCOMPARE(users.total, 4);
        QSet<QString> names;
        for (const auto& row : users.rows)
        {
            names.insert(row.name);
            QCOMPARE(row.dataSet, primary);
            QVERIFY(row.path.startsWith("Publisher/AP1/Server/LD0/LLN0/"));
            const auto& ref = analysis.references.at(row.reference);
            QCOMPARE(ref.source, row.node);
            QCOMPARE(ref.state, LinkState::Resolved);
            QCOMPARE(ref.candidates, QList<NodeId>{primary});
        }
        QCOMPARE(names, (QSet<QString>{"ReportA", "ReportB", "GooseA", "SvA"}));
        request.userKind = SclHierarchyRowKind::Sv;
        QCOMPARE(QuerySclHierarchy(snapshot, request, nullptr).rows.front().name, QStringLiteral("SvA"));
        request.userKind.reset();
        request.dataSet = list.rows.back().node;
        const auto otherUsers = QuerySclHierarchy(snapshot, request, nullptr);
        QCOMPARE(otherUsers.total, 1);
        QCOMPARE(otherUsers.rows.front().name, QStringLiteral("ReportOtherScope"));
        request.dataSet = Find(*analysis.document, "DataSet", "Clash");
        QCOMPARE(QuerySclHierarchy(snapshot, request, nullptr).total, 0);
        const auto empty = QuerySclHierarchy(snapshot,
            Children(SclHierarchyKind::Members, Find(*analysis.document, "DataSet", "Empty")), nullptr);
        QCOMPARE(empty.parent, SclHierarchyParent::Available);
        QCOMPARE(empty.total, 0);
        const auto unavailable = QuerySclHierarchy(snapshot, Children(SclHierarchyKind::Members,
            Find(*analysis.document, "DataSet", "Hidden")), nullptr);
        QCOMPARE(unavailable.parent, SclHierarchyParent::Unavailable);
    }

    void BudgetAndScope()
    {
        auto xml = Fixture();
        QByteArray controls;
        for (int i = 0; i < 513; ++i)
            controls += "<ReportControl name='Bulk" + QByteArray::number(i) + "' datSet='Shared'/>";
        xml.insert(xml.indexOf("<ReportControl"), controls);
        const auto snapshot = OpenFixture(xml);
        QVERIFY(snapshot);
        const auto& doc = *snapshot->analysis.document;
        const auto dataSet = Find(doc, "DataSet", "Shared");
        const auto publisher = Find(doc, "IED", "Publisher");
        const auto sink = Find(doc, "IED", "Sink");
        const auto ap = Find(doc, "AccessPoint");
        const auto server = Find(doc, "Server");
        const auto sinkAp = doc.nodes[sink].firstChild;
        const auto sinkServer = doc.nodes[sinkAp].firstChild;

        auto request = Children(SclHierarchyKind::Users, dataSet);
        request.scope = {publisher, ap, server};
        request.offset = 400;
        const auto limited = QuerySclHierarchy(snapshot, request, nullptr);
        QVERIFY(limited.limited);
        QCOMPARE(limited.total, 512);
        QCOMPARE(limited.rows.size(), size_t(112));
        QVERIFY(!limited.hasMore);
        request.scope.ied = sink;
        const auto outside = QuerySclHierarchy(snapshot, request, nullptr);
        QCOMPARE(outside.parent, SclHierarchyParent::OutsideScope);
        QCOMPARE(outside.total, 0);

        SclHierarchyRequest list;
        list.scope = {publisher, ap, server};
        QCOMPARE(QuerySclHierarchy(snapshot, list, nullptr).total, 2);
        list.scope.accessPoint = sinkAp;
        QCOMPARE(QuerySclHierarchy(snapshot, list, nullptr).total, 0);
        list.scope = {publisher, ap, sinkServer};
        QCOMPARE(QuerySclHierarchy(snapshot, list, nullptr).total, 0);
        list.scope = {};
        list.workLimit = 2;
        QVERIFY(!QuerySclHierarchy(snapshot, list, nullptr).limited);

        auto members = Children(SclHierarchyKind::Members, dataSet);
        members.workLimit = 2;
        members.pageSize = 1;
        const auto partial = QuerySclHierarchy(snapshot, members, nullptr);
        QVERIFY(partial.limited);
        QVERIFY(partial.hasMore);
        QCOMPARE(partial.total, 2);
        QCOMPARE(partial.rows.front().member->ordinal, 1);
        members.locate = snapshot->browse.memberRows[3];
        const auto notLocated = QuerySclHierarchy(snapshot, members, nullptr);
        QVERIFY(notLocated.limited);
        QVERIFY(!notLocated.locateFound);
        QVERIFY(notLocated.rows.empty());
        TaskControl control;
        control.cancelled = true;
        QVERIFY_EXCEPTION_THROWN(QuerySclHierarchy(snapshot, members, &control), TaskCancelled);
    }

    void AsyncSession()
    {
        auto first = OpenFixture(Fixture());
        const auto second = OpenFixture(Fixture());
        QVERIFY(first && second);
        QCOMPARE(first->analysis.sha256, second->analysis.sha256);
        const auto publisher = Find(*second->analysis.document, "IED", "Publisher");
        const auto sink = Find(*second->analysis.document, "IED", "Sink");
        const auto dataSet = Find(*second->analysis.document, "DataSet", "Shared");
        const auto oldSnapshot = std::weak_ptr<const SclSnapshot>(first);
        SclHierarchyQuerySession list, users;
        QSignalSpy listReady(&list, &SclHierarchyQuerySession::Ready);
        QSignalSpy usersReady(&users, &SclHierarchyQuerySession::Ready);
        list.SetContext(first);
        list.Query({});
        list.SetContext(second, {sink});
        list.Query({});
        list.SetContext(second, {publisher});
        list.Query(Children(SclHierarchyKind::Members, dataSet));
        const auto newest = list.Query({});
        users.SetContext(second, {publisher});
        const auto relation = users.Query(Children(SclHierarchyKind::Users, dataSet));
        first.reset();
        QTRY_COMPARE(listReady.count(), 1);
        QTRY_COMPARE(usersReady.count(), 1);
        QCOMPARE(list.result()->requestId, newest);
        QCOMPARE(list.result()->snapshot, second);
        QCOMPARE(list.result()->request.scope.ied, publisher);
        QCOMPARE(list.result()->request.kind, SclHierarchyKind::DataSets);
        QCOMPARE(list.result()->total, 2);
        QCOMPARE(users.result()->requestId, relation);
        QCOMPARE(users.result()->request.dataSet, dataSet);
        QCOMPARE(users.result()->total, 3);
        QTRY_VERIFY(oldSnapshot.expired());

        users.Query(Children(SclHierarchyKind::Users, dataSet));
        users.Cancel();
        users.SetContext({});
        const auto request = list.Query({});
        QTRY_COMPARE(listReady.count(), 2);
        QCOMPARE(list.result()->requestId, request);
        QVERIFY(!users.result());
        QCOMPARE(usersReady.count(), 1);
    }

    void LocalInventory()
    {
        const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
        if (path.isEmpty())
            QSKIP("Set IEC61850_LOCAL_SCL for optional local read-only acceptance.");
        const auto before = HashFile(path);
        QVERIFY(!before.isEmpty());
        SclAnalysisService service;
        service.Open(path);
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), SclAnalysisService::State::Ready, 60000);
        const auto snapshot = service.snapshot();
        QCOMPARE(snapshot->analysis.sha256, before);

        SclHierarchyQuerySession session;
        QSignalSpy ready(&session, &SclHierarchyQuerySession::Ready);
        session.SetContext(snapshot);
        session.Query({});
        QTRY_COMPARE(ready.count(), 1);
        const auto list = session.result();
        QCOMPARE(list->snapshot, snapshot);
        QCOMPARE(list->total, int(snapshot->browse.dataSetMembers.size()));
        QVERIFY(list->hasMore);
        QVERIFY(!list->limited);
        NodeId largest = NoNode;
        int count = 0;
        for (auto it = snapshot->browse.dataSetMembers.cbegin(); it != snapshot->browse.dataSetMembers.cend(); ++it)
            if (it->count > count || (it->count == count && it.key() < largest))
            {
                largest = it.key();
                count = it->count;
            }
        QVERIFY(count > 0);
        auto request = Children(SclHierarchyKind::Members, largest);
        request.offset = (count - 1) / 200 * 200;
        session.Query(request);
        QTRY_COMPARE(ready.count(), 2);
        const auto members = session.result();
        QCOMPARE(members->total, count);
        QVERIFY(!members->limited);
        QCOMPARE(members->rows.back().member->ordinal, count);
        request.locate = members->rows.back().node;
        request.offset = 0;
        session.Query(request);
        QTRY_COMPARE(ready.count(), 3);
        QVERIFY(session.result()->locateFound);
        QCOMPARE(session.result()->rows.back().node, request.locate);

        const auto& reports = snapshot->mms.reports;
        const auto report = std::find_if(reports.begin(), reports.end(),
            [](const auto& row) { return row.dataSet.target() != NoNode; });
        QVERIFY(report != reports.end());
        request = Children(SclHierarchyKind::Users, report->dataSet.target());
        request.locate = report->node;
        session.Query(request);
        QTRY_COMPARE(ready.count(), 4);
        QVERIFY(session.result()->locateFound);
        QVERIFY(!session.result()->limited);
        qInfo().noquote() << "LOCAL sha256=" << before << "dataSets=" << list->total
                          << "firstPage=" << list->rows.size() << "largestMembers=" << count
                          << "lastMemberPage=" << members->rows.size()
                          << "reportDataSetUsers=" << session.result()->total;
        session.SetContext({});
        service.Close();
        QCOMPARE(HashFile(path), before);
    }
};

QTEST_GUILESS_MAIN(SclHierarchyQueryTest)
#include "SclHierarchyQueryTest.moc"
