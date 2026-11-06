#pragma once

#include "SclTaskControl.hpp"

#include <QByteArray>
#include <QString>
#include <vector>

namespace Scl {

struct SourceLocation {
    int byteOffset = -1; // Includes the BOM, when present.
    int line = 0;        // One-based; zero means unavailable.
};

struct SourceLine {
    QString text; // Original line ending included when it fits.
    bool truncated = false;
};

class SourceIndex {
public:
    enum class Encoding { Utf8, Utf16LE, Utf16BE, Unsupported };
    SourceIndex(QByteArray bytes, int maxLines, TaskControl *task = nullptr);

    const QByteArray &bytes() const { return m_bytes; }
    Encoding encoding() const { return m_encoding; }
    int lineCount() const { return int(m_lines.size()); }
    SourceLocation locate(int byteOffset) const;
    SourceLine line(int number) const;
    int previewColumn(SourceLocation location) const; // Zero-based UTF-16 column within the preview budget, or -1.

private:
    friend class SourceCursor;
    int unit(int offset) const;

    QByteArray m_bytes;
    Encoding m_encoding = Encoding::Utf8;
    int m_bom = 0;
    int m_width = 1;
    std::vector<int> m_lines;
};

// Aligns XML start-element tokens with the original opening '<'.
// QXmlStreamReader remains responsible for interpreting and validating XML.
class SourceCursor {
public:
    explicit SourceCursor(const SourceIndex &source, TaskControl *task = nullptr);
    enum { End = -1, Doctype = -2 };
    int nextOpeningTag();
    SourceLocation location() const { return m_source.locate(m_offset); }

private:
    bool matches(const char *pattern) const;
    void skipThrough(const char *pattern);

    const SourceIndex &m_source;
    int m_offset;
    TaskControl *m_task;
};

} // namespace Scl
