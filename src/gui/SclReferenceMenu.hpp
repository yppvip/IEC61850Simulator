#pragma once

#include "SclReferenceValues.hpp"
#include <QPointer>
#include <functional>

class QModelIndex;
class QAbstractItemView;

namespace iec61850sim
{
class SclTableInteraction;

struct SclReferenceMenuContext
{
    std::shared_ptr<const SclSnapshot> snapshot;
    SclReferenceRequest request;
    std::function<bool()> isCurrent; // Optional view generation check beyond model changes.
};

struct SclReferenceTraceSelection
{
    std::shared_ptr<const SclSnapshot> snapshot;
    std::vector<SclReferenceValue> values;
    QPointer<QAbstractItemView> sourceView;
    bool limited = false;
};

void ConnectSclReferenceMenu(SclTableInteraction*, std::function<SclReferenceMenuContext(const QModelIndex&)>);
} // namespace iec61850sim
