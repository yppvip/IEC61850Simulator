#include "SclOfflineCheckView.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QShowEvent>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QLabel>
#include <QUrl>

namespace iec61850sim {
SclOfflineCheckView::SclOfflineCheckView(QWidget* parent) : QWidget(parent), m_query(this) {
    setObjectName("SclOfflineCheck"); setMinimumSize(0,0);
    setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    auto* outer = new QVBoxLayout(this);
    auto* actions = new QHBoxLayout;
    auto* back = new QPushButton(QStringLiteral("后退"),this); back->setObjectName("SclCheckBack");
    actions->addWidget(back); actions->addWidget(new QLabel(QStringLiteral("对象引用 / MMS Item 离线校核"),this)); actions->addStretch();
    outer->addLayout(actions); connect(back,&QPushButton::clicked,this,&SclOfflineCheckView::Back);
    m_scroll = new QScrollArea(this); m_scroll->setWidgetResizable(true); outer->addWidget(m_scroll);
    auto* body = new QWidget; auto* layout = new QVBoxLayout(body); m_scroll->setWidget(body);
    auto* form = new QFormLayout;
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_kind = new QComboBox(body); m_kind->setObjectName("SclCheckKind");
    m_kind->addItems({QStringLiteral("对象引用"),QStringLiteral("MMS Domain + Item")});
    m_original = new QLineEdit(body); m_original->setObjectName("SclCheckInput"); m_original->setMaxLength(8192);
    m_original->setPlaceholderText("LDName/LN.DO.DA  或  LN$FC$DO$DA");
    m_domain = new QLineEdit(body); m_domain->setObjectName("SclCheckDomain"); m_domain->setMaxLength(512);
    m_domain->setPlaceholderText(QStringLiteral("MMS Domain；也可在原文中粘贴 Domain/Item"));
    m_fc = new QLineEdit(body); m_fc->setObjectName("SclCheckFc"); m_fc->setMaxLength(64);
    m_fc->setPlaceholderText(QStringLiteral("对象引用的 FC；MMS Item 已含 FC 时可留空"));
    form->addRow(QStringLiteral("输入类型"),m_kind); form->addRow(QStringLiteral("粘贴原文"),m_original);
    form->addRow(QStringLiteral("Domain / LDName"),m_domain); form->addRow(QStringLiteral("FC"),m_fc);
    layout->addLayout(form);
    auto* run = new QPushButton(QStringLiteral("校核当前文件模型"),body); run->setObjectName("SclCheckRun"); layout->addWidget(run);
    auto* choices = new QHBoxLayout; m_candidates = new QComboBox(body); m_candidates->setObjectName("SclCheckCandidates");
    m_candidates->setMinimumWidth(0); m_candidates->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_choose = new QPushButton(QStringLiteral("使用所选范围重新校核"),body); m_choose->setObjectName("SclCheckChoose");
    choices->addWidget(m_candidates,1); choices->addWidget(m_choose); layout->addLayout(choices);
    m_evidence = new QTextBrowser(body); m_evidence->setObjectName("SclCheckEvidence"); m_evidence->setOpenLinks(false);
    m_evidence->setMinimumSize(0,120); layout->addWidget(m_evidence,1);
    m_open = new QPushButton(QStringLiteral("到任务视图查阅"),body); m_open->setObjectName("SclCheckOpenTarget");
    m_inspect = new QPushButton(QStringLiteral("查看最后确认对象的实际属性"),body); m_inspect->setObjectName("SclCheckInspectConfirmed");
    layout->addWidget(m_open); layout->addWidget(m_inspect);
    connect(m_original,&QLineEdit::textChanged,this,[this] { if (!m_applying) m_device = Scl::NoNode; Invalidate(); });
    connect(m_domain,&QLineEdit::textChanged,this,[this] { if (!m_applying) m_device = Scl::NoNode; Invalidate(); });
    connect(m_fc,&QLineEdit::textChanged,this,&SclOfflineCheckView::Invalidate);
    connect(m_kind,&QComboBox::currentIndexChanged,this,[this] { if (!m_applying) m_device = Scl::NoNode; Invalidate(); });
    connect(run,&QPushButton::clicked,this,&SclOfflineCheckView::Run);
    connect(m_original,&QLineEdit::returnPressed,this,&SclOfflineCheckView::Run);
    connect(&m_query,&SclMmsQuerySession::Ready,this,[this] {
        const auto result = m_query.result();
        if (!isVisible() || !m_snapshot || !result || result->requestId != m_request || result->document != m_snapshot->analysis.document) return;
        m_result = result; Render(); emit Changed();
    });
    connect(m_choose,&QPushButton::clicked,this,[this] {
        if (!m_result || !m_result->check || !isVisible()) return;
        const int i = m_candidates->currentIndex(); const auto result = m_result;
        if (i < 0 || i >= result->check->candidates.size()) return;
        const auto candidate = result->check->candidates[i];
        const QScopedValueRollback<bool> guard(m_applying,true);
        m_device = candidate.logicalDevice;
        m_domain->setText(m_snapshot->mmsIndex.domainsByDevice.value(m_device).name);
        Run();
    });
    connect(m_open,&QPushButton::clicked,this,[this] {
        if (isVisible() && m_snapshot && m_result && m_result->document == m_snapshot->analysis.document
            && m_result->check && m_result->check->mappingPassed && m_result->check->target) emit OpenTarget(*m_result->check->target);
    });
    connect(m_inspect,&QPushButton::clicked,this,[this] {
        if (!isVisible() || !m_result || !m_result->check) return;
        for (auto it = m_result->check->steps.crbegin(); it != m_result->check->steps.crend(); ++it)
            if (m_snapshot && IsSclMmsAttributeTarget(*m_snapshot,it->source.node)) { emit InspectConfirmed(it->source.node); break; }
    });
    connect(m_evidence,&QTextBrowser::anchorClicked,this,[this](const QUrl& url) {
        if (!isVisible() || !m_snapshot || !m_result || !m_result->check || url.host() != "request-" + QString::number(m_epoch)) return;
        if (url.scheme() == "scl-check-fc") {
            const auto fc = url.path().mid(1);
            if (m_result->check->fcCandidates.contains(fc)) { m_fc->setText(fc); Run(); }
        } else if (url.scheme() == "scl-check-source") {
            bool ok = false; const auto node = url.path().mid(1).toInt(&ok); if (!ok) return;
            bool allowed = m_result->check->lastConfirmed.node == node;
            for (const auto& step : m_result->check->steps) allowed |= step.source.node == node;
            for (const auto& candidate : m_result->check->typeCandidates) allowed |= candidate.node == node;
            if (allowed && node != Scl::NoNode) { m_sourceNode = node; emit OpenSource(node); }
        }
    });
    Render();
}
void SclOfflineCheckView::SetContext(std::shared_ptr<const SclSnapshot> snapshot,SclMmsScope scope) {
    if (snapshot == m_snapshot && scope == m_scope) return;
    m_snapshot = std::move(snapshot); m_scope = scope; m_device = Scl::NoNode;
    m_query.SetContext(m_snapshot,scope); Invalidate();
}
SclCheckInput SclOfflineCheckView::Input() const {
    return {m_kind->currentIndex() == 0 ? SclCheckInputKind::ObjectReference : SclCheckInputKind::MmsItem,
        m_original->text(),m_domain->text(),m_fc->text(),m_device};
}
void SclOfflineCheckView::Open(const SclCheckInput& input) {
    Cancel();
    const QScopedValueRollback<bool> guard(m_applying,true);
    m_kind->setCurrentIndex(input.kind == SclCheckInputKind::ObjectReference ? 0 : 1);
    m_original->setText(input.original); m_domain->setText(input.domain); m_fc->setText(input.fc); m_device = input.logicalDevice;
    m_result.reset(); m_sourceNode = Scl::NoNode; ++m_epoch; Render(); m_original->setFocus();
}
void SclOfflineCheckView::Invalidate() {
    if (m_applying) return;
    Cancel(); m_result.reset(); m_sourceNode = Scl::NoNode; Render(); emit Changed();
}
void SclOfflineCheckView::Cancel() { ++m_epoch; m_query.Cancel(); }
void SclOfflineCheckView::hideEvent(QHideEvent* event) { Cancel(); QWidget::hideEvent(event); }
void SclOfflineCheckView::showEvent(QShowEvent* event) { QWidget::showEvent(event); if (!m_result) Render(); }
void SclOfflineCheckView::Run() {
    if (!m_snapshot) return;
    Cancel(); m_result.reset(); m_sourceNode = Scl::NoNode; Render();
    m_evidence->setPlainText(QStringLiteral("正在后台校核当前快照…"));
    SclMmsRequest request; request.kind = SclMmsQueryKind::OfflineCheck; request.check = Input();
    request.identityDocument = m_snapshot->analysis.document; m_request = m_query.Query(request);
    emit Changed();
}
std::shared_ptr<SclCheckLocation> SclOfflineCheckView::Location() const {
    return std::make_shared<SclCheckLocation>(SclCheckLocation{m_snapshot,Input(),m_result,m_scroll->verticalScrollBar()->value(),m_sourceNode});
}
bool SclOfflineCheckView::Restore(std::shared_ptr<SclCheckLocation> location) {
    if (!location || location->snapshot != m_snapshot) return false;
    Open(location->input); m_result = location->result; m_sourceNode = location->source;
    Render(); m_scroll->verticalScrollBar()->setValue(location->scroll); return true;
}
void SclOfflineCheckView::Render() {
    ++m_epoch; m_candidates->clear();
    m_open->setEnabled(false); m_inspect->setVisible(false); m_candidates->hide(); m_choose->hide();
    if (!m_result || !m_result->check) {
        m_evidence->setPlainText(m_result ? m_result->limitation : QStringLiteral("一次校核一个引用。修改输入后需重新校核。范围沿用当前 IED；Domain 同名时须显式选择。在线未验证；完整 XSD / CDC 标准符合性未执行。")); return;
    }
    const auto& c = *m_result->check;
    const auto title = c.state == SclCheckState::Supported ? QStringLiteral("当前文件模型支持 · 离线映射通过")
        : c.state == SclCheckState::Rejected ? QStringLiteral("当前文件模型不支持此输入")
        : c.state == SclCheckState::ContextRequired ? QStringLiteral("需补充上下文")
        : QStringLiteral("当前文件模型尚不能确定");
    QString html = "<h3>" + title + "</h3>";
    const auto line = [&](const QString& label,const QString& value) { return "<p><b>" + label + "：</b>" + value.toHtmlEscaped() + "</p>"; };
    html += line(QStringLiteral("粘贴原文"),m_result->request.check.original);
    html += line(QStringLiteral("解析结果"),c.domain + " / " + c.logicalNode + "." + c.path + " · FC=" + c.fc);
    html += line(QStringLiteral("格式"),c.format); html += line(QStringLiteral("模型"),c.model);
    html += line(QStringLiteral("映射"),c.mapping.isEmpty() ? QStringLiteral("未通过 / 未执行完整映射") : c.mapping);
    if (m_result->objectEvidence) {
        const auto& e = *m_result->objectEvidence;
        html += line(QStringLiteral("同源对象证据"),QStringLiteral("DO/SDO：%1；属性：%2；CDC：%3；基本类型：%4；有效 FC：%5")
            .arg(e.objectPath,e.attributePath,e.cdc,e.basicType,e.fc));
    }
    html += line(QStringLiteral("适用规则"),QStringLiteral("普通对象离线校核 0.1；基础引用 0.2；MMS 映射 mms-0.1；不执行版本相关的报告实例规则。"));
    html += line(QStringLiteral("执行范围"),QStringLiteral("当前 IED/AP/Server；工作单元 %1 / %2；后台耗时 %3 ms")
        .arg(c.workUsed).arg(m_result->request.workLimit).arg(c.elapsedUs/1000.0,0,'f',3));
    if (!c.stoppedAt.isEmpty()) html += line(QStringLiteral("首个停止段"),c.stoppedAt);
    const auto source = [&](const SclEvidenceSource& evidence) {
        if (evidence.node == Scl::NoNode) return QString();
        return QStringLiteral(" <a href=\"scl-check-source://request-%1/%2\">原文（标签起始行 %3）</a>")
            .arg(m_epoch).arg(evidence.node).arg(evidence.location.line);
    };
    for (const auto& step : c.steps.mid(0,64)) html += "<p>" + step.segment.toHtmlEscaped() + " — " + step.conclusion.toHtmlEscaped() + source(step.source) + "</p>";
    if (c.steps.size() > 64) html += "<p>" + QStringLiteral("逐段原文预览仅展示前 64 段，最后确认位置单列。") + "</p>";
    html += "<p>" + QStringLiteral("最后确认位置") + source(c.lastConfirmed) + "</p>";
    for (const auto& candidate : c.typeCandidates) html += "<p>" + QStringLiteral("声明/类型候选原文（最多预览 64 项，不代表成功目标）") + source(candidate) + "</p>";
    if (!c.fcCandidates.isEmpty()) {
        html += "<p>" + QStringLiteral("实际 FC 候选：");
        for (const auto& fc : c.fcCandidates) html += QStringLiteral(" <a href=\"scl-check-fc://request-%1/%2\">%3</a>").arg(m_epoch).arg(fc.toHtmlEscaped(),fc.toHtmlEscaped());
        html += "</p>";
    }
    QString coverage = c.dataSets.conclusion == SclDataSetConclusion::Referenced ? QStringLiteral("发现数据集引用 / 覆盖")
        : c.dataSets.conclusion == SclDataSetConclusion::NotReferenced ? QStringLiteral("当前扫描未发现数据集引用") : QStringLiteral("尚未确定 / 未执行完整数据集扫描");
    for (auto kind : c.coverageKinds) coverage += " · " + SclMmsReportMatchText(kind);
    coverage += QStringLiteral("；已观察 %1 项；扫描完整：%2；未确定声明 %3 项").arg(c.dataSets.observedMatches).arg(c.dataSets.scanComplete ? QStringLiteral("是") : QStringLiteral("否")).arg(c.dataSets.uncertainOccurrences);
    html += line(QStringLiteral("独立数据集证据"),coverage);
    html += line(QStringLiteral("限制"),m_result->limitation + QStringLiteral(" 在线未验证；完整 XSD、完整 CDC/标准符合性未执行。对象/结构成立不代表标量读取。"));
    m_evidence->setHtml(html);
    m_open->setEnabled(c.mappingPassed && c.target.has_value());
    for (const auto& step : c.steps) if (m_snapshot && IsSclMmsAttributeTarget(*m_snapshot,step.source.node)) m_inspect->setVisible(!c.mappingPassed);
    for (const auto& candidate : c.candidates) m_candidates->addItem(candidate.label);
    m_candidates->setCurrentIndex(-1); // A candidate is never preselected as a resolved target.
    m_candidates->setVisible(!c.candidates.isEmpty()); m_choose->setVisible(!c.candidates.isEmpty());
}
}
