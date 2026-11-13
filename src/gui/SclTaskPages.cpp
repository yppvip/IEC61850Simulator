#include "SclTaskPages.hpp"
#include "SclConnectionView.hpp"

#include <QComboBox>
#include <QCompleter>
#include <QHeaderView>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTabBar>
#include <QTableView>
#include <QTextBrowser>
#include <QTimer>
#include <QVBoxLayout>

namespace iec61850sim
{
SclTaskPages::SclTaskPages(QWidget* parent) : QWidget(parent)
{
    setObjectName("SclTaskPages");
    auto* layout = new QVBoxLayout(this);
    auto* navigation = new QHBoxLayout;
    auto* scopeLabel = new QLabel(QStringLiteral("装置(&I)"), this);
    m_scope = new QComboBox(this);
    m_scope->setObjectName("SclTaskIed");
    m_scope->setEditable(true);
    m_scope->setInsertPolicy(QComboBox::NoInsert);
    m_scope->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_scope->setMinimumContentsLength(10);
    m_scope->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_scope->completer()->setFilterMode(Qt::MatchContains);
    m_scope->completer()->setCaseSensitivity(Qt::CaseInsensitive);
    scopeLabel->setBuddy(m_scope);
    navigation->addWidget(scopeLabel);
    navigation->addWidget(m_scope, 1);
    layout->addLayout(navigation);

    m_pages = new QTabBar(this);
    m_pages->setObjectName("SclTaskNavigation");
    m_pages->setUsesScrollButtons(true);
    m_pages->setExpanding(false);
    const auto addPage = [this](QStyle::StandardPixmap icon, const QString& label) {
        m_pages->addTab(style()->standardIcon(icon), label);
    };
    addPage(QStyle::SP_DirHomeIcon, QStringLiteral("概览"));
    addPage(QStyle::SP_FileDialogListView, QStringLiteral("信号查阅"));
    addPage(QStyle::SP_ArrowRight, QStringLiteral("GOOSE"));
    addPage(QStyle::SP_BrowserReload, QStringLiteral("SV"));
    addPage(QStyle::SP_FileDialogDetailedView, QStringLiteral("文件检查"));
    addPage(QStyle::SP_ComputerIcon, QStringLiteral("收发关系"));
    layout->addWidget(m_pages);

    m_body = new QStackedWidget(this);
    layout->addWidget(m_body, 1);
    m_overview = new QLabel(m_body);
    m_overview->setTextFormat(Qt::PlainText);
    m_overview->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_overview->setWordWrap(true);
    m_body->addWidget(m_overview);
    auto* results = new QWidget(m_body);
    auto* resultLayout = new QVBoxLayout(results);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    m_checks = new QWidget(results);
    auto* checkLayout = new QVBoxLayout(m_checks);
    checkLayout->setContentsMargins(0, 0, 0, 0);
    m_coverage = new QLabel(m_checks);
    m_coverage->setObjectName("SclTaskCoverage");
    m_coverage->setTextFormat(Qt::PlainText);
    m_coverage->setWordWrap(true);
    checkLayout->addWidget(m_coverage);
    auto* checkActions = new QHBoxLayout;
    m_coverageButton = new QPushButton(QStringLiteral("规则覆盖"), m_checks);
    m_exportIssues = new QPushButton(QStringLiteral("导出全工程问题"), m_checks);
    checkActions->addWidget(m_coverageButton);
    checkActions->addWidget(m_exportIssues);
    checkActions->addStretch();
    checkLayout->addLayout(checkActions);
    resultLayout->addWidget(m_checks);
    m_checks->hide();
    m_filter = new QLineEdit(results);
    m_filter->setObjectName("SclTaskFilter");
    m_filter->setAccessibleName(QStringLiteral("筛选当前页"));
    m_filter->setPlaceholderText(QStringLiteral("筛选当前页"));
    m_filter->setToolTip(QStringLiteral("可按对象路径、分类建议或当前页的声明标识筛选。"));
    m_filter->setClearButtonEnabled(true);
    auto* filters = new QHBoxLayout;
    m_direction = new QComboBox(results);
    m_direction->setObjectName("SclTaskDirection");
    m_direction->setAccessibleName(QStringLiteral("信号方向"));
    m_direction->addItems({QStringLiteral("本机对象"), QStringLiteral("外部输入")});
    m_direction->hide();
    filters->addWidget(m_direction);
    filters->addWidget(m_filter, 1);
    resultLayout->addLayout(filters);
    m_status = new QLabel(results);
    m_status->setObjectName("SclTaskStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    resultLayout->addWidget(m_status);
    m_table = new QTableView(results);
    m_table->setObjectName("SclTaskResults");
    m_table->setAccessibleName(QStringLiteral("当前任务的查阅结果"));
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    m_table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_table->verticalHeader()->setDefaultSectionSize(qMax(28, fontMetrics().height() + 8));
    m_table->setMinimumHeight(88);
    m_table->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    resultLayout->addWidget(m_table, 3);
    m_detail = new QTextBrowser(results);
    m_detail->setObjectName("SclTaskDetail");
    m_detail->setAccessibleName(QStringLiteral("所选条目的解释与证据"));
    m_detail->setMinimumHeight(72);
    m_detail->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    resultLayout->addWidget(m_detail, 2);
    m_source = new QPushButton(QStringLiteral("查看文件依据(&E)"), results);
    m_source->setObjectName("SclTaskSource");
    m_source->setEnabled(false);
    auto* sources = new QHBoxLayout;
    m_evidence = new QComboBox(results);
    m_evidence->setAccessibleName(QStringLiteral("选择文件依据"));
    m_evidence->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_evidence->hide();
    sources->addWidget(m_evidence, 1);
    sources->addWidget(m_source);
    resultLayout->addLayout(sources);
    m_body->addWidget(results);
    m_connections = new SclConnectionView(m_body);
    auto* relationScroll = new QScrollArea(m_body);
    relationScroll->setWidgetResizable(true);
    relationScroll->setFrameShape(QFrame::NoFrame);
    relationScroll->setWidget(m_connections);
    m_body->addWidget(relationScroll);
    auto* relations = new QPushButton(style()->standardIcon(QStyle::SP_FileLinkIcon),
        QStringLiteral("查看双向收发与成员映射 →"), this);
    relations->setObjectName("SclOpenConnections");
    navigation->addWidget(relations);
    connect(relations, &QPushButton::clicked, this, [this] {
        const int page = m_pages->currentIndex();
        m_connections->SetService(page == Goose ? QStringLiteral("GOOSE")
            : page == SampledValues ? QStringLiteral("SV") : QString{});
        SetPage(Connections);
    });

    connect(m_pages, &QTabBar::currentChanged, this, [this](int index) {
        m_body->setCurrentIndex(index == Connections ? 2 : index == Overview ? 0 : 1);
        const bool communication = index == Goose || index == SampledValues;
        m_direction->setItemText(0, communication ? QStringLiteral("发送配置") : QStringLiteral("本机对象"));
        m_direction->setItemText(1, communication ? QStringLiteral("接收声明") : QStringLiteral("外部输入"));
        m_direction->setVisible(index == Signals || communication);
        m_checks->setVisible(index == Checks);
        emit PageChanged(static_cast<Page>(index));
    });
    connect(m_scope, &QComboBox::activated, this, [this](int index) {
        m_appliedScope = m_scope->itemData(index).toInt();
        emit ScopeChanged(m_appliedScope);
    });
    connect(m_scope->lineEdit(), &QLineEdit::editingFinished, this, [this] {
        QTimer::singleShot(0, this, [this] { SetScope(m_appliedScope); });
    });
    connect(m_filter, &QLineEdit::textChanged, this, &SclTaskPages::FilterChanged);
    connect(m_direction, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit DirectionChanged(index == 1);
    });
    connect(m_source, &QPushButton::clicked, this, &SclTaskPages::OpenSource);
    connect(m_exportIssues, &QPushButton::clicked, this, &SclTaskPages::ExportIssues);
    connect(m_coverageButton, &QPushButton::clicked, this, [this] {
        QMessageBox dialog(QMessageBox::Information, QStringLiteral("规则覆盖"), m_rules, QMessageBox::Ok, this);
        dialog.setTextFormat(Qt::PlainText);
        dialog.exec();
    });
    connect(m_table, &QTableView::activated, this, [this](const QModelIndex&) {
        if (m_source->isEnabled())
            emit OpenSource();
    });
}

void SclTaskPages::SetScopeOptions(const QList<QPair<int, QString>>& options, int current)
{
    const QSignalBlocker blocker(m_scope);
    m_scope->clear();
    for (const auto& option : options)
        m_scope->addItem(option.second, option.first);
    SetScope(current);
}

void SclTaskPages::SetScope(int id)
{
    const QSignalBlocker blocker(m_scope);
    m_appliedScope = id;
    m_scope->setCurrentIndex(m_scope->findData(id));
    m_scope->setEditText(m_scope->itemText(m_scope->currentIndex()));
}

void SclTaskPages::SetPage(Page page)
{
    m_pages->setCurrentIndex(page);
}

void SclTaskPages::SetModel(QAbstractItemModel* model)
{
    m_table->setModel(model);
    if (auto* selection = m_table->selectionModel())
        connect(selection, &QItemSelectionModel::currentRowChanged, this, &SclTaskPages::Selected,
                Qt::UniqueConnection);
}

void SclTaskPages::SetOverview(const QString& text)
{
    m_overview->setText(text);
}

void SclTaskPages::SetStatus(const QString& text)
{
    m_status->setText(text);
}

void SclTaskPages::SetDetail(const QString& text, bool hasSource)
{
    m_detail->setPlainText(text);
    m_source->setEnabled(hasSource);
}

void SclTaskPages::SetEvidence(const QStringList& labels)
{
    m_evidence->clear();
    m_evidence->addItems(labels);
    m_evidence->setVisible(!labels.empty());
}

int SclTaskPages::EvidenceIndex() const
{
    return m_evidence->currentIndex();
}

void SclTaskPages::SetFilterText(const QString& text)
{
    const QSignalBlocker blocker(m_filter);
    m_filter->setText(text);
}

void SclTaskPages::SetInputDirection(bool inputs)
{
    const QSignalBlocker blocker(m_direction);
    m_direction->setCurrentIndex(inputs ? 1 : 0);
}

void SclTaskPages::SetCoverage(const QString& summary, const QString& rules)
{
    m_coverage->setText(summary);
    m_rules = rules;
    m_exportIssues->setEnabled(!rules.isEmpty());
    m_coverageButton->setEnabled(!rules.isEmpty());
}
}
