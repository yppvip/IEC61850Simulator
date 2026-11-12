#include "SclAnalysisService.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QtConcurrentRun>
#include <exception>
#include <utility>

namespace iec61850sim
{

SclAnalysisService::SclAnalysisService(QObject* parent)
    : QObject(parent)
{
    m_progress.setInterval(40);
    connect(&m_progress, &QTimer::timeout, this, [this] {
        if (!m_control || m_control->cancelled.load(std::memory_order_relaxed))
            return;
        const auto stage = m_control->stage.load(std::memory_order_relaxed);
        if (m_stage != stage)
        {
            m_stage = stage;
            emit Changed();
        }
    });
    connect(&m_fileWatcher, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) {
        if (m_snapshot && m_snapshot->path == path && !m_sourceChanged)
        {
            m_sourceChanged = true;
            emit Changed();
        }
    });
}

SclAnalysisService::~SclAnalysisService()
{
    StopWorker();
}

void SclAnalysisService::StopWorker()
{
    m_pendingPath.clear();
    m_progress.stop();
    if (m_control)
        m_control->cancelled.store(true, std::memory_order_relaxed);
}

void SclAnalysisService::Open(const QString& path)
{
    if (path.isEmpty())
        return;
    StopWorker();
    m_failure = {};
    m_requestedPath = QFileInfo(path).absoluteFilePath();
    const auto suffix = QFileInfo(path).suffix().toLower();
    if (suffix != "icd" && suffix != "scd" && suffix != "cid" && suffix != "ccd")
    {
        m_failure.diagnostic = {Scl::Error::File, QStringLiteral("仅支持 ICD、SCD、CID 和 CCD 文件。")};
        m_state = State::Failed;
    }
    else
    {
        m_pendingPath = m_requestedPath;
        m_stage = Scl::TaskStage::Reading;
        m_state = State::Loading;
        StartPending();
    }
    emit Changed();
}

void SclAnalysisService::Reload()
{
    // Reload the displayed document. On the first failed open, retry that path.
    const QString path = m_snapshot ? m_snapshot->path : m_requestedPath;
    Open(path);
}

void SclAnalysisService::Cancel()
{
    if (m_state != State::Loading)
        return;
    StopWorker();
    m_state = State::Cancelled;
    emit Changed();
}

void SclAnalysisService::Close()
{
    if (m_state == State::Idle)
        return;
    StopWorker();
    const auto paths = m_fileWatcher.files();
    if (!paths.isEmpty())
        m_fileWatcher.removePaths(paths);
    m_requestedPath.clear();
    m_snapshot.reset();
    m_failure = {};
    m_sourceChanged = false;
    m_stage = Scl::TaskStage::Reading;
    m_state = State::Idle;
    emit Changed();
}

void SclAnalysisService::StartPending()
{
    if (m_control || m_pendingPath.isEmpty())
        return;

    const QString path = m_pendingPath;
    m_pendingPath.clear();
    m_control = std::make_shared<Scl::TaskControl>();
    m_progress.start();
    m_watcher = new QFutureWatcher<Completion>(this);
    connect(m_watcher, &QFutureWatcher<Completion>::finished, this, &SclAnalysisService::Finish);
    m_watcher->setFuture(QtConcurrent::run([path, control = m_control] {
        return Run(path, control.get());
    }));
}

SclAnalysisService::Completion SclAnalysisService::Run(const QString& path, Scl::TaskControl* task)
{
    try
    {
        Scl::checkpoint(task);
        const QFileInfo before(path);
        const auto modified = before.lastModified();
        const auto size = before.size();
        auto input = Scl::Reader::readFile(path, {}, task);
        if (!input.document)
            return {{}, std::move(input)};
        auto analysis = Scl::ReferenceResolver::analyze(input, task);
        Scl::enterStage(task, Scl::TaskStage::BrowseIndex);
        SclBrowseIndex browse(*analysis.document, task);
        browse.BuildScopes(analysis, task);
        Scl::checkpoint(task);
        const QFileInfo after(path);
        if (!after.isFile() || after.size() != size || after.lastModified() != modified)
            return {{}, {{}, {}, {Scl::Error::File, QStringLiteral("文件在分析期间发生变化，请重新加载。")}}};
        return {std::make_shared<const SclSnapshot>(SclSnapshot{path, std::move(analysis), std::move(browse)}), {}};
    }
    catch (const Scl::TaskCancelled&)
    {
        return {};
    }
    catch (const std::exception&)
    {
        return {{}, {{}, {}, {Scl::Error::File, QStringLiteral("无法完成 SCL 分析。")}}};
    }
}

void SclAnalysisService::Finish()
{
    m_progress.stop();
    auto* watcher = std::exchange(m_watcher, nullptr);
    watcher->deleteLater();
    const bool cancelled = m_control->cancelled.load(std::memory_order_relaxed);
    m_control.reset();
    if (cancelled)
    {
        // The cancelled worker may already have produced a result before its
        // completion event arrived. Its result still belongs to the old request.
        StartPending();
        return;
    }

    auto completion = watcher->result();
    if (completion.snapshot)
    {
        if (!m_fileWatcher.files().isEmpty())
            m_fileWatcher.removePaths(m_fileWatcher.files());
        m_snapshot = std::move(completion.snapshot);
        m_fileWatcher.addPath(m_snapshot->path);
        m_sourceChanged = false;
        m_state = State::Ready;
    }
    else
    {
        m_failure = std::move(completion.failure);
        m_state = State::Failed;
    }
    emit Changed();
}

} // namespace iec61850sim
