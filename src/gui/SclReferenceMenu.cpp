#include "SclReferenceMenu.hpp"
#include "SclTableInteraction.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QClipboard>
#include <QFutureWatcher>
#include <QMenu>
#include <QPointer>
#include <QtConcurrentRun>

namespace iec61850sim
{
namespace
{
QString Preview(QString value)
{
    value.replace("\r\n", QStringLiteral(" ↵ "));
    value.replace('\n', QStringLiteral(" ↵ "));
    value.replace('\r', QStringLiteral(" ↵ "));
    value.replace('\t', QStringLiteral(" ⇥ "));
    constexpr int limit = 88;
    if (value.size() > limit) {
        value.truncate(value.at(limit - 1).isHighSurrogate() ? limit - 1 : limit);
        value += QStringLiteral("…");
    }
    return value.replace('&', "&&");
}
}

void ConnectSclReferenceMenu(SclTableInteraction* interaction,
    std::function<SclReferenceMenuContext(const QModelIndex&)> provider)
{
    interaction->EnableReferenceMenus();
    QObject::connect(interaction, &SclTableInteraction::PopulateContextMenu, interaction,
        [interaction, provider = std::move(provider)](QMenu& menu, const QModelIndex& index) {
            auto context = provider(index);
            if (!context.snapshot) return;
            const QPointer<QAbstractItemView> view = interaction->View();
            const QPointer<SclTableInteraction> owner = interaction;
            const auto epoch = interaction->ReferenceEpoch();
            const QPersistentModelIndex hit(index);
            const auto control = std::make_shared<Scl::TaskControl>();
            const auto invalidate = [control] { control->cancelled = true; };
            auto* model = interaction->View()->model();
            QObject::connect(model, &QAbstractItemModel::modelAboutToBeReset, &menu, invalidate);
            QObject::connect(model, &QAbstractItemModel::layoutAboutToBeChanged, &menu, invalidate);
            QObject::connect(model, &QAbstractItemModel::rowsAboutToBeRemoved, &menu, invalidate);
            QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &menu, invalidate);
            QObject::connect(model, &QAbstractItemModel::rowsAboutToBeMoved, &menu, invalidate);
            QObject::connect(model, &QAbstractItemModel::dataChanged, &menu, invalidate);
            QObject::connect(&menu, &QObject::destroyed, invalidate);
            const auto current = [view, owner, epoch, hit, control, guard = std::move(context.isCurrent)] {
                return view && owner && owner->ReferenceEpoch() == epoch && view->isVisible() && view->isEnabled()
                    && hit.isValid() && view->model() == hit.model()
                    && !control->cancelled && (!guard || guard());
            };
            auto* pending = menu.addAction(QStringLiteral("正在准备引用…"));
            pending->setEnabled(false);
            auto* watcher = new QFutureWatcher<SclReferenceValues>(&menu);
            QObject::connect(watcher, &QFutureWatcherBase::finished, &menu, [&menu, pending, watcher, current] {
                const auto result = watcher->result();
                watcher->deleteLater();
                if (current()) {
                    QHash<int, QMenu*> groups;
                    for (const auto& entry : result.entries) {
                        auto*& group = groups[int(entry.kind)];
                        if (!group) {
                            group = menu.addMenu(SclReferenceLabel(entry.kind));
                            group->setObjectName("SclReferenceGroup");
                            group->menuAction()->setProperty("SclReferenceKind", int(entry.kind));
                        }
                        group->addAction(Preview(entry.value), &menu, [value = entry.value, current] {
                            if (current()) QApplication::clipboard()->setText(value);
                        });
                    }
                    if (result.limited) menu.addAction(QStringLiteral("引用列表受限 · 可在详情逐条查阅"))->setEnabled(false);
                }
                menu.removeAction(pending);
                pending->deleteLater();
                if (menu.isEmpty()) menu.close();
            });
            watcher->setFuture(QtConcurrent::run([snapshot = std::move(context.snapshot),
                request = std::move(context.request), control] {
                try { return CollectSclReferences(*snapshot, request, control.get()); }
                catch (const Scl::TaskCancelled&) { return SclReferenceValues{}; }
            }));
        });
}
} // namespace iec61850sim
