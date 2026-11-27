#include "SclAnalysisService.hpp"
#include "SclReferenceTrace.hpp"
#include "SclHierarchyQuery.hpp"
#include <QtTest>
#include <QFile>
#include <algorithm>

using namespace Scl;
using namespace iec61850sim;
namespace {
QByteArray Fixture()
{
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task10Evidence.scd"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("fixture unavailable");
    return file.readAll();
}
std::shared_ptr<const SclSnapshot> Snapshot(QByteArray xml = Fixture())
{
    auto a = ReferenceResolver::analyze(Reader::readBytes(std::move(xml)));
    if (!a.document) qFatal("invalid fixture XML");
    SclBrowseIndex b(*a.document,nullptr); b.BuildScopes(a,nullptr);
    auto t = SclTaskIndex::Build(a,b,nullptr);
    auto p = MmsProjector::Build(a);
    auto i = SclMmsIndex::Build(p,a,nullptr);
    return std::make_shared<const SclSnapshot>(SclSnapshot{{},std::move(a),std::move(b),std::move(t),std::move(p),std::move(i)});
}
NodeId Named(const SclSnapshot& s, const char* tag, const char* name)
{
    const auto& doc = *s.analysis.document;
    for (int n = 0; n < int(doc.nodes.size()); ++n)
        if (doc.nodes[n].isScl() && doc.nodes[n].name == QLatin1String(tag) && doc.attribute(n,"name") == QLatin1String(name)) return n;
    return NoNode;
}
SclMmsRow Address(const SclSnapshot& s, const QString& object, const QString& field,
                  const char* owner = "EvidenceIED", NodeId arrayMember = NoNode)
{
    SclMmsRequest r; r.kind = SclMmsQueryKind::Addresses; r.scope.ied = Named(s,"IED",owner); r.arrayMember = arrayMember;
    for (int i = 0; i < int(s.tasks.catalog.instances.size()); ++i) {
        const auto& signal = s.tasks.catalog.instances[i];
        if (s.browse.iedOwners[signal.logicalNode] == r.scope.ied && s.tasks.catalog.definitions[signal.definition].path == object) {
            r.signal = i;
            const auto result = QuerySclMms(s,r,nullptr);
            for (const auto& row : result.rows) if (row.key.field == field && row.fc == "MX") return row;
        }
    }
    return {};
}
SclMmsRequest Request(const SclSnapshot& s, SclMmsQueryKind kind, const SclMmsRow& row)
{
    SclMmsRequest r; r.kind = kind; r.key = row.key; r.scope = row.addressScope;
    r.identityDocument = s.analysis.document; return r;
}
QByteArray AddMember(const QByteArray& declaration)
{
    auto xml = Fixture();
    xml.replace("<DataSet name=\"ResidualMag\">", "<DataSet name=\"ResidualMag\">" + declaration);
    return xml;
}
}

class SclObjectEvidenceTest : public QObject {
    Q_OBJECT
private slots:
    void AdvancedStructureMatchesTaskEvidence()
    {
        const auto s = Snapshot(); const auto row = Address(*s,"PhV.phsA","cVal.mag.f");
        const auto task = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr).objectEvidence;
        QVERIFY(task && task->instance.state == SclInstanceLocationState::Exact);
        SclMmsRequest r; r.kind = SclMmsQueryKind::NodeEvidence; r.identityDocument = s->analysis.document;
        r.target = task->instance.target.node; r.scope = row.addressScope;
        const auto advanced = QuerySclMms(*s,r,nullptr).objectEvidence; QVERIFY(advanced);
        QCOMPARE(advanced->cdc,task->cdc); QCOMPARE(advanced->objectType,task->objectType);
        QCOMPARE(advanced->objectPath,task->objectPath); QCOMPARE(advanced->attributePath,task->attributePath);
        QCOMPARE(advanced->fcEvidence.node,task->fcEvidence.node);
        QCOMPARE(advanced->instance.target.node,task->instance.target.node);
        QVERIFY(std::none_of(advanced->values.begin(),advanced->values.end(),[](const auto& value) {
            return value.kind == SclReferenceValueKind::MmsItem && !value.value.isEmpty();
        })); // Structure evidence does not fabricate an Item.
        const auto meta = s->browse.objects.value(r.target);
        QCOMPARE(meta.cdcType,task->objectType); QCOMPARE(meta.objectPath,task->objectPath);
        QCOMPARE(meta.attributePath,task->attributePath);
        r.target = row.key.declaration;
        const auto unbound = QuerySclMms(*s,r,nullptr).objectEvidence; QVERIFY(unbound);
        QVERIFY(unbound->values.isEmpty()); QCOMPARE(unbound->logicalNode,NoNode);
        QCOMPARE(unbound->instance.target.node,NoNode); QVERIFY(unbound->limitation.contains(QStringLiteral("未绑定")));
        SclHierarchyRequest hierarchy; hierarchy.kind = SclHierarchyKind::Members;
        for (const auto* name : {"LeafDs","ObjectDs","StructDs"}) {
            hierarchy.dataSet = Named(*s,"DataSet",name);
            const auto members = QuerySclHierarchy(s,hierarchy,nullptr); QVERIFY(!members.rows.empty());
            const auto& member = *members.rows.front().member;
            QCOMPARE(member.cdc,task->cdc); QCOMPARE(member.objectType,QStringLiteral("Phase"));
            r.target = members.rows.front().node;
            const auto e = QuerySclMms(*s,r,nullptr).objectEvidence; QVERIFY(e);
            QCOMPARE(e->cdc,member.cdc); QCOMPARE(e->objectPath,member.objectPath);
            QCOMPARE(e->attributePath,member.attributePath);
            if (QString::fromLatin1(name) == "LeafDs") QVERIFY(member.level.contains(QStringLiteral("精确属性")));
            else if (QString::fromLatin1(name) == "ObjectDs") QVERIFY(member.level.contains(QStringLiteral("DO / SDO")));
            else QVERIFY(member.level.contains(QStringLiteral("结构属性")));
        }
        r.target = task->instance.target.node; r.workLimit = 1;
        const auto limited = QuerySclMms(*s,r,nullptr); QVERIFY(limited.limited && !limited.objectEvidence);
        r.workLimit = 100000; r.identityDocument = Snapshot()->analysis.document;
        QVERIFY(!QuerySclMms(*s,r,nullptr).objectEvidence);
    }

    void AdvancedMissingAndAmbiguousDefinitions()
    {
        auto xml = Fixture(); xml.replace("<DA name=\"cVal\" fc=\"MX\"", "<DA name='cVal' fc='CF' bType='Struct' type='Vector'/><DA name=\"cVal\" fc=\"MX\"");
        auto s = Snapshot(xml);
        const auto node = Named(*s,"SDI","cVal"); QVERIFY(node != NoNode);
        QCOMPARE(s->browse.objects.value(node).state,LinkState::Ambiguous);
        QCOMPARE(s->browse.objects.value(node).declaration,NoNode);
        SclMmsRequest r; r.kind = SclMmsQueryKind::NodeEvidence; r.identityDocument = s->analysis.document; r.target = node;
        const auto e = QuerySclMms(*s,r,nullptr).objectEvidence; QVERIFY(e);
        QVERIFY(e->state != LinkState::Resolved); QVERIFY(e->values.isEmpty());
        xml = Fixture(); xml.replace("type=\"Phase\"","type=\"UnknownPhase\""); s = Snapshot(xml);
        const auto phase = Named(*s,"SDI","phsA");
        QCOMPARE(s->browse.objects.value(phase).state,LinkState::Missing);
        QVERIFY(s->browse.objects.value(phase).cdcType == NoNode);
    }

    void AdvancedPathBudgetDoesNotRestartAtDescendants()
    {
        QByteArray instances, definitions;
        const auto name = [](int i) { return QByteArray("Part") + QByteArray(110,'x') + QByteArray::number(i); };
        for (int i = 0; i < 24; ++i) {
            instances += (i ? "<SDI name='" : "<DOI name='") + name(i) + "'>";
            definitions += "<DOType id='T" + QByteArray::number(i) + "' cdc='SPS'>";
            if (i < 23) definitions += "<SDO name='" + name(i+1) + "' type='T" + QByteArray::number(i+1) + "'/>";
            definitions += "</DOType>";
        }
        instances += QByteArray("</SDI>").repeated(23) + "</DOI>";
        const auto s = Snapshot("<SCL xmlns='http://www.iec.ch/61850/2003/SCL'><IED name='Budget'><AccessPoint name='AP'><Server>"
            "<LDevice inst='LD'><LN lnClass='GGIO' inst='1' lnType='L'>" + instances + "</LN></LDevice></Server></AccessPoint></IED>"
            "<DataTypeTemplates><LNodeType id='L' lnClass='GGIO'><DO name='" + name(0) + "' type='T0'/></LNodeType>"
            + definitions + "</DataTypeTemplates></SCL>");
        QVERIFY(s->browse.objectsLimited);
        const auto last = Named(*s,"SDI",name(23).constData()); QVERIFY(last != NoNode);
        const auto meta = s->browse.objects.value(last);
        QCOMPARE(meta.state,LinkState::Unsupported); QVERIFY(meta.objectPath.isEmpty());
        QCOMPARE(meta.declaration,NoNode);
    }
    void TypedPathsOriginsAndCopyConsistency()
    {
        const auto s = Snapshot();
        const auto row = Address(*s,"PhV.phsA","cVal.mag.f"); QVERIFY(row.key.record >= 0);
        const auto result = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr);
        QVERIFY(result.objectEvidence && !result.limited);
        const auto& e = *result.objectEvidence;
        QCOMPARE(e.state,LinkState::Resolved); QCOMPARE(e.objectPath,QStringLiteral("PhV.phsA"));
        QCOMPARE(e.attributePath,QStringLiteral("cVal.mag.f")); QCOMPARE(e.cdc,QStringLiteral("CMV"));
        QCOMPARE(e.basicType,QStringLiteral("FLOAT32")); QCOMPARE(e.fc,QStringLiteral("MX"));
        QVERIFY(e.objectType == e.cdcEvidence.node && e.basicTypeEvidence.node == row.key.declaration);
        QCOMPARE(e.fcEvidence.node,row.fcEvidence.node);
        QCOMPARE(e.instance.state,SclInstanceLocationState::Exact);
        QCOMPARE(s->analysis.document->nodes[e.instance.target.node].name,QStringLiteral("DAI"));
        QCOMPARE(e.declarations.size(),5); QVERIFY(e.resolutions.size() >= 4);
        const auto values = ProjectSclMmsReferences(*s,row,nullptr);
        for (const auto& v : e.values) {
            const auto found = std::find_if(values.begin(),values.end(),[&](const auto& x) { return x.kind == v.kind; });
            QVERIFY(found != values.end()); QCOMPARE(found->value,v.value);
            SclTraceNode root; root.branch = SclTraceBranch::Root;
            QCOMPARE(TraceSclReference(*s,*found,root,nullptr).node.value,v.value);
            QVERIFY(!v.parts.isEmpty());
            for (const auto& part : v.parts) for (const auto& source : part.sources) {
                if (source.node == NoNode) continue;
                QCOMPARE(source.location.line,s->analysis.document->nodes[source.node].source.line);
                QCOMPARE(source.precision,SclEvidencePrecision::OpeningTag);
            }
        }
        QVERIFY(e.member.node == NoNode); // An independently queried address has no invented FCDA parent.
        const auto mv = Address(*s,"Vol","mag.f");
        const auto mvEvidence = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,mv),nullptr);
        QCOMPARE(mvEvidence.objectEvidence->cdc,QStringLiteral("MV"));
        QVERIFY(Address(*s,"Vol","ang.f").key.record < 0);
    }

    void AllDataSetsAndOccurrenceIdentity()
    {
        const auto s = Snapshot(); const auto mag = Address(*s,"PhV.phsA","cVal.mag.f");
        auto r = Request(*s,SclMmsQueryKind::DataSetRelations,mag); r.pageSize = 3;
        const auto first = QuerySclMms(*s,r,nullptr);
        QCOMPARE(first.total,7); QCOMPARE(first.rows.size(),size_t(3)); QVERIFY(first.hasMore);
        QVERIFY(first.dataSetCoverage.scanComplete); QCOMPARE(first.dataSetCoverage.observedMatches,7);
        QCOMPARE(first.dataSetCoverage.conclusion,SclDataSetConclusion::Referenced);
        QCOMPARE(first.rows[0].dataSet,first.rows[1].dataSet); QVERIFY(first.rows[0].key.node != first.rows[1].key.node);
        QCOMPARE(first.rows[0].key.ordinal,1); QCOMPARE(first.rows[1].key.ordinal,2);
        QCOMPARE(first.rows[0].dataSetRelation->usage->users.size(),2);
        QCOMPARE(first.rows[0].dataSetRelation->usage,first.rows[1].dataSetRelation->usage);
        QCOMPARE(first.rows[2].reportMatch,SclMmsReportMatch::ObjectCoverage);
        r.offset = 3; r.pageSize = 200;
        const auto rest = QuerySclMms(*s,r,nullptr); QCOMPARE(rest.rows.size(),size_t(4));
        QVERIFY(rest.rows[0].dataSetRelation->daNamePresent);
        QVERIFY(rest.rows[0].dataSetRelation->rawDaName.isEmpty());
        QVERIFY(!first.rows[2].dataSetRelation->daNamePresent);
        QCOMPARE(rest.rows[1].reportMatch,SclMmsReportMatch::AttributeCoverage);
        QCOMPARE(rest.rows[2].dataSetRelation->usage->users[0].kind,SclDataSetUserKind::Goose);
        QCOMPARE(rest.rows[3].dataSetRelation->usage->users[0].kind,SclDataSetUserKind::Sv);
        r.locate = rest.rows[3].key; r.offset = 0; r.pageSize = 2;
        const auto located = QuerySclMms(*s,r,nullptr);
        QVERIFY(located.locateFound); QCOMPARE(located.request.offset,6); QVERIFY(located.rows.front().dataSetRelation);

        const auto angle = Address(*s,"PhV.phsA","cVal.ang.f");
        r = Request(*s,SclMmsQueryKind::DataSetRelations,angle);
        const auto uses = QuerySclMms(*s,r,nullptr); QCOMPARE(uses.total,4);
        const auto unused = std::find_if(uses.rows.begin(),uses.rows.end(),[&](const auto& row) { return row.dataSet == Named(*s,"DataSet","WithoutUsers"); });
        QVERIFY(unused != uses.rows.end()); QVERIFY(unused->dataSetRelation->usage->complete);
        QVERIFY(unused->dataSetRelation->usage->users.isEmpty());
        r.target = Named(*s,"DataSet","LeafDs");
        const auto scoped = QuerySclMms(*s,r,nullptr);
        QCOMPARE(scoped.total,0); QCOMPARE(scoped.dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
    }

    void MemberEvidenceAndSignalEvidence()
    {
        const auto s = Snapshot();
        auto uses = QuerySclMms(*s,Request(*s,SclMmsQueryKind::DataSetRelations,Address(*s,"PhV.phsA","cVal.mag.f")),nullptr);
        for (int i : {0,2,3,4}) {
            const auto& row = uses.rows[i];
            auto r = Request(*s,SclMmsQueryKind::ObjectEvidence,row);
            const auto result = QuerySclMms(*s,r,nullptr); QVERIFY(result.objectEvidence);
            const auto& e = *result.objectEvidence;
            QCOMPARE(e.member.node,row.key.node); QCOMPARE(e.dataSet.node,row.dataSet);
            QCOMPARE(e.rawDoName,QStringLiteral("PhV.phsA"));
            QCOMPARE(e.daNamePresent,row.dataSetRelation->daNamePresent);
            if (row.reportMatch == SclMmsReportMatch::ObjectCoverage) {
                QVERIFY(e.attributePath.isEmpty()); QCOMPARE(e.objectPath,QStringLiteral("PhV.phsA"));
            }
            r.key.ordinal += 100; QVERIFY(!QuerySclMms(*s,r,nullptr).objectEvidence);
        }
        SclMmsRequest signalRequest; signalRequest.kind = SclMmsQueryKind::Signals; signalRequest.scope.ied = Named(*s,"IED","EvidenceIED");
        const auto rows = QuerySclMms(*s,signalRequest,nullptr);
        auto r = Request(*s,SclMmsQueryKind::ObjectEvidence,rows.rows.front()); r.scope = signalRequest.scope;
        const auto e = QuerySclMms(*s,r,nullptr).objectEvidence; QVERIFY(e);
        QCOMPARE(e->objectPath,QStringLiteral("PhV")); QCOMPARE(e->cdc,QStringLiteral("WYE"));
        QVERIFY(e->attributePath.isEmpty());
    }

    void InstanceAncestorsCandidatesAndSourcePrecision()
    {
        auto s = Snapshot();
        auto row = Address(*s,"PhV.res","cVal.ang.f");
        auto e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr).objectEvidence;
        QCOMPARE(e->instance.state,SclInstanceLocationState::Ancestor);
        QCOMPARE(s->analysis.document->attribute(e->instance.target.node,"name"),QStringLiteral("cVal"));
        QCOMPARE(e->instance.stoppedAt,QStringLiteral("ang"));
        const auto noData = Address(*s,"NoData","cVal.mag.f");
        e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,noData),nullptr).objectEvidence;
        QCOMPARE(e->instance.target.node,noData.key.node);
        auto xml = Fixture(); xml.replace("<SDI name=\"mag\"><DAI name=\"f\"><Val>110</Val></DAI></SDI>",
            "<SDI name='mag'><DAI name='f'/><DAI name='f'/></SDI>");
        s = Snapshot(xml); row = Address(*s,"PhV.phsA","cVal.mag.f");
        e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr).objectEvidence;
        QCOMPARE(e->instance.state,SclInstanceLocationState::Ambiguous); QCOMPARE(e->instance.candidates.size(),2);
        QCOMPARE(s->analysis.document->attribute(e->instance.target.node,"name"),QStringLiteral("mag"));
        xml = Fixture(); xml.replace("<DAI name=\"f\"><Val>110", "<DAI\n name=\"f\"><Val>110");
        s = Snapshot(xml); row = Address(*s,"PhV.phsA","cVal.mag.f");
        e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr).objectEvidence;
        const auto anchor = e->instance.target;
        QCOMPARE(anchor.precision,SclEvidencePrecision::OpeningTag);
        QVERIFY(s->analysis.source->openingTag(anchor.location).contains('\n'));
        QCOMPARE(s->analysis.source->bytes().mid(anchor.location.byteOffset,4),QByteArray("<DAI"));
    }

    void AbsenceUncertaintyFcAndScope()
    {
        const auto check = [](const std::shared_ptr<const SclSnapshot>& s, const char* path) {
            return QuerySclMms(*s,Request(*s,SclMmsQueryKind::DataSetRelations,Address(*s,QLatin1String(path),"cVal.ang.f")),nullptr);
        };
        auto s = Snapshot(); auto result = check(s,"PhV.res");
        QCOMPARE(result.dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced); QVERIFY(result.dataSetCoverage.scanComplete);
        const QByteArray broken = "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='NoData' daName='cVal.ang.missing' fc='MX'/>";
        s = Snapshot(AddMember(broken)); result = check(s,"NoData");
        QCOMPARE(result.total,0); QCOMPARE(result.dataSetCoverage.uncertainOccurrences,1);
        QCOMPARE(result.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain); QVERIFY(result.dataSetCoverage.scanComplete);
        QCOMPARE(result.dataSetCoverage.uncertainSources.size(),1);
        auto positiveAndUncertain = broken; positiveAndUncertain.replace("doName='NoData'","doName='PhV.phsA'");
        const auto mixed = Snapshot(AddMember(positiveAndUncertain));
        const auto mixedResult = check(mixed,"PhV.phsA");
        QCOMPARE(mixedResult.dataSetCoverage.conclusion,SclDataSetConclusion::Referenced);
        QCOMPARE(mixedResult.dataSetCoverage.observedMatches,4);
        QCOMPARE(mixedResult.dataSetCoverage.uncertainOccurrences,1);
        // A failure on a proven different branch does not invalidate this absence.
        QCOMPARE(check(s,"PhV.res").dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
        auto wrongFc = broken; wrongFc.replace("fc='MX'","fc='CF'");
        s = Snapshot(AddMember(wrongFc)); QCOMPARE(check(s,"NoData").dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
        const QByteArray beforeLn = "<FCDA ldInst='LD0' lnClass='' lnInst='1' doName='NoData' fc='MX'/>";
        s = Snapshot(AddMember(beforeLn)); QCOMPARE(check(s,"NoData").dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
        const auto other = Address(*s,"NoData","cVal.ang.f","OtherIED");
        QCOMPARE(QuerySclMms(*s,Request(*s,SclMmsQueryKind::DataSetRelations,other),nullptr).dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
    }

    void ArraySelectionAndBudgets()
    {
        const auto s = Snapshot(); auto array = Address(*s,"Array","cVal.ang.f");
        auto r = Request(*s,SclMmsQueryKind::DataSetRelations,array);
        auto result = QuerySclMms(*s,r,nullptr); QCOMPARE(result.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
        const auto ds = Named(*s,"DataSet","ArrayDs"); const auto member = s->analysis.document->nodes[ds].firstChild;
        array = Address(*s,"Array","cVal.ang.f","EvidenceIED",member);
        QCOMPARE(array.key.arrayIndex,1);
        r = Request(*s,SclMmsQueryKind::DataSetRelations,array); result = QuerySclMms(*s,r,nullptr);
        QCOMPARE(result.total,1); QCOMPARE(result.rows[0].key.node,member);
        const auto e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,array),nullptr).objectEvidence;
        QVERIFY(e); QCOMPARE(e->instance.state,SclInstanceLocationState::Exact);
        r.workLimit = 1; result = QuerySclMms(*s,r,nullptr);
        QVERIFY(result.limited && !result.dataSetCoverage.scanComplete);
        QCOMPARE(result.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
        r = Request(*s,SclMmsQueryKind::ObjectEvidence,array); r.workLimit = 1;
        QVERIFY(!QuerySclMms(*s,r,nullptr).objectEvidence);
        TaskControl cancelled; cancelled.cancelled.store(true);
        QVERIFY_EXCEPTION_THROWN(QuerySclMms(*s,r,&cancelled),TaskCancelled);
    }

    void SnapshotIdentityAndIndependentSessions()
    {
        const auto a = Snapshot(), b = Snapshot(); const auto row = Address(*a,"PhV.phsA","cVal.mag.f");
        auto r = Request(*a,SclMmsQueryKind::ObjectEvidence,row);
        QVERIFY(QuerySclMms(*b,r,nullptr).limited); QVERIFY(!QuerySclMms(*b,r,nullptr).objectEvidence);
        r.identityDocument.reset(); QVERIFY(QuerySclMms(*a,r,nullptr).limited);
        SclMmsQuerySession evidence, relations;
        evidence.SetContext(a,row.addressScope); relations.SetContext(a,row.addressScope);
        evidence.Query(Request(*a,SclMmsQueryKind::ObjectEvidence,row));
        relations.Query(Request(*a,SclMmsQueryKind::DataSetRelations,row));
        evidence.SetContext(b,Address(*b,"PhV.res","cVal.ang.f").addressScope);
        const auto newRow = Address(*b,"PhV.res","cVal.ang.f");
        const auto generation = evidence.Query(Request(*b,SclMmsQueryKind::ObjectEvidence,newRow));
        QTRY_VERIFY(evidence.result() && relations.result());
        QCOMPARE(evidence.result()->requestId,generation); QCOMPARE(evidence.result()->document,b->analysis.document);
        QCOMPARE(evidence.result()->objectEvidence->objectPath,QStringLiteral("PhV.res"));
        QCOMPARE(relations.result()->document,a->analysis.document);
        evidence.Cancel(); relations.Cancel(); QVERIFY(!evidence.result() && !relations.result());
    }

    void BoundedUncertaintyAndUsers()
    {
        const QByteArray broken = "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='NoData' daName='cVal.ang.missing' fc='MX'/>";
        auto s = Snapshot(AddMember(broken.repeated(70)));
        auto row = Address(*s,"NoData","cVal.ang.f");
        auto r = Request(*s,SclMmsQueryKind::DataSetRelations,row);
        auto result = QuerySclMms(*s,r,nullptr);
        QCOMPARE(result.dataSetCoverage.uncertainOccurrences,70); QCOMPARE(result.dataSetCoverage.uncertainSources.size(),64);
        QCOMPARE(result.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
        auto xml = Fixture();
        xml.replace("<ReportControl name=\"ReportA\"",QByteArray("<ReportControl name='Many' datSet='LeafDs'/>").repeated(520) + "<ReportControl name=\"ReportA\"");
        s = Snapshot(xml); row = Address(*s,"PhV.phsA","cVal.mag.f");
        result = QuerySclMms(*s,Request(*s,SclMmsQueryKind::DataSetRelations,row),nullptr);
        QVERIFY(result.limited && result.dataSetCoverage.scanComplete);
        QCOMPARE(result.rows.front().dataSetRelation->usage->users.size(),512);
        QVERIFY(!result.rows.front().dataSetRelation->usage->complete);
        QCOMPARE(result.rows.front().dataSetRelation->usage->observed,522);
    }

    void MissingTypesAmbiguousInstancesAndArrayBoundary()
    {
        auto xml = Fixture(); xml.replace("type=\"Vector\"","type=\"MissingVector\"");
        auto s = Snapshot(xml); auto row = Address(*s,"PhV.phsA","cVal");
        auto r = Request(*s,SclMmsQueryKind::ObjectEvidence,row);
        auto e = QuerySclMms(*s,r,nullptr).objectEvidence;
        QVERIFY(e && e->state != LinkState::Resolved); QCOMPARE(e->instance.state,SclInstanceLocationState::Unavailable);
        QVERIFY(std::none_of(e->values.begin(),e->values.end(),[](const auto& v) { return v.kind == SclReferenceValueKind::MmsItem; }));
        xml = Fixture(); xml.replace("<DA name=\"cVal\" fc=\"MX\"", "<DA name='cVal' fc='CF' bType='Struct' type='Vector'/><DA name=\"cVal\" fc=\"MX\"");
        s = Snapshot(xml); row = Address(*s,"PhV.phsA","cVal.mag.f");
        e = QuerySclMms(*s,Request(*s,SclMmsQueryKind::ObjectEvidence,row),nullptr).objectEvidence;
        QCOMPARE(e->instance.state,SclInstanceLocationState::Ambiguous);
        QCOMPARE(s->analysis.document->attribute(e->instance.target.node,"name"),QStringLiteral("phsA"));
        s = Snapshot(); const auto ds = Named(*s,"DataSet","ArrayDs");
        row = Address(*s,"Array","cVal.ang.f","EvidenceIED",s->analysis.document->nodes[ds].firstChild);
        row.key.arrayIndex = 99;
        const auto invalid = QuerySclMms(*s,Request(*s,SclMmsQueryKind::DataSetRelations,row),nullptr);
        QVERIFY(!invalid.dataSetCoverage.scanComplete); QCOMPARE(invalid.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
    }

    void DistinctServerAndAliasScope()
    {
        auto xml = Fixture();
        xml.replace("</AccessPoint></IED>", "</AccessPoint><AccessPoint name='Alias'><ServerAt apName='AP1'/></AccessPoint>"
            "<AccessPoint name='AP2'><Server><LDevice inst='LD0'><LN0 lnClass='LLN0' inst='' lnType='Zero'>"
            "<DataSet name='OtherServerDs'><FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='NoData' daName='cVal.ang.f' fc='MX'/></DataSet>"
            "</LN0><LN lnClass='MMXU' inst='1' lnType='Measurements'/></LDevice></Server></AccessPoint></IED>");
        const auto s = Snapshot(xml); const auto row = Address(*s,"NoData","cVal.ang.f");
        auto r = Request(*s,SclMmsQueryKind::DataSetRelations,row);
        QCOMPARE(QuerySclMms(*s,r,nullptr).dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
        r.scope.accessPoint = Named(*s,"AccessPoint","Alias");
        QCOMPARE(QuerySclMms(*s,r,nullptr).dataSetCoverage.conclusion,SclDataSetConclusion::NotReferenced);
        r.scope.accessPoint = Named(*s,"AccessPoint","AP2");
        const auto wrongScope = QuerySclMms(*s,r,nullptr);
        QVERIFY(!wrongScope.dataSetCoverage.scanComplete); QCOMPARE(wrongScope.dataSetCoverage.conclusion,SclDataSetConclusion::Uncertain);
    }
};
QTEST_GUILESS_MAIN(SclObjectEvidenceTest)
#include "SclObjectEvidenceTest.moc"
