#include "SclSourceIndex.hpp"

#include <algorithm>
#include <utility>

namespace Scl {
namespace {
constexpr int MaxPreviewBytes = 8192;
}

SourceIndex::SourceIndex(QByteArray bytes, int maxLines, TaskControl *task) : m_bytes(std::move(bytes))
{
    if (m_bytes.startsWith(QByteArray("\xff\xfe\0\0", 4))
        || m_bytes.startsWith(QByteArray("\0\0\xfe\xff", 4))
        || m_bytes.startsWith(QByteArray("<\0\0\0", 4))
        || m_bytes.startsWith(QByteArray("\0\0\0<", 4))) {
        m_encoding = Encoding::Unsupported;
        return;
    }
    if (m_bytes.startsWith("\xef\xbb\xbf")) {
        m_bom = 3;
    } else if (m_bytes.startsWith("\xff\xfe") || m_bytes.startsWith(QByteArray("<\0", 2))) {
        m_encoding = Encoding::Utf16LE;
        m_bom = m_bytes.startsWith("\xff\xfe") ? 2 : 0;
        m_width = 2;
    } else if (m_bytes.startsWith("\xfe\xff") || m_bytes.startsWith(QByteArray("\0<", 2))) {
        m_encoding = Encoding::Utf16BE;
        m_bom = m_bytes.startsWith("\xfe\xff") ? 2 : 0;
        m_width = 2;
    }

    if (maxLines < 1)
        return;
    m_lines.push_back(m_bom);
    int nextCheck = 0;
    for (int i = m_bom; i < m_bytes.size(); i += m_width) {
        if (i >= nextCheck) {
            checkpoint(task);
            nextCheck = i + 65536;
        }
        const int c = unit(i);
        if (c == '\r' && unit(i + m_width) == '\n')
            i += m_width;
        if (c == '\r' || c == '\n') {
            if (lineCount() == maxLines) {
                m_lines.clear();
                return;
            }
            m_lines.push_back(i + m_width);
        }
    }
}

int SourceIndex::unit(int offset) const
{
    if (offset + m_width > m_bytes.size())
        return -1;
    const auto *p = reinterpret_cast<const unsigned char *>(m_bytes.constData()) + offset;
    if (m_width == 1)
        return p[0];
    return m_encoding == Encoding::Utf16LE ? p[0] + 256 * p[1] : 256 * p[0] + p[1];
}

SourceLocation SourceIndex::locate(int byteOffset) const
{
    if (byteOffset < m_bom || byteOffset > m_bytes.size() || m_lines.empty())
        return {};
    const auto it = std::upper_bound(m_lines.begin(), m_lines.end(), byteOffset);
    return {byteOffset, int(it - m_lines.begin())};
}

SourceLine SourceIndex::line(int number) const
{
    if (number < 1 || number > lineCount())
        return {};
    const int start = m_lines[number - 1];
    const int end = number < lineCount() ? m_lines[number] : int(m_bytes.size());
    int stop = std::min(end, start + MaxPreviewBytes);
    if (stop < end) {
        if (m_width == 1) {
            while (stop > start && (unit(stop) & 0xc0) == 0x80)
                --stop;
        } else if (unit(stop - 2) >= 0xd800 && unit(stop - 2) <= 0xdbff) {
            stop -= 2;
        }
    }
    if (m_width == 1)
        return {QString::fromUtf8(m_bytes.constData() + start, stop - start), stop < end};
    QString text;
    text.reserve((stop - start) / 2);
    for (int i = start; i + 1 < stop; i += 2)
        text.append(QChar(ushort(unit(i))));
    return {text, stop < end};
}

int SourceIndex::previewColumn(SourceLocation location) const
{
    if (location.line < 1 || location.line > lineCount())
        return -1;
    const int start = m_lines[location.line - 1];
    const int end = location.line < lineCount() ? m_lines[location.line] : int(m_bytes.size());
    const int bytes = location.byteOffset - start;
    if (bytes < 0 || bytes >= end - start || bytes >= MaxPreviewBytes)
        return -1;
    return m_width == 2 ? bytes / 2 : int(QString::fromUtf8(m_bytes.constData() + start, bytes).size());
}

SourceCursor::SourceCursor(const SourceIndex &source, TaskControl *task)
    : m_source(source), m_offset(source.m_bom), m_task(task) {}

bool SourceCursor::matches(const char *pattern) const
{
    int pos = m_offset;
    for (; *pattern; ++pattern, pos += m_source.m_width) {
        if (m_source.unit(pos) != *pattern)
            return false;
    }
    return true;
}

void SourceCursor::skipThrough(const char *pattern)
{
    int nextCheck = m_offset + 65536;
    while (m_offset < m_source.m_bytes.size() && !matches(pattern)) {
        if (m_offset >= nextCheck) {
            checkpoint(m_task);
            nextCheck = m_offset + 65536;
        }
        m_offset += m_source.m_width;
    }
    while (*pattern++ && m_offset < m_source.m_bytes.size())
        m_offset += m_source.m_width;
}

int SourceCursor::nextOpeningTag()
{
    const int step = m_source.m_width;
    int nextCheck = m_offset + 65536;
    while (m_offset < m_source.m_bytes.size()) {
        if (m_offset >= nextCheck) {
            checkpoint(m_task);
            nextCheck = m_offset + 65536;
        }
        if (m_source.unit(m_offset) != '<') {
            m_offset += step;
        } else if (matches("<!--")) {
            m_offset += 4 * step;
            skipThrough("-->");
        } else if (matches("<![CDATA[")) {
            m_offset += 9 * step;
            skipThrough("]]>");
        } else if (matches("<?")) {
            m_offset += 2 * step;
            skipThrough("?>");
        } else if (matches("<!DOCTYPE")) {
            return Doctype;
        } else if (matches("</")) {
            m_offset += 2 * step;
        } else {
            const int opening = m_offset;
            m_offset += step;
            return opening;
        }
    }
    return End;
}

} // namespace Scl
