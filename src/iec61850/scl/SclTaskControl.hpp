#pragma once

#include <atomic>

namespace Scl {

enum class TaskStage { Reading, SourceIndex, Xml, ReferenceIndex, Validation, BrowseIndex };
struct TaskCancelled {};

struct TaskControl {
    std::atomic<bool> cancelled{false};
    std::atomic<TaskStage> stage{TaskStage::Reading};
};

inline void checkpoint(TaskControl *task)
{
    if (task && task->cancelled.load(std::memory_order_relaxed))
        throw TaskCancelled{};
}

inline void enterStage(TaskControl *task, TaskStage stage)
{
    if (task)
        task->stage.store(stage, std::memory_order_relaxed);
    checkpoint(task);
}

} // namespace Scl
