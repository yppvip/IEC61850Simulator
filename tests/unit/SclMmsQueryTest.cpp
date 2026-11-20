#include "SclAnalysisService.hpp"
#include "SclMmsQuery.hpp"
#include "SclIssueExport.hpp"
#include <QtTest>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QThreadPool>
#include <QTimer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

using namespace Scl;
using namespace iec61850sim;

namespace {
QByteArray Bytes() {
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task6Mms.scd"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("fixture unavailable");
    return file.readAll();
}
std::shared_ptr<const SclSnapshot> Snapshot(QByteArray xml = Bytes()) {
    auto a = ReferenceResolver::analyze(Reader::readBytes(std::move(xml)));
    if (!a.document) qFatal("invalid test XML");
    SclBrowseIndex b(*a.document, nullptr); b.BuildScopes(a, nullptr);
    auto t = SclTaskIndex::Build(a, b, nullptr);
    auto p = MmsProjector::Build(a);
    auto i = SclMmsIndex::Build(p, a, nullptr);
    return std::make_shared<const SclSnapshot>(SclSnapshot{{}, std::move(a), std::move(b), std::move(t), std::move(p), std::move(i)});
}
NodeId Named(const SclSnapshot& s, const char* kind, const char* name) {
    const auto& doc = *s.analysis.document;
    for (int i = 0; i < int(doc.nodes.size()); ++i)
        if (doc.nodes[i].isScl() && doc.nodes[i].name == QLatin1String(kind) && doc.attribute(i, "name") == QLatin1String(name)) return i;
    return NoNode;
}
int Signal(const SclSnapshot& s, const QString& path, NodeId ied) {
    for (int i = 0; i < int(s.tasks.catalog.instances.size()); ++i) {
        const auto& signal = s.tasks.catalog.instances[i];
        if (s.tasks.catalog.definitions[signal.definition].path == path && s.browse.iedOwners[signal.logicalNode] == ied) return i;
    }
    return -1;
}
SclMmsRequest Request(SclMmsQueryKind kind, NodeId report = NoNode) {
    SclMmsRequest r; r.kind = kind; r.report = report; return r;
}
}

class SclMmsQueryTest : public QObject {
    Q_OBJECT
private slots:
    void LocateIdentityAndDirectDataSet() {
        auto bytes = Bytes(); const int begin = bytes.indexOf("<FCDA "), end = bytes.indexOf("/>", begin) + 2;
        bytes.insert(end, bytes.mid(begin, end - begin).repeated(250));
        const auto s = Snapshot(bytes); const auto report = Named(*s, "ReportControl", "brStatus");
        auto r = Request(SclMmsQueryKind::Members, report); r.offset = 200;
        const auto secondPage = QuerySclMms(*s, r, nullptr); QCOMPARE(secondPage.rows.size(), size_t(53));
        const auto key = secondPage.rows[17].key;
        r.offset = 0; r.locate = key; r.presentation = true;
        auto found = QuerySclMms(*s, r, nullptr); QVERIFY(found.locateFound); QCOMPARE(found.request.offset, 200);
        QCOMPARE(found.total, 253); QCOMPARE(found.rows[17].key.node, key.node); QVERIFY(found.rows[17].description.has_value());
        r.report = NoNode; r.target = s->mms.reports.front().dataSet.target();
        found = QuerySclMms(*s, r, nullptr); QVERIFY(found.locateFound); QCOMPARE(found.rows[17].report, NoNode);
        QCOMPARE(found.rows[17].dataSet, r.target); QCOMPARE(found.rows[17].key.ordinal, 218);
        r.text = "never-matches"; found = QuerySclMms(*s, r, nullptr);
        QVERIFY(!found.locateFound); QVERIFY(found.rows.empty()); QVERIFY(!found.limitation.isEmpty());
        r.text.clear(); r.workLimit = 5; found = QuerySclMms(*s, r, nullptr);
        QVERIFY(found.limited); QVERIFY(!found.locateFound); QVERIFY(found.rows.empty());
        r.workLimit = 100000; r.scope.ied = Named(*s, "IED", "SinkA");
        found = QuerySclMms(*s, r, nullptr); QVERIFY(!found.locateFound); QVERIFY(found.rows.empty());
        // An index budget may omit this member even though its declaration remains browsable.
        auto limited = *s; limited.mmsIndex.memberReferences.remove(key.node); limited.mmsIndex.limited = true;
        r.locate.reset(); r.scope = {}; r.offset = 200;
        found = QuerySclMms(limited, r, nullptr); QVERIFY(found.limited);
        QCOMPARE(found.rows[17].key.record, -1); QCOMPARE(found.rows[17].state, LinkState::Blocked);
        QVERIFY(found.rows[17].cells[0].contains("218")); QVERIFY(!found.rows[17].limitation.isEmpty());
        r.kind = SclMmsQueryKind::Detail; r.key = found.rows[17].key;
        const auto detail = QuerySclMms(limited, r, nullptr); QVERIFY(detail.limited); QVERIFY(!detail.fields.empty());
    }
    void SnapshotService() {
        SclAnalysisService service;
        service.Open(QStringLiteral(SCL_FIXTURE_DIR "/Task6Mms.scd"));
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), SclAnalysisService::State::Ready, 10000);
        const auto s = service.snapshot();
        QVERIFY(s->mms.complete); QCOMPARE(s->mms.document, s->analysis.document);
        QCOMPARE(s->mms.source, s->analysis.source); QCOMPARE(s->mms.sha256, s->analysis.sha256);
        QCOMPARE(s->mmsIndex.reports.size(), 4);
        service.Open(QStringLiteral(SCL_FIXTURE_DIR "/Missing.scd"));
        QTRY_COMPARE(service.state(), SclAnalysisService::State::Failed);
        QCOMPARE(service.snapshot(), s);
        service.Close(); QVERIFY(!service.snapshot());
        QVERIFY(s->mms.complete); // Retained immutable snapshot survives close.
    }
    void ReportsMembersAndInstances() {
        const auto s = Snapshot(); const auto report = Named(*s, "ReportControl", "brStatus");
        auto r = Request(SclMmsQueryKind::Reports); r.pageSize = 1;
        auto first = QuerySclMms(*s, r, nullptr);
        QCOMPARE(first.total, 4); QCOMPARE(first.rows.size(), size_t(1)); QVERIFY(first.hasMore);
        QCOMPARE(first.rows[0].key.node, report);
        r.offset = 1; auto next = QuerySclMms(*s, r, nullptr);
        QVERIFY(next.rows[0].key.node != report);
        r = Request(SclMmsQueryKind::Members, report); r.pageSize = 2;
        auto members = QuerySclMms(*s, r, nullptr);
        QCOMPARE(members.total, 3); QCOMPARE(members.rows.size(), size_t(2));
        QVERIFY(members.rows[0].key.node != members.rows[1].key.node);
        QCOMPARE(members.rows[0].item, QStringLiteral("GGIO1$ST$Ind1$stVal"));
        QVERIFY(members.rows[0].description.has_value());
        r.offset = 2; members = QuerySclMms(*s, r, nullptr);
        QCOMPARE(members.rows[0].shape, SclMmsAddressShape::ObjectMember);
        QCOMPARE(members.rows[0].key.ordinal, 3); QCOMPARE(members.rows[0].report, report);
        const auto instances = QuerySclMms(*s, Request(SclMmsQueryKind::Instances, report), nullptr);
        QCOMPARE(instances.rows.size(), size_t(2));
        QCOMPARE(instances.rows[1].key.ordinal, 2);
        QCOMPARE(instances.rows[1].cells[2], QStringLiteral("CustomReport01"));
        // A shared dataset is queried in the parent report context.
        const auto other = Named(*s, "ReportControl", "urStatus");
        const auto shared = QuerySclMms(*s, Request(SclMmsQueryKind::Members, other), nullptr);
        QCOMPARE(shared.rows[0].key.node, QuerySclMms(*s, Request(SclMmsQueryKind::Members, report), nullptr).rows[0].key.node);
        QCOMPARE(shared.rows[0].report, other);
    }
    void ScopeAndAlias() {
        const auto s = Snapshot();
        auto r = Request(SclMmsQueryKind::Reports);
        r.scope.ied = Named(*s, "IED", "SinkZ");
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);
        r.scope = {Named(*s, "IED", "Publisher"), Named(*s, "AccessPoint", "Alias")};
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 4);
        r.kind = SclMmsQueryKind::Servers;
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1);
        r.kind = SclMmsQueryKind::Endpoints;
        const auto endpoints = QuerySclMms(*s, r, nullptr);
        QCOMPARE(endpoints.total, 1); QVERIFY(endpoints.rows[0].cells.join(' ').contains("192.0.2.11"));
        r.scope.server = Named(*s, "IED", "SinkZ"); // Invalid Server identity cannot leak rows.
        r.kind = SclMmsQueryKind::Members; r.report = Named(*s, "ReportControl", "brStatus");
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);
    }
    void UnlistedSignalAndNestedAddress() {
        auto xml = Bytes();
        xml.replace("<DA name=\"q\" bType=\"Quality\" fc=\"ST\" qchg=\"true\"/>",
            "<DA name='q' bType='Quality' fc='ST'/><DA name='detail' bType='Struct' type='Nested' fc='MX'/>");
        xml.replace("</DataTypeTemplates>", "<DAType id='Nested'><BDA name='f' bType='FLOAT32' desc='完整中文描述'/></DAType></DataTypeTemplates>");
        const auto s = Snapshot(xml);
        auto r = Request(SclMmsQueryKind::Addresses);
        r.signal = Signal(*s, "Unlisted", Named(*s, "IED", "Publisher")); QVERIFY(r.signal >= 0);
        auto addresses = QuerySclMms(*s, r, nullptr);
        QCOMPARE(addresses.total, 4);
        QCOMPARE(addresses.rows[0].domain, QStringLiteral("DemoDomain"));
        QCOMPARE(addresses.rows[0].item, QStringLiteral("GGIO1$ST$Unlisted$stVal"));
        QCOMPARE(addresses.rows.back().item, QStringLiteral("GGIO1$MX$Unlisted$detail$f"));
        QCOMPARE(addresses.rows[2].shape, SclMmsAddressShape::Structure);
        QCOMPARE(addresses.rows.back().shape, SclMmsAddressShape::Scalar);
        QVERIFY(addresses.rows.back().description.has_value());
        r.fc = "MX"; r.pageSize = 1; r.offset = 1;
        addresses = QuerySclMms(*s, r, nullptr); QCOMPARE(addresses.total, 2);
        QCOMPARE(addresses.rows[0].key.field, QStringLiteral("detail.f"));
        r.kind = SclMmsQueryKind::ReverseSignal; r.offset = 0; r.fc.clear();
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);
        r.signal = Signal(*s, "Ind1", Named(*s, "IED", "Publisher"));
        const auto reverse = QuerySclMms(*s, r, nullptr); QCOMPARE(reverse.total, 12); // 3 occurrences x 4 definitions.
    }
    void ArrayAndAmbiguousTypes() {
        auto xml = Bytes(); xml.replace("name=\"stVal\" bType=\"BOOLEAN\"", "name=\"stVal\" count=\"3\" bType=\"BOOLEAN\"");
        const auto s = Snapshot(xml);
        auto r = Request(SclMmsQueryKind::Addresses); r.signal = Signal(*s, "Ind1", Named(*s, "IED", "Publisher"));
        const auto result = QuerySclMms(*s, r, nullptr);
        QCOMPARE(result.rows[0].shape, SclMmsAddressShape::ArrayTemplate);
        QVERIFY(result.rows[0].item.isEmpty()); QCOMPARE(result.rows[0].state, LinkState::Partial);
        xml = Bytes(); xml.replace("</DataTypeTemplates>", "<DOType id='Status' cdc='SPS'/></DataTypeTemplates>");
        const auto ambiguous = Snapshot(xml);
        r.signal = Signal(*ambiguous, "Ind1", Named(*ambiguous, "IED", "Publisher"));
        const auto blocked = QuerySclMms(*ambiguous, r, nullptr);
        QCOMPARE(blocked.rows[0].state, LinkState::Blocked); QVERIFY(blocked.rows[0].item.isEmpty());
    }
    void NameFcAndScopeEvidence() {
        auto xml = Bytes();
        xml.replace("<DA name=\"q\" bType=\"Quality\" fc=\"ST\" qchg=\"true\"/>",
            "<DA name='stVal' bType='BOOLEAN' fc='CF'/><DA name='bad$name' bType='BOOLEAN' fc='ST'/>");
        const auto s = Snapshot(xml);
        auto r = Request(SclMmsQueryKind::Addresses); r.signal = Signal(*s, "Ind1", Named(*s, "IED", "Publisher"));
        const auto rows = QuerySclMms(*s, r, nullptr);
        QCOMPARE(rows.total, 3);
        QCOMPARE(rows.rows[0].state, LinkState::Resolved); QCOMPARE(rows.rows[1].state, LinkState::Resolved);
        QVERIFY(rows.rows[0].item != rows.rows[1].item);
        QCOMPARE(rows.rows[2].state, LinkState::Blocked); QVERIFY(rows.rows[2].item.isEmpty());
        auto unknownXml = Bytes(); unknownXml.replace("bType=\"BOOLEAN\"", "bType='VendorUnknown'");
        const auto unknown = Snapshot(unknownXml);
        r.signal = Signal(*unknown, "Ind1", Named(*unknown, "IED", "Publisher"));
        const auto unknownRows = QuerySclMms(*unknown, r, nullptr);
        QCOMPARE(unknownRows.rows[0].shape, SclMmsAddressShape::None);
        QCOMPARE(unknownRows.rows[0].state, LinkState::Blocked); QVERIFY(unknownRows.rows[0].item.isEmpty());
        xml = Bytes(); xml.replace("<LN lnClass=\"GGIO\" inst=\"1\" lnType=\"Signals\"/>",
            "<LN lnClass='GGIO' inst='1' lnType='Signals'/><LN lnClass='GGIO' inst='1' lnType='Signals'/>");
        const auto duplicate = Snapshot(xml);
        r.signal = Signal(*duplicate, "Ind1", Named(*duplicate, "IED", "Publisher"));
        const auto blocked = QuerySclMms(*duplicate, r, nullptr);
        QCOMPARE(blocked.rows[0].state, LinkState::Blocked); QVERIFY(blocked.rows[0].item.isEmpty());
    }
    void DescriptionAndObjectSearch() {
        auto xml = Bytes(); xml.replace("name=\"Unlisted\" type=\"Status\"", "name='Unlisted' type='Status' desc='未入数据集描述'");
        const auto s = Snapshot(xml); auto r = Request(SclMmsQueryKind::Search);
        r.text = QStringLiteral("未入数据集描述");
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 3);
        r.text = "DemoDomain/GGIO1.Ind1.stVal";
        const auto members = QuerySclMms(*s, r, nullptr); QCOMPARE(members.total, 2);
        for (const auto& row : members.rows) {
            QCOMPARE(row.key.kind, SclMmsRecordKind::Member); QVERIFY(row.dataSet != NoNode);
        }
    }
    void PaginationFullValueAndSource() {
        auto xml = Bytes(); QByteArray extra;
        for (int i = 0; i < 250; ++i)
            extra += "<FCDA ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal' fc='ST'/>";
        xml.replace("</DataSet>", extra + "</DataSet>");
        const QString longId = QStringLiteral("完整中文\n") + QString(20000, QChar('X'));
        auto encoded = longId.toUtf8(); encoded.replace("\n", "&#10;");
        xml.replace("CustomReport01", encoded);
        const auto s = Snapshot(xml);
        const auto report = Named(*s, "ReportControl", "brStatus");
        auto r = Request(SclMmsQueryKind::Reports);
        auto result = QuerySclMms(*s, r, nullptr);
        QCOMPARE(result.rows[0].cells[1], longId);
        QCOMPARE(result.document, s->analysis.document); QCOMPARE(result.source, s->analysis.source);
        r = Request(SclMmsQueryKind::Members, report); r.pageSize = 5000;
        const auto page = QuerySclMms(*s, r, nullptr);
        QCOMPARE(page.rows.size(), size_t(200)); QCOMPARE(page.total, 253); QVERIFY(page.hasMore);
        r.offset = 200;
        const auto last = QuerySclMms(*s, r, nullptr);
        QCOMPARE(last.rows.size(), size_t(53)); QCOMPARE(last.rows[0].key.ordinal, 201); QVERIFY(!last.hasMore);
        QVERIFY(page.rows.back().key.node != last.rows.front().key.node);
        const auto node = last.rows.front().evidence[0].node;
        const auto location = s->analysis.document->nodes[node].source;
        QCOMPARE(s->analysis.source->bytes().mid(location.byteOffset, 5), QByteArray("<FCDA"));
        r = Request(SclMmsQueryKind::Search); r.text = "GGIO1$ST$Unlisted$stVal";
        const auto addressSearch = QuerySclMms(*s, r, nullptr);
        QCOMPARE(addressSearch.total, 3); // One per LN instance, including sinks.
        for (const auto& row : addressSearch.rows) QCOMPARE(row.key.kind, SclMmsRecordKind::Address);
        r.text = "DemoDomain/GGIO1$ST$Unlisted$stVal";
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1);
    }
    void AlternateAccess() {
        auto xml = Bytes();
        xml.replace("<DO name=\"Ind1\" type=\"Status\"/>", "<DO name='Ind1' type='Status' count='3'/>");
        xml.replace("daName=\"stVal\" fc=\"ST\"", "daName='stVal' fc='ST' ix='1'");
        const auto s = Snapshot(xml);
        auto r = Request(SclMmsQueryKind::Members, Named(*s, "ReportControl", "brStatus"));
        const auto result = QuerySclMms(*s, r, nullptr);
        QCOMPARE(result.rows[0].shape, SclMmsAddressShape::AlternateAccess);
        QCOMPARE(result.rows[0].arrayIndex, 1); QCOMPARE(result.rows[0].component, QStringLiteral("stVal"));
        QCOMPARE(result.rows[0].item, QStringLiteral("GGIO1$ST$Ind1"));
    }
    void ClientsSearchGraphAndBudgets() {
        const auto s = Snapshot(); const auto report = Named(*s, "ReportControl", "brStatus");
        const auto clients = QuerySclMms(*s, Request(SclMmsQueryKind::Clients, report), nullptr);
        QCOMPARE(clients.total, 2);
        const auto target = s->mms.clients[clients.rows[0].key.record].target.target(); QVERIFY(target != NoNode);
        auto r = Request(SclMmsQueryKind::ReverseClients); r.target = target;
        const auto reverse = QuerySclMms(*s, r, nullptr); QCOMPARE(reverse.total, 1);
        QCOMPARE(reverse.rows[0].key.node, clients.rows[0].key.node);
        r.target = Named(*s, "IED", "SinkZ");
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1);
        r = Request(SclMmsQueryKind::Search); r.text = "brStatus02";
        const auto search = QuerySclMms(*s, r, nullptr); QCOMPARE(search.total, 1);
        QCOMPARE(search.rows[0].key.kind, SclMmsRecordKind::Instance);
        r = Request(SclMmsQueryKind::Graph, report); r.graphLimit = 3;
        const auto graph = QuerySclMms(*s, r, nullptr); QCOMPARE(graph.rows.size(), size_t(3));
        QVERIFY(graph.limited); QVERIFY(graph.total > 3); QVERIFY(!graph.limitation.isEmpty());
        for (const auto& e : graph.edges) { QVERIFY(e.from >= 0); QVERIFY(e.to < 3); }
        r.graphLimit = 128;
        const auto fullGraph = QuerySclMms(*s, r, nullptr); QVERIFY(!fullGraph.limited);
        int memberEdges = 0, objectEdges = 0;
        for (const auto& e : fullGraph.edges) {
            memberEdges += e.kind == SclMmsEdgeKind::DataSetMember;
            objectEdges += e.kind == SclMmsEdgeKind::MemberObject;
        }
        QCOMPARE(memberEdges, 3); QCOMPARE(objectEdges, 3);
        r = Request(SclMmsQueryKind::Reports); r.workLimit = 1;
        const auto limited = QuerySclMms(*s, r, nullptr); QVERIFY(limited.limited); QVERIFY(limited.hasMore);
        TaskControl cancelled; cancelled.cancelled = true;
        QVERIFY_EXCEPTION_THROWN(QuerySclMms(*s, r, &cancelled), TaskCancelled);
        auto blocked = std::make_shared<SclSnapshot>(*s); blocked->mms = MmsProjector::Build(s->analysis, nullptr, {1, 1});
        const auto noPartial = QuerySclMms(*blocked, Request(SclMmsQueryKind::Reports), nullptr);
        QVERIFY(noPartial.limited); QVERIFY(noPartial.rows.empty());
    }
    void PresentationDetailsAndUnifiedExport() {
        auto s = std::make_shared<SclSnapshot>(*Snapshot());
        auto request = Request(SclMmsQueryKind::Reports); request.presentation = true;
        auto result = QuerySclMms(*s, request, nullptr);
        QCOMPARE(result.rows[0].cells.size(), 8);
        request.kind = SclMmsQueryKind::Detail; request.key = result.rows[0].key;
        const auto detail = QuerySclMms(*s, request, nullptr);
        QVERIFY(!detail.fields.empty());
        bool raw = false, offline = false;
        for (const auto& field : detail.fields) {
            if (field.key == "@rptID") { raw = true; QCOMPARE(field.value, QStringLiteral("CustomReport01")); QVERIFY(field.present); QCOMPARE(field.evidence.node, result.rows[0].key.node); }
            if (field.key == "runtime") { offline = true; QVERIFY(!field.present); QVERIFY(field.value.isEmpty()); }
        }
        QVERIFY(raw && offline);
        s->mms.findings.push_back({"MMS_TEST", Severity::Warning, result.rows[0].key.node, {0, 1}, "@formula", "=cmd", "中文建议", "offline"});
        request.kind = SclMmsQueryKind::Findings; request.text = "MMS_TEST";
        result = QuerySclMms(*s, request, nullptr);
        QCOMPARE(result.total, 1); QCOMPARE(result.rows[0].cells[7], s->mms.ruleSetVersion);
        QTemporaryDir temp; QVERIFY(temp.isValid());
        const auto jsonPath = temp.filePath("issues.json"), csvPath = temp.filePath("issues.csv");
        QCOMPARE(ExportSclIssues(*s, jsonPath, IssueFormat::Json), QString());
        QCOMPARE(ExportSclIssues(*s, csvPath, IssueFormat::Csv), QString());
        QFile json(jsonPath); QVERIFY(json.open(QIODevice::ReadOnly));
        const auto object = QJsonDocument::fromJson(json.readAll()).object();
        QCOMPARE(object.value("mmsRuleSetVersion").toString(), QStringLiteral("mms-0.1"));
        QCOMPARE(object.value("findings").toArray().last().toObject().value("ruleSetVersion").toString(), QStringLiteral("mms-0.1"));
        QFile csv(csvPath); QVERIFY(csv.open(QIODevice::ReadOnly)); const auto bytes = csv.readAll();
        QVERIFY(bytes.contains("\"'=cmd\"")); QVERIFY(bytes.contains("mms-0.1"));
    }
    void LatestRequestAndIndependentConsumers() {
        const auto s = Snapshot();
        SclMmsQuerySession list, detail;
        list.SetContext(s); detail.SetContext(s);
        QSignalSpy listReady(&list, &SclMmsQuerySession::Ready), detailReady(&detail, &SclMmsQuerySession::Ready);
        auto r = Request(SclMmsQueryKind::Reports);
        list.Query(r); list.Cancel(); // Completed-but-undelivered also must not publish.
        list.SetContext(s, {Named(*s, "IED", "SinkZ")}); list.Query(r);
        list.SetContext(s, {Named(*s, "IED", "Publisher")});
        r.text = "urStatus"; const auto latest = list.Query(r);
        detail.Query(Request(SclMmsQueryKind::Members, Named(*s, "ReportControl", "brStatus")));
        QTRY_COMPARE(listReady.count(), 1); QTRY_COMPARE(detailReady.count(), 1);
        QCOMPARE(list.result()->requestId, latest); QCOMPARE(list.result()->total, 1);
        QCOMPARE(list.result()->rows[0].cells[0], QStringLiteral("urStatus"));
        QCOMPARE(detail.result()->total, 3);
        list.Cancel(); QVERIFY(!list.result()); QVERIFY(detail.result());
        // A new snapshot with the same bytes is still a different session identity.
        auto second = Snapshot(); list.Query(r); list.SetContext(second);
        const auto id = list.Query(Request(SclMmsQueryKind::Reports));
        QTRY_COMPARE(listReady.count(), 2); QCOMPARE(list.result()->requestId, id);
        list.Query(r); list.SetContext({});
        QTest::qWait(100); QCOMPARE(listReady.count(), 2); QVERIFY(!list.result());
        auto* closing = new SclMmsQuerySession; closing->SetContext(s); closing->Query(r); delete closing;
        QThreadPool::globalInstance()->waitForDone(); // Worker owns snapshot/control, never this.
    }
    void LocalMmsInventory() {
        const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
        if (path.isEmpty() || !QCoreApplication::arguments().contains("LocalMmsInventory")) QSKIP("Explicit local aggregate inventory only");
        QElapsedTimer clock; clock.start();
        const auto input = Reader::readFile(path); QVERIFY(input.document);
        const auto readerMs = clock.restart();
        const auto analysis = ReferenceResolver::analyze(input); const auto referencesMs = clock.restart();
        SclBrowseIndex browse(*analysis.document, nullptr); browse.BuildScopes(analysis, nullptr); const auto browseMs = clock.restart();
        const auto tasks = SclTaskIndex::Build(analysis, browse, nullptr); const auto taskMs = clock.restart();
        const auto p = MmsProjector::Build(analysis); const auto mmsMs = clock.restart();
        const auto index = SclMmsIndex::Build(p, analysis, nullptr); const auto mmsIndexMs = clock.elapsed();
        QVERIFY(p.complete); QVERIFY(!index.limited);
        int br = 0, rp = 0, unknown = 0, missingId = 0, emptyId = 0, explicitId = 0, occurrences = 0;
        quint64 maxSum = 0; QSet<NodeId> sets; QSet<QString> uniqueSignals;
        for (const auto& report : p.reports) {
            const auto buffered = report.fields.value("buffered");
            if (!buffered.value.isValid()) ++unknown; else if (buffered.value.toBool()) ++br; else ++rp;
            const auto id = report.fields.value("rptID");
            if (!id.rawPresent) ++missingId; else if (id.raw.isEmpty()) ++emptyId; else ++explicitId;
            if (report.enabled.size() == 1) maxSum += report.enabled[0].fields.value("max").value.toULongLong();
            if (report.dataSet.target() != NoNode && !sets.contains(report.dataSet.target())) {
                sets.insert(report.dataSet.target()); occurrences += report.memberCount;
            }
        }
        for (const auto& instance : tasks.catalog.instances) uniqueSignals.insert(QString::number(instance.logicalNode) + '/' + tasks.catalog.definitions[instance.definition].path);
        const QJsonObject data{{"sha256", QString::fromLatin1(analysis.sha256)}, {"readerMs", readerMs},
            {"referencesMs", referencesMs}, {"browseMs", browseMs}, {"taskIndexMs", taskMs},
            {"mmsMs", mmsMs}, {"mmsIndexMs", mmsIndexMs}, {"profile", int(p.profile)},
            {"reports", int(p.reports.size())}, {"br", br}, {"rp", rp}, {"unknownBuffered", unknown},
            {"rptIdMissing", missingId}, {"rptIdEmpty", emptyId}, {"rptIdExplicit", explicitId},
            {"declaredMaxSum", qint64(maxSum)}, {"expectedInstances", int(p.instances.size())},
            {"clients", int(p.clients.size())}, {"reportDataSets", sets.size()},
            {"reportDataSetMemberOccurrences", occurrences}, {"allFcda", analysis.document->standardCounts.value("FCDA")},
            {"uniqueCatalogSignals", uniqueSignals.size()}, {"catalogLimited", tasks.catalog.limited},
            {"endpoints", int(p.endpoints.size())}, {"servers", int(p.servers.size())}, {"capabilities", int(p.capabilities.size())}};
        qInfo().noquote() << "MMS_INVENTORY" << QJsonDocument(data).toJson(QJsonDocument::Compact);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(), analysis.sha256);
    }
    void LocalMmsQueries() {
        const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
        if (path.isEmpty()) QSKIP("Explicit local read-only acceptance only");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto before = QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(); file.close();
        QElapsedTimer timer; timer.start();
        QElapsedTimer heartbeat; heartbeat.start(); qint64 maxHeartbeat = 0;
        QTimer pulse; pulse.setInterval(10);
        connect(&pulse, &QTimer::timeout, &pulse, [&] { maxHeartbeat = std::max(maxHeartbeat, heartbeat.restart()); });
        pulse.start();
        SclAnalysisService service; service.Open(path);
        QTRY_COMPARE_WITH_TIMEOUT(service.state(), SclAnalysisService::State::Ready, 180000);
        const auto s = service.snapshot(); const auto loadMs = timer.elapsed();
        QCOMPARE(s->analysis.sha256, before); QVERIFY(s->mms.complete);
        const auto reports = QuerySclMms(*s, Request(SclMmsQueryKind::Reports), nullptr);
        QVERIFY(reports.total > 0); QVERIFY(!reports.limited);
        auto r = Request(SclMmsQueryKind::Members, reports.rows[0].key.node);
        const auto members = QuerySclMms(*s, r, nullptr);
        r = Request(SclMmsQueryKind::Signals); r.pageSize = 1;
        const auto signalRows = QuerySclMms(*s, r, nullptr); QVERIFY(!signalRows.rows.empty());
        r = Request(SclMmsQueryKind::Addresses); r.signal = signalRows.rows[0].key.record;
        const auto addresses = QuerySclMms(*s, r, nullptr);
        QVERIFY(addresses.total > 0 || addresses.limited);
        SclMmsQuerySession session; session.SetContext(s);
        QSignalSpy ready(&session, &SclMmsQuerySession::Ready);
        r = Request(SclMmsQueryKind::Search); r.text = s->mms.reports[0].fields.value("name").raw;
        session.Query(r); QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, 30000);
        QVERIFY(session.result()->total > 0);
        const bool globalLimited = session.result()->limited;
        session.SetContext(s, {s->mms.reports[0].ied});
        session.Query(r); QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 2, 30000);
        QVERIFY(session.result()->total > 0); QVERIFY(!session.result()->limited);
        r = Request(SclMmsQueryKind::Graph, s->mms.reports[0].node);
        session.Query(r); QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 3, 30000);
        QVERIFY(!session.result()->rows.empty());
        pulse.stop();
        qInfo().noquote() << "LOCAL sha256=" << before << "loadMs=" << loadMs
            << "IED=" << s->analysis.document->standardCounts.value("IED")
            << "FCDA=" << s->analysis.document->standardCounts.value("FCDA")
            << "reports=" << s->mms.reports.size() << "instances=" << s->mms.instances.size()
            << "clients=" << s->mms.clients.size() << "memberPage=" << members.rows.size()
            << "addressPage=" << addresses.rows.size() << "globalSearchLimited=" << globalLimited
            << "graphNodes=" << session.result()->rows.size() << "heartbeatMaxMs=" << maxHeartbeat;
        service.Close(); session.SetContext({});
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256).toHex(), before);
    }
};
QTEST_GUILESS_MAIN(SclMmsQueryTest)
#include "SclMmsQueryTest.moc"
