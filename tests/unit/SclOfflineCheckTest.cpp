#include "SclAnalysisService.hpp"
#include <QtTest>
#include <QFile>
using namespace Scl;
using namespace iec61850sim;
namespace {
QByteArray Fixture() {
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task10Evidence.scd"));
    if (!file.open(QIODevice::ReadOnly)) qFatal("fixture unavailable"); return file.readAll();
}
std::shared_ptr<const SclSnapshot> Snapshot(QByteArray xml = Fixture()) {
    auto a = ReferenceResolver::analyze(Reader::readBytes(std::move(xml)));
    if (!a.document) qFatal("invalid fixture");
    SclBrowseIndex b(*a.document,nullptr); b.BuildScopes(a,nullptr);
    auto t = SclTaskIndex::Build(a,b,nullptr); auto p = MmsProjector::Build(a); auto i = SclMmsIndex::Build(p,a,nullptr);
    return std::make_shared<const SclSnapshot>(SclSnapshot{{},std::move(a),std::move(b),std::move(t),std::move(p),std::move(i)});
}
SclMmsRequest Request(const SclSnapshot& s,QString value,QString fc = "MX",bool mms = false) {
    SclMmsRequest r; r.kind = SclMmsQueryKind::OfflineCheck; r.identityDocument = s.analysis.document;
    r.check = {mms ? SclCheckInputKind::MmsItem : SclCheckInputKind::ObjectReference,value,{},fc}; return r;
}
std::shared_ptr<const SclOfflineCheck> Check(const SclSnapshot& s,QString value,QString fc = "MX",bool mms = false) {
    return QuerySclMms(s,Request(s,value,fc,mms),nullptr).check;
}
}
class SclOfflineCheckTest final : public QObject {
    Q_OBJECT
private slots:
    void UniqueIdentityAndIndependentCoverage() {
        const auto s = Snapshot();
        const auto c = Check(*s,"EvidenceDomain/MMXU1.PhV.phsA.cVal.mag.f");
        QVERIFY(c && c->mappingPassed && c->target); QCOMPARE(c->state,SclCheckState::Supported);
        QCOMPARE(c->target->item,QStringLiteral("MMXU1$MX$PhV$phsA$cVal$mag$f"));
        QCOMPARE(c->target->key.field,QStringLiteral("cVal.mag.f")); QCOMPARE(c->target->key.parentReport,NoNode);
        QCOMPARE(c->dataSets.conclusion,SclDataSetConclusion::Referenced);
        QVERIFY(c->coverageKinds.contains(SclMmsReportMatch::ExactAttribute));
        QVERIFY(c->coverageKinds.contains(SclMmsReportMatch::AttributeCoverage));
        QVERIFY(c->coverageKinds.contains(SclMmsReportMatch::ObjectCoverage));
        const auto m = Check(*s,"EvidenceDomain/MMXU1$MX$PhV$phsA$cVal$mag$f",{},true);
        QVERIFY(m && m->mappingPassed); QCOMPARE(m->target->key,c->target->key);
        const auto noData = Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f");
        QVERIFY(noData->mappingPassed && noData->target); QCOMPARE(noData->dataSets.conclusion,SclDataSetConclusion::NotReferenced);
        const auto structure = Check(*s,"EvidenceDomain/MMXU1.PhV.phsA.cVal");
        QVERIFY(structure->mappingPassed); QCOMPARE(structure->target->shape,SclMmsAddressShape::Structure);
        const auto object = Check(*s,"EvidenceDomain/MMXN1.Vol");
        QVERIFY(object->mappingPassed); QCOMPARE(object->target->key.kind,SclMmsRecordKind::Signal);
        QVERIFY(Check(*s,"EvidenceDomain/MMXU1.PhV")->mappingPassed); // FC inherited through real SDO types.
    }
    void FailureKeepsActualTypeEvidence() {
        const auto s = Snapshot(); const auto c = Check(*s,"EvidenceDomain/MMXN1.Vol.ang.f");
        QVERIFY(c && !c->mappingPassed && !c->target); QCOMPARE(c->state,SclCheckState::Rejected);
        QCOMPARE(c->stoppedAt,QStringLiteral("ang"));
        QCOMPARE(s->analysis.document->nodes[c->lastConfirmed.node].name,QStringLiteral("DOType"));
        QCOMPARE(s->analysis.document->attribute(c->lastConfirmed.node,"id"),QStringLiteral("MagnitudeValue"));
        QVERIFY(c->lastConfirmed.location.line > 0); QCOMPARE(c->steps.back().segment,QStringLiteral("Vol"));
        QVERIFY(Check(*s,"EvidenceDomain/MMXN1$MX$Vol$mag$f",{},true)->mappingPassed);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f","CF")->state,SclCheckState::Rejected);
        QCOMPARE(Check(*s,"EvidenceDomain/NoSuchLN.NoData","MX")->state,SclCheckState::Rejected);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol.mag.f.extra")->state,SclCheckState::Rejected);
    }
    void MissingContextAndAmbiguity() {
        auto s = Snapshot();
        auto c = Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f",{});
        QCOMPARE(c->state,SclCheckState::ContextRequired); QCOMPARE(c->fcCandidates,QStringList{"MX"}); QVERIFY(!c->target);
        auto r = Request(*s,"MMXU1$MX$NoData$cVal$ang$f",{},true);
        c = QuerySclMms(*s,r,nullptr).check; QVERIFY(c && c->candidates.size() == 2 && !c->target);
        r.check.logicalDevice = c->candidates[0].logicalDevice;
        r.check.domain = s->mmsIndex.domainsByDevice.value(r.check.logicalDevice).name;
        QVERIFY(QuerySclMms(*s,r,nullptr).check->mappingPassed);
        auto xml = Fixture();
        xml.replace("</AccessPoint></IED>","</AccessPoint><AccessPoint name='AP2'><Server><LDevice inst='Duplicate' ldName='EvidenceDomain'><LN0 lnClass='LLN0' inst='' lnType='Zero'/><LN lnClass='MMXU' inst='1' lnType='Measurements'/></LDevice></Server></AccessPoint></IED>");
        s = Snapshot(xml); c = Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f");
        QCOMPARE(c->state,SclCheckState::Uncertain); QVERIFY(c->candidates.size() > 1 && !c->target);
        xml = Fixture(); xml.replace("<DA name=\"cVal\" fc=\"MX\"","<DA name='cVal' fc='CF' bType='Struct' type='Vector'/><DA name=\"cVal\" fc=\"MX\"");
        s = Snapshot(xml); QCOMPARE(Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f",{})->state,SclCheckState::ContextRequired);
        QVERIFY(Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f","CF")->mappingPassed);
        xml = Fixture(); xml.replace("type=\"Vector\"","type=\"Absent\"");
        s = Snapshot(xml); c = Check(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f");
        QCOMPARE(c->state,SclCheckState::Uncertain); QVERIFY(!c->target);
    }
    void ArraysSyntaxAndBudgets() {
        const auto s = Snapshot();
        QCOMPARE(Check(*s,"EvidenceDomain/MMXU1.Array.cVal.ang.f")->state,SclCheckState::ContextRequired);
        auto c = Check(*s,"EvidenceDomain/MMXU1.Array(1).cVal.ang.f");
        QVERIFY(c->mappingPassed && c->target); QCOMPARE(c->target->shape,SclMmsAddressShape::AlternateAccess);
        QCOMPARE(c->target->item,QStringLiteral("MMXU1$MX$Array")); QCOMPARE(c->target->component,QStringLiteral("cVal$ang$f"));
        QCOMPARE(c->target->key.arrayIndex,1); QCOMPARE(c->dataSets.observedMatches,1);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXU1.Array(3).cVal.ang.f")->state,SclCheckState::Rejected);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol(1).mag.f")->state,SclCheckState::Rejected);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXN1..Vol")->state,SclCheckState::Rejected);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol[1].mag.f")->state,SclCheckState::Unsupported);
        QCOMPARE(Check(*s,"EvidenceDomain/LLN0$BR$ReportA",{},true)->state,SclCheckState::Unsupported);
        QCOMPARE(Check(*s,"EvidenceDomain/rptID",{},true)->state,SclCheckState::Unsupported);
        QCOMPARE(Check(*s,"EvidenceDomain/LLN0.ReportA")->state,SclCheckState::Unsupported);
        QCOMPARE(Check(*s,"EvidenceDomain/MMXU1$$NoData",{},true)->state,SclCheckState::Rejected);
        auto r = Request(*s,"EvidenceDomain/MMXU1.NoData.cVal.ang.f"); r.workLimit = 1;
        auto result = QuerySclMms(*s,r,nullptr); QVERIFY(result.limited && result.check && !result.check->target && !result.check->mappingPassed);
        TaskControl control; control.cancelled.store(true); QVERIFY_EXCEPTION_THROWN(QuerySclMms(*s,r,&control),TaskCancelled);
        r.identityDocument.reset(); QVERIFY(!QuerySclMms(*s,r,nullptr).check);
    }
    void UnknownVersionTypesAndScopeCompleteness() {
        auto invalidLn = Fixture(); invalidLn.replace("lnClass=\"MMXN\" inst=\"1\"","lnClass=\"MMXN\"");
        QCOMPARE(Check(*Snapshot(invalidLn),"EvidenceDomain/MMXN1.Vol")->state,SclCheckState::Uncertain);
        auto xml = Fixture(); xml.replace("version=\"2007\"","version=\"2099\"");
        auto s = Snapshot(xml); QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol.mag.f")->state,SclCheckState::Uncertain);
        xml = Fixture(); xml.replace("bType=\"FLOAT32\"","bType=\"FutureType\"");
        s = Snapshot(xml); QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol.mag.f.more")->state,SclCheckState::Uncertain);
        xml = Fixture(); xml.replace("<DAType id=\"FloatValue\">","<DAType id='FloatValue'/><DAType id=\"FloatValue\">");
        s = Snapshot(xml); QCOMPARE(Check(*s,"EvidenceDomain/MMXN1.Vol.mag.f")->state,SclCheckState::Uncertain);
        xml = Fixture(); xml.replace("</AccessPoint></IED>","</AccessPoint><AccessPoint name='Broken'><ServerAt apName='Missing'/></AccessPoint></IED>");
        s = Snapshot(xml); auto r = Request(*s,"EvidenceDomain/MMXN1.Vol.mag.f");
        QCOMPARE(QuerySclMms(*s,r,nullptr).check->state,SclCheckState::Uncertain);
        r.scope.ied = s->browse.ieds.front();
        for (NodeId n = 0; n < int(s->analysis.document->nodes.size()); ++n)
            if (s->analysis.document->nodes[n].name == "AccessPoint" && s->analysis.document->attribute(n,"name") == "AP1") { r.scope.accessPoint = n; break; }
        QVERIFY(QuerySclMms(*s,r,nullptr).check->mappingPassed);
        // Work exhaustion after resolving a model also revokes a previously prepared target.
        s = Snapshot(); r = Request(*s,"EvidenceDomain/MMXU1.PhV.phsA.cVal.mag.f");
        bool sawPartial = false;
        for (int budget = 40; budget < 1000; budget += 20) {
            r.workLimit = budget; const auto result = QuerySclMms(*s,r,nullptr);
            if (result.limited && result.check && result.check->steps.size() >= 5) {
                sawPartial = true; QVERIFY(!result.check->mappingPassed && !result.check->target);
                QVERIFY(result.rows.empty());
            }
        }
        QVERIFY(sawPartial);
    }
    void LatestRequestAndSnapshot() {
        const auto a = Snapshot(), b = Snapshot(); SclMmsQuerySession session;
        session.SetContext(a); session.Query(Request(*a,"EvidenceDomain/MMXN1.Vol.ang.f"));
        session.Query(Request(*a,"EvidenceDomain/MMXN1.Vol.mag.f"));
        session.SetContext(b); const auto id = session.Query(Request(*b,"EvidenceDomain/MMXU1.NoData.cVal.ang.f"));
        QTRY_VERIFY(session.result()); QCOMPARE(session.result()->requestId,id);
        QCOMPARE(session.result()->document,b->analysis.document); QVERIFY(session.result()->check->mappingPassed);
        QVERIFY(QuerySclMms(*b,Request(*a,"EvidenceDomain/MMXN1.Vol.mag.f"),nullptr).limited);
        session.Cancel(); QVERIFY(!session.result());
    }
    void CandidateDeliveryIsBounded() {
        auto xml = Fixture(); QByteArray many;
        for (int i = 0; i < 70; ++i) many += "<DA name='cVal' fc='F" + QByteArray::number(i) + "' bType='Struct' type='Vector'/>";
        xml.replace("<DOType id=\"Phase\" cdc=\"CMV\">","<DOType id=\"Phase\" cdc=\"CMV\">" + many);
        const auto s = Snapshot(xml);
        auto result = QuerySclMms(*s,Request(*s,"EvidenceDomain/MMXU1.NoData"),nullptr);
        QVERIFY(result.limited && result.check && !result.check->mappingPassed && !result.check->target);
        QVERIFY(result.check->fcCandidates.size() <= 64);
        result = QuerySclMms(*s,Request(*s,"EvidenceDomain/MMXU1.NoData.cVal",{}),nullptr);
        QVERIFY(result.limited && result.check && !result.check->target);
        QVERIFY(result.check->fcCandidates.size() <= 64);
    }
};
QTEST_GUILESS_MAIN(SclOfflineCheckTest)
#include "SclOfflineCheckTest.moc"
