#pragma once

#include "SclSearch.hpp"
#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <optional>

namespace iec61850sim
{
class SclSearchSession final : public QObject
{
    Q_OBJECT
public:
    explicit SclSearchSession(QObject* parent = nullptr);
    ~SclSearchSession() override;
    void Start(std::shared_ptr<const SclSnapshot> snapshot, SclQuery query);
    void Cancel();

signals:
    void BatchReady(const SclHitBatch& hits);
    void Finished(const QString& error);

private:
    struct Request { std::shared_ptr<const SclSnapshot> snapshot; SclQuery query; };
    struct Worker;
    void Launch();
    void Drain();

    QFutureWatcher<QString> m_watcher;
    QTimer m_timer;
    std::shared_ptr<Worker> m_worker;
    std::optional<Request> m_pending;
};
} // namespace iec61850sim
