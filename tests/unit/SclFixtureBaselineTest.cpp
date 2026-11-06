#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QtTest>

// Preparation checks only: no product parser, reference resolver or XSD validator.
class SclFixtureBaselineTest final : public QObject
{
    Q_OBJECT
private slots:
    void FixtureInventory();
};

void SclFixtureBaselineTest::FixtureInventory()
{
    const QDir directory(QString::fromUtf8(SCL_FIXTURE_DIR));
    QFile manifest(directory.filePath(QStringLiteral("Manifest.json")));
    QVERIFY(manifest.open(QIODevice::ReadOnly));
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(manifest.readAll(), &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    const auto fixtures = document.object().value(QStringLiteral("fixtures")).toArray();
    QVERIFY(!fixtures.isEmpty());
    QStringList inventory;
    for (const auto& value : fixtures)
    {
        const auto entry = value.toObject();
        const auto name = entry.value(QStringLiteral("file")).toString();
        QVERIFY2(!inventory.contains(name), qPrintable(name));
        inventory.append(name);
        QFile file(directory.filePath(name));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(name));
        QXmlStreamReader reader(&file);
        QHash<QString, int> counts;
        QString root;
        QString rootNamespace;
        while (!reader.atEnd())
        {
            reader.readNext();
            if (!reader.isStartElement())
                continue;
            if (root.isEmpty())
            {
                root = reader.name().toString();
                rootNamespace = reader.namespaceUri().toString();
            }
            if (reader.namespaceUri() == QLatin1String("http://www.iec.ch/61850/2003/SCL"))
                ++counts[reader.name().toString()];
        }
        QCOMPARE(!reader.hasError(), entry.value(QStringLiteral("wellFormed")).toBool());
        if (!reader.hasError())
        {
            QCOMPARE(root, entry.value(QStringLiteral("root")).toString());
            QCOMPARE(rootNamespace, entry.value(QStringLiteral("namespace")).toString());
            const auto expected = entry.value(QStringLiteral("counts")).toObject();
            for (auto it = expected.begin(); it != expected.end(); ++it)
                QVERIFY2(counts.value(it.key()) == it.value().toInt(),
                    qPrintable(name + QStringLiteral(": ") + it.key()));
        }
    }
    auto files = directory.entryList({QStringLiteral("*.icd"), QStringLiteral("*.cid"),
        QStringLiteral("*.scd"), QStringLiteral("*.ccd")}, QDir::Files);
    files.sort();
    inventory.sort();
    QCOMPARE(files, inventory);
}

QTEST_GUILESS_MAIN(SclFixtureBaselineTest)
#include "SclFixtureBaselineTest.moc"
