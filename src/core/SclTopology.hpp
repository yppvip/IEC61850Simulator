#pragma once

#include "SclAnalysisService.hpp"
#include <QJsonObject>

namespace iec61850sim
{
bool CanShowSclTopology(const SclSnapshot& snapshot, Scl::NodeId node);
QJsonObject BuildSclTopology(const SclSnapshot& snapshot, Scl::NodeId focus);
QString WriteSclTopology(const SclSnapshot& snapshot, Scl::NodeId focus, const QString& directory, QString& error);
} // namespace iec61850sim
