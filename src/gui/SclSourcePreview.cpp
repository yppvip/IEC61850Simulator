#include "SclSourcePreview.hpp"

#include <QFontDatabase>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <algorithm>
#include <functional>

namespace iec61850sim
{
namespace
{
constexpr int PreviewCharacters = 1024;

struct PreviewLine final : QTextBlockUserData
{
    int number = 0;
    int anchor = -1;
    bool clipped = false;
    bool startsFile = false;
    bool continuation = false;
    bool sourceLineClipped = false;
};

class Gutter final : public QWidget
{
public:
    Gutter(QWidget* parent, std::function<void()> paint) : QWidget(parent), m_paint(std::move(paint)) {}

private:
    void paintEvent(QPaintEvent*) override { m_paint(); }
    std::function<void()> m_paint;
};
} // namespace

class SclXmlHighlighter final : public QSyntaxHighlighter
{
public:
    explicit SclXmlHighlighter(QPlainTextEdit* edit) : QSyntaxHighlighter(edit->document()), m_edit(edit) {}

private:
    enum State { Unknown, Text, Tag, SingleQuote, DoubleQuote, Comment, Cdata, Instruction };

    void highlightBlock(const QString& text) override
    {
        const auto* line = static_cast<const PreviewLine*>(currentBlock().userData());
        if (!line)
            return;
        State state = previousBlockState() < 0 ? Unknown : State(previousBlockState());
        int pos = 0;
        if (line->startsFile)
            state = Text;
        if (state == Unknown && line->anchor >= 0)
        {
            pos = line->anchor;
            state = Text;
        }
        const bool dark = m_edit->palette().base().color().lightness() < 128;
        const QColor tag(dark ? "#8ab4f8" : "#2355a4");
        const QColor attribute(dark ? "#d6a9f5" : "#693c85");
        const QColor value(dark ? "#8de0c5" : "#13675e");
        const QColor comment(dark ? "#aeb9c4" : "#596675");
        const QColor entity(dark ? "#ffc885" : "#8c470a");
        const auto markEntities = [&](int start, int end) {
            static const QRegularExpression pattern(QStringLiteral("&(?:#[0-9]+|#x[0-9a-fA-F]+|[\\w:.-]+);"));
            auto matches = pattern.globalMatch(text.mid(start, end - start));
            while (matches.hasNext())
            {
                const auto match = matches.next();
                setFormat(start + match.capturedStart(), match.capturedLength(), entity);
            }
        };
        const auto starts = [&](QLatin1String token) {
            return text.size() - pos >= token.size() && QStringView(text).mid(pos, token.size()) == token;
        };
        const auto delimited = [&](QLatin1String terminator, const QColor& color, int skip, State continuation) {
            const int found = text.indexOf(terminator, pos + skip);
            const int end = found < 0 ? int(text.size()) : found + terminator.size();
            setFormat(pos, end - pos, color);
            pos = end;
            state = found < 0 ? continuation : Text;
        };
        while (pos < text.size() && state != Unknown)
        {
            if (state == Comment || (state == Text && starts(QLatin1String("<!--"))))
                delimited(QLatin1String("-->"), comment, state == Text ? 4 : 0, Comment);
            else if (state == Cdata || (state == Text && starts(QLatin1String("<![CDATA["))))
                delimited(QLatin1String("]]>"), value, state == Text ? 9 : 0, Cdata);
            else if (state == Instruction || (state == Text && starts(QLatin1String("<?"))))
                delimited(QLatin1String("?>"), comment, state == Text ? 2 : 0, Instruction);
            else if (state == SingleQuote || state == DoubleQuote)
            {
                const int start = pos;
                const int end = text.indexOf(state == SingleQuote ? QChar('\'') : QChar('"'), pos);
                pos = end < 0 ? int(text.size()) : end + 1;
                setFormat(start, pos - start, value);
                markEntities(start, pos);
                if (end >= 0)
                    state = Tag;
            }
            else if (state == Text)
            {
                const int start = pos;
                if (text[pos] != '<')
                {
                    const int next = text.indexOf('<', pos);
                    pos = next < 0 ? int(text.size()) : next;
                    markEntities(start, pos);
                    continue;
                }
                if (starts(QLatin1String("<!")))
                {
                    state = Unknown;
                    break;
                }
                ++pos;
                if (pos < text.size() && text[pos] == '/')
                    ++pos;
                while (pos < text.size() && !text[pos].isSpace() && text[pos] != '/' && text[pos] != '>')
                    ++pos;
                setFormat(start, pos - start, tag);
                state = Tag;
            }
            else
            {
                const int start = pos++;
                const auto ch = text[start];
                if (ch == '\'' || ch == '"')
                {
                    setFormat(start, 1, value);
                    state = ch == '\'' ? SingleQuote : DoubleQuote;
                }
                else if (ch == '>' || ch == '/')
                {
                    setFormat(start, 1, tag);
                    if (ch == '>')
                        state = Text;
                }
                else if (!ch.isSpace() && ch != '=')
                {
                    while (pos < text.size() && !text[pos].isSpace()
                           && QStringView(u"=/'\">").indexOf(text[pos]) < 0)
                        ++pos;
                    setFormat(start, pos - start, attribute);
                }
            }
        }
        setCurrentBlockState(line->clipped ? Unknown : state);
    }

    QPlainTextEdit* m_edit;
};

SclSourcePreview::SclSourcePreview(QWidget* parent)
    : QPlainTextEdit(parent), m_gutter(new Gutter(this, [this] { PaintGutter(); })),
      m_highlighter(new SclXmlHighlighter(this))
{
    setObjectName("SclSource");
    setReadOnly(true);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_gutter->setObjectName("SclSourceGutter");
    connect(this, &QPlainTextEdit::updateRequest, m_gutter, [this] { m_gutter->update(); });
    setViewportMargins(fontMetrics().horizontalAdvance(QStringLiteral("00000000  ")), 0, 0, 0);
}

void SclSourcePreview::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const auto area = contentsRect();
    m_gutter->setGeometry(area.left(), area.top(), viewportMargins().left(), area.height());
}

void SclSourcePreview::PaintGutter()
{
    QPainter painter(m_gutter);
    painter.fillRect(m_gutter->rect(), palette().alternateBase());
    for (auto block = firstVisibleBlock(); block.isValid(); block = block.next())
    {
        const auto geometry = blockBoundingGeometry(block).translated(contentOffset());
        if (geometry.top() >= m_gutter->height())
            break;
        const auto* line = static_cast<const PreviewLine*>(block.userData());
        if (!line)
            continue;
        const QRect row(0, qRound(geometry.top()), m_gutter->width(), qRound(geometry.height()));
        if (line->number == m_targetLine)
            painter.fillRect(row, palette().highlight());
        painter.setPen(line->number == m_targetLine ? palette().highlightedText().color() : palette().text().color());
        if (!line->continuation)
            painter.drawText(row.adjusted(0, 0, -4, 0), Qt::AlignVCenter | Qt::AlignRight,
                QString::number(line->number) + (line->sourceLineClipped ? QStringLiteral(" …") : QString()));
    }
}

QString SclSourcePreview::Show(const Scl::SourceIndex* source, Scl::SourceLocation location, const Scl::Document* document)
{
    m_targetLine = 0;
    if (!source || source->lineCount() == 0)
    {
        clear();
        return source ? QStringLiteral("原文行索引不可用") : QString();
    }
    const bool located = location.line > 0 && location.line <= source->lineCount();
    m_targetLine = located ? location.line : 0;
    const int center = located ? location.line : 1;
    const int first = qMax(1, center - 15);
    const int last = qMin(source->lineCount(), center + 15);
    QStringList lines;
    std::vector<std::unique_ptr<PreviewLine>> metadata;
    auto node = document ? std::lower_bound(document->nodes.begin(), document->nodes.end(), first,
        [](const auto& item, int line) { return item.source.line < line; }) : std::vector<Scl::Node>::const_iterator{};
    int clipped = 0;
    bool targetClipped = false;
    int targetBlock = -1;
    const int targetColumn = source->previewColumn(location);
    for (int n = first; n <= last; ++n)
    {
        auto entry = source->line(n);
        while (entry.text.endsWith('\r') || entry.text.endsWith('\n'))
            entry.text.chop(1);
        const bool lineClipped = entry.truncated || entry.text.size() > PreviewCharacters;
        if (entry.text.size() > PreviewCharacters)
            entry.text.truncate(entry.text.at(PreviewCharacters - 1).isHighSurrogate()
                ? PreviewCharacters - 1 : PreviewCharacters);
        int anchor = -1;
        if (document)
        {
            // Skip the rest of dense source lines by binary search.
            node = std::lower_bound(node, document->nodes.end(), n,
                [](const auto& item, int number) { return item.source.line < number; });
            if (node != document->nodes.end() && node->source.line == n)
                anchor = source->previewColumn(node->source);
        }
        clipped += lineClipped;
        if (n == m_targetLine)
        {
            targetBlock = lines.size();
            targetClipped = lineClipped && (targetColumn < 0 || targetColumn >= entry.text.size());
        }
        // Qt treats U+2029 as a paragraph boundary even inside an XML source line.
        const auto parts = entry.text.split(QChar::ParagraphSeparator);
        int offset = 0;
        for (int i = 0; i < parts.size(); ++i)
        {
            const auto& part = parts[i];
            auto line = std::make_unique<PreviewLine>();
            line->number = n;
            line->continuation = i != 0;
            line->startsFile = n == 1 && i == 0;
            line->clipped = lineClipped && i + 1 == parts.size();
            line->sourceLineClipped = lineClipped;
            if (anchor >= offset && anchor < offset + part.size())
                line->anchor = anchor - offset;
            if (n == m_targetLine && !targetClipped && targetColumn >= offset && targetColumn < offset + part.size())
                targetBlock = lines.size();
            lines.push_back(part);
            metadata.push_back(std::move(line));
            offset += part.size() + 1;
        }
    }
    setPlainText(lines.join('\n'));
    auto block = this->document()->firstBlock();
    for (auto& line : metadata)
    {
        block.setUserData(line.release());
        block = block.next();
    }
    m_highlighter->rehighlight();
    const auto target = targetBlock >= 0 ? this->document()->findBlockByNumber(targetBlock) : QTextBlock{};
    if (target.isValid())
    {
        QTextEdit::ExtraSelection selection;
        selection.cursor = QTextCursor(target);
        selection.format.setBackground(palette().alternateBase());
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        setTextCursor(selection.cursor);
        setExtraSelections({selection});
        centerCursor();
    }
    else
        setExtraSelections({});
    m_gutter->update();
    auto label = located ? QStringLiteral("第 %1 行（%2–%3）").arg(location.line).arg(first).arg(last)
        : QStringLiteral("位置不可用 · 显示第 %1–%2 行").arg(first).arg(last);
    if (clipped)
        label += QStringLiteral(" · %1 行已截断").arg(clipped);
    if (targetClipped)
        label += QStringLiteral(" · 仅定位到行");
    return label;
}

QMimeData* SclSourcePreview::createMimeDataFromSelection() const
{
    const auto cursor = textCursor();
    QString text;
    const int end = cursor.selectionEnd();
    int start = cursor.selectionStart();
    for (auto block = document()->findBlock(start); block.isValid() && start < end; block = block.next())
    {
        const int textEnd = block.position() + block.length() - 1;
        text += block.text().mid(start - block.position(), qMin(end, textEnd) - start);
        if (end > textEnd)
        {
            const auto* next = static_cast<const PreviewLine*>(block.next().userData());
            text += next && next->continuation ? QChar(QChar::ParagraphSeparator) : QChar('\n');
        }
        start = textEnd + 1;
    }
    auto* data = new QMimeData;
    data->setText(text);
    return data;
}
} // namespace iec61850sim
