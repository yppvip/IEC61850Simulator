#pragma once

#include "SclDocument.hpp"
#include <QPlainTextEdit>

namespace iec61850sim
{
class SclXmlHighlighter;

class SclSourcePreview final : public QPlainTextEdit
{
public:
    explicit SclSourcePreview(QWidget* parent);
    QString Show(const Scl::SourceIndex* source, Scl::SourceLocation location, const Scl::Document* document);

private:
    void resizeEvent(QResizeEvent* event) override;
    QMimeData* createMimeDataFromSelection() const override;
    void PaintGutter();
    QWidget* m_gutter;
    SclXmlHighlighter* m_highlighter;
    int m_targetLine = 0;
};
} // namespace iec61850sim
