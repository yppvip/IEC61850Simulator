#include "SclTaskBrowser.hpp"
#include "SclTableInteraction.hpp"

#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QScrollBar>
#include <QTableView>
#include <QtConcurrentRun>
#include <algorithm>
#include <utility>

namespace iec61850sim
{
namespace
{
QStringList Headers(SclTaskKind kind)
{
    switch (kind)
    {
    case SclTaskKind::Signal:
        return {QStringLiteral("对象名称"), QStringLiteral("对象描述（来源）"), QStringLiteral("对象引用"),
                QStringLiteral("类别"), QStringLiteral("CDC"), QStringLiteral("状态")};
    case SclTaskKind::Input:
    case SclTaskKind::GooseInput:
    case SclTaskKind::SvInput:
        return {QStringLiteral("内部地址"), QStringLiteral("接收证据"), QStringLiteral("声明来源"),
                QStringLiteral("serviceType"), QStringLiteral("接收装置"), QStringLiteral("APPID / 通信端点（候选单列）")};
    case SclTaskKind::Goose:
    case SclTaskKind::Sv:
        return {QStringLiteral("控制块"), QStringLiteral("发送装置 / 路径"),
                kind == SclTaskKind::Goose ? QStringLiteral("appID") : QStringLiteral("smvID"),
                QStringLiteral("数据集"), QStringLiteral("成员数"), QStringLiteral("端点数"), QStringLiteral("明确接收声明"),
                QStringLiteral("APPID / MAC / VLAN")};
    case SclTaskKind::Finding:
        return {QStringLiteral("规则"), QStringLiteral("发现"), QStringLiteral("对象"), QStringLiteral("原文行")};
    }
    return {};
}
}

class SclTaskTableModel final : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    void Reset(std::shared_ptr<const SclSnapshot> snapshot, SclTaskKind kind, std::vector<SclTaskRow> rows = {})
    {
        beginResetModel();
        m_snapshot = std::move(snapshot);
        m_rows = std::move(rows);
        m_headers = Headers(kind);
        m_cachedRow = -1;
        m_cells.clear();
        endResetModel();
    }

    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : int(m_rows.size()); }
    int columnCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_headers.size(); }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override
    {
        return orientation == Qt::Horizontal && role == Qt::DisplayRole ? m_headers.value(section) : QVariant{};
    }
    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!m_snapshot || !index.isValid() || (role != Qt::DisplayRole && role != Qt::ToolTipRole
            && role != SclTableInteraction::FullValueRole))
            return {};
        if (m_cachedRow != index.row())
        {
            m_cachedRow = index.row();
            const auto row = m_rows[index.row()];
            m_cells = SclTaskCells(*m_snapshot, row, true);
            m_descriptionLabel.clear();
            if (row.kind == SclTaskKind::Signal)
            {
                const auto& tasks = m_snapshot->tasks;
                const auto& definition = tasks.catalog.definitions[tasks.catalog.instances[row.index].definition];
                const auto source = tasks.signalDescriptionNodes[row.index];
                if (source != Scl::NoNode)
                    m_descriptionLabel = m_snapshot->analysis.document->nodes[source].name == "Val"
                        ? QStringLiteral("实例 dU") : source == definition.declaration
                            ? QStringLiteral("模板") : QStringLiteral("实例 desc");
                m_cells = {definition.path, source == Scl::NoNode ? QString{} : m_cells[0], m_cells[2],
                    m_cells[1], m_cells[3], m_cells[4].isEmpty() ? QStringLiteral("类型已解析") : m_cells[4]};
            }
        }
        auto cell = m_cells.value(index.column());
        if (role == SclTableInteraction::FullValueRole)
            return cell;
        if (m_rows[index.row()].kind == SclTaskKind::Signal && cell.size() > SclTableInteraction::PreviewLimit)
            cell = tr("完整值 %1 字符（右键查看）").arg(cell.size());
        else if (index.column() == 1 && m_rows[index.row()].kind == SclTaskKind::Signal)
            cell = m_descriptionLabel.isEmpty() ? QStringLiteral("未声明描述")
                : cell + QStringLiteral("（%1）").arg(m_descriptionLabel);
        return role == Qt::ToolTipRole ? cell.toHtmlEscaped() : cell;
    }

    std::optional<SclTaskRow> Row(const QModelIndex& index) const
    {
        return index.isValid() && index.row() < int(m_rows.size()) ? std::optional(m_rows[index.row()]) : std::nullopt;
    }

    QModelIndex Locate(SclTaskRow row) const
    {
        const auto found = std::lower_bound(m_rows.begin(), m_rows.end(), row.index,
            [](const auto& candidate, int id) { return candidate.index < id; });
        return found != m_rows.end() && found->kind == row.kind && found->index == row.index
            ? index(int(found - m_rows.begin()), 0) : QModelIndex{};
    }

private:
    std::shared_ptr<const SclSnapshot> m_snapshot;
    QStringList m_headers;
    std::vector<SclTaskRow> m_rows;
    mutable int m_cachedRow = -1;
    mutable QStringList m_cells;
    mutable QString m_descriptionLabel;
};

SclTaskBrowser::SclTaskBrowser(QWidget* parent)
    : QObject(parent), m_view(new SclTaskPages(parent)), m_model(new SclTaskTableModel(this))
{
    m_view->SetModel(m_model);
    m_delay.setInterval(160);
    m_delay.setSingleShot(true);
    connect(&m_delay, &QTimer::timeout, this, &SclTaskBrowser::RunFilter);
    connect(m_view, &SclTaskPages::ScopeChanged, this, &SclTaskBrowser::ScopeChanged);
    connect(m_view, &SclTaskPages::PageChanged, this, [this](SclTaskPages::Page page) {
        m_page = page;
        m_signal.reset();
        Schedule();
        emit SignalPageChanged();
    });
    connect(m_view, &SclTaskPages::DirectionChanged, this, [this](bool inputs) {
        m_inputs = inputs;
        m_signal.reset();
        Schedule();
        emit SignalPageChanged();
    });
    connect(m_view, &SclTaskPages::FilterChanged, this, [this](const QString& text) {
        m_filter = text.trimmed();
        Schedule();
    });
    connect(m_view, &SclTaskPages::Selected, this, &SclTaskBrowser::SelectRow);
    connect(m_view->ConnectionsView(), &SclConnectionView::ScopeChanged, this, &SclTaskBrowser::ScopeChanged);
    connect(m_view->ConnectionsView(), &SclConnectionView::OpenEvidence, this, [this](int node) {
        emit OpenEvidence(node, -1);
    });
    connect(m_view->PublicationView(), &SclConnectionView::OpenEvidence, this, [this](int node) {
        emit OpenEvidence(node, -1);
    });
    connect(m_view, &SclTaskPages::OpenSource, this, [this] {
        if (IsSignalPage())
        {
            OpenSignalEvidence(0);
            return;
        }
        const int item = m_view->EvidenceIndex();
        if (item < 0 || item >= int(m_detail.evidence.size()))
            return;
        const auto row = m_model->Row(m_view->Table()->currentIndex());
        emit OpenEvidence(m_detail.evidence[item].node,
            row && row->kind == SclTaskKind::Finding ? row->index : -1);
    });
}

SclTaskBrowser::~SclTaskBrowser()
{
    Cancel();
}

void SclTaskBrowser::Cancel()
{
    m_delay.stop();
    if (m_query)
        m_query->cancelled = true;
    if (m_selection)
        m_selection->cancelled = true;
    m_query.reset();
    m_selection.reset();
}

void SclTaskBrowser::SetSnapshot(std::shared_ptr<const SclSnapshot> snapshot)
{
    Cancel();
    m_snapshot = std::move(snapshot);
    m_signal.reset();
    m_ied = Scl::NoNode;
    m_filter.clear();
    m_inputs = false;
    m_restore.reset();
    QList<QPair<int, QString>> options{{Scl::NoNode, QStringLiteral("整个工程")}};
    if (m_snapshot)
    {
        const auto& doc = *m_snapshot->analysis.document;
        for (auto ied : m_snapshot->browse.ieds)
        {
            auto label = doc.attribute(ied, QStringLiteral("name")).left(256);
            const auto desc = doc.attribute(ied, QStringLiteral("desc")).left(128);
            if (!desc.isEmpty())
                label += QStringLiteral(" · ") + desc;
            options.push_back({ied, label});
        }
        const auto& analysis = m_snapshot->analysis;
        QStringList rules;
        rules << QStringLiteral("规则集 %1 · SHA-256 %2").arg(analysis.ruleSetVersion, QString::fromLatin1(analysis.sha256));
        for (const auto& coverage : analysis.coverage)
        {
            QString state;
            switch (coverage.state)
            {
            case Scl::Coverage::Executed: state = QStringLiteral("已执行"); break;
            case Scl::Coverage::NotApplicable: state = QStringLiteral("不适用"); break;
            case Scl::Coverage::Blocked: state = QStringLiteral("受前置检查阻断"); break;
            case Scl::Coverage::NotImplemented: state = QStringLiteral("未执行"); break;
            }
            rules << QStringLiteral("%1 · %2 · 检查 %3 · 覆盖受限 %4")
                .arg(coverage.id, state).arg(coverage.evaluated).arg(coverage.limited);
        }
        rules << QStringLiteral("工程点表完整性 · 未执行（需要外部基准）");
        m_view->SetCoverage(QStringLiteral("XML 已读取 · 内部一致性按规则集 %1 检查\n"
            "全工程 %2 条发现 · 完整 XSD 未执行 · 工程点表完整性未执行")
            .arg(analysis.ruleSetVersion).arg(analysis.findings.size()), rules.join('\n'));
    }
    else
        m_view->SetCoverage(QStringLiteral("尚无可检查的快照。"), {});
    m_view->SetScopeOptions(options, m_ied);
    m_view->SetFilterText({});
    m_view->SetInputDirection(false);
    m_view->SetPage(SclTaskPages::Overview);
    m_view->ConnectionsView()->SetContext(m_snapshot, m_ied);
    m_view->PublicationView()->SetContext(m_snapshot, m_ied);
    UpdateOverview();
    Schedule();
}

void SclTaskBrowser::SetScope(Scl::NodeId ied)
{
    if (m_ied == ied)
        return;
    m_ied = ied;
    m_signal.reset();
    m_view->SetScope(ied);
    m_view->ConnectionsView()->SetContext(m_snapshot, m_ied);
    m_view->PublicationView()->SetContext(m_snapshot, m_ied);
    UpdateOverview();
    Schedule();
}

SclTaskBrowser::Location SclTaskBrowser::CurrentLocation() const
{
    auto* table = m_view->Table();
    const int top = table->rowAt(0);
    return {m_page, m_filter, m_inputs, IsSignalPage() ? m_signal : m_model->Row(table->currentIndex()),
        table->verticalScrollBar()->value(), m_view->ConnectionsView()->CurrentLocation(),
        m_model->Row(m_model->index(top, 0)), top < 0 ? 0 : -table->rowViewportPosition(top),
        m_view->PublicationView()->CurrentLocation()};
}

void SclTaskBrowser::RestoreLocation(const Location& location)
{
    m_filter = location.filter;
    m_inputs = location.inputs;
    m_view->SetFilterText(m_filter);
    m_view->SetInputDirection(m_inputs);
    m_view->SetPage(location.page);
    Schedule();
    m_restore = location;
    m_signal = IsSignalPage() ? location.row : std::nullopt;
    PresentSignal(m_signal ? QStringLiteral("正在恢复信号…") : QString{});
    emit SignalPageChanged();
    if (location.page == SclTaskPages::Connections)
        m_view->ConnectionsView()->RestoreLocation(location.connection);
}

bool SclTaskBrowser::IsSignalPage() const
{
    return m_page == SclTaskPages::Signals && !m_inputs;
}

bool SclTaskBrowser::IsPublicationPage() const
{
    return !m_inputs && (m_page == SclTaskPages::Goose || m_page == SclTaskPages::SampledValues);
}

QString SclTaskBrowser::SignalPath() const
{
    return m_snapshot && IsSignalPage() && m_signal ? SclTaskCells(*m_snapshot, *m_signal, true).value(2) : QString{};
}

void SclTaskBrowser::LocateSignal()
{
    if (!IsSignalPage() || !m_signal)
        return;
    const auto index = m_model->Locate(*m_signal);
    if (index.isValid())
    {
        m_view->Table()->setCurrentIndex(index);
        m_view->Table()->scrollTo(index, QAbstractItemView::PositionAtCenter);
        m_view->Table()->setFocus();
    }
    else if (!m_filter.isEmpty())
    {
        m_filter.clear();
        m_view->SetFilterText({});
        Schedule();
    }
}

void SclTaskBrowser::OpenSignalEvidence(int index)
{
    if (IsSignalPage() && m_signal && index >= 0 && index < int(m_detail.evidence.size()))
    {
        const auto& evidence = m_detail.evidence[index];
        emit OpenSignalSource(evidence.node, evidence.label);
    }
}

void SclTaskBrowser::PresentSignal(const QString& message)
{
    if (!IsSignalPage())
        return;
    auto html = message.toHtmlEscaped();
    if (html.isEmpty() && m_signal)
    {
        const auto field = [](const QString& text) { return text.toHtmlEscaped().replace('\n', QStringLiteral("<br>")); };
        const auto link = [](int index) { return QStringLiteral(" <a href='%1'>原文</a>").arg(index); };
        html += QStringLiteral("<h4>对象信息</h4>");
        for (const auto& group : m_detail.descriptions)
        {
            if (!group.label.isEmpty())
                html += QStringLiteral("<b>%1</b><br>").arg(field(group.label));
            for (const auto& item : group.projection.fields)
            {
                const auto label = group.label + SclDescriptionLabel(item.source);
                const auto value = SclDescriptionText(item);
                html += field(SclDescriptionLabel(item.source) + QStringLiteral("  ")
                    + (value.size() > 4096 ? QStringLiteral("完整值 %1 字符，请查看原文").arg(value.size()) : value));
                const auto found = std::find_if(m_detail.evidence.cbegin(), m_detail.evidence.cend(),
                    [&](const auto& entry) { return entry.node == item.node && entry.label == label; });
                if (found != m_detail.evidence.cend())
                    html += link(int(found - m_detail.evidence.cbegin()));
                html += QStringLiteral("<br>");
            }
        }
        html += QStringLiteral("<h4>技术字段</h4>") + field(m_detail.signalFields.join('\n').left(12000));
        html += QStringLiteral("<h4>通信使用</h4>") + field(m_detail.signalUsage.join('\n').left(12000));
        if (!m_detail.evidence.empty())
        {
            html += QStringLiteral("<h4>文件依据</h4>");
            for (int i = 0; i < int(m_detail.evidence.size()); ++i)
                html += field(m_detail.evidence[i].label) + link(i) + QStringLiteral("<br>");
        }
    }
    emit SignalDetailChanged(SignalPath(), html);
}

SclTaskKind SclTaskBrowser::Kind() const
{
    switch (m_page)
    {
    case SclTaskPages::Goose: return m_inputs ? SclTaskKind::GooseInput : SclTaskKind::Goose;
    case SclTaskPages::SampledValues: return m_inputs ? SclTaskKind::SvInput : SclTaskKind::Sv;
    case SclTaskPages::Checks: return SclTaskKind::Finding;
    default: return m_inputs ? SclTaskKind::Input : SclTaskKind::Signal;
    }
}

void SclTaskBrowser::Schedule()
{
    Cancel();
    m_restore.reset();
    m_view->PublicationView()->SetPublication(Scl::NoNode);
    m_detail = {};
    m_model->Reset({}, Kind());
    m_view->SetEvidence({});
    m_view->SetDetail({}, false);
    PresentSignal(m_signal ? QStringLiteral("正在检查筛选范围…") : QString{});
    if (!m_snapshot || m_page == SclTaskPages::Overview || m_page == SclTaskPages::Connections)
        return;
    m_view->SetStatus(QStringLiteral("正在准备当前范围…"));
    m_delay.start();
}

void SclTaskBrowser::RunFilter()
{
    const auto snapshot = m_snapshot;
    const auto kind = Kind();
    const auto ied = m_ied;
    const auto filter = m_filter;
    const auto task = m_query = std::make_shared<Scl::TaskControl>();
    auto* watcher = new QFutureWatcher<std::optional<std::vector<SclTaskRow>>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, snapshot, kind, task] {
        watcher->deleteLater();
        if (m_query != task || m_snapshot != snapshot)
            return;
        auto rows = watcher->future().takeResult();
        if (!rows)
        {
            m_view->SetStatus(QStringLiteral("无法完成当前范围查询。"));
            PresentSignal(QStringLiteral("无法完成当前范围查询。"));
            return;
        }
        const auto count = rows->size();
        m_model->Reset(snapshot, kind, std::move(*rows));
        auto* header = m_view->Table()->horizontalHeader();
        header->setSectionResizeMode(QHeaderView::Interactive);
        const bool signalOrInput = kind == SclTaskKind::Signal || kind == SclTaskKind::Input
            || kind == SclTaskKind::GooseInput || kind == SclTaskKind::SvInput;
        const int flexible = kind == SclTaskKind::Signal ? 1 : signalOrInput ? 2 : 1;
        header->setSectionResizeMode(flexible, QHeaderView::Stretch);
        if (kind == SclTaskKind::Signal)
        {
            header->resizeSection(0, 105);
            header->resizeSection(2, 235);
            header->resizeSection(3, 120);
            header->resizeSection(4, 60);
            header->resizeSection(5, 90);
        }
        m_view->SetStatus(count ? QStringLiteral("%1 条").arg(count)
            : m_filter.isEmpty() ? (kind == SclTaskKind::Finding
                ? QStringLiteral("当前范围没有已执行规则的发现。")
                : QStringLiteral("当前范围没有可展示的条目。"))
                : QStringLiteral("筛选结果为空，可清除筛选。"));
        if (!m_restore && IsSignalPage() && m_signal)
        {
            const auto index = m_model->Locate(*m_signal);
            if (index.isValid())
                m_view->Table()->setCurrentIndex(index);
            else
                PresentSignal(QStringLiteral("所选信号被当前筛选遮挡。可清除筛选并定位。"));
        }
        if (m_restore)
        {
            const auto location = std::exchange(m_restore, std::nullopt);
            if (location->row)
            {
                const auto index = m_model->Locate(*location->row);
                if (index.isValid())
                {
                    m_view->Table()->setCurrentIndex(index);
                    if (location->publication.control != Scl::NoNode
                        && m_view->PublicationView()->CurrentLocation().control == location->publication.control)
                        m_view->PublicationView()->SetPublication(location->publication.control, location->publication);
                }
                else
                {
                    m_view->SetStatus(QStringLiteral("当前范围或筛选条件遮挡了原条目。"));
                    PresentSignal(QStringLiteral("所选信号被当前筛选遮挡。可清除筛选并定位。"));
                }
            }
            auto* table = m_view->Table();
            const auto top = location->top ? m_model->Locate(*location->top) : QModelIndex{};
            if (kind == SclTaskKind::Signal && top.isValid())
            {
                table->scrollTo(top, QAbstractItemView::PositionAtTop);
                table->verticalScrollBar()->setValue(table->verticalScrollBar()->value()
                    + qMin(location->topOffset, table->rowHeight(top.row()) - 1));
            }
            else
                table->verticalScrollBar()->setValue(location->scroll);
        }
    });
    watcher->setFuture(QtConcurrent::run([snapshot, kind, ied, filter, task]
        () -> std::optional<std::vector<SclTaskRow>> {
        try { return FilterSclTaskRows(*snapshot, kind, ied, filter, task.get()); }
        catch (const Scl::TaskCancelled&) { return std::nullopt; }
        catch (const std::exception&) { return std::nullopt; }
    }));
}

void SclTaskBrowser::SelectRow(const QModelIndex& row)
{
    if (m_selection)
        m_selection->cancelled = true;
    m_selection.reset();
    m_detail = {};
    m_view->SetEvidence({});
    const auto item = m_model->Row(row);
    const auto snapshot = m_snapshot;
    if (!item || !snapshot)
    {
        m_view->SetDetail({}, false);
        return;
    }
    if (item->kind == SclTaskKind::Goose || item->kind == SclTaskKind::Sv)
    {
        const auto& streams = item->kind == SclTaskKind::Goose ? snapshot->tasks.goose : snapshot->tasks.sv;
        m_view->SetDetail({}, false);
        m_view->PublicationView()->SetPublication(streams[item->index].control);
        return;
    }
    if (IsSignalPage())
    {
        const bool changed = !m_signal || m_signal->index != item->index;
        m_signal = item;
        PresentSignal(QStringLiteral("正在加载所选信号…"));
        if (changed && !m_restore)
            emit SignalSelectionChanged();
    }
    m_view->SetDetail(QStringLiteral("正在查找文件依据…"), false);
    const auto task = m_selection = std::make_shared<Scl::TaskControl>();
    auto* watcher = new QFutureWatcher<std::optional<SclTaskDetail>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, snapshot, task] {
        watcher->deleteLater();
        if (m_selection != task || m_snapshot != snapshot)
            return;
        auto result = watcher->future().takeResult();
        if (!result)
        {
            m_view->SetDetail(QStringLiteral("无法展开当前条目的文件依据。"), false);
            PresentSignal(QStringLiteral("无法加载所选信号详情。"));
            return;
        }
        m_detail = std::move(*result);
        QStringList labels;
        for (const auto& entry : m_detail.evidence)
            labels << entry.label;
        m_view->SetEvidence(labels);
        m_view->SetDetail(m_detail.text, !labels.empty());
        PresentSignal();
    });
    watcher->setFuture(QtConcurrent::run([snapshot, item, task]() -> std::optional<SclTaskDetail> {
        try { return DescribeSclTaskRow(*snapshot, *item, task.get()); }
        catch (const Scl::TaskCancelled&) { return std::nullopt; }
        catch (const std::exception&) { return std::nullopt; }
    }));
}

void SclTaskBrowser::UpdateOverview()
{
    if (!m_snapshot)
    {
        m_view->SetOverview(QStringLiteral("打开 SCL 文件以查阅装置信号及通信声明。"));
        return;
    }
    const auto& index = m_snapshot->tasks;
    const auto scope = index.signalsByIed.constFind(m_ied);
    const auto count = m_ied == Scl::NoNode ? index.catalog.instances.size()
        : scope == index.signalsByIed.cend() ? 0 : scope->size();
    QString overview = QStringLiteral("当前范围 · %1 个对象路径\n"
        "信号查阅提供对象分类建议及数据集用法。GOOSE 和 SV 提供发送配置、原始成员和有证据的接收关系。"
        "\n文件检查提供现有规则的发现和原文位置。").arg(count);
    if (index.catalog.limited || index.relationsLimited)
        overview += QStringLiteral("\n部分目录达到资源上限，当前结果覆盖受限。");
    if (index.catalog.unresolvedLogicalNodes)
        overview += QStringLiteral("\n全工程 %1 个逻辑节点未解析到唯一类型。").arg(index.catalog.unresolvedLogicalNodes);
    m_view->SetOverview(overview);
}
}
