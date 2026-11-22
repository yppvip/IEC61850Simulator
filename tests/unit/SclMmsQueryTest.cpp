#include "SclAnalysisService.hpp"
#include "SclMmsQuery.hpp"
#include "SclReferenceValues.hpp"
#include "SclReferenceTrace.hpp"
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
#include <algorithm>

using namespace Scl;
using namespace iec61850sim;

namespace {
QByteArray Bytes(const char* fixture = "Task6Mms.scd") {
    QFile file(QStringLiteral(SCL_FIXTURE_DIR "/") + QString::fromLatin1(fixture));
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
const SclReferenceValue* RefValue(const std::vector<SclReferenceValue>& values, SclReferenceValueKind kind) {
    const auto at = std::find_if(values.begin(), values.end(), [kind](const auto& value) { return value.kind == kind; });
    return at == values.end() ? nullptr : &*at;
}
}

class SclMmsQueryTest : public QObject {
    Q_OBJECT
private slots:
    void Task8VoltageBaseline() {
        QFile file(QStringLiteral(SCL_FIXTURE_DIR "/Task8Voltage.scd"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto snapshot = Snapshot(file.readAll());
        const auto& doc = *snapshot->analysis.document;
        const auto listed = Named(*snapshot, "IED", "DemoListed");
        const auto unlisted = Named(*snapshot, "IED", "DemoUnlisted");
        const auto noAngle = Named(*snapshot, "IED", "DemoNoAngle");
        const auto addresses = [&](NodeId ied, const QString& object) {
            auto request = Request(SclMmsQueryKind::Addresses);
            request.scope.ied = ied;
            request.signal = Signal(*snapshot, object, ied);
            return QuerySclMms(*snapshot, request, nullptr);
        };
        const auto find = [](const SclMmsResult& result, const QString& key) {
            return std::find_if(result.rows.begin(), result.rows.end(),
                [&](const auto& row) { return row.key.field == key; });
        };
        const auto phase = addresses(listed, "PhV.phsA");
        const auto mag = find(phase, "cVal.mag.f");
        const auto ang = find(phase, "cVal.ang.f");
        QVERIFY(mag != phase.rows.end() && ang != phase.rows.end());
        QCOMPARE(mag->item, QStringLiteral("MMXU1$MX$PhV$phsA$cVal$mag$f"));
        QCOMPARE(ang->item, QStringLiteral("MMXU1$MX$PhV$phsA$cVal$ang$f"));
        QCOMPARE(ang->domain, QStringLiteral("ListedMeasurements"));
        QCOMPARE(ang->fc, QStringLiteral("MX"));
        QCOMPARE(ang->state, LinkState::Resolved);
        QCOMPARE(ang->shape, SclMmsAddressShape::Scalar);
        QCOMPARE(doc.nodes[ang->evidence.back().node].name, QStringLiteral("BDA"));
        QCOMPARE(doc.attribute(ang->evidence.back().node, "name"), QStringLiteral("f"));
        QVERIFY(find(phase, "q") != phase.rows.end() && find(phase, "t") != phase.rows.end());
        const auto structure = find(phase, "cVal");
        QVERIFY(structure != phase.rows.end());
        QCOMPARE(structure->shape, SclMmsAddressShape::Structure);

        const auto independent = addresses(unlisted, "PhV.phsA");
        const auto independentAngle = find(independent, "cVal.ang.f");
        QVERIFY(independentAngle != independent.rows.end());
        QCOMPARE(independentAngle->domain, QStringLiteral("DemoUnlistedLD0"));
        QCOMPARE(independentAngle->item, ang->item);
        QVERIFY(independentAngle->key.node != ang->key.node);
        auto reverse = Request(SclMmsQueryKind::ReverseSignal);
        reverse.scope.ied = listed;
        reverse.signal = ang->key.record;
        const auto uses = QuerySclMms(*snapshot, reverse, nullptr);
        QCOMPARE(uses.total, 2);
        QCOMPARE(uses.rows[0].shape, SclMmsAddressShape::Scalar);
        QCOMPARE(uses.rows[1].shape, SclMmsAddressShape::ObjectMember);
        reverse.scope.ied = unlisted;
        QCOMPARE(QuerySclMms(*snapshot, reverse, nullptr).total, 0);
        reverse.signal = independentAngle->key.record;
        QCOMPARE(QuerySclMms(*snapshot, reverse, nullptr).total, 0);

        const auto missing = addresses(noAngle, "PhV.phsA");
        QVERIFY(find(missing, "cVal.mag.f") != missing.rows.end());
        QVERIFY(find(missing, "cVal.ang.f") == missing.rows.end());
        const auto members = QuerySclMms(*snapshot, Request(SclMmsQueryKind::Members,
            Named(*snapshot, "ReportControl", "AngleReport")), nullptr);
        QCOMPARE(members.total, 1);
        QCOMPARE(members.rows.front().state, LinkState::Missing);
        QVERIFY(members.rows.front().item.isEmpty());

        const auto setting = addresses(listed, "AngSet");
        const auto settingValue = find(setting, "setMag.f");
        QVERIFY(settingValue != setting.rows.end());
        QCOMPARE(settingValue->fc, QStringLiteral("SP"));
        QCOMPARE(settingValue->item, QStringLiteral("MMXU1$SP$AngSet$setMag$f"));
        for (const auto& stored : {QStringLiteral("110"), QStringLiteral("15"), QStringLiteral("30")})
            QVERIFY(std::any_of(doc.nodes.begin(), doc.nodes.end(), [&](const auto& node) {
                return node.isScl() && node.name == "Val" && node.text == stored;
            }));
        const auto protection = doc.nodes[ang->key.node].parent;
        auto node = doc.nodes[protection].firstChild;
        while (node != NoNode && doc.attribute(node, "lnClass") != "PTOV")
            node = doc.nodes[node].nextSibling;
        QVERIFY(node != NoNode && node != ang->key.node);
        QVERIFY(std::none_of(snapshot->tasks.inputs.begin(), snapshot->tasks.inputs.end(), [&](const auto& input) {
            return snapshot->browse.iedOwners[input.node] == listed;
        }));
    }

    void Task8AttributeSearchAndProjection() {
        using Kind = SclReferenceValueKind;
        auto xml = Bytes("Task8Voltage.scd");
        xml.replace("<DAI name=\"f\"><Val>15</Val>",
            QStringLiteral("<DAI name='f' desc='独立角度描述'><Val>15</Val>").toUtf8());
        xml.replace("</Server></AccessPoint>\n  </IED>\n  <IED name=\"DemoUnlisted\">",
            "</Server></AccessPoint><AccessPoint name='Alias'><ServerAt apName='AP1'/></AccessPoint>\n"
            "  </IED>\n  <IED name=\"DemoUnlisted\">");
        const auto s = Snapshot(xml);
        const auto& doc = *s->analysis.document;
        auto r = Request(SclMmsQueryKind::Attributes);
        r.scope.ied = Named(*s, "IED", "DemoListed");
        r.text = QStringLiteral("A 相电压"); r.fc = "MX"; r.pageSize = 1;
        const auto first = QuerySclMms(*s, r, nullptr);
        QCOMPARE(first.total, 2); QCOMPARE(first.rows.size(), size_t(1)); QVERIFY(first.hasMore);
        QCOMPARE(first.rows.front().key.field, QStringLiteral("cVal.mag.f"));
        r.offset = 1;
        const auto second = QuerySclMms(*s, r, nullptr);
        QCOMPARE(second.rows.size(), size_t(1)); QVERIFY(!second.hasMore);
        const auto& phase = second.rows.front();
        QCOMPARE(phase.key.field, QStringLiteral("cVal.ang.f"));
        QCOMPARE(phase.key.fc, QStringLiteral("MX"));
        QCOMPARE(phase.attributeMatch, SclMmsAttributeMatch::BusinessTerm);
        QCOMPARE(phase.direct, SclMmsDirectState::Mapped);
        QCOMPARE(phase.online, SclMmsOnlineState::Unverified);
        QCOMPARE(phase.addressScope.ied, r.scope.ied);
        QCOMPARE(doc.nodes[phase.logicalDevice].name, QStringLiteral("LDevice"));
        const auto* object = RefValue(phase.references, Kind::ObjectReference);
        const auto* domain = RefValue(phase.references, Kind::MmsDomain);
        const auto* item = RefValue(phase.references, Kind::MmsItem);
        const auto* fc = RefValue(phase.references, Kind::FunctionalConstraint);
        QVERIFY(object && domain && item && fc);
        QCOMPARE(object->value, QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA.cVal.ang.f"));
        QCOMPARE(domain->value, QStringLiteral("ListedMeasurements"));
        QCOMPARE(domain->evidence.node, phase.logicalDevice);
        QCOMPARE(domain->evidence.attribute, QStringLiteral("ldName"));
        QCOMPARE(domain->origin, MmsValueState::Explicit);
        QCOMPARE(item->value, QStringLiteral("MMXU1$MX$PhV$phsA$cVal$ang$f"));
        QCOMPARE(item->evidence.node, phase.key.declaration);
        QCOMPARE(doc.nodes[item->evidence.node].name, QStringLiteral("BDA"));
        QCOMPARE(doc.attribute(fc->evidence.node, "name"), QStringLiteral("cVal"));
        QCOMPARE(fc->evidence.attribute, QStringLiteral("fc"));

        r.offset = 0; r.locate = phase.key;
        const auto located = QuerySclMms(*s, r, nullptr);
        QVERIFY(located.locateFound); QCOMPARE(located.request.offset, 1);
        r.locate.reset(); r.pageSize = 200;
        const auto alias = Named(*s, "AccessPoint", "Alias");
        QVERIFY(alias != NoNode);
        r.scope.accessPoint = alias; r.scope.server = phase.addressScope.server;
        r.text = object->value;
        const auto scoped = QuerySclMms(*s, r, nullptr);
        QCOMPARE(scoped.total, 1);
        QCOMPARE(scoped.rows.front().addressScope.accessPoint, alias);
        QCOMPARE(scoped.rows.front().key, phase.key);
        r.text = item->value;
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1);
        r.text = QStringLiteral("独立角度描述");
        const auto described = QuerySclMms(*s, r, nullptr);
        QCOMPARE(described.total, 1);
        QCOMPARE(described.rows.front().attributeMatch, SclMmsAttributeMatch::Description);
        QCOMPARE(doc.nodes[described.rows.front().matchEvidence.node].name, QStringLiteral("DAI"));
        r.scope.server = phase.logicalDevice;
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);

        r.scope = {Named(*s, "IED", "DemoListed")}; r.text = QStringLiteral("相位"); r.fc = "SP";
        const auto settings = QuerySclMms(*s, r, nullptr);
        QCOMPARE(settings.total, 2);
        for (const auto& row : settings.rows) {
            QCOMPARE(row.objectPath, QStringLiteral("AngSet"));
            QCOMPARE(row.attributeMatch, SclMmsAttributeMatch::Description);
        }

        r.scope = {Named(*s, "IED", "DemoUnlisted")}; r.fc = "MX";
        const auto unlisted = QuerySclMms(*s, r, nullptr);
        QCOMPARE(unlisted.total, 1);
        const auto* derived = RefValue(unlisted.rows.front().references, Kind::MmsDomain);
        QVERIFY(derived);
        QCOMPARE(derived->value, QStringLiteral("DemoUnlistedLD0"));
        QCOMPARE(derived->evidence.attribute, QStringLiteral("inst"));
        QCOMPARE(derived->origin, MmsValueState::Derived);
        auto reverse = Request(SclMmsQueryKind::AttributeReports);
        reverse.scope = r.scope; reverse.key = unlisted.rows.front().key;
        const auto unused = QuerySclMms(*s, reverse, nullptr);
        QCOMPARE(unused.total, 0); QVERIFY(!unused.limited);
        QVERIFY(unused.attributeDirect == SclMmsDirectState::Mapped);
        r.scope = {Named(*s, "IED", "DemoNoAngle")};
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);
        r.text = QStringLiteral("幅值");
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1);
        r.workLimit = 2;
        const auto limited = QuerySclMms(*s, r, nullptr);
        QVERIFY(limited.limited && limited.hasMore && !limited.limitation.isEmpty());
    }

    void Task8ExactAndStructuralReportEvidence() {
        using Kind = SclReferenceValueKind;
        auto xml = Bytes("Task8Voltage.scd");
        const QByteArray angle = "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1'\n"
            "    doName='PhV.phsA' daName='cVal.ang.f' fc='MX' desc='a > b &amp; c'/>";
        xml.replace("</DataSet>\n          <ReportControl name=\"VoltageReport\"",
            angle + angle +
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA' daName='cVal' fc='MX'/>"
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV' fc='MX'/>"
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA' daName='cVal.ang.f' fc='CF'/>"
            "</DataSet><DataSet name='WithoutReport'>" + angle + "</DataSet>"
            "<ReportControl name='SharedVoltage' datSet='Voltage' rptID='file$raw' buffered='true'/>"
            "<ReportControl name=\"VoltageReport\"");
        xml.replace("<DA name=\"cVal\" fc=\"MX\" bType=\"Struct\" type=\"Phasor\"/>",
            "<DA name='cVal' fc='MX' bType='Struct' type='Phasor'/>"
            "<DA name='cVal' fc='CF' bType='Struct' type='Phasor'/>");
        const auto s = Snapshot(xml);
        auto search = Request(SclMmsQueryKind::Attributes);
        search.scope.ied = Named(*s, "IED", "DemoListed");
        search.text = "PhV.phsA.cVal.ang.f";
        const auto choices = QuerySclMms(*s, search, nullptr);
        QCOMPARE(choices.total, 2);
        QCOMPARE(choices.rows[0].key.field, choices.rows[1].key.field);
        QVERIFY(choices.rows[0].key != choices.rows[1].key);
        search.fc = "MX";
        const auto phase = QuerySclMms(*s, search, nullptr);
        QCOMPARE(phase.total, 1);
        auto r = Request(SclMmsQueryKind::AttributeReports);
        r.scope = search.scope; r.key = phase.rows.front().key; r.pageSize = 3;
        const auto first = QuerySclMms(*s, r, nullptr);
        QCOMPARE(first.total, 10); QCOMPARE(first.rows.size(), size_t(3)); QVERIFY(first.hasMore);
        QCOMPARE(first.rows[0].reportMatch, SclMmsReportMatch::ObjectCoverage);
        QCOMPARE(first.rows[0].key.ordinal, 2);
        QCOMPARE(first.rows[0].key.node, first.rows[1].key.node);
        QVERIFY(first.rows[0].key != first.rows[1].key);
        QCOMPARE(first.rows[2].reportMatch, SclMmsReportMatch::ExactAttribute);
        QCOMPARE(first.rows[2].key.ordinal, 3);
        r.offset = 4; r.pageSize = 6;
        const auto rest = QuerySclMms(*s, r, nullptr);
        QCOMPARE(rest.rows[0].key.ordinal, 4);
        QVERIFY(rest.rows[0].key.node != first.rows[2].key.node);
        QCOMPARE(rest.rows[2].reportMatch, SclMmsReportMatch::AttributeCoverage);
        QCOMPARE(rest.rows[4].reportMatch, SclMmsReportMatch::ObjectCoverage);
        QCOMPARE(rest.rows[4].key.ordinal, 6);
        const auto& shared = first.rows[2];
        QVERIFY(shared.report != NoNode && shared.dataSet != NoNode);
        const auto* rptId = RefValue(shared.references, Kind::RawReportId);
        QVERIFY(rptId && RefValue(shared.references, Kind::DataSetReference) && RefValue(shared.references, Kind::MmsDataSetList));
        QCOMPARE(rptId->value, QStringLiteral("file$raw"));
        auto detail = Request(SclMmsQueryKind::Detail);
        detail.scope = search.scope; detail.key = shared.key;
        const auto projected = QuerySclMms(*s, detail, nullptr);
        const auto* declaration = RefValue(projected.references, Kind::RawFcdaDeclaration);
        QVERIFY(declaration);
        QCOMPARE(declaration->value, QString::fromUtf8(angle));
        QCOMPARE(declaration->evidence.node, shared.key.node);
        QCOMPARE(declaration->origin, MmsValueState::Explicit);
        QVERIFY(RefValue(projected.references, Kind::ReportDefinition));
        const auto cf = std::find_if(choices.rows.begin(), choices.rows.end(), [](const auto& row) { return row.fc == "CF"; });
        QVERIFY(cf != choices.rows.end());
        detail.key = cf->key;
        const auto cfDetail = QuerySclMms(*s, detail, nullptr);
        const auto* cfItem = RefValue(cfDetail.references, Kind::MmsItem);
        QVERIFY(cfItem); QCOMPARE(cfItem->value, QStringLiteral("MMXU1$CF$PhV$phsA$cVal$ang$f"));
        r.scope.ied = Named(*s, "IED", "DemoUnlisted");
        const auto outside = QuerySclMms(*s, r, nullptr);
        QCOMPARE(outside.total, 0); QVERIFY(!outside.attributeDirect.has_value());
    }

    void Task8ArraySelectionAndMissingTypes() {
        using Kind = SclReferenceValueKind;
        auto xml = Bytes("Task8Voltage.scd");
        xml.replace("name=\"phsA\" type=\"FullPhase\"", "name='phsA' type='FullPhase' count='3'");
        xml.replace("<DO name=\"PhV\" type=\"FullWye\"", "<DO name='OtherVoltage' type='FullWye'/><DO name=\"PhV\" type=\"FullWye\"");
        xml.replace("</DataSet>\n          <ReportControl name=\"VoltageReport\"",
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA(1)' daName='cVal.ang.f' fc='MX' ix='01'/>"
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA' daName='cVal.ang.f' fc='MX' ix='2'/>"
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA(1)' fc='MX'/>"
            "<FCDA ldInst='LD0' lnClass='MMXU' lnInst='1' doName='PhV.phsA' daName='cVal.ang.f' fc='MX' ix='3'/>"
            "</DataSet>\n          <ReportControl name=\"VoltageReport\"");
        const auto s = Snapshot(xml);
        auto r = Request(SclMmsQueryKind::Attributes);
        r.scope.ied = Named(*s, "IED", "DemoListed"); r.text = QStringLiteral("相位"); r.fc = "MX";
        const auto unselected = QuerySclMms(*s, r, nullptr);
        QCOMPARE(unselected.total, 1);
        QCOMPARE(unselected.rows.front().direct, SclMmsDirectState::ArraySelectionRequired);
        QVERIFY(!RefValue(unselected.rows.front().references, Kind::MmsItem));
        auto usage = Request(SclMmsQueryKind::AttributeReports);
        usage.scope = r.scope; usage.key = unselected.rows.front().key;
        const auto unresolvedUsage = QuerySclMms(*s, usage, nullptr);
        QCOMPARE(unresolvedUsage.total, 0);
        QVERIFY(unresolvedUsage.attributeDirect == SclMmsDirectState::ArraySelectionRequired);

        const auto members = QuerySclMms(*s, Request(SclMmsQueryKind::Members, Named(*s, "ReportControl", "VoltageReport")), nullptr);
        QCOMPARE(members.total, 6);
        r.arrayMember = members.rows[2].key.node;
        const auto selected = QuerySclMms(*s, r, nullptr);
        QCOMPARE(selected.total, 1);
        const auto& phase = selected.rows.front();
        QCOMPARE(phase.direct, SclMmsDirectState::AlternateAccess);
        QCOMPARE(phase.key.arrayIndex, 1);
        QCOMPARE(phase.item, QStringLiteral("MMXU1$MX$PhV$phsA"));
        QCOMPARE(phase.component, QStringLiteral("cVal$ang$f"));
        QCOMPARE(phase.objectReference, QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA(1).cVal.ang.f"));
        r.text = "cVal.ang.f";
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 1); // The same template in OtherVoltage has its own array identity.
        usage.key = phase.key;
        const auto matches = QuerySclMms(*s, usage, nullptr);
        QCOMPARE(matches.total, 3);
        QCOMPARE(matches.rows[0].key.ordinal, 2);
        QCOMPARE(matches.rows[0].reportMatch, SclMmsReportMatch::ObjectCoverage);
        QCOMPARE(matches.rows[1].key.ordinal, 3);
        QCOMPARE(matches.rows[1].reportMatch, SclMmsReportMatch::ExactAttribute);
        const auto* rawIndex = RefValue(matches.rows[1].references, Kind::RawArrayIndex);
        const auto* effectiveIndex = RefValue(matches.rows[1].references, Kind::ArrayIndex);
        QVERIFY(rawIndex && effectiveIndex);
        QCOMPARE(rawIndex->value, QStringLiteral("01"));
        QCOMPARE(effectiveIndex->value, QStringLiteral("1"));
        QCOMPARE(matches.rows[2].key.ordinal, 5);
        QVERIFY(!RefValue(matches.rows[2].references, Kind::RawArrayIndex));
        r.arrayMember = members.rows[5].key.node;
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 0);

        xml = Bytes("Task8Voltage.scd");
        xml.replace("name=\"ang\" bType=\"Struct\" type=\"AnalogFloat\"", "name='ang' bType='Struct' type='MissingAnalog'");
        const auto missing = Snapshot(xml);
        r.scope.ied = Named(*missing, "IED", "DemoListed"); r.arrayMember = NoNode; r.text = "cVal.ang";
        const auto missingResult = QuerySclMms(*missing, r, nullptr);
        QCOMPARE(missingResult.total, 1);
        QCOMPARE(missingResult.rows.front().direct, SclMmsDirectState::Missing);
        QVERIFY(!RefValue(missingResult.rows.front().references, Kind::MmsItem));

        xml = Bytes("Task8Voltage.scd");
        xml.replace("<BDA name=\"ang\"", "<BDA name='ang' bType='Struct' type='AnalogFloat'/><BDA name=\"ang\"");
        const auto ambiguous = Snapshot(xml);
        r.scope.ied = Named(*ambiguous, "IED", "DemoListed");
        const auto duplicate = QuerySclMms(*ambiguous, r, nullptr);
        QCOMPARE(duplicate.total, 2);
        QVERIFY(duplicate.rows[0].key != duplicate.rows[1].key);
        for (const auto& row : duplicate.rows) {
            QCOMPARE(row.direct, SclMmsDirectState::Ambiguous);
            QVERIFY(!RefValue(row.references, Kind::MmsItem));
        }
    }

    void Task8AttributeSessionsDiscardStaleResults() {
        const auto first = Snapshot(Bytes("Task8Voltage.scd"));
        const auto second = Snapshot(Bytes("Task8Voltage.scd"));
        SclMmsQuerySession list, detail;
        QSignalSpy listReady(&list, &SclMmsQuerySession::Ready), detailReady(&detail, &SclMmsQuerySession::Ready);
        auto r = Request(SclMmsQueryKind::Attributes);
        r.text = QStringLiteral("A 相电压"); r.fc = "MX";
        list.SetContext(first, {Named(*first, "IED", "DemoNoAngle")});
        list.Query(r);
        list.SetContext(first, {Named(*first, "IED", "DemoListed")});
        list.Query(r);
        list.Cancel();
        list.SetContext(second, {Named(*second, "IED", "DemoUnlisted")});
        r.text = QStringLiteral("相位");
        const auto latest = list.Query(r);
        QTRY_COMPARE(listReady.count(), 1);
        QCOMPARE(list.result()->requestId, latest);
        QCOMPARE(list.result()->document, second->analysis.document);
        QCOMPARE(list.result()->source, second->analysis.source);
        QCOMPARE(list.result()->rows.size(), size_t(1));
        QCOMPARE(list.result()->rows.front().domain, QStringLiteral("DemoUnlistedLD0"));
        const auto address = list.result()->rows.front().key;
        detail.SetContext(second, list.result()->request.scope);
        auto use = Request(SclMmsQueryKind::AttributeReports);
        use.key = address;
        detail.Query(use);
        list.Query(r);
        list.SetContext({});
        QTRY_COMPARE(detailReady.count(), 1);
        QCOMPARE(detail.result()->total, 0);
        QVERIFY(detail.result()->attributeDirect == SclMmsDirectState::Mapped);
        QThreadPool::globalInstance()->waitForDone();
        QCoreApplication::processEvents();
        QCOMPARE(listReady.count(), 1); QVERIFY(!list.result());
    }

    void Task8AttributeTargets() {
        auto xml = Bytes("Task8Voltage.scd");
        xml.replace("<DOI name=\"PhV\"><SDI name=\"phsA\">", "<DOI name='PhV'><SDI name='phsA' ix='1'>");
        xml.replace("name=\"phsA\" type=\"FullPhase\"", "name='phsA' type='FullPhase' count='3'");
        xml.replace("daName=\"cVal.mag.f\" fc=\"MX\"", "daName='cVal.mag.f' fc='MX' ix='1'");
        const auto s = Snapshot(xml);
        const auto& doc = *s->analysis.document;
        const auto members = QuerySclMms(*s, Request(SclMmsQueryKind::Members, Named(*s, "ReportControl", "VoltageReport")), nullptr);
        QCOMPARE(members.total, 2);
        auto r = Request(SclMmsQueryKind::Attributes);
        r.scope.ied = Named(*s, "IED", "DemoListed");
        r.target = members.rows.front().key.node;
        const auto member = QuerySclMms(*s, r, nullptr);
        QCOMPARE(member.total, 1);
        QCOMPARE(member.rows.front().key.field, QStringLiteral("cVal.mag.f"));
        QCOMPARE(member.rows.front().objectReference, QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA(1).cVal.mag.f"));
        r.target = members.rows.back().key.node;
        const auto aggregate = QuerySclMms(*s, r, nullptr);
        QVERIFY(aggregate.total > 1);
        for (const auto& row : aggregate.rows) {
            QCOMPARE(row.objectPath, QStringLiteral("PhV.phsA"));
            QCOMPARE(row.fc, QStringLiteral("MX"));
            QCOMPARE(row.direct, SclMmsDirectState::ArraySelectionRequired);
        }
        auto value = std::find_if(doc.nodes.cbegin(), doc.nodes.cend(), [](const auto& node) {
            return node.isScl() && node.name == "Val" && node.text == "15";
        });
        QVERIFY(value != doc.nodes.cend());
        r.target = value->parent;
        const auto instance = QuerySclMms(*s, r, nullptr);
        QCOMPARE(instance.total, 1);
        const auto& phase = instance.rows.front();
        QCOMPARE(phase.key.field, QStringLiteral("cVal.ang.f"));
        QCOMPARE(phase.key.arrayIndex, 1);
        auto detail = Request(SclMmsQueryKind::Detail);
        detail.scope = r.scope; detail.key = phase.key;
        const auto fields = QuerySclMms(*s, detail, nullptr).fields;
        const auto val = std::find_if(fields.cbegin(), fields.cend(), [](const auto& field) { return field.label == "文件 Val"; });
        QVERIFY(val != fields.cend());
        QCOMPARE(val->value, QStringLiteral("15"));
        QCOMPARE(val->evidence.node, int(value - doc.nodes.cbegin()));

        r.target = phase.key.declaration;
        QCOMPARE(QuerySclMms(*s, r, nullptr).total, 3); // Same f declaration in magnitude, angle and the SP object.
        r.scope.ied = Named(*s, "IED", "DemoUnlisted"); r.target = value->parent;
        const auto outside = QuerySclMms(*s, r, nullptr);
        QCOMPARE(outside.total, 0); QVERIFY(outside.summary.contains(QStringLiteral("不在当前")));
        r.scope.ied = Named(*s, "IED", "DemoNoAngle");
        r.target = QuerySclMms(*s, Request(SclMmsQueryKind::Members, Named(*s, "ReportControl", "AngleReport")), nullptr).rows.front().key.node;
        const auto missing = QuerySclMms(*s, r, nullptr);
        QCOMPARE(missing.total, 0); QVERIFY(missing.summary.contains(QStringLiteral("FCDA 引用未解析")));
    }

    void Task8ReferenceValueSources() {
        using Kind = SclReferenceValueKind;
        auto xml = Bytes("Task4Signals.scd");
        xml.replace("<IED name=\"Source\"", "<Communication><SubNetwork name='Bus'><ConnectedAP iedName='Source' apName='A'>"
            "<GSE ldInst='L' cbName='EventsGo'><Address><P type='APPID'>00AF</P><P type='MAC-Address'>01-0C-CD-01-00-01</P>"
            "</Address></GSE></ConnectedAP></SubNetwork></Communication><IED name=\"Source\"");
        const auto s = Snapshot(xml);
        auto project = [](const SclSnapshot& snapshot, NodeId node) {
            SclReferenceRequest request;
            request.node = node;
            return CollectSclReferences(snapshot, request, nullptr);
        };
        const auto verify = [](const SclReferenceValues& values, Kind kind, const QString& expected) {
            const auto* value = RefValue(values.entries, kind);
            QVERIFY(value);
            QCOMPARE(value->value, expected);
            QVERIFY(value->evidence.node != NoNode);
        };
        const auto control = Named(*s, "GSEControl", "EventsGo");
        QVERIFY(control != NoNode);
        const auto go = project(*s, control);
        verify(go, Kind::AppId, QStringLiteral("EventsText"));
        verify(go, Kind::NumericAppId, QStringLiteral("00AF"));
        verify(go, Kind::DataSetDeclaration, QStringLiteral("Events"));
        QVERIFY(RefValue(go.entries, Kind::ControlPath));
        const auto& doc = *s->analysis.document;
        const auto endpoint = std::find_if(doc.nodes.cbegin(), doc.nodes.cend(), [](const auto& item) { return item.isScl() && item.name == "GSE"; });
        QVERIFY(endpoint != doc.nodes.cend());
        const auto ep = project(*s, int(endpoint - doc.nodes.cbegin()));
        verify(ep, Kind::NumericAppId, QStringLiteral("00AF"));
        verify(ep, Kind::MacAddress, QStringLiteral("01-0C-CD-01-00-01"));
        QVERIFY(!RefValue(ep.entries, Kind::AppId));
        const auto input = project(*s, s->tasks.inputs.front().node);
        QVERIFY(RefValue(input.entries, Kind::RawExtRef));
        QVERIFY(RefValue(input.entries, Kind::ObjectReference));
        const auto external = project(*s, s->tasks.inputs.back().node);
        QVERIFY(RefValue(external.entries, Kind::RawExtRef));
        QVERIFY(!RefValue(external.entries, Kind::ObjectReference));

        const auto voltage = Snapshot(Bytes("Task8Voltage.scd"));
        const auto ied = Named(*voltage, "IED", "DemoListed");
        const auto ds = Named(*voltage, "DataSet", "Voltage");
        const auto member = voltage->browse.memberRows[voltage->browse.dataSetMembers.value(ds).first];
        const auto refs = project(*voltage, member);
        verify(refs, Kind::DataSetReference, QStringLiteral("ListedMeasurements/LLN0$Voltage"));
        verify(refs, Kind::MmsDataSetList, QStringLiteral("LLN0$Voltage"));
        verify(refs, Kind::ObjectReference, QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA.cVal.mag.f"));
        verify(refs, Kind::MmsItem, QStringLiteral("MMXU1$MX$PhV$phsA$cVal$mag$f"));
        verify(refs, Kind::RawFcdaDeclaration, QStringLiteral("<FCDA ldInst=\"LD0\" lnClass=\"MMXU\" lnInst=\"1\" doName=\"PhV.phsA\" daName=\"cVal.mag.f\" fc=\"MX\"/>"));
        SclReferenceRequest request;
        request.anchor = SclReferenceAnchor::Signal;
        request.record = Signal(*voltage, "PhV.phsA", ied);
        verify(CollectSclReferences(*voltage, request, nullptr), Kind::ObjectReference,
            QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA"));
        request.anchor = SclReferenceAnchor::Hit;
        request.hit = {member, MatchField::Reference, voltage->mmsIndex.memberReferences.value(member)};
        verify(CollectSclReferences(*voltage, request, nullptr), Kind::MmsItem,
            QStringLiteral("MMXU1$MX$PhV$phsA$cVal$mag$f"));
        request.anchor = SclReferenceAnchor::Candidate;
        request.node = voltage->analysis.references[request.hit.detail].candidates.front();
        const auto candidate = CollectSclReferences(*voltage, request, nullptr);
        QVERIFY(!RefValue(candidate.entries, Kind::ObjectReference));
        QVERIFY(RefValue(candidate.entries, Kind::CandidatePath));
    }

    void Task9ReferenceTrace() {
        using Kind = SclReferenceValueKind;
        using Branch = SclTraceBranch;
        const auto root = [] {
            SclTraceNode request;
            request.branch = Branch::Root;
            return request;
        }();
        const auto trace = [&](const SclSnapshot& s, const SclReferenceValue& value, const SclTraceNode& node) {
            return TraceSclReference(s, value, node, nullptr);
        };
        const auto phase = [&](const SclSnapshot& s, const char* ied, const char* ap = nullptr) {
            auto request = Request(SclMmsQueryKind::Attributes);
            request.scope.ied = Named(s, "IED", ied);
            request.text = "cVal.ang.f";
            if (ap) request.scope.accessPoint = Named(s, "AccessPoint", ap);
            return QuerySclMms(s, request, nullptr);
        };
        const auto s = Snapshot(Bytes("Task8Voltage.scd"));
        const auto result = phase(*s, "DemoListed");
        QCOMPARE(result.rows.size(), size_t(1));
        SclReferenceRequest request;
        request.anchor = SclReferenceAnchor::Mms;
        request.mms = result.rows.front().key;
        request.prepared = result.rows.front().references;
        const auto values = CollectSclReferences(*s, request, nullptr, true);
        for (const auto kind : {Kind::ObjectReference, Kind::FunctionalConstraint, Kind::MmsDomain, Kind::MmsItem, Kind::TypeReference}) {
            const auto* copied = RefValue(values.entries, kind);
            const auto* selected = RefValue(values.traceEntries, kind);
            QVERIFY(copied && selected);
            const auto graph = trace(*s, *selected, root);
            QCOMPARE(graph.node.value, copied->value);
            QVERIFY(!graph.children.empty());
            QVERIFY(graph.children.front().source != NoNode);
        }
        const auto& item = *RefValue(values.traceEntries, Kind::MmsItem);
        const auto graph = trace(*s, item, root);
        QCOMPARE(graph.children.front().value, QStringLiteral("MMXU1"));
        QCOMPARE(graph.children.front().location.line, 17);
        const auto ln = trace(*s, item, graph.children[0]);
        QVERIFY(ln.children[0].value.isNull());
        QCOMPARE(ln.children[1].value, QStringLiteral("MMXU"));
        const auto fc = trace(*s, item, graph.children[1]);
        QCOMPARE(fc.children.front().location.line, 60);
        QCOMPARE(fc.children.front().relation, QStringLiteral("继承 FC"));
        QCOMPARE(graph.children.back().location.line, 71);

        const auto& domain = *RefValue(values.traceEntries, Kind::MmsDomain);
        const auto ld = trace(*s, domain, trace(*s, domain, root).children.front());
        QCOMPARE(ld.children.front().attribute, QStringLiteral("ldName"));
        QCOMPARE(ld.children.front().location.line, 7);
        const auto unlisted = phase(*s, "DemoUnlisted");
        QCOMPARE(unlisted.rows.size(), size_t(1));
        const auto& derived = *RefValue(unlisted.rows.front().references, Kind::MmsDomain);
        const auto derivedLd = trace(*s, derived, trace(*s, derived, root).children.front());
        QCOMPARE(derivedLd.children[0].value, QStringLiteral("DemoUnlisted"));
        QCOMPARE(derivedLd.children[1].value, QStringLiteral("LD0"));

        request = {};
        const auto dataSet = Named(*s, "DataSet", "Voltage");
        request.node = s->browse.memberRows[s->browse.dataSetMembers.value(dataSet).first + 1];
        const auto member = CollectSclReferences(*s, request, nullptr, true);
        const auto* object = RefValue(member.traceEntries, Kind::ObjectReference);
        QVERIFY(object);
        const auto objectGraph = trace(*s, *object, root);
        QCOMPARE(objectGraph.node.value, QStringLiteral("ListedMeasurements/MMXU1.PhV.phsA"));
        QCOMPARE(objectGraph.children[3].value, QStringLiteral("phsA"));
        QCOMPARE(objectGraph.children.back().location.line, 11);
        const auto memberFc = trace(*s, *RefValue(member.traceEntries, Kind::FunctionalConstraint), root);
        QCOMPARE(memberFc.children.size(), size_t(2));
        QCOMPARE(memberFc.children.back().attribute, QStringLiteral("doName"));

        auto usedBy = Request(SclMmsQueryKind::AttributeReports);
        usedBy.scope.ied = Named(*s, "IED", "DemoListed");
        usedBy.key = result.rows.front().key;
        const auto usages = QuerySclMms(*s, usedBy, nullptr);
        QCOMPARE(usages.rows.size(), size_t(1));
        request = {};
        request.anchor = SclReferenceAnchor::Mms;
        request.mms = usages.rows.front().key;
        request.prepared = usages.rows.front().references;
        const auto reportMember = CollectSclReferences(*s, request, nullptr, true);
        QCOMPARE(std::count_if(reportMember.traceEntries.begin(), reportMember.traceEntries.end(),
            [](const auto& entry) { return entry.kind == Kind::FunctionalConstraint; }), 1);

        request = {};
        request.node = Named(*s, "ReportControl", "VoltageReport");
        const auto report = CollectSclReferences(*s, request, nullptr, true);
        const auto* reportItem = RefValue(report.traceEntries, Kind::MmsItem);
        QVERIFY(reportItem);
        const auto reportGraph = trace(*s, *reportItem, root);
        QCOMPARE(reportGraph.children[0].value, QStringLiteral("LLN0"));
        QCOMPARE(trace(*s, *reportItem, reportGraph.children[0]).children.front().value, QStringLiteral("LLN0"));
        QCOMPARE(reportGraph.children[1].value, QStringLiteral("BR"));
        QCOMPARE(reportGraph.children[2].value, QStringLiteral("VoltageReport"));
        const auto* reportDomain = RefValue(report.traceEntries, Kind::MmsDomain);
        QVERIFY(reportDomain);
        QCOMPARE(trace(*s, *reportDomain, root).children.front().value, QStringLiteral("ListedMeasurements"));

        auto xml = Bytes("Task8Voltage.scd");
        xml.replace("<LN lnClass=\"MMXU\"", "<LN prefix=\"V\" lnClass=\"MMXU\"");
        xml.replace("lnClass=\"MMXU\" lnInst=", "prefix=\"V\" lnClass=\"MMXU\" lnInst=");
        xml.replace("cVal.mag.f\" fc=\"MX\"", "cVal.mag.f\" fc=\"ST\"");
        xml.replace("<IED name=\"DemoListed\">", "<IED name=\"DemoListed\"><AccessPoint name='Alias'><ServerAt apName='AP1'/></AccessPoint>");
        xml.replace("<LDevice inst=\"LD0\" ldName", "<LDevice inst=\"LD0\"\n        ldName");
        const auto changed = Snapshot(xml);
        const auto aliased = phase(*changed, "DemoListed", "Alias");
        QCOMPARE(aliased.rows.size(), size_t(1));
        const auto& aliasItem = *RefValue(aliased.rows.front().references, Kind::MmsItem);
        const auto aliasGraph = trace(*changed, aliasItem, root);
        QCOMPARE(aliasGraph.children.front().value, QStringLiteral("VMMXU1"));
        QCOMPARE(trace(*changed, aliasItem, aliasGraph.children.front()).children.front().value, QStringLiteral("V"));
        const auto& aliasDomain = *RefValue(aliased.rows.front().references, Kind::MmsDomain);
        const auto aliasLd = trace(*changed, aliasDomain, trace(*changed, aliasDomain, root).children.front());
        QCOMPARE(aliasLd.children.front().location.line, 7);
        QVERIFY(!aliasLd.children.front().excerpt.contains(QStringLiteral("ldName")));
        const auto aliasScope = trace(*changed, aliasDomain, aliasLd.children.back());
        QVERIFY(std::any_of(aliasScope.children.begin(), aliasScope.children.end(), [](const auto& node) {
            return node.attribute == "apName" && node.value == "AP1";
        }));
        const auto wrongDataSet = Named(*changed, "DataSet", "Voltage");
        request.node = changed->browse.memberRows[changed->browse.dataSetMembers.value(wrongDataSet).first];
        const auto conflict = CollectSclReferences(*changed, request, nullptr, true);
        QVERIFY(!RefValue(conflict.traceEntries, Kind::MmsItem));
        const auto* conflictFc = RefValue(conflict.traceEntries, Kind::FunctionalConstraint);
        QVERIFY(conflictFc);
        const auto conflictGraph = trace(*changed, *conflictFc, root);
        QCOMPARE(conflictGraph.children.front().value, QStringLiteral("ST"));
        QCOMPARE(conflictGraph.children.back().value, QStringLiteral("MX"));
        QCOMPARE(conflictGraph.children.back().relation, QStringLiteral("候选依据"));
        TaskControl cancelled;
        cancelled.cancelled = true;
        QVERIFY_EXCEPTION_THROWN(TraceSclReference(*s, item, root, &cancelled), TaskCancelled);
    }

    void Task9TypeAndArrayTrace() {
        using Kind = SclReferenceValueKind;
        using Branch = SclTraceBranch;
        SclTraceNode root;
        root.branch = Branch::Root;
        auto xml = Bytes("Task8Voltage.scd");
        xml.replace("name=\"phsA\" type=\"FullPhase\"", "name='phsA' type='FullPhase' count='3'");
        xml.replace("daName=\"cVal.mag.f\" fc=\"MX\"", "daName=\"cVal.mag.f\" fc=\"MX\" ix=\"01\"");
        const auto s = Snapshot(xml);
        SclReferenceRequest request;
        const auto dataSet = Named(*s, "DataSet", "Voltage");
        request.node = s->browse.memberRows[s->browse.dataSetMembers.value(dataSet).first];
        const auto values = CollectSclReferences(*s, request, nullptr, true);
        const auto* item = RefValue(values.traceEntries, Kind::MmsItem);
        QVERIFY(item);
        const auto graph = TraceSclReference(*s, *item, root, nullptr);
        const auto array = std::find_if(graph.children.begin(), graph.children.end(), [](const auto& node) {
            return node.branch == Branch::Array;
        });
        QVERIFY(array != graph.children.end());
        const auto indexed = TraceSclReference(*s, *item, *array, nullptr);
        QCOMPARE(indexed.children[0].value, QStringLiteral("3"));
        QCOMPARE(indexed.children[1].value, QStringLiteral("01"));
        QCOMPARE(indexed.children[2].value, QStringLiteral("1"));
        QCOMPARE(indexed.children[3].value, QStringLiteral("cVal$mag$f"));

        auto address = Request(SclMmsQueryKind::Attributes);
        address.scope.ied = Named(*s, "IED", "DemoListed");
        address.text = "cVal.mag.f";
        QVERIFY(!RefValue(QuerySclMms(*s, address, nullptr).rows.front().references, Kind::MmsItem));
        address.arrayMember = request.node;
        auto result = QuerySclMms(*s, address, nullptr);
        QCOMPARE(result.rows.size(), size_t(1));
        const auto& row = result.rows.front();
        request = {};
        request.anchor = SclReferenceAnchor::Mms;
        request.mms = row.key;
        request.prepared = row.references;
        auto collected = CollectSclReferences(*s, request, nullptr, true);
        auto sameText = [](const auto& value) { return value.kind == Kind::TypeReference && value.value == "AnalogFloat"; };
        QCOMPARE(std::count_if(collected.entries.begin(), collected.entries.end(), sameText), 1);
        // A node-based provider may legitimately deliver two declarations of the same ID.
        const auto typed = *std::find_if(row.references.begin(), row.references.end(), sameText);
        auto other = typed;
        other.evidence.node = Named(*s, "BDA", "ang");
        request.prepared.push_back(other);
        collected = CollectSclReferences(*s, request, nullptr, true);
        QCOMPARE(std::count_if(collected.entries.begin(), collected.entries.end(), sameText), 1);
        QCOMPARE(std::count_if(collected.traceEntries.begin(), collected.traceEntries.end(), sameText), 2);
        const auto type = TraceSclReference(*s, typed, root, nullptr);
        QCOMPARE(type.children[1].relation, QStringLiteral("引用定义"));
        QCOMPARE(type.children[1].attribute, QStringLiteral("id"));
        const auto typeMember = TraceSclReference(*s, typed, type.children[1], nullptr);
        QCOMPARE(typeMember.children.front().value, QStringLiteral("f"));

        xml = Bytes("Task8Voltage.scd");
        xml.replace("type=\"AnalogFloat\"", "type=\"MissingAnalog\"");
        const auto missing = Snapshot(xml);
        request = {};
        request.node = Named(*missing, "BDA", "ang");
        auto uncertain = CollectSclReferences(*missing, request, nullptr, true);
        const auto* missingType = RefValue(uncertain.traceEntries, Kind::TypeReference);
        QVERIFY(missingType);
        const auto missingGraph = TraceSclReference(*missing, *missingType, root, nullptr);
        QCOMPARE(missingGraph.children.back().value, QStringLiteral("缺失"));

        xml = Bytes("Task8Voltage.scd");
        xml.replace("</DataTypeTemplates>", QByteArray("<DAType id='AnalogFloat'/>").repeated(90) + "</DataTypeTemplates>");
        const auto ambiguous = Snapshot(xml);
        request.node = Named(*ambiguous, "BDA", "ang");
        uncertain = CollectSclReferences(*ambiguous, request, nullptr, true);
        const auto* ambiguousType = RefValue(uncertain.traceEntries, Kind::TypeReference);
        QVERIFY(ambiguousType);
        const auto ambiguousGraph = TraceSclReference(*ambiguous, *ambiguousType, root, nullptr);
        QVERIFY(ambiguousGraph.limited);
        QCOMPARE(ambiguousGraph.children.size(), size_t(SclReferenceTrace::VisibleLimit - 1));
        QCOMPARE(ambiguousGraph.children.back().relation, QStringLiteral("候选"));
    }

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
        QCOMPARE(blocked.rows[0].state, LinkState::Ambiguous); QVERIFY(blocked.rows[0].item.isEmpty());
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
        QCOMPARE(blocked.rows[0].state, LinkState::Ambiguous); QVERIFY(blocked.rows[0].item.isEmpty());
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
        const auto* rawId = RefValue(result.rows[0].references, SclReferenceValueKind::RawReportId);
        QVERIFY(rawId); QCOMPARE(rawId->value, longId);
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
