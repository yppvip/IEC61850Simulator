#include "SclBrowseIndex.hpp"
#include "SclAnalysisService.hpp"
#include "SclTaskIndex.hpp"
#include "SclConnections.hpp"
#include "SclRelationGraph.hpp"

#include <QtTest>
#include <set>

class SclTaskIndexTest final : public QObject
{
    Q_OBJECT
private slots:
    void SignalsAndEvidence();
    void DescriptionSources();
    void SourceControlEvidence();
    void ObjectBudget();
    void MemberConnections();
    void LocalConnections();
    void LocalMmsStages();
    void MemberPagination();
};

void SclTaskIndexTest::DescriptionSources()
{
    using namespace Scl;
    using namespace iec61850sim;
    const auto parsed = Reader::readBytes(R"xml(
<SCL xmlns="http://www.iec.ch/61850/2003/SCL">
  <IED name="Synthetic">
    <AccessPoint name="A"><Server><LDevice inst="L">
      <LN0 lnClass="LLN0" lnType="Zero"><DataSet name="D">
        <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Pos" daName="stVal" fc="ST" desc="Member only"/>
        <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Pos" daName="stVal" fc="ST"/>
        <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Arr" daName="stVal" fc="ST" ix="1"/>
        <FCDA ldInst="L" lnClass="XCBR" lnInst="1" doName="Fc" daName="shared" fc="ST"/>
      </DataSet></LN0>
      <LN lnClass="XCBR" inst="1" lnType="Breaker">
        <DOI name="Pos" desc="Object A"><DAI name="dU"><Val>  A description  </Val></DAI><DAI name="stVal" desc="Attribute A"/></DOI>
        <DOI name="Arr" ix="0" desc="Array zero"/>
        <DOI name="Arr" ix="1" desc="Array one"/>
        <DOI name="Grouped" desc="Fallback"><DAI name="dU"><Val sGroup="1">Group only</Val></DAI></DOI>
        <DOI name="Multi"><DAI name="dU"><Val>First</Val><Val>Second</Val></DAI></DOI>
        <DOI name="Dup" desc="First object"/><DOI name="Dup" desc="Second object"/>
        <DOI name="Empty" desc=""><DAI name="dU"><Val/></DAI></DOI>
        <DOI name="Fc"><DAI name="shared" desc="FC is not declared here"/></DOI>
      </LN>
    </LDevice></Server></AccessPoint>
    <AccessPoint name="B"><Server><LDevice inst="L">
      <LN lnClass="XCBR" inst="1" lnType="Breaker">
        <DOI name="Pos" desc="Object B"><DAI name="dU"><Val>Unused B</Val></DAI></DOI>
      </LN>
    </LDevice></Server></AccessPoint>
  </IED>
  <DataTypeTemplates>
    <LNodeType id="Zero" lnClass="LLN0"/>
    <LNodeType id="Breaker" lnClass="XCBR">
      <DO name="Pos" type="P" desc="Template object"/><DO name="Arr" type="P" count="2"/>
      <DO name="Grouped" type="P"/><DO name="Multi" type="P" desc="Template fallback"/>
      <DO name="Dup" type="P"/><DO name="Empty" type="P"/>
      <DO name="Fc" type="F"/>
    </LNodeType>
    <DOType id="P" cdc="DPC" desc="Type only"><DA name="dU" bType="VisString255" fc="DC"/><DA name="stVal" bType="Dbpos" fc="ST"/></DOType>
    <DOType id="F" cdc="SPS"><DA name="shared" bType="BOOLEAN" fc="ST" desc="Status"/><DA name="shared" bType="BOOLEAN" fc="CF" desc="Config"/></DOType>
  </DataTypeTemplates>
</SCL>)xml");
    QVERIFY(parsed.document);
    auto analysis = ReferenceResolver::analyze(parsed);
    SclBrowseIndex browse(*parsed.document, nullptr);
    browse.BuildScopes(analysis, nullptr);
    auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    const SclSnapshot snapshot{{}, std::move(analysis), std::move(browse), std::move(index)};
    const auto& doc = *parsed.document;
    const auto& refs = snapshot.analysis;
    using Source = SclDescriptionSource;
    using State = SclDescriptionState;
    const auto field = [](const SclDescription& description, Source source) {
        for (const auto& item : description.fields)
            if (item.source == source)
                return item;
        return SclDescriptionField{source, State::Unresolved};
    };
    const auto& members = snapshot.browse.memberRows;
    QCOMPARE(members.size(), 4);
    const auto first = DescribeSclMember(refs, members[0], nullptr);
    const auto repeated = DescribeSclMember(refs, members[1], nullptr);
    const auto array = DescribeSclMember(refs, members[2], nullptr);
    QVERIFY(first.ObjectTitle() && repeated.ObjectTitle() && array.ObjectTitle());
    QCOMPARE(first.ObjectTitle()->value, QStringLiteral("  A description  "));
    QCOMPARE(first.ObjectTitle()->node, repeated.ObjectTitle()->node);
    QVERIFY(first.member != repeated.member);
    QCOMPARE(first.memberOrdinal, 1);
    QCOMPARE(repeated.memberOrdinal, 2);
    QCOMPARE(field(first, Source::Member).value, QStringLiteral("Member only"));
    QCOMPARE(field(repeated, Source::Member).state, State::Missing);
    QCOMPARE(field(first, Source::InstanceAttribute).value, QStringLiteral("Attribute A"));
    QCOMPARE(field(first, Source::TemplateAttribute).state, State::Missing);
    QCOMPARE(field(first, Source::ObjectType).value, QStringLiteral("Type only"));
    QCOMPARE(field(first, Source::TemplateObject).value, QStringLiteral("Template object"));
    QCOMPARE(array.arrayIndex, 1);
    QCOMPARE(array.ObjectTitle()->value, QStringLiteral("Array one"));
    const auto fc = DescribeSclMember(refs, members[3], nullptr);
    QCOMPARE(field(fc, Source::InstanceAttribute).state, State::Ambiguous);
    QCOMPARE(field(fc, Source::TemplateAttribute).value, QStringLiteral("Status"));
    QCOMPARE(field(DescribeSclSignal(refs, first.logicalNode, "Arr", nullptr), Source::InstanceObject).state, State::Array);
    const auto grouped = DescribeSclSignal(refs, first.logicalNode, "Grouped", nullptr);
    QCOMPARE(field(grouped, Source::InstanceDU).state, State::SettingGroup);
    QVERIFY(grouped.ObjectTitle());
    QCOMPARE(grouped.ObjectTitle()->value, QStringLiteral("Fallback"));
    const auto multi = DescribeSclSignal(refs, first.logicalNode, "Multi", nullptr);
    QCOMPARE(field(multi, Source::InstanceDU).state, State::Ambiguous);
    QCOMPARE(field(multi, Source::InstanceDU).candidates.size(), 2);
    QVERIFY(multi.ObjectTitle());
    QCOMPARE(multi.ObjectTitle()->source, Source::TemplateObject);
    const auto duplicate = DescribeSclSignal(refs, first.logicalNode, "Dup", nullptr);
    QCOMPARE(field(duplicate, Source::InstanceObject).state, State::Ambiguous);
    QVERIFY(!duplicate.ObjectTitle());
    const auto empty = DescribeSclSignal(refs, first.logicalNode, "Empty", nullptr);
    QCOMPARE(field(empty, Source::InstanceDU).state, State::Empty);
    QCOMPARE(field(empty, Source::InstanceObject).state, State::Empty);
    QVERIFY(!empty.ObjectTitle());
    int unused = -1;
    for (int i = 0; i < int(snapshot.tasks.catalog.instances.size()); ++i)
    {
        const auto& instance = snapshot.tasks.catalog.instances[i];
        if (instance.logicalNode != first.logicalNode && snapshot.tasks.catalog.definitions[instance.definition].path == "Pos")
            unused = i;
    }
    QVERIFY(unused >= 0);
    QCOMPARE(SclTaskCells(snapshot, {SclTaskKind::Signal, unused}).front(), QStringLiteral("Unused B"));
    const auto detail = DescribeSclTaskRow(snapshot, {SclTaskKind::Signal, unused}, nullptr);
    const auto source = detail.descriptions.front().projection.ObjectTitle();
    QVERIFY(source);
    QCOMPARE(doc.nodes[source->node].name, QStringLiteral("Val"));
    QVERIFY(std::any_of(detail.evidence.begin(), detail.evidence.end(),
        [source](const auto& item) { return item.node == source->node; }));
}

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
    const auto& root = detail.members.front();
    const auto compact = ProjectSclRelation(snapshot, candidates.rows.front(), root, 0, nullptr);
    const auto expanded = ProjectSclRelation(snapshot, candidates.rows.front(), root, 1, nullptr);
    QVERIFY(expanded.nodes.size() > compact.nodes.size());
    QVERIFY(compact.hiddenMappings > 0);
    QCOMPARE(expanded.hiddenMappings, 0);
    QVERIFY(std::none_of(expanded.edges.begin(), expanded.edges.end(),
        [](const auto& edge) { return edge.relation == SclGraphRelation::Bound; }));
    QVERIFY(std::any_of(expanded.nodes.begin(), expanded.nodes.end(), [&](const auto& node) {
        return node.role == SclGraphRole::Input && node.member == root.member && node.input == root.input;
    }));
    auto crowded = candidates.rows.front();
    auto crowdedSnapshot = snapshot;
    auto document = std::make_shared<Document>(*snapshot.analysis.document);
    crowdedSnapshot.analysis.document = document;
    crowded.links.clear();
    const auto originalLink = *std::find_if(snapshot.tasks.memberLinks.cbegin(), snapshot.tasks.memberLinks.cend(),
        [&](const auto& link) { return snapshot.analysis.references[link.memberReference].source == root.member; });
    for (int i = 0; i < 100; ++i)
    {
        auto link = originalLink;
        link.input = int(crowdedSnapshot.tasks.inputs.size());
        crowdedSnapshot.tasks.inputs.push_back({NodeId(document->nodes.size())});
        const auto input = document->nodes[root.input];
        document->nodes.push_back(input);
        crowded.links.push_back(int(crowdedSnapshot.tasks.memberLinks.size()));
        crowdedSnapshot.tasks.memberLinks.push_back(link);
    }
    const auto bounded = ProjectSclRelation(crowdedSnapshot, crowded, root, 1, nullptr);
    QCOMPARE(bounded.nodes.size(), SclRelationGraph::NodeLimit);
    QVERIFY(bounded.edges.size() <= SclRelationGraph::EdgeLimit);
    QVERIFY(bounded.limited && bounded.hiddenMappings > 0);
    NodeId sink = NoNode;
    for (auto ied : snapshot.browse.ieds)
        if (parsed.document->attribute(ied, QStringLiteral("name")) == "RECEIVER")
            sink = ied;
    const auto incoming = QuerySclConnections(snapshot, sink, QStringLiteral("SV"), "4001", nullptr);
    QCOMPARE(incoming.rows.size(), 1);
    QCOMPARE(incoming.rows.front().candidates, 3);
    TaskControl cancelled;
    cancelled.cancelled = true;
    QVERIFY_EXCEPTION_THROWN(ProjectSclRelation(snapshot, candidates.rows.front(), root, 1, &cancelled), TaskCancelled);
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

void SclTaskIndexTest::LocalMmsStages()
{
    using namespace Scl;
    using namespace iec61850sim;
    const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
    if (path.isEmpty() || !QCoreApplication::arguments().contains("LocalMmsStages"))
        QSKIP("Run LocalMmsStages explicitly with IEC61850_LOCAL_SCL");
    QElapsedTimer timer;
    timer.start();
    const auto parsed = Reader::readFile(path);
    const auto readMs = timer.restart();
    QVERIFY(parsed.document);
    const auto analysis = ReferenceResolver::analyze(parsed);
    const auto referenceMs = timer.restart();
    SclBrowseIndex browse(*parsed.document, nullptr);
    const auto browseMs = timer.restart();
    browse.BuildScopes(analysis, nullptr);
    const auto scopeMs = timer.restart();
    const auto index = SclTaskIndex::Build(analysis, browse, nullptr);
    const auto taskMs = timer.elapsed();
    qInfo() << "MMS worker baseline Reader/References/Browse/Scopes/TaskIndex ms:"
            << readMs << referenceMs << browseMs << scopeMs << taskMs;
    qInfo() << "MMS worker baseline reports/signals/definitions/limited/sha256:"
            << analysis.reports.size() << index.catalog.instances.size() << index.catalog.definitions.size()
            << (index.catalog.limited || index.relationsLimited) << analysis.sha256;
    QVERIFY(!analysis.reports.empty());
    QVERIFY(!index.catalog.instances.empty());
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
    const auto publication = DescribeSclPublication(snapshot, snapshot.tasks.goose.front().control, 200, nullptr);
    QCOMPARE(publication.total, 223);
    QCOMPARE(publication.members.size(), 23);
    QCOMPARE(publication.members.front().cells[0], QStringLiteral("201"));
    QVERIFY(!publication.endpoints.empty());
    QCOMPARE(publication.endpoints.front().cells[2], QStringLiteral("0001"));
    const auto sv = DescribeSclPublication(snapshot, snapshot.tasks.sv.front().control, 0, nullptr);
    QVERIFY(sv.description.contains(QStringLiteral("sampleRate=true")));
    QVERIFY(sv.description.contains(QStringLiteral("单位/换算依据未确认")));
    QVERIFY(DescribeSclConnection(snapshot, query.rows[0], 0, true, nullptr).members.empty());
}

QTEST_GUILESS_MAIN(SclTaskIndexTest)
#include "SclTaskIndexTest.moc"
