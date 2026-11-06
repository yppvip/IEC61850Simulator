#include "SclReader.hpp"

#include <QFile>
#include <QtTest>

using namespace Scl;

namespace {
Result fixture(const char *name)
{
    return Reader::readFile(QStringLiteral(SCL_FIXTURE_DIR "/") + QString::fromLatin1(name));
}

NodeId find(const Document &doc, const QString &name)
{
    for (int id = 0; id < int(doc.nodes.size()); ++id) {
        if (doc.nodes[id].isScl() && doc.nodes[id].name == name)
            return id;
    }
    return NoNode;
}

QByteArray utf16(const QString &text, bool little)
{
    QByteArray result = little ? QByteArray::fromHex("fffe") : QByteArray::fromHex("feff");
    for (QChar c : text) {
        const char low = char(c.unicode() & 255);
        const char high = char(c.unicode() >> 8);
        result += little ? low : high;
        result += little ? high : low;
    }
    return result;
}
} // namespace

class SclReaderTest : public QObject {
    Q_OBJECT

private slots:
    void model()
    {
        const auto result = fixture("NamespaceCollision.scd");
        QVERIFY2(result.document, qPrintable(result.diagnostic.detail));
        const auto &doc = *result.document;
        QCOMPARE(doc.nodes.front().parent, NoNode);
        QCOMPARE(doc.attribute(0, "version"), QStringLiteral("2007"));
        QCOMPARE(doc.standardCounts.value("IED"), 1);
        QCOMPARE(doc.standardCounts.value("DataSet"), 1);
        QCOMPARE(doc.standardCounts.value("ReportControl"), 2);
        QCOMPARE(doc.standardCounts.value("FCDA"), 3);
        QCOMPARE(doc.standardCounts.value("P"), 6);

        const auto ld = find(doc, "LDevice");
        QVERIFY(ld != NoNode);
        QCOMPARE(doc.attribute(ld, "ldName"), QStringLiteral("EXPLICIT_LD"));
        const auto ln0 = doc.nodes[ld].firstChild;
        QCOMPARE(doc.nodes[ln0].name, QStringLiteral("LN0"));
        QVERIFY(!doc.attribute(ln0, "inst").isNull());
        const auto dataSet = find(doc, "DataSet");
        QVERIFY(dataSet != NoNode);
        QCOMPARE(doc.nodes[dataSet].parent, ln0);
        QCOMPARE(doc.attribute(dataSet, "name"), QStringLiteral("dsEvents"));
        auto member = doc.nodes[dataSet].firstChild;
        QVERIFY(doc.attribute(member, "daName").isNull());
        member = doc.nodes[member].nextSibling;
        QCOMPARE(doc.attribute(member, "daName"), QStringLiteral("stVal"));
        member = doc.nodes[member].nextSibling;
        QCOMPARE(doc.attribute(member, "daName"), QStringLiteral("stVal"));
        QCOMPARE(doc.nodes[member].nextSibling, NoNode);
        const auto report = doc.nodes[dataSet].nextSibling;
        QCOMPARE(doc.attribute(report, "rptID"), QStringLiteral("TEST_EVENTS"));
        QCOMPARE(doc.attribute(report, "datSet"), QStringLiteral("dsEvents"));
        QCOMPARE(doc.nodes[doc.nodes[report].firstChild].name, QStringLiteral("TrgOps"));

        const auto p = find(doc, "P");
        QVERIFY(p != NoNode);
        QCOMPARE(doc.attribute(p, "type"), QStringLiteral("IP"));
        QCOMPARE(doc.nodes[p].text, QStringLiteral("192.0.2.10"));
        const auto definition = find(doc, "LNodeType");
        QVERIFY(definition != NoNode);
        QCOMPARE(doc.attribute(definition, "id"), QStringLiteral("T_LLN0"));
        QCOMPARE(doc.attribute(doc.nodes[definition].firstChild, "type"), QStringLiteral("T_ENS"));
        QCOMPARE(doc.nodes[find(doc, "EnumVal")].text, QStringLiteral("on"));

        const auto ext = doc.nodes[find(doc, "Private")].firstChild;
        QCOMPARE(doc.nodes[ext].namespaceUri, QStringLiteral("urn:synthetic:extension"));
        QCOMPARE(doc.nodes[ext].name, QStringLiteral("IED"));
        QVERIFY(!doc.nodes[ext].isScl());
    }

    void namespacesAndFailures()
    {
        for (const auto *name : {"Minimal.icd", "Minimal.cid", "Minimal.scd", "NamespaceAlias.scd",
                                 "DuplicateDataSet.scd", "DanglingDataSet.scd", "MissingType.scd",
                                 "UnknownVersion.scd"})
            QVERIFY2(fixture(name).document, name);
        QCOMPARE(fixture("NamespaceAlias.scd").document->standardCounts.value("IED"), 1);
        QCOMPARE(fixture("UnknownDialect.ccd").diagnostic.code, Error::UnsupportedRoot);
        QCOMPARE(fixture("Empty.scd").diagnostic.code, Error::Empty);
        const auto truncated = fixture("Truncated.scd");
        QCOMPARE(truncated.diagnostic.code, Error::Xml);
        QVERIFY(truncated.diagnostic.line > 0);
        QVERIFY(truncated.source);
        QVERIFY(!truncated.document);
        QCOMPARE(Reader::readFile(QStringLiteral(SCL_FIXTURE_DIR "/NotPresent.scd")).diagnostic.code, Error::File);
        QCOMPARE(Reader::readBytes("<SCL/>").diagnostic.code, Error::UnsupportedRoot);
        const auto dtd = Reader::readBytes("<?xml version='1.0'?>\n<!-- harmless -->\n"
            "<!DOCTYPE SCL SYSTEM 'file:///not-read.dtd'><SCL/>");
        QCOMPARE(dtd.diagnostic.code, Error::Dtd);
        QCOMPARE(dtd.diagnostic.line, 3);
        const auto internal = Reader::readBytes("<!DOCTYPE SCL [<!ENTITY x 'secret'>]><SCL>&x;</SCL>");
        QCOMPARE(internal.diagnostic.code, Error::Dtd);
        QVERIFY(!internal.document);
    }

    void sourceLocations_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::addColumn<int>("width");
        QTest::addColumn<int>("offset");
        const auto makeXml = [](const QString &encoding) {
            return QStringLiteral("<?xml version='1.0' encoding='%1'?>\r\n"
                "<?test <fake/>?>\n<SCL xmlns='%2'>\r"
                "<!-- <IED name='fake'/> -->\r\n"
                "  <IED\r\n    name='测点😀' xmlns:v='urn:test' v:name='foreign'>\n"
                "    <Private><![CDATA[<IED>伪造</IED>]]></Private>\n"
                "  </IED>\n  <IED name='next'/></SCL>").arg(encoding, NamespaceUri);
        };
        const auto u8 = makeXml("UTF-8");
        const auto u16 = makeXml("UTF-16");
        const int offset8 = u8.left(u8.indexOf("  <IED") + 2).toUtf8().size();
        const int offset16 = 2 + 2 * (u16.indexOf("  <IED") + 2);
        QTest::newRow("utf8") << u8.toUtf8() << 1 << offset8;
        QTest::newRow("utf8-bom") << QByteArray::fromHex("efbbbf") + u8.toUtf8() << 1 << offset8 + 3;
        QTest::newRow("utf16-le") << utf16(u16, true) << 2 << offset16;
        QTest::newRow("utf16-be") << utf16(u16, false) << 2 << offset16;
    }

    void sourceLocations()
    {
        QFETCH(QByteArray, bytes);
        QFETCH(int, width);
        QFETCH(int, offset);
        const auto result = Reader::readBytes(bytes);
        QVERIFY2(result.document, qPrintable(result.diagnostic.detail));
        QCOMPARE(result.source->bytes(), bytes);
        const auto &doc = *result.document;
        const auto ied = find(doc, "IED");
        QVERIFY(ied != NoNode);
        QCOMPARE(doc.standardCounts.value("IED"), 2);
        QCOMPARE(doc.nodes[ied].source.line, 5);
        QCOMPARE(doc.nodes[ied].source.byteOffset, offset);
        QCOMPARE(bytes.mid(offset, width), width == 1 ? QByteArray("<") : bytes.startsWith(QByteArray::fromHex("fffe"))
                     ? QByteArray::fromHex("3c00") : QByteArray::fromHex("003c"));
        QCOMPARE(result.source->line(5).text, QStringLiteral("  <IED\r\n"));
        QVERIFY(result.source->line(6).text.contains(QStringLiteral("测点😀")));
        QCOMPARE(doc.attribute(ied, "name"), QStringLiteral("测点😀"));
        QCOMPARE(doc.attribute(ied, "name", "urn:test"), QStringLiteral("foreign"));
        QCOMPARE(doc.nodes[doc.nodes[ied].nextSibling].source.line, 9);
        QCOMPARE(doc.nodes[doc.nodes[ied].firstChild].text, QStringLiteral("<IED>伪造</IED>"));
    }

    void resourceLimits()
    {
        const auto xml = "<SCL xmlns='" + NamespaceUri.toUtf8() + "'>\n<IED name='x'>123<!-- split -->456</IED></SCL>";
        const auto check = [&](const Limits &limits) {
            const auto result = Reader::readBytes(xml, limits);
            QCOMPARE(result.diagnostic.code, Error::Limit);
            QVERIFY(!result.document);
        };
        Limits limits;
        limits.fileBytes = xml.size() - 1;
        check(limits);
        limits = {};
        limits.depth = 1;
        check(limits);
        limits = {};
        limits.nodes = 1;
        check(limits);
        limits = {};
        limits.attributes = 0;
        check(limits);
        limits = {};
        limits.valueChars = 5;
        check(limits);
        limits = {};
        limits.lines = 1;
        check(limits);

        const auto longLine = Reader::readBytes("<SCL xmlns='" + NamespaceUri.toUtf8() + "'>\n"
            + QByteArray(8191, ' ') + QStringLiteral("测点</SCL>").toUtf8());
        QVERIFY(longLine.document);
        const auto line = longLine.source->line(2);
        QVERIFY(line.truncated);
        QCOMPARE(line.text, QString(8191, QChar(' ')));
    }

    void unsupportedEncoding()
    {
        const auto result = Reader::readBytes("<?xml version='1.0' encoding='ISO-8859-1'?><SCL xmlns='"
            + NamespaceUri.toUtf8() + "'/>");
        QCOMPARE(result.diagnostic.code, Error::UnsupportedEncoding);
        QVERIFY(!result.document);
        QCOMPARE(Reader::readBytes(QByteArray::fromHex("fffe00003c000000")).diagnostic.code,
                 Error::UnsupportedEncoding);
    }

    void snapshotOwnsInput()
    {
        auto input = "<SCL xmlns='" + NamespaceUri.toUtf8() + "'/>\n";
        const auto result = Reader::readBytes(QByteArray::fromRawData(input.constData(), input.size()));
        QVERIFY(result.document);
        input.fill('x');
        QVERIFY(result.source->line(1).text.startsWith(QStringLiteral("<SCL ")));
    }
};

QTEST_APPLESS_MAIN(SclReaderTest)
#include "SclReaderTest.moc"
