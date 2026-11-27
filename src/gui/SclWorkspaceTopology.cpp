#include "SclWorkspace.hpp"
#include "SclTopology.hpp"
#include "SclReferenceMenu.hpp"
#include "SclTableInteraction.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLockFile>
#include <QMenu>
#include <QPointer>
#include <QTableView>
#include <QPushButton>
#include <QStandardPaths>
#include <QStyle>
#include <QTemporaryDir>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace iec61850sim
{
namespace
{
struct Output { QString path; QString error; };

QString TopologyRoot()
{
    const auto temp = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).canonicalPath();
    if (temp.isEmpty())
        return {};
    const auto root = QDir(temp).filePath(QStringLiteral("IEC61850Simulator-topology"));
    if (!QDir().mkpath(root) || QFileInfo(root).canonicalFilePath() != root)
        return {};
    return root;
}

void CleanOldPages(const QString& root)
{
    QDir base(root);
    const auto runs = base.entryInfoList({QStringLiteral("run-*")}, QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const auto& run : runs)
    {
        const auto path = run.absoluteFilePath();
        if (run.canonicalFilePath() != path || run.lastModified().secsTo(QDateTime::currentDateTime()) < 60)
            continue;
        QDir dir(path);
        QLockFile lock(dir.filePath(QStringLiteral("active.lock")));
        lock.setStaleLockTime(0);
        if (!lock.tryLock())
            continue;
        for (const auto& page : dir.entryList({QStringLiteral("topology-*.html")}, QDir::Files | QDir::NoSymLinks))
            dir.remove(page);
        lock.unlock();
        base.rmdir(run.fileName());
    }
}
} // namespace

class SclTopologyFiles
{
public:
    SclTopologyFiles()
    {
        const auto root = TopologyRoot();
        if (root.isEmpty())
            return;
        CleanOldPages(root);
        m_directory = std::make_unique<QTemporaryDir>(QDir(root).filePath(QStringLiteral("run-XXXXXX")));
        if (!m_directory->isValid())
            return;
        m_lock = std::make_unique<QLockFile>(QDir(m_directory->path()).filePath(QStringLiteral("active.lock")));
        m_lock->setStaleLockTime(0);
        if (!m_lock->tryLock())
            m_directory.reset();
    }

    QString Path() const { return m_directory && m_directory->isValid() ? m_directory->path() : QString(); }

private:
    std::unique_ptr<QTemporaryDir> m_directory;
    std::unique_ptr<QLockFile> m_lock;
};

void SclWorkspace::CreateTopologyControls(QVBoxLayout* layout)
{
    m_topologyFiles = std::make_shared<SclTopologyFiles>();
    auto* actions = new QHBoxLayout;
    m_topology = new QPushButton(style()->standardIcon(QStyle::SP_FileLinkIcon), QStringLiteral("打开局部引用拓扑"), this);
    m_topology->setObjectName("SclOpenTopology");
    m_topology->setToolTip(QStringLiteral("打开当前所选节点的本地只读关系图"));
    m_copyTopologyPath = new QPushButton(QStringLiteral("复制本地页面路径"), this);
    actions->addWidget(m_topology);
    actions->addWidget(m_copyTopologyPath);
    actions->addStretch();
    layout->addLayout(actions);
    m_topologyStatus = new QLabel(this);
    m_topologyStatus->setObjectName("SclTopologyStatus");
    m_topologyStatus->setTextFormat(Qt::PlainText);
    m_topologyStatus->setWordWrap(true);
    layout->addWidget(m_topologyStatus);
    connect(m_topology, &QPushButton::clicked, this, [this] { OpenTopology(m_selected); });
    connect(m_copyTopologyPath, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(QDir::toNativeSeparators(m_topologyPath));
    });
    UpdateTopologyControls();
}

void SclWorkspace::AddTopologyAction(QAbstractItemView* view)
{
    auto* interaction = new SclTableInteraction(view);
    if (view->objectName() == "SclObjects" || view->objectName() == "SclTemplates") interaction->SetEnabled(true);
    interaction->EnableReferenceMenus();
    connect(interaction, &SclTableInteraction::PopulateContextMenu, this, [this, view](QMenu& menu, const QModelIndex& index) {
        if (!m_snapshot) return;
        const auto id = index.data(Qt::UserRole).toInt();
        const std::weak_ptr<const SclSnapshot> snapshot = m_snapshot;
        if (!m_buildingTopology && CanShowSclTopology(*m_snapshot, id))
            menu.addAction(QStringLiteral("打开引用拓扑"), this, [this, id, snapshot] {
                if (snapshot.lock() == m_snapshot) OpenTopology(id);
            });
        AddMmsAttributeAction(menu, id, view, index);
    });
    if (qobject_cast<QTableView*>(view)) AddNodeReferenceMenu(interaction);
}

void SclWorkspace::AddMmsAttributeAction(QMenu& menu, Scl::NodeId node, QAbstractItemView* view, const QModelIndex& index)
{
    if (!m_snapshot || !IsSclMmsAttributeTarget(*m_snapshot, node)) return;
    const std::weak_ptr<const SclSnapshot> snapshot = m_snapshot;
    const auto scope = m_ied;
    const auto generation = m_referenceGeneration;
    const auto identity = index.data(Qt::UserRole);
    auto* action = menu.addAction(QStringLiteral("在 MMS 中查阅属性"), this,
        [this, snapshot, scope, node, generation, identity, view = QPointer<QAbstractItemView>(view), hit = QPersistentModelIndex(index)] {
        if (snapshot.lock() != m_snapshot || scope != m_ied || generation != m_referenceGeneration
            || !view || !view->isVisible() || !view->isEnabled() || !hit.isValid()
            || view->model() != hit.model() || hit.data(Qt::UserRole) != identity) return;
        view->setCurrentIndex(hit);
        OpenMmsAttributeTarget(node);
    });
    action->setObjectName("SclOpenMmsAttributes");
}

SclReferenceMenuContext SclWorkspace::ReferenceMenu(SclReferenceRequest request)
{
    const std::weak_ptr<const SclSnapshot> snapshot = m_snapshot;
    const auto generation = m_referenceGeneration;
    return {m_snapshot, std::move(request), [self = QPointer<SclWorkspace>(this), snapshot, generation] {
        return self && snapshot.lock() == self->m_snapshot && generation == self->m_referenceGeneration;
    }};
}

void SclWorkspace::AddNodeReferenceMenu(SclTableInteraction* interaction, bool candidate)
{
    ConnectSclReferenceMenu(interaction, [this, candidate](const QModelIndex& index) {
        const auto value = index.data(Qt::UserRole);
        if (!value.isValid() || value.toInt() == Scl::NoNode) return SclReferenceMenuContext{};
        SclReferenceRequest request;
        request.anchor = candidate ? SclReferenceAnchor::Candidate : SclReferenceAnchor::Node;
        request.node = value.toInt();
        return ReferenceMenu(std::move(request));
    });
}

void SclWorkspace::UpdateTopologyControls()
{
    m_topology->setEnabled(!m_buildingTopology && m_snapshot && CanShowSclTopology(*m_snapshot, m_selected));
    if (m_overviewTopology)
        m_overviewTopology->setEnabled(m_topology->isEnabled());
    m_copyTopologyPath->setVisible(!m_topologyPath.isEmpty());
    m_topologyStatus->setVisible(!m_topologyStatus->text().isEmpty());
}

void SclWorkspace::OpenTopology(Scl::NodeId id)
{
    const auto snapshot = m_snapshot;
    if (!snapshot || m_buildingTopology || !CanShowSclTopology(*snapshot, id))
        return;
    const auto files = m_topologyFiles;
    const auto directory = files->Path();
    if (directory.isEmpty())
    {
        m_topologyStatus->setText(QStringLiteral("无法创建引用拓扑的本地临时目录。"));
        UpdateTopologyControls();
        return;
    }
    m_buildingTopology = true;
    m_topologyPath.clear();
    m_topologyStatus->setText(QStringLiteral("正在生成局部引用拓扑…"));
    UpdateTopologyControls();
    auto* watcher = new QFutureWatcher<Output>(this);
    connect(watcher, &QFutureWatcher<Output>::finished, this, [this, watcher, snapshot] {
        const auto output = watcher->result();
        watcher->deleteLater();
        m_buildingTopology = false;
        if (snapshot == m_snapshot)
        {
            m_topologyPath = output.path;
            if (output.path.isEmpty())
                m_topologyStatus->setText(QStringLiteral("生成引用拓扑失败 · %1").arg(output.error));
            else
                m_topologyStatus->setText(QDesktopServices::openUrl(QUrl::fromLocalFile(output.path))
                    ? QStringLiteral("已请求浏览器打开本地只读快照。")
                    : QStringLiteral("无法启动浏览器，可重试或复制本地页面路径后手动打开。"));
        }
        else if (!output.path.isEmpty())
            QFile::remove(output.path);
        UpdateTopologyControls();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, id, directory, files] {
        Output output;
        output.path = WriteSclTopology(*snapshot, id, directory, output.error);
        return output;
    }));
}
} // namespace iec61850sim
