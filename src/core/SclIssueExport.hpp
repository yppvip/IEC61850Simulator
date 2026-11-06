#pragma once

#include "SclAnalysisService.hpp"

namespace iec61850sim
{
enum class IssueFormat { Json, Csv };

// Returns an empty string after atomically saving the complete report.
QString ExportSclIssues(const SclSnapshot& snapshot, const QString& path, IssueFormat format);
} // namespace iec61850sim
