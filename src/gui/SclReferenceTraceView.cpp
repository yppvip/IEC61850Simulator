#include "SclReferenceTraceView.hpp"
#include "SclReferenceMenu.hpp"

#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtConcurrentRun>
#include <algorithm>
#include <map>

namespace iec61850sim
{
namespace
{
constexpr int Root = -1;
constexpr int NoSelection = -2;

class TraceCanvas final : public QGraphicsView
{
public:
    explicit TraceCanvas(QWidget* parent) : QGraphicsView(parent) {
        setDragMode(ScrollHandDrag);
        setRenderHint(QPainter::Antialiasing);
        setTransformationAnchor(AnchorUnderMouse);
        setMinimumSize(0, 0);
        setAccessibleName(QStringLiteral("引用来源图，滚轮缩放，空白处拖动平移"));
    }
    std::function<void(int, bool)> activate;
protected:
    void wheelEvent(QWheelEvent* event) override {
        const qreal factor = event->angleDelta().y() > 0 ? 1.15 : 1 / 1.15;
        const qreal zoom = transform().m11();
        if (!qFuzzyIsNull(zoom) && (factor > 1 ? zoom < 3 : zoom > 0.03))
            scale(factor, factor);
        event->accept();
    }
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            for (auto* item = itemAt(event->pos()); item; item = item->parentItem()) {
                if (!item->data(0).isValid()) continue;
                activate(item->data(0).toInt(), item->data(1).toBool());
                event->accept();
                return;
            }
        }
        QGraphicsView::mousePressEvent(event);
    }
};
}

struct SclReferenceTraceLocation
{
    struct Entry {
        SclTraceNode node;
        int parent;
        bool expanded = false;
        bool limited = false;
    };
    std::shared_ptr<const SclReferenceTraceSelection> selection;
    std::map<int, Entry> entries;
    QList<int> sourceSplitterSizes;
    int candidate = -1, selected = Root, nextId = 0;
    Scl::NodeId evidence = Scl::NoNode;
    bool tree = false;
};

SclReferenceTraceView::SclReferenceTraceView(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclReferenceTrace");
    setMinimumSize(0, 0);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* tools = new QHBoxLayout();
    auto button = [this](QHBoxLayout* row, const QString& text, const char* id) {
        auto* action = new QPushButton(text, this);
        action->setObjectName(id);
        row->addWidget(action);
        return action;
    };
    auto* back = button(tools, QStringLiteral("返回来源"), "SclTraceBack");
    m_treeMode = button(tools, QStringLiteral("证据树"), "SclTraceSwitch");
    m_treeMode->setCheckable(true);
    m_fit = button(tools, QStringLiteral("适应视图"), "SclTraceFit");
    auto* first = button(tools, QStringLiteral("仅看首层"), "SclTraceFirstLayer");
    tools->addStretch();
    layout->addLayout(tools);

    m_title = new QLabel(this);
    m_title->setTextFormat(Qt::PlainText);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_title->setObjectName("SclTraceTitle");
    m_title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_title);
    m_choices = new QComboBox(this);
    m_choices->setObjectName("SclTraceChoices");
    m_choices->setAccessibleName(QStringLiteral("引用值和声明来源"));
    m_choices->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    layout->addWidget(m_choices);

    auto* split = m_split = new QSplitter(Qt::Vertical, this);
    split->setChildrenCollapsible(false);
    m_views = new QStackedWidget(split);
    m_views->setMinimumSize(0, 0);
    m_views->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    auto* canvas = new TraceCanvas(m_views);
    canvas->setObjectName("SclTraceGraph");
    m_graph = canvas;
    m_scene = new QGraphicsScene(m_graph);
    m_graph->setScene(m_scene);
    m_views->addWidget(m_graph);
    m_tree = new QTreeWidget(m_views);
    m_tree->setObjectName("SclTraceTree");
    m_tree->setAccessibleName(QStringLiteral("引用证据树"));
    m_tree->setHeaderLabels({QStringLiteral("组成或证据"), QStringLiteral("值"), QStringLiteral("关系")});
    m_tree->setColumnWidth(0, 146);
    m_tree->setMinimumSize(0, 0);
    m_tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_views->addWidget(m_tree);

    m_detail = new QPlainTextEdit(split);
    m_detail->setObjectName("SclTraceDetail");
    m_detail->setAccessibleName(QStringLiteral("所选节点的值、规则和原文依据"));
    m_detail->setReadOnly(true);
    m_detail->setMinimumSize(0, 0);
    m_detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    layout->addWidget(split, 1);

    auto* actions = new QHBoxLayout();
    m_expand = button(actions, QStringLiteral("展开所选"), "SclTraceExpand");
    m_source = button(actions, QStringLiteral("查看原文"), "SclTraceSource");
    m_status = new QLabel(this);
    m_status->setObjectName("SclTraceStatus");
    m_status->setWordWrap(true);
    m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    actions->addWidget(m_status, 1);
    layout->addLayout(actions);

    connect(back, &QPushButton::clicked, this, &SclReferenceTraceView::ReturnToSource);
    connect(m_treeMode, &QPushButton::toggled, this, [this](bool tree) {
        if (m_location) m_location->tree = tree;
        m_views->setCurrentIndex(tree ? 1 : 0);
        m_fit->setEnabled(!tree);
        if (tree) {
            if (auto* current = m_tree->currentItem()) m_tree->scrollToItem(current);
            m_tree->setFocus();
        }
    });
    connect(m_fit, &QPushButton::clicked, this, &SclReferenceTraceView::Fit);
    connect(first, &QPushButton::clicked, this, &SclReferenceTraceView::FirstLayer);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem* item) {
        if (item) Select(item->data(0, Qt::UserRole).toInt());
    });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        if (item) Toggle(item->data(0, Qt::UserRole).toInt());
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        if (!m_location) return;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto entry = m_location->entries.find(id);
        if (entry != m_location->entries.end() && !entry->second.expanded) Queue(id);
    });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem* item) {
        if (!m_location) return;
        const int id = item->data(0, Qt::UserRole).toInt();
        const auto entry = m_location->entries.find(id);
        if (entry != m_location->entries.end() && entry->second.expanded)
            Toggle(id);
        else if ((m_running && m_running->id == id) || (m_pending && m_pending->id == id))
            Cancel();
    });
    canvas->activate = [this](int id, bool expand) {
        Select(id);
        if (expand) Toggle(id);
    };
    connect(m_expand, &QPushButton::clicked, this, [this] { if (m_location) Toggle(m_location->selected); });
    connect(m_source, &QPushButton::clicked, this, [this] {
        if (!m_location) return;
        const auto item = m_location->entries.find(m_location->selected);
        if (item != m_location->entries.end() && item->second.node.location.line > 0) {
            m_location->evidence = item->second.node.source;
            emit OpenEvidence(item->second.node.source);
        }
    });
    connect(m_choices, &QComboBox::currentIndexChanged, this, [this](int item) {
        if (!m_location || !m_choices->isVisible()) return;
        Cancel();
        m_location->candidate = item - 1;
        m_location->selected = Root;
        m_location->entries.clear();
        if (m_location->evidence != Scl::NoNode) {
            m_location->evidence = Scl::NoNode;
            emit OpenEvidence(Scl::NoNode);
        }
        Refresh();
        if (m_location->candidate >= 0) Queue(Root);
    });
    m_worker = new QFutureWatcher<SclReferenceTrace>(this);
    connect(m_worker, &QFutureWatcherBase::finished, this, &SclReferenceTraceView::Receive);
    Refresh();
}

SclReferenceTraceView::~SclReferenceTraceView()
{
    Cancel();
}

void SclReferenceTraceView::Open(std::shared_ptr<const SclReferenceTraceSelection> selection, QList<int> sourceSplitterSizes)
{
    Cancel();
    m_location = std::make_shared<SclReferenceTraceLocation>();
    m_location->selection = std::move(selection);
    m_location->sourceSplitterSizes = std::move(sourceSplitterSizes);
    m_location->candidate = m_location->selection->values.size() == 1 ? 0 : -1;
    const auto available = parentWidget()->contentsRect().size();
    m_location->tree = available.width() < 480 || available.height() < 270;
    PopulateChooser();
    Refresh();
    if (m_location->candidate >= 0 && isVisible()) Queue(Root);
}

bool SclReferenceTraceView::Restore(std::shared_ptr<SclReferenceTraceLocation> location,
    const std::shared_ptr<const SclSnapshot>& snapshot)
{
    if (!location || !snapshot || location->selection->snapshot != snapshot) return false;
    Cancel();
    m_location = std::move(location);
    PopulateChooser();
    Refresh();
    if (isVisible() && m_location->candidate >= 0 && m_location->entries.empty()) Queue(Root);
    return true;
}

void SclReferenceTraceView::Clear()
{
    Cancel();
    m_location.reset();
    PopulateChooser();
    Refresh();
}

QAbstractItemView* SclReferenceTraceView::SourceView() const
{
    return m_location ? m_location->selection->sourceView.data() : nullptr;
}

Scl::NodeId SclReferenceTraceView::OpenedEvidence() const
{
    return m_location ? m_location->evidence : Scl::NoNode;
}

QList<int> SclReferenceTraceView::SourceSplitterSizes() const
{
    return m_location ? m_location->sourceSplitterSizes : QList<int>{};
}

void SclReferenceTraceView::hideEvent(QHideEvent* event)
{
    Cancel();
    QWidget::hideEvent(event);
}

void SclReferenceTraceView::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    const auto available = m_split->height();
    m_split->setSizes({available * 3 / 5, available * 2 / 5});
    if (m_location && m_location->entries.empty() && m_location->candidate >= 0) Queue(Root);
}

void SclReferenceTraceView::Cancel()
{
    m_pending.reset();
    if (m_running) m_running->control->cancelled = true;
}

void SclReferenceTraceView::Queue(int id)
{
    if (!m_location || m_location->candidate < 0 || !isVisible()) return;
    if (id != Root && m_location->entries.size() >= SclReferenceTrace::VisibleLimit) {
        m_status->setText(QStringLiteral("已到显示上限。折叠其他分支后重试。"));
        return;
    }
    if (m_running && m_running->location == m_location && m_running->candidate == m_location->candidate
        && m_running->id == id && !m_running->control->cancelled) return;
    m_pending = Job{id, m_location->candidate, m_location, std::make_shared<Scl::TaskControl>()};
    StartPending();
}

void SclReferenceTraceView::StartPending()
{
    if (!m_pending || m_running) return;
    m_running = std::move(m_pending);
    m_pending.reset();
    const auto& job = *m_running;
    const auto selection = job.location->selection;
    SclTraceNode requested;
    if (job.id == Root) {
        requested.branch = SclTraceBranch::Root;
    } else {
        const auto entry = job.location->entries.find(job.id);
        if (entry == job.location->entries.end() || entry->second.expanded) {
            m_running.reset();
            return;
        }
        requested = entry->second.node;
    }
    m_status->setText(QStringLiteral("正在查找依据…"));
    m_worker->setFuture(QtConcurrent::run([snapshot = selection->snapshot, value = selection->values[job.candidate],
        requested, control = job.control] {
        try { return TraceSclReference(*snapshot, value, requested, control.get()); }
        catch (const Scl::TaskCancelled&) { return SclReferenceTrace{}; }
    }));
}

void SclReferenceTraceView::Receive()
{
    const auto job = std::move(*m_running);
    m_running.reset();
    if (!job.control->cancelled && isVisible() && m_location == job.location && job.candidate == m_location->candidate) {
        auto result = m_worker->result();
        auto& entries = m_location->entries;
        if (job.id == Root && entries.empty())
            entries.emplace(Root, SclReferenceTraceLocation::Entry{std::move(result.node), NoSelection});
        const auto parent = entries.find(job.id);
        if (parent != entries.end()) {
            parent->second.expanded = true;
            for (auto& child : result.children) {
                if (entries.size() >= SclReferenceTrace::VisibleLimit) {
                    result.limited = true;
                    break;
                }
                for (auto ancestor = parent; ancestor != entries.end() && child.branch != SclTraceBranch::None;
                    ancestor = entries.find(ancestor->second.parent)) {
                    if (ancestor->second.node.source == child.source && ancestor->second.node.branch == child.branch) {
                        child.branch = SclTraceBranch::None;
                        child.explanation += QStringLiteral("\n本分支已访问该定义，停止继续展开。");
                    }
                }
                entries.emplace(m_location->nextId++, SclReferenceTraceLocation::Entry{std::move(child), job.id});
            }
            parent->second.limited = result.limited;
            Refresh();
            if (job.id == Root) Fit();
        }
    }
    StartPending();
}

void SclReferenceTraceView::PopulateChooser()
{
    const QSignalBlocker blocked(m_choices);
    m_choices->clear();
    const bool multiple = m_location && m_location->selection->values.size() > 1;
    m_choices->setVisible(multiple);
    if (!m_location) return;
    const QSignalBlocker switchBlocked(m_treeMode);
    m_treeMode->setChecked(m_location->tree);
    m_views->setCurrentIndex(m_location->tree ? 1 : 0);
    m_fit->setEnabled(!m_location->tree);
    if (!multiple) return;
    m_choices->addItem(QStringLiteral("选择要追溯的值或声明"));
    const auto& doc = *m_location->selection->snapshot->analysis.document;
    for (const auto& value : m_location->selection->values) {
        auto display = value.value;
        if (value.basis) {
            for (const auto node : {value.basis->scope.ied, value.basis->scope.accessPoint}) {
                if (node < 0 || node >= int(doc.nodes.size())) continue;
                const auto name = doc.attribute(node, QStringLiteral("name"));
                if (!name.isEmpty()) display += QStringLiteral(" · ") + name;
            }
        }
        const auto id = value.evidence.node;
        if (id >= 0 && id < int(doc.nodes.size())) {
            const auto& source = doc.nodes[id];
            const auto name = doc.attribute(id, QStringLiteral("name"));
            display += QStringLiteral(" · %1 %2").arg(source.name, name);
            if (source.source.line > 0) display += QStringLiteral(" · 第 %1 行").arg(source.source.line);
        }
        m_choices->addItem(display);
        m_choices->setItemData(m_choices->count() - 1, display, Qt::ToolTipRole);
    }
    m_choices->setCurrentIndex(m_location->candidate + 1);
}

void SclReferenceTraceView::Select(int id)
{
    if (!m_location || !m_location->entries.count(id)) return;
    m_location->selected = id;
    const QSignalBlocker blocked(m_tree);
    for (QTreeWidgetItemIterator it(m_tree); *it; ++it) {
        if ((*it)->data(0, Qt::UserRole).toInt() != id) continue;
        m_tree->setCurrentItem(*it);
        if (m_views->currentWidget() == m_tree) m_tree->scrollToItem(*it);
        break;
    }
    Draw();
    RefreshDetail();
}

void SclReferenceTraceView::Toggle(int id)
{
    if (!m_location || id == Root) return;
    auto& entries = m_location->entries;
    const auto entry = entries.find(id);
    if (entry == entries.end() || entry->second.node.branch == SclTraceBranch::None) return;
    if (!entry->second.expanded) {
        Queue(id);
        return;
    }
    Cancel();
    QSet<int> removed{id};
    for (auto it = std::next(entry); it != entries.end();) {
        if (removed.contains(it->second.parent)) {
            removed.insert(it->first);
            it = entries.erase(it);
        } else ++it;
    }
    if (removed.contains(m_location->selected)) m_location->selected = id;
    entry->second.expanded = false;
    entry->second.limited = false;
    Refresh();
}

void SclReferenceTraceView::FirstLayer()
{
    if (!m_location) return;
    Cancel();
    auto& entries = m_location->entries;
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->first != Root && it->second.parent != Root) it = entries.erase(it);
        else {
            it->second.expanded = it->first == Root;
            if (it->first != Root) it->second.limited = false;
            ++it;
        }
    }
    m_location->selected = Root;
    Refresh();
    Fit();
    if (entries.empty() && m_location->candidate >= 0) Queue(Root);
}

void SclReferenceTraceView::Refresh()
{
    const QSignalBlocker blocked(m_tree);
    m_tree->clear();
    m_title->clear();
    m_status->clear();
    if (m_location) {
        const auto& selection = *m_location->selection;
        const auto kind = selection.values.front().kind;
        if (m_location->candidate < 0) {
            m_title->setText(SclReferenceLabel(kind));
            m_status->setText(selection.limited
                ? QStringLiteral("来源列表受限。请选择一个可查阅的来源。")
                : QStringLiteral("请选择一个引用值或原始声明。"));
        } else {
            const auto& value = selection.values[m_location->candidate];
            m_title->setText(SclReferenceLabel(kind) + QStringLiteral(" · ") + value.value);
            if (selection.limited || std::any_of(m_location->entries.begin(), m_location->entries.end(),
                [](const auto& item) { return item.second.limited; }))
                m_status->setText(QStringLiteral("部分证据达到交付上限。可折叠展开项再查看其他分支。"));
        }
        QHash<int, QTreeWidgetItem*> items;
        for (const auto& [id, entry] : m_location->entries) {
            auto* parent = items.value(entry.parent);
            auto* item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
            item->setText(0, entry.node.label);
            item->setText(1, entry.node.value);
            item->setText(2, entry.node.relation);
            item->setToolTip(1, entry.node.value.toHtmlEscaped());
            item->setData(0, Qt::UserRole, id);
            items.insert(id, item);
            if (id != Root && entry.node.branch != SclTraceBranch::None)
                item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        }
        for (const auto& [id, entry] : m_location->entries)
            items[id]->setExpanded(entry.expanded);
        if (auto* item = items.value(m_location->selected)) m_tree->setCurrentItem(item);
    }
    m_title->setToolTip(m_title->text().toHtmlEscaped());
    Draw();
    RefreshDetail();
}

void SclReferenceTraceView::Draw()
{
    m_scene->clear();
    if (!m_location || m_location->entries.empty()) return;
    const auto& entries = m_location->entries;
    QMultiMap<int, int> children;
    for (const auto& [id, entry] : entries)
        if (id != Root) children.insert(entry.parent, id);
    QHash<int, QPointF> positions;
    qreal row = 0;
    const auto position = [&](auto&& visit, int id, int depth) -> qreal {
        auto links = children.values(id);
        std::reverse(links.begin(), links.end());
        const auto start = row;
        if (links.isEmpty()) row += 88;
        else for (const auto child : links) visit(visit, child, depth + 1);
        const auto top = (start + row - 88) / 2;
        positions.insert(id, QPointF(depth * 316, top));
        return top;
    };
    position(position, Root, 0);
    for (const auto& [id, entry] : entries) {
        if (id == Root) continue;
        const auto start = positions.value(entry.parent) + QPointF(224, 29);
        const auto finish = positions.value(id) + QPointF(0, 29);
        QPainterPath path(start);
        const auto mid = (start.x() + finish.x()) / 2;
        path.cubicTo(mid, start.y(), mid, finish.y(), finish.x(), finish.y());
        m_scene->addPath(path, QPen(palette().color(QPalette::Mid), 1.5));
        auto* label = m_scene->addSimpleText(entry.node.relation);
        label->setBrush(palette().color(QPalette::Text));
        label->setPos(finish.x() - 88, finish.y() - 18);
    }
    for (const auto& [id, entry] : entries) {
        const auto at = positions.value(id);
        const bool selected = m_location->selected == id;
        const auto background = palette().color(selected ? QPalette::Highlight : QPalette::Base);
        const auto foreground = palette().color(selected ? QPalette::HighlightedText : QPalette::Text);
        auto* card = m_scene->addRect(QRectF(at, QSizeF(224, 58)), QPen(palette().color(QPalette::Mid)), QBrush(background));
        card->setData(0, id);
        card->setToolTip((entry.node.label + '\n' + entry.node.value).toHtmlEscaped());
        auto addText = [&](const QString& value, const QPointF& offset, int width) {
            auto* text = new QGraphicsSimpleTextItem(fontMetrics().elidedText(value, Qt::ElideRight, width), card);
            text->setBrush(foreground);
            text->setPos(at + offset);
            return text;
        };
        addText(entry.node.label, {8, 5}, 180);
        addText(entry.node.value, {8, 29}, 205);
        if (id != Root && entry.node.branch != SclTraceBranch::None) {
            auto* control = addText(entry.expanded ? QStringLiteral("−") : QStringLiteral("+"), {199, 5}, 22);
            control->setData(0, id);
            control->setData(1, true);
            control->setToolTip(entry.expanded ? QStringLiteral("折叠依据") : QStringLiteral("展开依据"));
        }
    }
    m_scene->setSceneRect(m_scene->itemsBoundingRect().adjusted(-20, -20, 20, 20));
}

void SclReferenceTraceView::Fit()
{
    if (!m_scene->items().isEmpty()) m_graph->fitInView(m_scene->sceneRect(), Qt::KeepAspectRatio);
}

void SclReferenceTraceView::RefreshDetail()
{
    m_expand->setEnabled(false);
    m_source->setEnabled(false);
    m_detail->clear();
    if (!m_location) return;
    const auto it = m_location->entries.find(m_location->selected);
    if (it == m_location->entries.end()) return;
    const auto& node = it->second.node;
    QStringList details{node.label};
    if (!node.value.isEmpty()) details << node.value;
    if (!node.explanation.isEmpty()) details << node.explanation;
    if (!node.path.isEmpty()) details << node.path;
    if (!node.attribute.isEmpty()) details << QStringLiteral("属性 @%1").arg(node.attribute);
    if (node.location.line > 0) {
        details << QStringLiteral("标签起始行 %1").arg(node.location.line) << node.excerpt;
        m_source->setEnabled(true);
    } else if (node.source != Scl::NoNode) {
        details << QStringLiteral("该证据没有可用的源码位置。");
    }
    m_detail->setPlainText(details.join('\n'));
    m_expand->setEnabled(it->first != Root && node.branch != SclTraceBranch::None);
    m_expand->setText(it->second.expanded ? QStringLiteral("折叠所选") : QStringLiteral("展开所选"));
}
}
