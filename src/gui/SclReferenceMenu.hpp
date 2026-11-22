#pragma once

#include "SclReferenceValues.hpp"
#include <functional>

class QModelIndex;

namespace iec61850sim
{
class SclTableInteraction;

struct SclReferenceMenuContext
{
    std::shared_ptr<const SclSnapshot> snapshot;
    SclReferenceRequest request;
    std::function<bool()> isCurrent; // Optional view generation check beyond model changes.
};

void ConnectSclReferenceMenu(SclTableInteraction*, std::function<SclReferenceMenuContext(const QModelIndex&)>);
} // namespace iec61850sim
