#include "SclSearchSession.hpp"

#include <QMutex>
#include <QMutexLocker>
#include <QtConcurrentRun>
#include <exception>
#include <utility>

namespace iec61850sim
{
struct SclSearchSession::Worker
{
    Scl::TaskControl task;
    QMutex mutex;
    SclHitBatch hits;
};

SclSearchSession::SclSearchSession(QObject* parent) : QObject(parent)
{
    m_timer.setInterval(35);
    connect(&m_timer, &QTimer::timeout, this, &SclSearchSession::Drain);
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, [this] {
        m_timer.stop();
        const auto worker = m_worker;
        if (!worker->task.cancelled.load(std::memory_order_relaxed))
        {
            Drain();
            if (!worker->task.cancelled.load(std::memory_order_relaxed))
            {
                const auto error = m_watcher.result();
                m_worker.reset();
                emit Finished(error);
                return;
            }
        }
        m_worker.reset();
        Launch();
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
    if (m_worker)
        m_worker->task.cancelled.store(true, std::memory_order_relaxed);
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
                QMutexLocker guard(&worker->mutex);
                worker->hits.insert(worker->hits.end(), batch.begin(), batch.end());
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
    if (!m_worker || m_worker->task.cancelled.load(std::memory_order_relaxed))
        return;
    SclHitBatch hits;
    {
        QMutexLocker guard(&m_worker->mutex);
        hits.swap(m_worker->hits);
    }
    if (!hits.empty())
        emit BatchReady(hits);
}
} // namespace iec61850sim
