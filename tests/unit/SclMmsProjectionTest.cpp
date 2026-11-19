#include "SclMmsProjection.hpp"

#include <QtTest>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

using namespace Scl;

namespace {
QByteArray fixtureBytes(const char *name = "Task6Mms.scd")
{
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

ReferenceAnalysis analyze(QByteArray xml)
{
    return ReferenceResolver::analyze(Reader::readBytes(std::move(xml)));
}

MmsProjection project(QByteArray xml) { return MmsProjector::Build(analyze(std::move(xml))); }

const MmsReportDefinition &report(const MmsProjection &p, const char *name)
{
    for (const auto &r : p.reports) if (r.fields.value("name").raw == QLatin1String(name)) return r;
    qFatal("Missing report");
}

RuleCoverage coverage(const MmsProjection &p, const char *id)
{
    for (const auto &rule : p.coverage) if (rule.id == QLatin1String(id)) return rule;
    qFatal("Missing coverage");
}

bool finding(const MmsProjection &p, const char *id)
{
    return std::any_of(p.findings.begin(), p.findings.end(), [id](const Finding &f) { return f.rule == QLatin1String(id); });
}

QByteArray reportXml(const QByteArray &attributes, const QByteArray &enabled = "<RptEnabled max='2'/>")
{
    return "<SCL xmlns='" + NamespaceUri.toUtf8() + "' version='2007' revision='B' release='4'>"
        "<IED name='A'><AccessPoint name='AP'><Server><LDevice inst='L'><LN0 lnClass='LLN0' inst='' lnType='T'>"
        "<ReportControl name='r' " + attributes + "><TrgOps/><OptFields/>" + enabled + "</ReportControl>"
        "</LN0></LDevice></Server></AccessPoint></IED>"
        "<DataTypeTemplates><LNodeType id='T' lnClass='LLN0'/></DataTypeTemplates></SCL>";
}
}

class SclMmsProjectionTest : public QObject {
    Q_OBJECT
private slots:
    void definitionsInstancesAndEvidence()
    {
        const auto base = analyze(fixtureBytes());
        const auto p = MmsProjector::Build(base);
        QVERIFY(p.complete); QVERIFY(!p.resourceLimited);
        QCOMPARE(p.sha256, base.sha256); QCOMPARE(p.document, base.document); QCOMPARE(p.source, base.source);
        QCOMPARE(p.profile, MmsProfile::Explicit2007B4);
        QCOMPARE(p.reports.size(), size_t(4)); QCOMPARE(p.instances.size(), size_t(2));
        const auto &r = report(p, "brStatus");
        QCOMPARE(r.dataSet.state, LinkState::Resolved); QCOMPARE(r.memberCount, 3);
        QCOMPARE(r.dataSetReference, QStringLiteral("DemoDomain/LLN0$Events"));
        QCOMPARE(r.dataSetMmsName, QStringLiteral("LLN0$Events"));
        QCOMPARE(r.definitionReference, QStringLiteral("DemoDomain/LLN0.BR.brStatus"));
        QCOMPARE(r.definitionMmsItem, QStringLiteral("LLN0$BR$brStatus"));
        QCOMPARE(p.instances[0].reference, QStringLiteral("DemoDomain/LLN0.BR.brStatus01"));
        QCOMPARE(p.instances[1].mmsItem, QStringLiteral("LLN0$BR$brStatus02"));
        QCOMPARE(p.instances[0].definition, r.node); QCOMPARE(p.instances[1].ordinal, 2);
        for (const auto &instance : p.instances) {
            QCOMPARE(instance.rptId.value.toString(), QStringLiteral("CustomReport01")); // No suffix added.
            QCOMPARE(instance.rptId.state, MmsValueState::Explicit);
            QCOMPARE(instance.evidence.size(), 4);
            for (const auto &e : instance.evidence) {
                QVERIFY(e.node >= 0); QCOMPARE(e.rule, QStringLiteral("MMS002"));
                QVERIFY(p.document->nodes[e.node].source.byteOffset >= 0);
            }
        }
        QCOMPARE(r.fields.value("bufTime").unit, QStringLiteral("ms"));
        QCOMPARE(r.fields.value("bufTime").value.toUInt(), 20U);
        QCOMPARE(r.triggers[0].fields.value("dupd").state, MmsValueState::Explicit);
        QCOMPARE(r.triggers[0].fields.value("dupd").value.toBool(), false);
        QCOMPARE(report(p, "urStatus").definitionMmsItem, QStringLiteral("LLN0$RP$urStatus"));
        QCOMPARE(report(p, "urStatus").instanceState, MmsValueState::Limited);
        QVERIFY(report(p, "defaults").fields.value("rptID").raw.isNull());
        QVERIFY(!report(p, "emptyId").fields.value("rptID").raw.isNull());
        QCOMPARE(report(p, "emptyId").fields.value("rptID").state, MmsValueState::Empty);
        QCOMPARE(report(p, "defaults").fields.value("buffered").state, MmsValueState::Missing);
        QVERIFY(!report(p, "defaults").fields.value("buffered").value.isValid());
        // DataSet mapping does not depend on a guessed buffered default.
        QCOMPARE(report(p, "defaults").dataSetMmsName, QStringLiteral("LLN0$Events"));
        QVERIFY(report(p, "defaults").definitionMmsItem.isEmpty());
        QVERIFY(!finding(p, "ENG004")); // CustomRptID shared by two instances is not a duplicate definition.
        QVERIFY(!finding(p, "REF001")); QVERIFY(!finding(p, "REF002"));
    }

    void clientOrderAndSharedServer()
    {
        const auto p = project(fixtureBytes());
        QCOMPARE(p.clients.size(), size_t(2));
        QCOMPARE(p.clients[0].fields.value("iedName").raw, QStringLiteral("SinkZ"));
        QCOMPARE(p.clients[1].fields.value("iedName").raw, QStringLiteral("SinkA"));
        for (int i = 0; i < 2; ++i) {
            QCOMPARE(p.clients[i].kind, MmsClientKind::ClientLn);
            QCOMPARE(p.clients[i].ordinal, i + 1); QCOMPARE(p.clients[i].expectedInstanceOrdinal, i + 1);
            QCOMPARE(p.clients[i].target.state, LinkState::Resolved);
            QVERIFY(p.clients[i].target.target() != NoNode);
        }
        QCOMPARE(p.servers.size(), size_t(4)); QCOMPARE(p.endpoints.size(), size_t(2));
        QCOMPARE(p.servers[0].model.target(), p.servers[1].model.target());
        QCOMPARE(p.servers[1].path.size(), 4); // Alias AP, ServerAt, Model AP, Server.
        QCOMPARE(p.endpoints[0].server, p.endpoints[1].server);
        QVERIFY(p.endpoints[0].accessPoint.target() != p.endpoints[1].accessPoint.target());
        QCOMPARE(p.servers[0].domains[0].name, QStringLiteral("DemoDomain"));
        QCOMPARE(p.servers[2].domains[0].name, QStringLiteral("SinkZC"));
        QCOMPARE(p.endpoints[0].addresses.size(), 1);
        QCOMPARE(p.endpoints[0].addresses[0].parameters.size(), 2);
        QCOMPARE(p.endpoints[0].addresses[0].parameters[0].fields.value("#text").raw, QStringLiteral("192.0.2.10"));
        QCOMPARE(p.endpoints[0].addresses[0].parameters[1].fields.value("#text").state, MmsValueState::Limited);
        QCOMPARE(p.endpoints[1].addresses[0].parameters.size(), 1); // No fabricated port P.
    }

    void unknownVersionAndRawStates()
    {
        auto xml = fixtureBytes();
        xml.replace("version=\"2007\" revision=\"B\" release=\"4\"", "");
        xml.replace("<Header id=\"Task6MmsSynthetic\"/>", "<Header id='H' version='2007B4'/>");
        const auto p = project(xml);
        QCOMPARE(p.profile, MmsProfile::Unknown); QVERIFY(p.complete); QVERIFY(p.instances.empty());
        QVERIFY(!report(p, "brStatus").definitionReference.isEmpty());
        QCOMPARE(report(p, "brStatus").instanceState, MmsValueState::Limited);
        QCOMPARE(p.clients[1].expectedInstanceOrdinal, 0);
        QCOMPARE(coverage(p, "MMS002").limited, 4);
        QVERIFY(!finding(p, "MMS002")); // Unknown version alone is not a configuration error.
        QVERIFY(!finding(p, "ENG004"));
        auto known = reportXml("buffered='1' indexed='1' rptID=''", "<RptEnabled max='1'/>");
        const auto empty = project(known);
        QCOMPARE(empty.instances.size(), size_t(1));
        QVERIFY(empty.instances[0].rptId.rawPresent); QVERIFY(empty.instances[0].rptId.raw.isEmpty());
        QCOMPARE(empty.instances[0].rptId.state, MmsValueState::Limited);
        QVERIFY(!empty.instances[0].rptId.value.isValid());
        known.replace(" rptID=''", "");
        const auto missing = project(known);
        QVERIFY(!missing.instances[0].rptId.rawPresent);
        QVERIFY(!missing.instances[0].rptId.value.isValid());
    }

    void instanceBoundary_data()
    {
        QTest::addColumn<QByteArray>("attributes"); QTest::addColumn<QByteArray>("enabled"); QTest::addColumn<int>("count");
        QTest::newRow("br") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max='2'/>") << 2;
        QTest::newRow("collapsed-boolean") << QByteArray("buffered=' true ' indexed=' 1 '") << QByteArray("<RptEnabled max=' +2 '/>") << 2;
        QTest::newRow("rp99") << QByteArray("buffered='0' indexed='1'") << QByteArray("<RptEnabled max='99'/>") << 99;
        QTest::newRow("missing-buffered") << QByteArray("indexed='true'") << QByteArray("<RptEnabled max='2'/>") << 0;
        QTest::newRow("missing-indexed") << QByteArray("buffered='true'") << QByteArray("<RptEnabled max='2'/>") << 0;
        QTest::newRow("missing-enabled") << QByteArray("buffered='true' indexed='true'") << QByteArray() << 0;
        QTest::newRow("missing-max") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled/>") << 0;
        QTest::newRow("zero") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max='0'/>") << 0;
        QTest::newRow("100") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max='100'/>") << 0;
        QTest::newRow("invalid-max") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max='1.2'/>") << 0;
        QTest::newRow("empty-max") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max=''/>") << 0;
        QTest::newRow("duplicate-enabled") << QByteArray("buffered='true' indexed='true'") << QByteArray("<RptEnabled max='1'/><RptEnabled max='2'/>") << 0;
        QTest::newRow("non-indexed-single") << QByteArray("buffered='true' indexed='false'") << QByteArray("<RptEnabled max='1'/>") << 0;
        QTest::newRow("association-urcb") << QByteArray("buffered='false' indexed='false'") << QByteArray("<RptEnabled max='5'/>") << 0;
        QTest::newRow("bad-buffered") << QByteArray("buffered='TRUE' indexed='true'") << QByteArray("<RptEnabled max='1'/>") << 0;
        QTest::newRow("bad-indexed") << QByteArray("buffered='true' indexed='yes'") << QByteArray("<RptEnabled max='1'/>") << 0;
    }
    void instanceBoundary()
    {
        QFETCH(QByteArray, attributes); QFETCH(QByteArray, enabled); QFETCH(int, count);
        const auto p = project(reportXml(attributes, enabled));
        QVERIFY(p.complete); QCOMPARE(p.instances.size(), size_t(count));
        if (count == 99) { QCOMPARE(p.instances.back().name, QStringLiteral("r99")); QCOMPARE(p.instances.back().mmsItem, QStringLiteral("LLN0$RP$r99")); }
        if (enabled == "<RptEnabled max='0'/>" || enabled == "<RptEnabled max='100'/>") {
            QCOMPARE(p.reports[0].instanceState, MmsValueState::Invalid);
            QCOMPARE(p.reports[0].enabled[0].fields.value("max").state, MmsValueState::Invalid);
        }
    }

    void invalidLexicalAndNoDuplicateReferenceFindings()
    {
        const auto base = analyze(fixtureBytes("Task6MmsLimited.scd"));
        const auto p = MmsProjector::Build(base);
        QVERIFY(p.complete); QVERIFY(p.instances.empty()); QCOMPARE(p.reports.size(), size_t(3));
        const auto &invalid = report(p, "invalid");
        QCOMPARE(invalid.fields.value("buffered").state, MmsValueState::Invalid);
        QVERIFY(invalid.definitionMmsItem.isEmpty());
        QCOMPARE(invalid.triggers[0].fields.value("dchg").state, MmsValueState::Invalid);
        QVERIFY(finding(p, "MMS001")); QVERIFY(!finding(p, "REF001")); QVERIFY(!finding(p, "KEY001"));
        QCOMPARE(report(p, "ambiguous").dataSet.state, LinkState::Ambiguous);
        QCOMPARE(report(p, "ambiguous").memberCount, 0);
        QVERIFY(!report(p, "ambiguous").dataSet.candidates.empty());
        QVERIFY(!finding(p, "MMS002")); // Unknown-version non-indexed URCB is limited, not globally rejected.
        auto xml = reportXml("buffered='true' indexed='true' confRev='4294967296' bufTime='-1' intgPd='1e3'");
        const auto bad = project(xml);
        const auto &r = bad.reports[0];
        for (const auto *name : {"confRev", "bufTime", "intgPd"}) {
            QCOMPARE(r.fields.value(name).state, MmsValueState::Invalid); QVERIFY(!r.fields.value(name).value.isValid());
        }
        xml = reportXml("buffered='true' indexed='true' intgPd='1000'");
        QVERIFY(!finding(project(xml), "MMS001")); // No forced intgPd/period combination.
    }

    void unresolvedClientDoesNotShiftPositions()
    {
        auto xml = fixtureBytes(); xml.replace("iedName=\"SinkZ\" apRef", "iedName=\"Outside\" apRef");
        const auto p = project(xml);
        QCOMPARE(p.clients[0].target.state, LinkState::External);
        QCOMPARE(p.clients[0].expectedInstanceOrdinal, 1);
        QCOMPARE(p.clients[1].expectedInstanceOrdinal, 2);
        xml = fixtureBytes(); xml.replace("<ClientLN iedName=\"SinkZ\"", "<ClientLN");
        const auto missing = project(xml);
        QCOMPARE(missing.clients[0].target.state, LinkState::Unbound);
        QCOMPARE(missing.clients[1].expectedInstanceOrdinal, 2);
        QVERIFY(finding(missing, "MMS003"));
        xml = fixtureBytes(); xml.replace("iedName=\"SinkA\" apRef", "iedName=\"SinkZ\" apRef");
        QVERIFY(finding(project(xml), "MMS003"));
        xml = fixtureBytes(); xml.replace("<RptEnabled max=\"2\">", "<RptEnabled max=\"1\">");
        const auto overflow = project(xml);
        QCOMPARE(overflow.clients[1].ordinal, 2); QCOMPARE(overflow.clients[1].expectedInstanceOrdinal, 0);
        QVERIFY(finding(overflow, "MMS003"));
    }

    void serverAtCycleAmbiguousClientAndDuplicateDomain()
    {
        auto xml = fixtureBytes(); xml.replace("<ServerAt apName=\"Model\"/>", "<ServerAt apName=\"Alias\"/>");
        auto p = project(xml);
        QCOMPARE(p.servers[1].model.state, LinkState::Blocked); QVERIFY(finding(p, "MMS004"));
        QVERIFY(p.endpoints[1].server == NoNode);
        xml = fixtureBytes();
        const QByteArray sink = "<AccessPoint name='Other'><Server><LDevice inst='C'><LN0 lnClass='LLN0' inst='' lnType='Zero'/><LN lnClass='GGIO' inst='1' lnType='Signals'/></LDevice></Server></AccessPoint>";
        xml.replace("<IED name=\"SinkZ\">", "<IED name=\"SinkZ\">" + sink);
        xml.replace("iedName=\"SinkZ\" apRef=\"Client\"", "iedName=\"SinkZ\"");
        p = project(xml); QCOMPARE(p.clients[0].target.state, LinkState::Ambiguous); QCOMPARE(p.clients[0].target.candidates.size(), 2);
        xml = fixtureBytes(); xml.replace("<AccessPoint name=\"Alias\"><ServerAt", "<AccessPoint name=\"Alias\"><Server/><ServerAt");
        p = project(xml); QCOMPARE(p.servers[1].model.state, LinkState::Ambiguous);
        xml = fixtureBytes(); xml.replace("</LDevice></Server></AccessPoint>\n    <AccessPoint name=\"Alias\"", "</LDevice><LDevice inst='Other' ldName='DemoDomain'/></Server></AccessPoint>\n    <AccessPoint name=\"Alias\"");
        p = project(xml); QVERIFY(p.instances.empty()); QCOMPARE(report(p, "brStatus").mappingState, MmsValueState::Limited);
        xml = fixtureBytes();
        // Duplicate same-LN report definitions remain ambiguous and generate no instances.
        xml.replace("<ReportControl name=\"urStatus\"", "<ReportControl name=\"brStatus\"");
        p = project(xml); QVERIFY(p.instances.empty()); QVERIFY(!finding(p, "ENG004"));
        xml = fixtureBytes();
        const QByteArray alias = "<AccessPoint name=\"Alias\"><ServerAt apName=\"Model\"/></AccessPoint>";
        xml.replace(alias, alias + "<AccessPoint name='Chain'><ServerAt apName='Alias'/></AccessPoint>");
        p = project(xml);
        QCOMPARE(p.servers[2].model.target(), p.servers[0].model.target()); QCOMPARE(p.servers[2].path.size(), 6);
    }

    void independentClientKindsAndExplicitConflict()
    {
        auto xml = fixtureBytes();
        xml.replace("</LDevice></Server></AccessPoint>\n    <AccessPoint name=\"Alias\"", "</LDevice><Association kind='pre-established' associationID='A1' iedName='SinkZ' ldInst='C' lnClass='GGIO' lnInst='1'/></Server></AccessPoint>\n    <AccessPoint name=\"Alias\"");
        const QByteArray inputs = "<Inputs>"
            "<ExtRef serviceType='Report' iedName='Publisher' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal' srcLDInst='LD0' srcLNClass='LLN0' srcCBName='brStatus'/>"
            "<ExtRef serviceType='Report' iedName='Publisher' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal' srcLDInst='LD0' srcLNClass='LLN0' srcCBName='Absent'/>"
            "<ExtRef iedName='Publisher' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal'/>"
            "<ExtRef serviceType='Report' iedName='Publisher' ldInst='LD0' lnClass='GGIO' lnInst='1' doName='Ind1' daName='stVal'/>"
            "</Inputs>";
        xml.replace("<LN lnClass=\"GGIO\" inst=\"1\" lnType=\"Signals\"/>", "<LN lnClass='GGIO' inst='1' lnType='Signals'>" + inputs + "</LN>");
        const auto p = project(xml);
        int associations = 0, explicitReports = 0, conflictReports = 0, unboundReports = 0;
        for (const auto &client : p.clients) {
            if (client.kind == MmsClientKind::Association) {
                ++associations; QCOMPARE(client.target.state, LinkState::Resolved); QCOMPARE(client.report, NoNode);
                QCOMPARE(client.expectedInstanceOrdinal, 0);
            } else if (client.kind == MmsClientKind::ReportExtRef) {
                QCOMPARE(client.expectedInstanceOrdinal, 0);
                if (client.fields.value("srcCBName").raw == "brStatus") { ++explicitReports; QCOMPARE(client.report, report(p, "brStatus").node); }
                else if (client.fields.value("srcCBName").raw == "Absent") { ++conflictReports; QCOMPARE(client.report, NoNode); QVERIFY(client.dataTarget.target() != NoNode); }
                else { ++unboundReports; QCOMPARE(client.report, NoNode); }
            }
        }
        QCOMPARE(associations, 1); QVERIFY(explicitReports > 0); QCOMPARE(conflictReports, explicitReports); QCOMPARE(unboundReports, explicitReports);
        QCOMPARE(p.clients.size(), size_t(2 + associations + explicitReports * 3)); // serviceType absent excluded.
        QVERIFY(!finding(p, "REF004")); // Existing resolver owns that root cause.
    }

    void capabilitiesEndpointAndExtensionBoundaries()
    {
        auto xml = fixtureBytes();
        xml.replace("<IED name=\"Publisher\">", "<IED name='Publisher'><Services><ConfReportControl max='20'/><ReportSettings rptID='Dyn'/><DynAssociation max='4'/><UnknownService/></Services>");
        xml.replace("<AccessPoint name=\"Model\">", "<AccessPoint name='Model'><Services><ConfReportControl max='1'/></Services>");
        xml.replace("<P type=\"IP\">192.0.2.10</P>", "<P type='IP'>999.2.3.4</P><P type='IP'>192.0.2.12</P><P type='IP-SUBNET'>255.0.255.0</P><P type='Vendor'>raw</P>");
        xml.replace("</Address></ConnectedAP>", "</Address><GSE ldInst='LD0' cbName='fake'><Address><P type='IP'>999.999.999.999</P><P type='APPID'>1234</P></Address></GSE></ConnectedAP>");
        xml.replace("<TrgOps dchg=\"true\"", "<TrgOps vendorFlag='maybe' dchg=\"true\"");
        xml.replace("<RptEnabled max=\"2\">", "<OtherReportChild/><RptEnabled max=\"2\">");
        const auto p = project(xml);
        QCOMPARE(p.capabilities.size(), size_t(2)); QCOMPARE(p.servers[0].services.size(), 2);
        QCOMPARE(p.capabilities[0].parameters[0].fields.value("max").value.toUInt(), 20U);
        QCOMPARE(p.capabilities[1].parameters[0].fields.value("max").value.toUInt(), 1U);
        QCOMPARE(coverage(p, "MMS006").state, Coverage::NotImplemented); // No false capacity=configuration check.
        QVERIFY(coverage(p, "MMS005").limited > 0);
        QVERIFY(finding(p, "MMS004"));
        int parameters = 0, invalid = 0;
        for (const auto &g : p.endpoints[0].addresses[0].parameters) {
            ++parameters; invalid += g.fields.value("#text").state == MmsValueState::Invalid;
            QVERIFY(g.fields.value("type").raw != "APPID");
        }
        QCOMPARE(parameters, 5); QCOMPARE(invalid, 2);
        QVERIFY(report(p, "brStatus").triggers[0].unknownAttributes.contains("vendorFlag"));
        QCOMPARE(report(p, "brStatus").unknownChildren.size(), 1);
        xml = fixtureBytes(); xml.replace("<SCL xmlns=", "<SCL xmlns:x='urn:vendor' xmlns=");
        xml.replace("buffered=\"true\" indexed=\"true\"", "buffered=\"true\" x:buffered='false' indexed=\"true\"");
        xml.replace("<RptEnabled max=\"2\">", "<x:RptEnabled max='99'/><Private><ReportControl name='hidden'/></Private><RptEnabled max=\"2\"><x:ClientLN iedName='bad'/>");
        xml.replace("<IED name=\"Publisher\">", "<x:wrapper><IED name='hidden'><AccessPoint name='hidden'><Server/></AccessPoint></IED></x:wrapper><IED name=\"Publisher\">");
        const auto foreign = project(xml);
        QCOMPARE(foreign.instances.size(), size_t(2)); QCOMPARE(foreign.reports.size(), size_t(4));
        QCOMPARE(foreign.servers.size(), size_t(4)); QCOMPARE(foreign.clients.size(), size_t(2));
        QCOMPARE(report(foreign, "brStatus").fields.value("buffered").value.toBool(), true);
        QVERIFY(report(foreign, "brStatus").unknownAttributes.contains("{urn:vendor}buffered"));
    }

    void reportIdScopeAndDefinitionReuse()
    {
        auto xml = fixtureBytes(); xml.replace("name=\"urStatus\" buffered", "name=\"urStatus\" rptID='CustomReport01' buffered");
        QVERIFY(finding(project(xml), "ENG004"));
        xml = fixtureBytes();
        xml.replace("<LN0 lnClass=\"LLN0\" inst=\"\" lnType=\"Zero\"/>", "<LN0 lnClass='LLN0' inst='' lnType='Zero'><ReportControl name='brStatus' rptID='CustomReport01' buffered='true' indexed='true'><RptEnabled max='1'/></ReportControl></LN0>");
        const auto cross = project(xml);
        QCOMPARE(cross.instances.size(), size_t(4)); QVERIFY(!finding(cross, "ENG004"));
        QCOMPARE(report(cross, "brStatus").memberCount, 3); // No members copied per instance.
        xml = reportXml("buffered='true' indexed='true' rptID='Same'", "<RptEnabled max='1'/>");
        const int begin = xml.indexOf("<AccessPoint");
        const int end = xml.indexOf("</AccessPoint>") + int(QByteArray("</AccessPoint>").size());
        const auto second = xml.mid(begin, end - begin).replace("name='AP'", "name='Other'");
        xml.replace("</IED>", second + "</IED>");
        const auto separateServers = project(xml);
        QCOMPARE(separateServers.instances.size(), size_t(2)); QVERIFY(!finding(separateServers, "ENG004"));
        QCOMPARE(separateServers.instances[0].reference, separateServers.instances[1].reference); // Separate server scopes.
    }

    void budgetCancellationSnapshotAndFailure()
    {
        const auto base = analyze(fixtureBytes());
        MmsLimits limits; limits.records = 2;
        const auto limited = MmsProjector::Build(base, nullptr, limits);
        QVERIFY(!limited.complete); QVERIFY(limited.resourceLimited); QVERIFY(limited.reports.empty()); QVERIFY(limited.servers.empty());
        QCOMPARE(limited.sha256, base.sha256); QCOMPARE(coverage(limited, "MMS002").state, Coverage::Blocked);
        TaskControl task; task.cancelled = true;
        QVERIFY_EXCEPTION_THROWN(MmsProjector::Build(base, &task), TaskCancelled);
        const auto failed = MmsProjector::Build(analyze("<SCL"));
        QVERIFY(!failed.complete); QVERIFY(!failed.resourceLimited); QVERIFY(!failed.document);
        QCOMPARE(coverage(failed, "MMS001").state, Coverage::Blocked);
        const auto other = project(fixtureBytes().replace("CustomReport01", "中文报告"));
        QVERIFY(other.sha256 != base.sha256);
        for (const auto &f : other.findings) QCOMPARE(f.location.byteOffset, other.document->nodes[f.node].source.byteOffset);
        QVERIFY(other.source->bytes().contains("中文报告"));
        limits = {}; limits.serverPath = 1;
        const auto pathLimited = MmsProjector::Build(base, nullptr, limits);
        QVERIFY(pathLimited.complete); QVERIFY(pathLimited.resourceLimited);
        QCOMPARE(pathLimited.servers[1].model.state, LinkState::Blocked);
        auto xml = reportXml("buffered='true' indexed='true'");
        xml.replace("name='r'", "name='bad$RCB'");
        const auto delimiter = project(xml);
        QVERIFY(delimiter.instances.empty()); QVERIFY(delimiter.reports[0].definitionMmsItem.isEmpty());
    }

    void noCommunicationNoClientsIsNotMissingEngineering()
    {
        const auto p = project(reportXml("buffered='true' indexed='true'"));
        QVERIFY(p.complete); QVERIFY(p.clients.empty()); QVERIFY(p.endpoints.empty()); QVERIFY(p.capabilities.empty());
        QVERIFY(!finding(p, "MMS003")); QVERIFY(!finding(p, "MMS004"));
        QCOMPARE(coverage(p, "MMS003").state, Coverage::NotApplicable);
        QCOMPARE(coverage(p, "MMS006").state, Coverage::NotImplemented);
        QCOMPARE(coverage(p, "XSD").state, Coverage::NotImplemented);
    }

    void LocalMmsProjection()
    {
        const auto path = qEnvironmentVariable("IEC61850_LOCAL_SCL");
        if (path.isEmpty()) QSKIP("Explicit local read-only acceptance; not a CTest prerequisite.");
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCryptographicHash before(QCryptographicHash::Sha256); QVERIFY(before.addData(&file)); file.close();
        QElapsedTimer timer; timer.start();
        const auto input = Reader::readFile(path); QVERIFY(input.document);
        const auto readMs = timer.restart(); const auto base = ReferenceResolver::analyze(input);
        const auto referenceMs = timer.restart(); const auto p = MmsProjector::Build(base);
        const auto projectionMs = timer.elapsed();
        QVERIFY(p.complete); QVERIFY(!p.resourceLimited); QCOMPARE(p.sha256, before.result().toHex());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCryptographicHash after(QCryptographicHash::Sha256); QVERIFY(after.addData(&file)); file.close();
        QCOMPARE(after.result(), before.result());
        QCOMPARE(p.profile, MmsProfile::Unknown); QCOMPARE(p.reports.size(), size_t(687));
        QCOMPARE(p.instances.size(), size_t(0)); QCOMPARE(p.clients.size(), size_t(0));
        QCOMPARE(p.endpoints.size(), size_t(182)); QCOMPARE(p.servers.size(), size_t(216));
        int members = 0; quint64 declaredMax = 0;
        QSet<NodeId> sets;
        for (const auto &r : p.reports) {
            if (!sets.contains(r.dataSet.target())) { sets.insert(r.dataSet.target()); members += r.memberCount; }
            QCOMPARE(r.instanceState, MmsValueState::Limited);
            if (r.enabled.size() == 1) declaredMax += r.enabled[0].fields.value("max").value.toULongLong();
        }
        QCOMPARE(declaredMax, quint64(10724));
        QJsonObject findingCounts, invalidFields;
        for (const auto &f : p.findings) findingCounts.insert(f.rule, findingCounts.value(f.rule).toInt() + 1);
        for (const auto &r : p.reports) for (auto it = r.fields.cbegin(); it != r.fields.cend(); ++it)
            if (it.value().state == MmsValueState::Invalid) invalidFields.insert(it.key(), invalidFields.value(it.key()).toInt() + 1);
        qInfo().noquote() << QJsonDocument(QJsonObject{{"sha256", QString::fromLatin1(p.sha256)}, {"readerMs", readMs},
            {"referencesMs", referenceMs}, {"projectionMs", projectionMs}, {"reports", int(p.reports.size())},
            {"expectedInstances", int(p.instances.size())}, {"clients", int(p.clients.size())},
            {"endpoints", int(p.endpoints.size())}, {"capabilities", int(p.capabilities.size())},
            {"uniqueReportDataSets", sets.size()}, {"reportDataSetMemberOccurrences", members}, {"declaredMaxSum", qint64(declaredMax)},
            {"incrementalFindings", int(p.findings.size())}, {"findingCounts", findingCounts}, {"invalidReportFields", invalidFields}}).toJson(QJsonDocument::Compact);
    }
};

QTEST_GUILESS_MAIN(SclMmsProjectionTest)
#include "SclMmsProjectionTest.moc"
