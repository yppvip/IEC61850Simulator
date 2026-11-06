#include "SclWorkspace.hpp"

#include "SclIssueExport.hpp"
#include "SclSearchSession.hpp"
#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QTabWidget>
#include <QTableView>
#include <QVBoxLayout>
#include <QtConcurrentRun>

namespace iec61850sim
{
namespace
{
constexpr int PageSize = 512;
using Snapshot = std::shared_ptr<const SclSnapshot>;

SearchScope TableScope(const QWidget* page)
{
    if (!page)
        return SearchScope::All;
    const auto name = page->objectName();
    if (name == "SclReports") return SearchScope::Reports;
    if (name == "SclDataSets") return SearchScope::DataSets;
    if (name == "SclMembers") return SearchScope::Members;
    if (name == "SclObjects") return SearchScope::Objects;
    if (name == "SclCommunication") return SearchScope::Communication;
    if (name == "SclTemplates") return SearchScope::Templates;
    if (name == "SclFindings") return SearchScope::Findings;
    return SearchScope::All;
}

QString NodeTitle(const SclSnapshot& snapshot, Scl::NodeId id)
{
    if (id < 0)
        return QStringLiteral("文件");
    const auto& doc = *snapshot.analysis.document;
    const auto& node = doc.nodes[id];
    for (const auto* field : {"name", "id", "inst", "cbName", "intAddr", "type"})
    {
        const auto value = doc.attribute(id, QString::fromLatin1(field));
        if (!value.isEmpty())
            return node.name + QStringLiteral(" · ") + value;
    }
    return node.name;
}

QString NodePath(const SclSnapshot& snapshot, Scl::NodeId id)
{
    QStringList parts;
    for (; id > 0; id = snapshot.analysis.document->nodes[id].parent)
        parts.prepend(NodeTitle(snapshot, id));
    return parts.isEmpty() ? QStringLiteral("SCL") : parts.join(QStringLiteral(" / "));
}
} // namespace

class SclResultsModel final : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;

    void Reset(Snapshot snapshot = {}, const QString& text = {})
    {
        beginResetModel();
        m_snapshot = std::move(snapshot);
        m_needle = text;
        m_hits.clear();
        m_visible = 0;
        endResetModel();
    }

    void Append(const SclHitBatch& hits)
    {
        m_hits.insert(m_hits.end(), hits.begin(), hits.end());
        if (m_visible < PageSize)
            Show(qMin(PageSize, total()));
    }

    void More() { Show(qMin(total(), m_visible + PageSize)); }
    int total() const { return int(m_hits.size()); }
    const SclHit& Hit(int row) const { return m_hits[row]; }
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : m_visible; }
    int columnCount(const QModelIndex& = {}) const override { return 6; }

    QVariant headerData(int column, Qt::Orientation orientation, int role) const override
    {
        static const QStringList headings{QStringLiteral("对象"), QStringLiteral("匹配字段"),
                                          QStringLiteral("片段"), QStringLiteral("行"), QStringLiteral("对象路径"), QStringLiteral("对象描述")};
        return orientation == Qt::Horizontal && role == Qt::DisplayRole ? headings.value(column) : QVariant{};
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid())
            return {};
        const auto& hit = m_hits[index.row()];
        if (role == Qt::UserRole)
            return hit.node;
        if (index.column() == 5 && (role == Qt::DisplayRole || role == Qt::ToolTipRole))
        {
            const auto desc = hit.node < 0 ? QString()
                : m_snapshot->analysis.document->attribute(hit.node, QStringLiteral("desc"));
            return role == Qt::ToolTipRole && !desc.isEmpty()
                ? QStringLiteral("<qt>%1</qt>").arg(desc.toHtmlEscaped().replace('\n', QStringLiteral("<br>"))) : desc;
        }
        if (role == Qt::ToolTipRole)
            return QStringLiteral("<qt>%1<br>%2</qt>").arg(NodePath(*m_snapshot, hit.node).toHtmlEscaped(),
                SearchSnippet(*m_snapshot, hit, m_needle).toHtmlEscaped());
        if (role != Qt::DisplayRole)
            return {};
        switch (index.column())
        {
        case 0: return NodeTitle(*m_snapshot, hit.node);
        case 1: return SearchField(*m_snapshot, hit);
        case 2: return SearchSnippet(*m_snapshot, hit, m_needle);
        case 3: return hit.node < 0 ? QVariant{} : QVariant(m_snapshot->analysis.document->nodes[hit.node].source.line);
        case 4: return NodePath(*m_snapshot, hit.node);
        }
        return {};
    }

private:
    void Show(int count)
    {
        if (count <= m_visible)
            return;
        beginInsertRows({}, m_visible, count - 1);
        m_visible = count;
        endInsertRows();
    }

    Snapshot m_snapshot;
    QString m_needle;
    SclHitBatch m_hits;
    int m_visible = 0;
};

void SclWorkspace::CreateSearchBar(QVBoxLayout* layout)
{
    auto* row = new QHBoxLayout;
    m_searchMode = new QComboBox(this);
    m_searchMode->setObjectName("SclSearchMode");
    m_searchMode->addItems({QStringLiteral("全局搜索"), QStringLiteral("筛选当前表格")});
    m_query = new QLineEdit(this);
    m_query->setObjectName("SclQuery");
    m_query->setAccessibleName(QStringLiteral("SCL 搜索关键词"));
    m_query->setPlaceholderText(QStringLiteral("搜索名称、描述、引用、地址或规则编号"));
    m_query->setClearButtonEnabled(true);
    m_exact = new QCheckBox(QStringLiteral("精确匹配"), this);
    m_exact->setObjectName("SclExact");
    m_cancelSearch = new QPushButton(style()->standardIcon(QStyle::SP_BrowserStop), QStringLiteral("取消搜索"), this);
    m_cancelSearch->setObjectName("SclCancelSearch");
    m_export = new QPushButton(style()->standardIcon(QStyle::SP_DialogSaveButton), QStringLiteral("导出问题"), this);
    m_export->setObjectName("SclExport");
    m_export->setEnabled(false);
    row->addWidget(m_searchMode);
    row->addWidget(m_query, 1);
    row->addWidget(m_exact);
    row->addWidget(m_cancelSearch);
    row->addWidget(m_export);
    layout->addLayout(row);

    auto* filters = new QHBoxLayout;
    const auto input = [&](const char* name, const QString& label, int width) {
        auto* edit = new QLineEdit(this);
        edit->setObjectName(QString::fromLatin1(name));
        edit->setPlaceholderText(label);
        edit->setAccessibleName(label);
        edit->setMaximumWidth(width);
        filters->addWidget(edit);
        return edit;
    };
    m_iedFilter = input("SclIedFilter", QStringLiteral("IED 名称"), 160);
    m_typeFilter = input("SclTypeFilter", QStringLiteral("对象类型"), 135);
    m_fcFilter = input("SclFcFilter", QStringLiteral("FC"), 65);
    m_severityFilter = new QComboBox(this);
    m_severityFilter->setObjectName("SclSeverityFilter");
    m_severityFilter->setAccessibleName(QStringLiteral("问题严重度"));
    m_severityFilter->addItems({QStringLiteral("全部级别"), QStringLiteral("错误"), QStringLiteral("警告"), QStringLiteral("提示")});
    filters->addWidget(m_severityFilter);
    m_searchStatus = new QLabel(this);
    m_searchStatus->setObjectName("SclSearchStatus");
    m_searchStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    filters->addWidget(m_searchStatus, 1);
    m_moreResults = new QPushButton(QStringLiteral("显示更多"), this);
    m_moreResults->setObjectName("SclMoreResults");
    filters->addWidget(m_moreResults);
    m_motion = new QCheckBox(QStringLiteral("动效"), this);
    m_motion->setChecked(true);
    filters->addWidget(m_motion);
    layout->addLayout(filters);
    m_exportStatus = new QLabel(this);
    m_exportStatus->setTextFormat(Qt::PlainText);
    m_exportStatus->hide();
    layout->addWidget(m_exportStatus);

    m_search = new SclSearchSession(this);
    m_searchDelay = new QTimer(this);
    m_searchDelay->setSingleShot(true);
    m_searchDelay->setInterval(200);
    connect(m_searchDelay, &QTimer::timeout, this, &SclWorkspace::BeginSearch);
    for (auto* edit : {m_query, m_iedFilter, m_typeFilter, m_fcFilter})
        connect(edit, &QLineEdit::textChanged, this, [this] { ScheduleSearch(); });
    connect(m_searchMode, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { ScheduleSearch(); });
    connect(m_severityFilter, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { ScheduleSearch(); });
    connect(m_exact, &QCheckBox::toggled, this, [this] { ScheduleSearch(); });
    connect(m_query, &QLineEdit::returnPressed, this, [this] {
        ResetSearch();
        BeginSearch();
    });
    connect(m_cancelSearch, &QPushButton::clicked, this, &SclWorkspace::CancelSearch);
    connect(m_export, &QPushButton::clicked, this, &SclWorkspace::SaveIssues);
    auto* find = new QShortcut(QKeySequence::Find, this);
    find->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find, &QShortcut::activated, m_query, [this] { m_query->setFocus(); });
    auto* escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] {
        if (m_searching || m_searchDelay->isActive())
            CancelSearch();
    });
}

void SclWorkspace::AttachSearchResults()
{
    m_results = new QTableView(m_tables);
    m_results->setObjectName("SclSearchResults");
    m_results->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_results->setSelectionMode(QAbstractItemView::SingleSelection);
    m_results->setAlternatingRowColors(true);
    m_results->setShowGrid(false);
    m_results->setAccessibleName(QStringLiteral("搜索结果"));
    m_results->setWordWrap(false);
    m_results->verticalHeader()->hide();
    m_results->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_results->verticalHeader()->setDefaultSectionSize(qMax(28, m_results->fontMetrics().height() + 8));
    m_results->horizontalHeader()->setDefaultSectionSize(110);
    m_resultsModel = new SclResultsModel(m_results);
    m_results->setModel(m_resultsModel);
    m_results->setColumnWidth(0, 160);
    m_results->setColumnWidth(2, 250);
    m_tables->addTab(m_results, QStringLiteral("搜索结果"));
    connect(m_results, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (!index.isValid())
            return;
        const auto hit = m_resultsModel->Hit(index.row());
        if (hit.field == MatchField::Finding)
            SelectFinding(hit.detail, true, true);
        else
            Select(hit.node, true);
    });
    connect(m_tables, &QTabWidget::currentChanged, this, [this] {
        if (m_searchMode->currentIndex() == 1 && !m_restoring)
            ScheduleSearch();
    });
    connect(m_moreResults, &QPushButton::clicked, this, [this] {
        m_resultsModel->More();
        UpdateSearchStatus();
    });
    connect(m_search, &SclSearchSession::BatchReady, this, [this](const SclHitBatch& hits) {
        if (m_filteredTable)
        {
            for (const auto& hit : hits)
                m_filteredRows.push_back(hit.row);
        }
        else
            m_resultsModel->Append(hits);
        UpdateSearchStatus();
    });
    connect(m_search, &SclSearchSession::Finished, this, [this](const QString& error) {
        m_searching = false;
        m_searchError = error;
        if (m_filteredTable && error.isEmpty())
            ApplyTableFilter(m_filteredTable, std::move(m_filteredRows));
        UpdateSearchStatus();

        if (m_motion->isChecked() && isVisible())
        {
            auto* effect = new QGraphicsOpacityEffect(m_searchStatus);
            m_searchStatus->setGraphicsEffect(effect);
            auto* animation = new QPropertyAnimation(effect, "opacity", effect);
            animation->setDuration(140);
            animation->setStartValue(0.68);
            animation->setEndValue(1.0);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        }
    });
    UpdateSearchStatus();
}

void SclWorkspace::ResetSearch(bool clearInputs)
{
    m_search->Cancel();
    m_searchDelay->stop();
    m_searching = false;
    m_searchCancelled = false;
    m_searchError.clear();
    m_filteredRows.clear();
    ApplyTableFilter(m_filteredTable, std::nullopt);
    m_filteredTable = nullptr;
    m_resultsModel->Reset();
    if (clearInputs)
    {
        for (auto* edit : {m_query, m_iedFilter, m_typeFilter, m_fcFilter})
        {
            const QSignalBlocker blocked(edit);
            edit->clear();
        }
        const QSignalBlocker blocked(m_severityFilter);
        m_severityFilter->setCurrentIndex(0);
    }
    UpdateSearchStatus();
}

void SclWorkspace::ScheduleSearch()
{
    ResetSearch();
    if (!m_snapshot)
        return;
    m_searchDelay->start();
    UpdateSearchStatus();
}

void SclWorkspace::BeginSearch()
{
    if (!m_snapshot)
        return;
    SclQuery query;
    query.text = m_query->text().trimmed();
    query.ied = m_iedFilter->text().trimmed();
    query.type = m_typeFilter->text().trimmed();
    query.fc = m_fcFilter->text().trimmed();
    query.severity = m_severityFilter->currentIndex() - 1;
    query.exact = m_exact->isChecked();
    query.dataSet = m_dataSet;
    if (query.empty())
    {
        UpdateSearchStatus();
        return;
    }
    if (m_searchMode->currentIndex() == 1)
    {
        query.scope = TableScope(m_tables->currentWidget());
        if (query.scope == SearchScope::All)
        {
            m_searchError = QStringLiteral("请选择可筛选的表格。");
            UpdateSearchStatus();
            return;
        }
    }
    if (query.scope == SearchScope::All)
    {
        m_resultsModel->Reset(m_snapshot, query.text);
        m_tables->setCurrentWidget(m_results);
    }
    else
        m_filteredTable = qobject_cast<QTableView*>(m_tables->currentWidget());
    m_searching = true;
    m_search->Start(m_snapshot, std::move(query));
    UpdateSearchStatus();
}

void SclWorkspace::CancelSearch()
{
    m_search->Cancel();
    m_searchDelay->stop();
    m_searching = false;
    m_searchCancelled = true;
    m_filteredRows.clear();
    UpdateSearchStatus();
}

void SclWorkspace::UpdateSearchStatus()
{
    m_cancelSearch->setEnabled(m_searching || m_searchDelay->isActive());
    m_moreResults->setVisible(m_resultsModel->rowCount() < m_resultsModel->total());
    QString text;
    if (!m_searchError.isEmpty())
        text = m_searchError;
    else if (m_searchCancelled)
        text = QStringLiteral("搜索已取消 · 已收到 %1 条（结果不完整）").arg(m_resultsModel->total());
    else if (m_searchDelay->isActive())
        text = QStringLiteral("等待搜索…");
    else if (m_searching)
        text = QStringLiteral("正在搜索… 已找到 %1 条").arg(m_filteredTable ? int(m_filteredRows.size()) : m_resultsModel->total());
    else if (m_filteredTable)
        text = QStringLiteral("筛选后 %1 行").arg(m_filteredTable->model()->rowCount());
    else if (m_tables->currentWidget() == m_results)
        text = m_resultsModel->total() == 0 ? QStringLiteral("没有匹配结果")
            : QStringLiteral("共 %1 条 · 已显示 %2 条").arg(m_resultsModel->total()).arg(m_resultsModel->rowCount());
    else
        text = QStringLiteral("Ctrl+F 搜索");
    m_searchStatus->setText(text);
    m_searchStatus->setToolTip(text.toHtmlEscaped());
}

void SclWorkspace::SaveIssues()
{
    const auto snapshot = m_snapshot;
    if (!snapshot || m_exporting)
        return;
    const QString jsonFilter = QStringLiteral("JSON 校验摘要 (*.json)");
    const QString csvFilter = QStringLiteral("CSV 校验摘要 (*.csv)");
    QFileDialog dialog(this, QStringLiteral("导出当前文档的全部校验问题与覆盖范围"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({jsonFilter, csvFilter});
    dialog.setDefaultSuffix(QStringLiteral("json"));
    dialog.selectFile(QFileInfo(snapshot->path).completeBaseName() + QStringLiteral("-issues"));
    connect(&dialog, &QFileDialog::filterSelected, &dialog, [&](const QString& filter) {
        dialog.setDefaultSuffix(filter == csvFilter ? QStringLiteral("csv") : QStringLiteral("json"));
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
        return;
    const auto path = dialog.selectedFiles().front();
    const bool csv = path.endsWith(QStringLiteral(".csv"), Qt::CaseInsensitive)
        || (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive) && dialog.selectedNameFilter() == csvFilter);
    m_exporting = true;
    m_export->setEnabled(false);
    m_exportStatus->setText(QStringLiteral("正在导出校验摘要…"));
    m_exportStatus->show();
    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, path] {
        const auto error = watcher->result();
        watcher->deleteLater();
        m_exporting = false;
        m_export->setEnabled(bool(m_snapshot));
        m_exportStatus->setText(error.isEmpty() ? QStringLiteral("已导出 %1").arg(QFileInfo(path).fileName())
                                                : QStringLiteral("导出失败：%1").arg(error));
    });
    watcher->setFuture(QtConcurrent::run([snapshot, path, csv] {
        return ExportSclIssues(*snapshot, path, csv ? IssueFormat::Csv : IssueFormat::Json);
    }));
}
} // namespace iec61850sim
