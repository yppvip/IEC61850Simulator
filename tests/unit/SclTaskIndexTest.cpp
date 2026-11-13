#include "SclBrowseIndex.hpp"
#include "SclAnalysisService.hpp"
#include "SclTaskIndex.hpp"
#include "SclConnections.hpp"

#include <QtTest>
#include <set>

class SclTaskIndexTest final : public QObject
{
    Q_OBJECT
private slots:
    void SignalsAndEvidence();
    void SourceControlEvidence();
    void ObjectBudget();
    void MemberConnections();
    void LocalConnections();
    void MemberPagination();
};

void SclTaskIndexTest::SignalsAndEvidence()
{
    using namespace Scl;
    using namespace iec61850sim;
    const auto input = Reader::readFile(QStringLiteral(SCL_FIXTURE_DIR "/Task4Signals.scd"));
    QVERIFY(input.document);
    const auto analysis = ReferenceResolver::analyze(input);
    SclBrowseIndex browse(*input.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    const auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    QVERIFY(!index.catalog.limited && !index.relationsLimited);
    QCOMPARE(index.catalog.unresolvedLogicalNodes, 0);
    std::set<NodeId> positions;
    int measured = 0;
    int settings = 0;
    int positionUses = 0;
    for (const auto& instance : index.catalog.instances)
    {
        const auto& definition = index.catalog.definitions[instance.definition];
        if (definition.path == "Pos")
        {
            positions.insert(instance.logicalNode);
            QCOMPARE(definition.capabilities, unsigned(Indication | ControlCandidate));
            const auto uses = index.usages.find({instance.logicalNode, definition.path});
            if (uses != index.usages.end())
            {
                QCOMPARE(uses->second.size(), 2);
                QCOMPARE(analysis.references[uses->second[0].reference].memberOrdinal, 1);
                QCOMPARE(analysis.references[uses->second[1].reference].memberOrdinal, 2);
                ++positionUses;
            }
        }
        else if (definition.path == "A.phsA")
        {
            QCOMPARE(definition.capabilities, unsigned(Measurement));
            ++measured;
        }
        else if (definition.path == "Limit")
        {
            QCOMPARE(definition.capabilities, unsigned(Configuration));
            ++settings;
        }
        else if (definition.path == "Beh")
            QCOMPARE(definition.capabilities, 0u);
    }
    QCOMPARE(positions.size(), 3);
    QCOMPARE(positionUses, 1);
    QCOMPARE(measured, 1);
    QCOMPARE(settings, 1);
    QCOMPARE(index.goose.size(), 1);
    QCOMPARE(index.inputs.size(), 2);
    QVERIFY(index.goose.front().receivers.empty());
    QCOMPARE(analysis.references[index.inputs[0].dataReference].state, LinkState::Resolved);
    QCOMPARE(analysis.references[index.inputs[1].dataReference].state, LinkState::External);
    for (const auto& item : index.inputs)
        QCOMPARE(item.matchedStream, NoNode);
    TaskControl cancelled;
    cancelled.cancelled = true;
    QVERIFY_EXCEPTION_THROWN(SclTaskIndex::Build(analysis, browse, &cancelled), TaskCancelled);
}

void SclTaskIndexTest::SourceControlEvidence()
{
    using namespace Scl;
    using namespace iec61850sim;
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto xml = file.readAll();
    const QByteArray source = "iedName='IED_TEST' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal'";
    const QByteArray control = "srcLDInst='LD0' srcLNClass='LLN0' srcCBName='gcb1'";
    const QByteArray sink = "<IED name='SINK'><AccessPoint name='A'><Server><LDevice inst='L'>"
        "<LN0 lnClass='LLN0' inst='' lnType='T_LLN0'><Inputs>"
        "<ExtRef " + source + " " + control + " serviceType='GOOSE'/>"
        "<ExtRef " + source + " " + control + "/>"
        "<ExtRef " + source + "/>"
        "</Inputs></LN0></LDevice></Server></AccessPoint></IED>";
    xml.replace("<DataTypeTemplates>", sink + "<DataTypeTemplates>");
    const auto input = Reader::readBytes(xml);
    QVERIFY(input.document);
    auto analysis = ReferenceResolver::analyze(input);
    SclBrowseIndex browse(*input.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    auto tasks = SclTaskIndex::Build(analysis, browse, nullptr);
    const SclSnapshot snapshot{{}, std::move(analysis), std::move(browse), std::move(tasks)};
    const auto& index = snapshot.tasks;
    QCOMPARE(index.goose.size(), 1);
    QCOMPARE(index.goose.front().receivers.size(), 2);
    QCOMPARE(index.inputs.size(), 3);
    QCOMPARE(index.inputs[0].matchedStream, index.goose.front().control);
    QVERIFY(!index.inputs[0].inferred);
    QCOMPARE(index.inputs[1].matchedStream, index.goose.front().control);
    QVERIFY(index.inputs[1].inferred);
    QCOMPARE(index.inputs[2].matchedStream, NoNode);
    QCOMPARE(FilterSclTaskRows(snapshot, SclTaskKind::GooseInput, NoNode, {}, nullptr).size(), 3);
    QCOMPARE(FilterSclTaskRows(snapshot, SclTaskKind::SvInput, NoNode, {}, nullptr).size(), 1);
    const auto goose = FilterSclTaskRows(snapshot, SclTaskKind::Goose, NoNode, QStringLiteral("0001"), nullptr);
    QCOMPARE(goose.size(), 1);
    const auto detail = DescribeSclTaskRow(snapshot, goose.front(), nullptr);
    QVERIFY(detail.text.contains("TEST_GOOSE") && detail.text.contains("0001"));
    QCOMPARE(detail.evidence.front().node, index.goose.front().control);
}

void SclTaskIndexTest::ObjectBudget()
{
    using namespace Scl;
    QByteArray xml = "<SCL xmlns='http://www.iec.ch/61850/2003/SCL'>"
        "<IED name='I'><AccessPoint name='A'><Server><LDevice inst='L'>"
        "<LN0 lnClass='LLN0' inst='' lnType='Zero'/>";
    for (int i = 1; i <= 4000; ++i)
        xml += "<LN lnClass='GGIO' inst='" + QByteArray::number(i) + "' lnType='Repeated'/>";
    xml += "</LDevice></Server></AccessPoint></IED><DataTypeTemplates>"
        "<LNodeType id='Zero' lnClass='LLN0'/>"
        "<LNodeType id='Repeated' lnClass='GGIO'>";
    for (int i = 1; i <= 128; ++i)
        xml += "<DO name='Ind" + QByteArray::number(i) + "' type='State'/>";
    xml += "</LNodeType><DOType id='State' cdc='SPS'>"
        "<DA name='stVal' bType='BOOLEAN' fc='ST' count='1000'/>"
        "</DOType></DataTypeTemplates></SCL>";
    const auto parsed = Reader::readBytes(xml);
    QVERIFY(parsed.document);
    const auto analysis = ReferenceResolver::analyze(parsed);
    const auto catalog = SignalCatalog::Build(analysis, nullptr);
    QVERIFY(catalog.limited);
    QCOMPARE(catalog.instances.size(), SignalCatalog::ObjectLimit);
    QCOMPARE(catalog.definitions.size(), 128);
}

void SclTaskIndexTest::MemberConnections()
{
    using namespace Scl;
    using namespace iec61850sim;
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto xml = file.readAll();
    const QByteArray source = "iedName='IED_TEST' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1'";
    xml.replace("<DataTypeTemplates>", "<IED name='RECEIVER'><AccessPoint name='A'><Server><LDevice inst='L'>"
        "<LN0 lnClass='LLN0' inst='' lnType='T_LLN0'><Inputs>"
        "<ExtRef " + source + " daName='stVal' intAddr='Local.Input'/>"
        "<ExtRef " + source + " serviceType='GOOSE' intAddr='Local.Aggregate'/>"
        "<ExtRef " + source + " daName='stVal' serviceType='Report' intAddr='Local.Report'/>"
        "<ExtRef " + source + " daName='stVal' srcLDInst='LD0' srcLNClass='LLN0' srcCBName='MISSING'/>"
        "</Inputs></LN0></LDevice></Server></AccessPoint></IED><DataTypeTemplates>");
    const auto parsed = Reader::readBytes(xml);
    QVERIFY(parsed.document);
    auto analysis = ReferenceResolver::analyze(parsed);
    SclBrowseIndex browse(*parsed.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    SclSnapshot snapshot{{}, std::move(analysis), std::move(browse), std::move(index)};
    QCOMPARE(snapshot.tasks.inputs.size(), 4);
    for (const auto& input : snapshot.tasks.inputs)
        QCOMPARE(input.matchedStream, NoNode); // Candidates never become confirmed bindings.
    const auto candidates = QuerySclConnections(snapshot, NoNode, {}, {}, nullptr);
    QCOMPARE(candidates.rows.size(), 4); // two service candidates + two unresolved declarations
    QCOMPARE(candidates.rows[0].candidates, 6); // duplicate FCDA occurrences remain separate
    QVERIFY(candidates.rows[0].addresses.contains("0001"));
    const auto detail = DescribeSclConnection(snapshot, candidates.rows[0], 0, true, nullptr);
    QCOMPARE(detail.members.size(), 6);
    QVERIFY(detail.members[0].candidate);
    QVERIFY(detail.members[0].cells[5].contains(QStringLiteral("候选")));
    NodeId sink = NoNode;
    for (auto ied : snapshot.browse.ieds)
        if (parsed.document->attribute(ied, QStringLiteral("name")) == "RECEIVER")
            sink = ied;
    const auto incoming = QuerySclConnections(snapshot, sink, QStringLiteral("SV"), "4001", nullptr);
    QCOMPARE(incoming.rows.size(), 1);
    QCOMPARE(incoming.rows.front().candidates, 3);
    TaskControl cancelled;
    cancelled.cancelled = true;
    QVERIFY_EXCEPTION_THROWN(QuerySclConnections(snapshot, sink, {}, {}, &cancelled), TaskCancelled);
    // Adding a second publisher preserves both candidates rather than picking the first.
    xml.replace("<GSEControl name=\"gcb1\"", "<GSEControl name='other' datSet='dsEvents'/><GSEControl name=\"gcb1\"");
    const auto ambiguous = Reader::readBytes(xml);
    const auto refs = ReferenceResolver::analyze(ambiguous);
    SclBrowseIndex ambiguousBrowse(*ambiguous.document, nullptr);
    ambiguousBrowse.BuildScopes(refs, nullptr);
    const auto ambiguousIndex = SclTaskIndex::Build(refs, ambiguousBrowse, nullptr);
    QCOMPARE(ambiguousIndex.goose.size(), 2);
    QVERIFY(ambiguousIndex.linksByInput.value(ambiguousIndex.inputs.front().node).size() > 6);
}

void SclTaskIndexTest::LocalConnections()
{
    using namespace Scl;
    using namespace iec61850sim;
    const auto path = qEnvironmentVariable("IEC61850_RELATION_SCL");
    if (path.isEmpty())
        QSKIP("Explicit local read-only acceptance requires IEC61850_RELATION_SCL");
    const auto parsed = Reader::readFile(path);
    QVERIFY(parsed.document);
    auto analysis = ReferenceResolver::analyze(parsed);
    SclBrowseIndex browse(*parsed.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    SclSnapshot snapshot{path, std::move(analysis), std::move(browse), std::move(index)};
    const auto rows = QuerySclConnections(snapshot, NoNode, {}, {}, nullptr);
    int sv = 0, goose = 0, unreceived = 0;
    for (const auto& row : rows.rows)
    {
        if (row.sourceName == "ML2201B" && row.targetName == "PL2201B" && row.service == "SV")
        {
            QCOMPARE(row.bound, 0);
            QCOMPARE(row.candidates, 14);
            QVERIFY(row.addresses.contains("400B"));
            const auto detail = DescribeSclConnection(snapshot, row, 0, true, nullptr);
            QCOMPARE(detail.total, 14);
            QVERIFY(detail.members.front().cells[4].contains("SVLD1/"));
            QCOMPARE(QuerySclConnections(snapshot, row.sender, row.service, row.targetName, nullptr).rows.size(), 1);
            QCOMPARE(QuerySclConnections(snapshot, row.receiver, row.service, row.sourceName, nullptr).rows.size(), 1);
            ++sv;
        }
        if (row.sourceName == "IL2201B" && row.targetName == "PL2201B" && row.service == "GOOSE")
        {
            QCOMPARE(row.candidates, 5);
            QVERIFY(row.addresses.contains("0132"));
            ++goose;
        }
        if (row.sourceName == "PL2201B" && row.service == "GOOSE" && row.receiver == NoNode)
        {
            QVERIFY(row.addresses.contains("0131"));
            QCOMPARE(DescribeSclConnection(snapshot, row, 0, false, nullptr).total, 19);
            ++unreceived;
        }
    }
    QCOMPARE(sv, 1);
    QCOMPARE(goose, 1);
    QCOMPARE(unreceived, 1);
    qInfo().noquote() << rows.statistics;
}

void SclTaskIndexTest::MemberPagination()
{
    using namespace Scl;
    using namespace iec61850sim;
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Minimal.scd"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    auto xml = file.readAll();
    QByteArray members;
    for (int i = 0; i < 220; ++i)
        members += "<FCDA ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal' fc='ST'/>";
    xml.replace("</DataSet>", members + "</DataSet>");
    const auto parsed = Reader::readBytes(xml);
    auto analysis = ReferenceResolver::analyze(parsed);
    SclBrowseIndex browse(*parsed.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    const SclSnapshot snapshot{{}, std::move(analysis), std::move(browse), std::move(index)};
    const auto query = QuerySclConnections(snapshot, NoNode, QStringLiteral("GOOSE"), {}, nullptr);
    QCOMPARE(query.rows.size(), 1);
    const auto first = DescribeSclConnection(snapshot, query.rows[0], 0, false, nullptr);
    QCOMPARE(first.total, 223);
    QCOMPARE(first.members.size(), 200);
    const auto second = DescribeSclConnection(snapshot, query.rows[0], 200, false, nullptr);
    QCOMPARE(second.members.size(), 23);
    QCOMPARE(second.members[0].cells[0], QStringLiteral("201"));
    QVERIFY(DescribeSclConnection(snapshot, query.rows[0], 0, true, nullptr).members.empty());
}

QTEST_GUILESS_MAIN(SclTaskIndexTest)
#include "SclTaskIndexTest.moc"
