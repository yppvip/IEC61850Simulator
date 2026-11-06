#pragma once

#include "SclReferenceResolver.hpp"
#include "SclBrowseIndex.hpp"

#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>

namespace iec61850sim
{

struct SclSnapshot
{
    QString path;
    Scl::ReferenceAnalysis analysis;
    SclBrowseIndex browse;
};

// Call from the owning event-loop thread. Each successful request publishes a
// complete snapshot, which consumers can retain across reloads.
class SclAnalysisService final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Loading, Ready, Failed, Cancelled };
    explicit SclAnalysisService(QObject* parent = nullptr);
    ~SclAnalysisService() override;

    void Open(const QString& path);
    void Reload();
    void Cancel();
    void Close();
    State state() const { return m_state; }
    Scl::TaskStage stage() const { return m_stage; }
    QString requestedPath() const { return m_requestedPath; }
    bool sourceChanged() const { return m_sourceChanged; }
    std::shared_ptr<const SclSnapshot> snapshot() const { return m_snapshot; }
    const Scl::Result& failure() const { return m_failure; }

signals:
    void Changed();

private:
    struct Completion
    {
        std::shared_ptr<const SclSnapshot> snapshot;
        Scl::Result failure;
    };

    static Completion Run(const QString& path, Scl::TaskControl* task);
    void StartPending();
    void Finish();
    void StopWorker();

    QFutureWatcher<Completion>* m_watcher = nullptr;
    QTimer m_progress;
    QFileSystemWatcher m_fileWatcher;
    std::shared_ptr<Scl::TaskControl> m_control;
    std::shared_ptr<const SclSnapshot> m_snapshot;
    Scl::Result m_failure;
    QString m_requestedPath;
    QString m_pendingPath;
    State m_state = State::Idle;
    Scl::TaskStage m_stage = Scl::TaskStage::Reading;
    bool m_sourceChanged = false;
};

} // namespace iec61850sim
