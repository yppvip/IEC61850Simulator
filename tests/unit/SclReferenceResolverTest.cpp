#include "SclReferenceResolver.hpp"

#include <QtTest>
#include <algorithm>

using namespace Scl;

namespace {
ReferenceAnalysis fixture(const char *name)
{
    return ReferenceResolver::analyze(Reader::readFile(QStringLiteral(SCL_FIXTURE_DIR "/") + QString::fromLatin1(name)));
}

ReferenceAnalysis analyze(const QByteArray &body)
{
    return ReferenceResolver::analyze(Reader::readBytes("<SCL xmlns='" + NamespaceUri.toUtf8() + "'>" + body + "</SCL>"));
}

std::vector<Reference> links(const ReferenceAnalysis &analysis, LinkKind kind)
{
    std::vector<Reference> result;
    std::copy_if(analysis.references.begin(), analysis.references.end(), std::back_inserter(result),
                 [kind](const Reference &ref) { return ref.kind == kind; });
    return result;
}

bool hasFinding(const ReferenceAnalysis &analysis, const QString &rule)
{
    return std::any_of(analysis.findings.begin(), analysis.findings.end(),
                       [&](const Finding &finding) { return finding.rule == rule; });
}

constexpr auto Templates = R"(
<DataTypeTemplates>
<LNodeType id="L" lnClass="LLN0"><DO name="Obj" type="D"/></LNodeType>
<DOType id="D" cdc="SPS"><SDO name="Sub" type="SubType"/></DOType>
<DOType id="SubType" cdc="SPS"><DA name="values" fc="ST" bType="Struct" type="A" count="3"/>
<DA name="dU" fc="DC" bType="Unicode255"/></DOType>
<DAType id="A"><BDA name="leaf" bType="Enum" type="E"/></DAType>
<EnumType id="E"><EnumVal ord="1">on</EnumVal></EnumType>
</DataTypeTemplates>)";
}

class SclReferenceResolverTest : public QObject {
    Q_OBJECT

private slots:
    void baselineMappings()
    {
        const auto model = fixture("NamespaceCollision.scd");
        QVERIFY(model.document);
        QVERIFY(hasFinding(model, "EXT001"));
        for (const auto &finding : model.findings)
            QVERIFY2(finding.severity != Severity::Error, qPrintable(finding.rule + ' ' + finding.detail));
        QCOMPARE(model.sha256.size(), 64);
        const auto members = links(model, LinkKind::Fcda);
        QCOMPARE(members.size(), size_t(3));
        for (int i = 0; i < 3; ++i) {
            QCOMPARE(members[i].memberOrdinal, i + 1);
            QCOMPARE(members[i].state, LinkState::Resolved);
        }
        QCOMPARE(members[1].candidates, members[2].candidates);
        QCOMPARE(model.incoming.count(members[1].candidates.front()), 2);
        QCOMPARE(members[0].mmsItem, QStringLiteral("GGIO1$ST$Ind1"));
        QCOMPARE(members[1].mmsItem, QStringLiteral("GGIO1$ST$Ind1$stVal"));
        QCOMPARE(members[1].mmsDomain, QStringLiteral("EXPLICIT_LD"));
        QCOMPARE(members[1].objectReference, QStringLiteral("EXPLICIT_LD/GGIO1.Ind1.stVal"));
        QCOMPARE(model.reports.size(), size_t(2));
        QCOMPARE(model.reports[0].rptId, QStringLiteral("TEST_EVENTS"));
        QCOMPARE(model.reports[0].definitionReference, QStringLiteral("EXPLICIT_LD/LLN0.BR.brcbEvents"));
        QCOMPARE(model.reports[0].dataSetReference, QStringLiteral("EXPLICIT_LD/LLN0$dsEvents"));
        QCOMPARE(model.reports[0].mmsItem, QStringLiteral("LLN0$BR$brcbEvents"));
        QCOMPARE(model.reports[1].mmsItem, QStringLiteral("LLN0$RP$urcbEvents"));
        QVERIFY(model.reports[1].rptId.isNull());
        for (const auto &link : links(model, LinkKind::NetworkControl))
            QCOMPARE(link.state, LinkState::Resolved);
        QCOMPARE(links(model, LinkKind::NetworkControl).size(), size_t(2));
    }

    void diagnosticAndCoverage()
    {
        const auto duplicate = fixture("DuplicateDataSet.scd");
        QVERIFY(hasFinding(duplicate, "KEY001"));
        QVERIFY(hasFinding(duplicate, "ENG001"));
        const auto ambiguous = links(duplicate, LinkKind::ControlDataSet);
        QVERIFY(!ambiguous.empty());
        QCOMPARE(ambiguous.front().state, LinkState::Ambiguous);
        QCOMPARE(ambiguous.front().candidates.size(), size_t(2));
        QVERIFY(hasFinding(fixture("DanglingDataSet.scd"), "REF001"));
        QVERIFY(hasFinding(fixture("MissingType.scd"), "TYP001"));
        QVERIFY(hasFinding(fixture("UnknownVersion.scd"), "VER001"));
        QVERIFY(hasFinding(analyze("<IED><AccessPoint name='A'/></IED>"), "STR001"));
        const auto bad = fixture("Truncated.scd");
        QVERIFY(hasFinding(bad, "XML002"));
        QVERIFY(bad.references.empty());
        for (const auto &rule : bad.coverage) {
            if (rule.id == "REF002") QCOMPARE(rule.state, Coverage::Blocked);
            if (rule.id == "XSD") QCOMPARE(rule.state, Coverage::NotImplemented);
        }
        const auto &finding = duplicate.findings.front();
        QVERIFY(finding.location.line > 0);
        QVERIFY(!finding.objectPath.isEmpty());
        QVERIFY(!finding.suggestion.isEmpty());
    }

    void nestedArraysAndCrossLd()
    {
        const auto model = analyze(QByteArray(R"(
<IED name="I"><AccessPoint name="A"><Server>
<LDevice inst="Owner"><LN0 lnType="L"><DataSet name="d">
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="2"/>
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj.Sub" daName="values(1).leaf" fc="ST"/>
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="ST" ix="3"/>
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj" fc="MX"/>
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj" fc="ST"/>
<FCDA ldInst="Target" lnClass="LLN0" doName="Obj.Sub" daName="values.leaf" fc="MX" ix="1"/>
</DataSet><ReportControl name="r" datSet="d"/></LN0></LDevice>
<LDevice inst="Target"><LN0 lnType="L"><DataSet name="d"/><ReportControl name="r" datSet="d"/>
<DOI name="Obj"><SDI name="Sub"><DAI name="dU"><Val>Target description</Val></DAI>
<SDI name="values" ix="2"><DAI name="leaf"><Val>on</Val></DAI></SDI>
</SDI></DOI></LN0></LDevice>
</Server></AccessPoint>
<AccessPoint name="B"><Server><LDevice inst="Target"><LN0 lnType="L"/></LDevice></Server></AccessPoint></IED>
)") + Templates);
        const auto members = links(model, LinkKind::Fcda);
        QCOMPARE(members.size(), size_t(6));
        QCOMPARE(members[0].state, LinkState::Resolved);
        QCOMPARE(members[0].mmsDomain, QStringLiteral("ITarget"));
        QCOMPARE(members[0].mmsItem, QStringLiteral("LLN0$ST$Obj$Sub$values"));
        QCOMPARE(members[0].mmsComponent, QStringLiteral("leaf"));
        QCOMPARE(members[0].arrayIndex, 2);
        QCOMPARE(members[0].objectReference, QStringLiteral("ITarget/LLN0.Obj.Sub.values(2).leaf"));
        QCOMPARE(members[1].state, LinkState::Resolved);
        QCOMPARE(members[1].arrayIndex, 1);
        QVERIFY(members[0].instanceNode != NoNode && members[0].instanceDescription != NoNode);
        QCOMPARE(model.document->attribute(members[0].instanceNode, QStringLiteral("name")), QStringLiteral("leaf"));
        QCOMPARE(model.document->nodes[members[0].instanceDescription].text, QStringLiteral("Target description"));
        QCOMPARE(members[1].instanceNode, NoNode);
        QCOMPARE(members[1].instanceDescription, members[0].instanceDescription);
        QCOMPARE(members[2].state, LinkState::Missing);
        QCOMPARE(members[2].instanceDescription, NoNode);
        QCOMPARE(members[3].state, LinkState::Missing);
        QCOMPARE(members[4].state, LinkState::Resolved);
        QCOMPARE(members[4].instanceDescription, NoNode);
        QCOMPARE(members[5].state, LinkState::Missing);
        const auto sets = links(model, LinkKind::ControlDataSet);
        QCOMPARE(sets[0].state, LinkState::Resolved);
        QCOMPARE(sets[1].state, LinkState::Resolved);
        QVERIFY(sets[0].candidates != sets[1].candidates);
        QVERIFY(!hasFinding(model, "KEY001"));
    }

    void externalAndCommunication()
    {
        const auto model = analyze(QByteArray(R"(
<Communication><SubNetwork name="Bus">
<ConnectedAP iedName="I" apName="Alias"><GSE ldInst="L1" cbName="g"/><SMV ldInst="L1" cbName="missing"/></ConnectedAP>
<ConnectedAP iedName="I" apName="Missing"/>
</SubNetwork></Communication>
<IED name="I"><AccessPoint name="Alias"><ServerAt apName="A"/></AccessPoint><AccessPoint name="A"><Server>
<LDevice inst="L1"><LN0 lnType="L"><GSEControl name="g"/><Inputs>
<ExtRef intAddr="unbound"/>
<ExtRef iedName="Remote" ldInst="L1" lnClass="LLN0" doName="Obj"/>
<ExtRef iedName="I" ldInst="L1"/>
<ExtRef iedName="I" ldInst="L1" lnClass="LLN0" doName="Obj" serviceType="GOOSE" srcLDInst="L1" srcLNClass="LLN0" srcCBName="g"/>
<ExtRef iedName="I" ldInst="L2" lnClass="LLN0" doName="Obj"/>
<ExtRef iedName="I" ldInst="L3" lnClass="LLN0" doName="Obj"/>
</Inputs></LN0></LDevice>
<LDevice inst="L2"><LN0 lnType="L"/></LDevice>
</Server></AccessPoint>
<AccessPoint name="B"><Server><LDevice inst="L2"><LN0 lnType="L"/></LDevice></Server></AccessPoint></IED>
)") + Templates);
        const auto data = links(model, LinkKind::ExtRefData);
        QCOMPARE(data.size(), size_t(6));
        QCOMPARE(data[0].state, LinkState::Unbound);
        QCOMPARE(data[1].state, LinkState::External);
        QCOMPARE(data[2].state, LinkState::Partial);
        QCOMPARE(data[3].state, LinkState::Resolved);
        QCOMPARE(data[4].state, LinkState::Ambiguous);
        QCOMPARE(data[5].state, LinkState::Missing);
        const auto controls = links(model, LinkKind::ExtRefControl);
        QCOMPARE(controls.size(), size_t(1));
        QCOMPARE(controls[0].state, LinkState::Resolved);
        const auto net = links(model, LinkKind::NetworkControl);
        QCOMPARE(net[0].state, LinkState::Resolved);
        QCOMPARE(net[1].state, LinkState::Missing);
        QCOMPARE(links(model, LinkKind::ConnectedAp)[1].state, LinkState::Missing);
    }

    void templateGraph()
    {
        const auto model = analyze(R"(
<IED name="I"><AccessPoint name="A"><Server><LDevice inst="L"><LN0 lnType="T"><DataSet name="d">
<FCDA ldInst="L" lnClass="LLN0" doName="Root" fc="ST"/>
</DataSet></LN0></LDevice></Server></AccessPoint></IED>
<DataTypeTemplates>
<LNodeType id="T" lnClass="LLN0"><DO name="Root" type="D"/></LNodeType>
<DOType id="D"><SDO name="Loop" type="D"/><DA name="bad" fc="MX" bType="Enum" type="D"/></DOType>
</DataTypeTemplates>)");
        QVERIFY(hasFinding(model, "TYP002"));
        QVERIFY(hasFinding(model, "TYP003"));
        QCOMPARE(links(model, LinkKind::Fcda).front().state, LinkState::Blocked);
        QVERIFY(!hasFinding(model, "REF002"));
        QVERIFY(!hasFinding(analyze(QByteArray(Templates)), "TYP003"));

        QByteArray chain("<DataTypeTemplates>");
        for (int i = 0; i < 66; ++i) {
            chain += "<DOType id='D" + QByteArray::number(i) + "'>";
            if (i < 65) chain += "<SDO name='next' type='D" + QByteArray::number(i + 1) + "'/>";
            chain += "</DOType>";
        }
        QVERIFY(hasFinding(analyze(chain + "</DataTypeTemplates>"), "TYP003"));
    }
};

QTEST_APPLESS_MAIN(SclReferenceResolverTest)
#include "SclReferenceResolverTest.moc"
