#include "SclBrowseIndex.hpp"
#include "SclAnalysisService.hpp"
#include "SclTaskIndex.hpp"

#include <QtTest>
#include <set>

class SclTaskIndexTest final : public QObject
{
    Q_OBJECT
private slots:
    void SignalsAndEvidence();
    void SourceControlEvidence();
    void ObjectBudget();
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
    QCOMPARE(FilterSclTaskRows(snapshot, SclTaskKind::GooseInput, NoNode, {}, nullptr).size(), 2);
    QVERIFY(FilterSclTaskRows(snapshot, SclTaskKind::SvInput, NoNode, {}, nullptr).empty());
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

QTEST_GUILESS_MAIN(SclTaskIndexTest)
#include "SclTaskIndexTest.moc"
