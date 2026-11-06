#include "SclReader.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QXmlStreamReader>
#include <utility>

namespace Scl {
namespace {

QString intern(QHash<QString, QString> &pool, const QString &text)
{
    const auto found = pool.constFind(text);
    if (found != pool.cend())
        return found.value();
    return pool.insert(text, text).value();
}

bool supportedEncoding(const QString &declaration, SourceIndex::Encoding actual)
{
    const auto name = declaration.toUpper();
    using Encoding = SourceIndex::Encoding;
    if (actual == Encoding::Utf8)
        return name.isEmpty() || name == QStringLiteral("UTF-8") || name == QStringLiteral("US-ASCII");
    return name.isEmpty() || name == QStringLiteral("UTF-16")
        || (actual == Encoding::Utf16LE && name == QStringLiteral("UTF-16LE"))
        || (actual == Encoding::Utf16BE && name == QStringLiteral("UTF-16BE"));
}

} // namespace

Result Reader::readFile(const QString &path, const Limits &limits, TaskControl *task)
{
    enterStage(task, TaskStage::Reading);
    const QFileInfo before(path);
    if (!before.isFile())
        return {{}, {}, {Error::File, QStringLiteral("The input is not a readable file")}};
    const auto modified = before.lastModified();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {{}, {}, {Error::File, file.errorString()}};
    const auto size = file.size();
    if (size > limits.fileBytes)
        return {{}, {}, {Error::Limit, QStringLiteral("SCL file size limit exceeded")}};
    QByteArray bytes;
    bytes.reserve(int(size));
    constexpr int ChunkBytes = 256 * 1024;
    while (!file.atEnd() && bytes.size() <= limits.fileBytes) {
        checkpoint(task);
        const auto chunk = file.read(qMin(qint64(ChunkBytes), qint64(limits.fileBytes) - bytes.size() + 1));
        if (file.error() != QFileDevice::NoError)
            return {{}, {}, {Error::File, file.errorString()}};
        if (chunk.isEmpty())
            break;
        bytes += chunk;
    }
    const QFileInfo after(path);
    if (bytes.size() <= limits.fileBytes && (!after.isFile() || bytes.size() != size || after.size() != size
            || after.lastModified() != modified))
        return {{}, {}, {Error::File, QStringLiteral("The file changed during reading. Reload to try again.")}};
    return readBytes(std::move(bytes), limits, task);
}

Result Reader::readBytes(QByteArray bytes, const Limits &limits, TaskControl *task)
{
    enterStage(task, TaskStage::SourceIndex);
    if (bytes.size() > limits.fileBytes)
        return {{}, {}, {Error::Limit, QStringLiteral("SCL file size limit exceeded")}};
    if (bytes.isEmpty())
        return {{}, {}, {Error::Empty, QStringLiteral("SCL file is empty")}};

    // QByteArray::fromRawData() may refer to caller-owned storage.
    bytes.detach();
    auto source = std::make_shared<const SourceIndex>(std::move(bytes), limits.lines, task);
    auto fail = [&](Error code, const QString &detail, qint64 line = 0, qint64 column = 0) -> Result {
        return {source, {}, {code, detail, line, column}};
    };
    if (source->encoding() == SourceIndex::Encoding::Unsupported)
        return fail(Error::UnsupportedEncoding, QStringLiteral("Unsupported XML encoding"));
    if (source->lineCount() == 0)
        return fail(Error::Limit, QStringLiteral("Source line limit exceeded"));

    enterStage(task, TaskStage::Xml);
    SourceCursor cursor(*source, task);
    int opening = cursor.nextOpeningTag();
    if (opening == SourceCursor::Doctype)
        return fail(Error::Dtd, QStringLiteral("DOCTYPE is disabled"), cursor.location().line);

    QBuffer buffer;
    buffer.setData(source->bytes());
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader xml(&buffer);
    auto document = std::make_shared<Document>();
    QHash<QString, QString> names;
    struct Frame {
        NodeId id;
        NodeId lastChild = NoNode;
        QString whitespace;
        int textLength = 0;
    };
    std::vector<Frame> stack;

    while (!xml.atEnd()) {
        checkpoint(task);
        switch (xml.readNext()) {
        case QXmlStreamReader::StartDocument:
            document->xmlEncoding = xml.documentEncoding().toString();
            if (!supportedEncoding(document->xmlEncoding, source->encoding()))
                return fail(Error::UnsupportedEncoding, QStringLiteral("Unsupported or inconsistent XML encoding"),
                            xml.lineNumber(), xml.columnNumber());
            break;
        case QXmlStreamReader::DTD:
            return fail(Error::Dtd, QStringLiteral("DOCTYPE is disabled"), xml.lineNumber(), xml.columnNumber());
        case QXmlStreamReader::StartElement: {
            if (opening < 0)
                return fail(Error::Xml, QStringLiteral("Cannot locate XML opening tag"), xml.lineNumber(), xml.columnNumber());
            const auto location = source->locate(opening);
            opening = cursor.nextOpeningTag();
            if (document->nodes.empty()
                && (xml.name() != QStringLiteral("SCL") || xml.namespaceUri() != NamespaceUri))
                return fail(Error::UnsupportedRoot, QStringLiteral("Expected SCL root in the IEC SCL namespace"), location.line);
            const auto attrs = xml.attributes();
            if (int(stack.size()) >= limits.depth)
                return fail(Error::Limit, QStringLiteral("XML depth limit exceeded"), location.line);
            if (int(document->nodes.size()) >= limits.nodes)
                return fail(Error::Limit, QStringLiteral("XML element count limit exceeded"), location.line);
            if (int(document->attributes.size()) + attrs.size() > limits.attributes)
                return fail(Error::Limit, QStringLiteral("XML attribute count limit exceeded"), location.line);

            Node node;
            node.name = intern(names, xml.name().toString());
            node.namespaceUri = intern(names, xml.namespaceUri().toString());
            node.source = location;
            node.firstAttribute = int(document->attributes.size());
            node.attributeCount = int(attrs.size());
            for (const auto &attr : attrs) {
                checkpoint(task);
                if (attr.value().size() > limits.valueChars)
                    return fail(Error::Limit, QStringLiteral("XML attribute value limit exceeded"), location.line);
                auto value = attr.value().toString();
                if (value.isNull())
                    value = QStringLiteral("");
                document->attributes.push_back({intern(names, attr.name().toString()),
                                                intern(names, attr.namespaceUri().toString()), std::move(value)});
            }
            if (node.isScl())
                ++document->standardCounts[node.name];
            const NodeId id = int(document->nodes.size());
            if (!stack.empty()) {
                auto &parent = stack.back();
                node.parent = parent.id;
                parent.whitespace.clear();
                if (parent.lastChild == NoNode)
                    document->nodes[parent.id].firstChild = id;
                else
                    document->nodes[parent.lastChild].nextSibling = id;
                parent.lastChild = id;
            }
            document->nodes.push_back(std::move(node));
            stack.push_back({id, NoNode, {}, 0});
            break;
        }
        case QXmlStreamReader::Characters:
            if (!stack.empty()) {
                auto &frame = stack.back();
                auto &node = document->nodes[frame.id];
                const auto text = xml.text();
                if (text.size() > limits.valueChars - frame.textLength)
                    return fail(Error::Limit, QStringLiteral("XML direct text limit exceeded"), xml.lineNumber(), xml.columnNumber());
                frame.textLength += int(text.size());
                if (xml.isWhitespace() && node.text.isNull()) {
                    if (frame.lastChild == NoNode)
                        frame.whitespace += text;
                } else {
                    node.text += frame.whitespace;
                    frame.whitespace.clear();
                    node.text += text;
                }
            }
            break;
        case QXmlStreamReader::EndElement: {
            auto &frame = stack.back();
            if (frame.lastChild == NoNode && document->nodes[frame.id].text.isNull())
                document->nodes[frame.id].text = std::move(frame.whitespace);
            stack.pop_back();
            break;
        }
        default:
            break;
        }
    }

    if (xml.hasError())
        return fail(Error::Xml, xml.errorString(), xml.lineNumber(), xml.columnNumber());
    return {std::move(source), std::move(document), {}};
}

} // namespace Scl
