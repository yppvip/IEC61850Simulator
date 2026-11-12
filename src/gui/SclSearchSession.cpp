#include "SclSearchSession.hpp"
#include "SclTiming.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QWaitCondition>
#include <QtConcurrentRun>
#include <deque>
#include <exception>
#include <utility>

namespace iec61850sim
{
struct SclSearchSession::Worker
{
    static constexpr size_t QueueCapacity = 16384;
    static constexpr size_t DeliveryLimit = 4096;
    Scl::TaskControl task;
    QMutex mutex;
    QWaitCondition spaceAvailable;
    std::deque<SclHit> hits;
    bool finished = false; // Owning event-loop thread.
};

SclSearchSession::SclSearchSession(QObject* parent) : QObject(parent)
{
    m_timer.setInterval(10);
    connect(&m_timer, &QTimer::timeout, this, &SclSearchSession::Drain);
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, [this] {
        const auto worker = m_worker;
        worker->finished = true;
        if (worker->task.cancelled.load(std::memory_order_relaxed))
        {
            m_worker.reset();
            Launch();
            return;
        }
        Drain();
    });
}

SclSearchSession::~SclSearchSession()
{
    Cancel();
}

void SclSearchSession::Cancel()
{
    m_pending.reset();
    m_timer.stop();
    if (const auto worker = m_worker)
    {
        {
            QMutexLocker guard(&worker->mutex);
            worker->task.cancelled.store(true, std::memory_order_relaxed);
            worker->spaceAvailable.wakeAll();
        }
        if (worker->finished)
            m_worker.reset();
    }
}

void SclSearchSession::Start(std::shared_ptr<const SclSnapshot> snapshot, SclQuery query)
{
    Cancel();
    m_pending = Request{std::move(snapshot), std::move(query)};
    Launch();
}

void SclSearchSession::Launch()
{
    if (m_worker || !m_pending)
        return;
    auto request = std::move(*m_pending);
    m_pending.reset();
    m_worker = std::make_shared<Worker>();
    m_timer.start();
    m_watcher.setFuture(QtConcurrent::run([request = std::move(request), worker = m_worker] {
        try
        {
            SearchScl(*request.snapshot, request.query, worker->task, [&](SclHitBatch batch) {
                auto next = batch.begin();
                while (next != batch.end())
                {
                    QMutexLocker guard(&worker->mutex);
                    while (worker->hits.size() == Worker::QueueCapacity
                        && !worker->task.cancelled.load(std::memory_order_relaxed))
                        worker->spaceAvailable.wait(&worker->mutex);
                    Scl::checkpoint(&worker->task);
                    const auto count = qMin(Worker::QueueCapacity - worker->hits.size(), size_t(batch.end() - next));
                    worker->hits.insert(worker->hits.end(), next, next + count);
                    next += count;
                }
            });
        }
        catch (const Scl::TaskCancelled&)
        {
        }
        catch (const std::exception&)
        {
            return QStringLiteral("搜索未能完成。");
        }
        return QString();
    }));
}

void SclSearchSession::Drain()
{
    const auto worker = m_worker;
    if (!worker || worker->task.cancelled.load(std::memory_order_relaxed))
        return;
    SclHitBatch hits;
    bool deliveredAll = false;
    {
        QMutexLocker guard(&worker->mutex);
        const auto count = qMin(worker->hits.size(), Worker::DeliveryLimit);
        const auto end = worker->hits.begin() + count;
        hits.assign(worker->hits.begin(), end);
        worker->hits.erase(worker->hits.begin(), end);
        deliveredAll = worker->finished && worker->hits.empty();
        worker->spaceAvailable.wakeOne();
    }
    if (!hits.empty())
    {
        {
            const SclTiming timing("Search Drain delivery");
            emit BatchReady(hits);
        }
        if (qEnvironmentVariableIntValue("IEC61850_SCL_TIMING") == 1)
            qInfo() << "SCL search batch rows" << hits.size();
    }
    if (deliveredAll && m_worker == worker && !worker->task.cancelled.load(std::memory_order_relaxed))
    {
        m_timer.stop();
        const auto error = m_watcher.result();
        m_worker.reset();
        emit Finished(error);
    }
}
} // namespace iec61850sim
