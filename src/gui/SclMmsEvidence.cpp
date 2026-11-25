#include "SclMmsView.hpp"
#include "SclReferenceMenu.hpp"
#include "SclTableInteraction.hpp"
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidgetAction>

namespace iec61850sim {
namespace {
QString Position(const SclEvidenceSource& source) {
    return source.precision == SclEvidencePrecision::OpeningTag
        ? QStringLiteral("标签起始行 %1").arg(source.location.line) : QStringLiteral("无可信位置");
}
}

void SclMmsView::SetupObjectEvidence(QVBoxLayout* layout) {
    m_evidenceLayout = layout;
    m_relations.setObjectName("SclMmsDataSetSession");
    connect(&m_relations, &SclMmsQuerySession::Ready, this, &SclMmsView::ApplyDataSets);
    m_objectSummary = new QLabel(m_inspector); m_objectSummary->setObjectName("SclMmsObjectSummary");
    m_objectSummary->setTextFormat(Qt::PlainText); m_objectSummary->setWordWrap(true);
    m_objectSummary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    layout->addWidget(m_objectSummary);
    auto* sources = new QGridLayout;
    const auto button = [this, sources](const QString& text, const char* name, int row, int column) {
        auto* b = new QToolButton(m_inspector); b->setText(text); b->setObjectName(name);
        b->setPopupMode(QToolButton::InstantPopup); b->setMenu(new QMenu(b));
        b->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed); b->setMinimumWidth(0);
        sources->addWidget(b,row,column); return b;
    };
    m_dataSetSource = button(QStringLiteral("数据集声明"),"SclMmsDataSetSource",0,0);
    m_instanceSource = button(QStringLiteral("实例配置"),"SclMmsInstanceSource",0,1);
    m_typeSource = button(QStringLiteral("类型定义"),"SclMmsTypeSource",1,0);
    m_composition = button(QStringLiteral("查看地址组成"),"SclMmsComposition",1,1);
    layout->addLayout(sources);
    m_objectCompact = new QToolButton(m_inspector); m_objectCompact->setObjectName("SclMmsObjectSources");
    m_objectCompact->setText(QStringLiteral("对象来源 / 数据集证据与原文"));
    m_objectCompact->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(m_objectCompact); m_objectCompact->setMenu(menu);
    auto* summary = new QWidgetAction(menu);
    m_compactSummary = new QLabel(menu); m_compactSummary->setTextFormat(Qt::PlainText);
    m_compactSummary->setWordWrap(true); m_compactSummary->setFixedWidth(360); m_compactSummary->setMargin(6);
    summary->setDefaultWidget(m_compactSummary); menu->addAction(summary);
    for (auto* b : {m_dataSetSource,m_instanceSource,m_typeSource,m_composition}) {
        b->menu()->setTitle(b->text()); menu->addMenu(b->menu());
    }
    layout->addWidget(m_objectCompact); m_objectCompact->hide();
    ResetObjectEvidence();
}

void SclMmsView::ResetObjectEvidence() {
    m_relations.Cancel(); m_relationRequest = 0; m_dataSets.reset();
    if (!m_objectSummary) return;
    m_objectSummary->clear();
    m_compactSummary->clear(); m_objectCompact->setEnabled(false);
    for (auto* b : {m_dataSetSource,m_instanceSource,m_typeSource,m_composition}) {
        b->menu()->clear(); b->setEnabled(false);
    }
}

void SclMmsView::QueryDataSets() {
    const auto* row = SelectedRow();
    if (!row || !m_snapshot || row->key.kind != SclMmsRecordKind::Address) return;
    SclMmsRequest request; request.kind = SclMmsQueryKind::DataSetRelations;
    request.key = row->key; request.identityDocument = m_snapshot->analysis.document;
    request.scope = row->addressScope;
    // A report/member entry first answers for that source dataset. Direct entry
    // scans the actual IED/AP/Server scope; the all-datasets action is explicit.
    const auto sourceMember = m_request.arrayMember != Scl::NoNode ? m_request.arrayMember : m_request.target;
    if (m_snapshot->mmsIndex.memberReferences.contains(sourceMember)) {
        const auto found = m_snapshot->mmsIndex.memberReferences.constFind(sourceMember);
        if (found != m_snapshot->mmsIndex.memberReferences.cend())
            request.target = m_snapshot->analysis.document->nodes[sourceMember].parent;
    } else if (m_request.report != Scl::NoNode) {
        const auto found = m_snapshot->mmsIndex.reports.constFind(m_request.report);
        if (found != m_snapshot->mmsIndex.reports.cend()) request.target = m_snapshot->mms.reports[*found].dataSet.target();
    }
    m_relations.SetContext(m_snapshot,request.scope);
    m_relationRequest = m_relations.Query(request);
}

void SclMmsView::ApplyDataSets() {
    const auto result = m_relations.result();
    if (!result || !m_snapshot || !isVisible() || result->requestId != m_relationRequest
        || result->document != m_snapshot->analysis.document || result->source != m_snapshot->analysis.source
        || !m_selected || result->request.key != *m_selected) return;
    m_dataSets = result; RenderObjectEvidence();
}

void SclMmsView::OpenDataSets(bool currentOnly) {
    if (!m_snapshot || !m_selected || m_selected->kind != SclMmsRecordKind::Address) return;
    const auto* row = SelectedRow(); if (!row) return;
    Location location; location.snapshot = m_snapshot; location.viewIed = m_ied;
    location.inspectorVisible = m_inspectorVisible;
    location.request.kind = SclMmsQueryKind::DataSetRelations;
    location.request.scope = row->addressScope; location.request.key = row->key;
    location.request.identityDocument = m_snapshot->analysis.document;
    if (currentOnly && m_dataSets) location.request.target = m_dataSets->request.target;
    location.parent = std::make_shared<const Location>(CurrentLocation()); location.searchOrigin = m_searchOrigin;
    Commit(std::move(location));
}

void SclMmsView::RenderObjectEvidence() {
    if (!m_detail || !m_selected || m_detail->request.key != *m_selected || !m_snapshot) return;
    const auto evidence = m_detail->objectEvidence;
    if (!evidence || evidence->document != m_snapshot->analysis.document) return;
    const auto epoch = m_epoch;
    const auto key = *m_selected;
    const auto snapshot = m_snapshot;
    const auto current = [this,epoch,key,snapshot] {
        return isVisible() && m_table->isEnabled() && m_fields->isVisible() && m_fields->isEnabled() && m_epoch == epoch
            && m_snapshot == snapshot && m_selected && *m_selected == key;
    };
    for (auto* b : {m_dataSetSource,m_instanceSource,m_typeSource,m_composition}) {
        b->menu()->clear(); b->setEnabled(true);
    }
    const auto sourceAction = [this,current](QMenu* menu, const QString& label, const SclEvidenceSource& source) {
        auto* action = menu->addAction(label + " · " + Position(source), this, [this,current,source,label] {
            if (current() && source.node != Scl::NoNode && source.precision == SclEvidencePrecision::OpeningTag)
                emit OpenEvidence(source.node,label + " · " + Position(source));
        });
        action->setEnabled(source.node != Scl::NoNode && source.precision == SclEvidencePrecision::OpeningTag);
    };
    QString origin = key.kind == SclMmsRecordKind::Member ? QStringLiteral("FCDA 文件声明 + 引用解析；地址规则组合")
        : key.kind == SclMmsRecordKind::Signal ? QStringLiteral("LN 类型展开 · 对象目录，展开属性查覆盖")
        : QStringLiteral("LN 类型展开 + MMS 规则组合");
    if (evidence->state != Scl::LinkState::Resolved) origin += " · " + SclMmsStateText(evidence->state);
    QString coverage;
    if (key.kind == SclMmsRecordKind::Address) {
        auto* all = m_dataSetSource->menu()->addAction(QStringLiteral("该属性全部数据集引用（当前 IED / AP / Server）"),this,[this,current] {
            if (current()) OpenDataSets(false);
        });
        all->setObjectName("SclMmsAllDataSets");
        if (!m_dataSets) coverage = QStringLiteral("数据集引用：后台查询中");
        else {
            const auto& c = m_dataSets->dataSetCoverage;
            const bool limited = m_dataSets->limited || !c.scanComplete || c.uncertainOccurrences > 0;
            const auto scope = m_dataSets->request.target == Scl::NoNode ? QStringLiteral("当前 IED / AP / Server") : QStringLiteral("当前来源数据集");
            if (c.observedMatches) {
                QStringList kinds;
                for (const auto& row : m_dataSets->rows) if (!kinds.contains(SclMmsReportMatchText(row.reportMatch))) kinds << SclMmsReportMatchText(row.reportMatch);
                coverage = scope + "：" + kinds.join(" / ") + QStringLiteral(" · %1%2 次 FCDA 出现")
                    .arg(limited ? QStringLiteral("至少 ") : QString()).arg(c.observedMatches);
            } else coverage = c.conclusion == SclDataSetConclusion::NotReferenced && !limited
                ? scope + QStringLiteral("：未发现数据集引用（已检索）") : scope + QStringLiteral("：尚未确定");
            if (limited) coverage += QStringLiteral("；尚有未确定关系 / 检索受限");
            if (m_dataSets->request.target != Scl::NoNode)
                m_dataSetSource->menu()->addAction(QStringLiteral("当前来源数据集关系"),this,[this,current] { if (current()) OpenDataSets(true); });
            for (const auto& row : m_dataSets->rows) if (row.dataSetRelation) {
                const auto& r = *row.dataSetRelation;
                const auto name = r.dataSet.raw + QStringLiteral(" · 原始成员 #%1 · ").arg(r.ordinal) + SclMmsReportMatchText(r.match);
                sourceAction(m_dataSetSource->menu(),name + QStringLiteral(" · FCDA 声明"),r.member);
                sourceAction(m_dataSetSource->menu(),name + QStringLiteral(" · DataSet 标签"),r.dataSet);
            }
            if (m_dataSets->rows.empty()) m_dataSetSource->menu()->addAction(QStringLiteral("无已确认数据集声明；可看实例 LN / 类型"))->setEnabled(false);
            if (m_dataSets->hasMore) m_dataSetSource->menu()->addAction(QStringLiteral("更多关系请打开分页关系表"))->setEnabled(false);
        }
    } else if (evidence->member.node != Scl::NoNode) {
        coverage = QStringLiteral("原始 FCDA 成员 · doName=%1；daName=%2")
            .arg(evidence->rawDoName,evidence->daNamePresent ? evidence->rawDaName.isEmpty() ? QStringLiteral("已声明空值") : evidence->rawDaName : QStringLiteral("未声明"));
        sourceAction(m_dataSetSource->menu(),QStringLiteral("FCDA 原始声明"),evidence->member);
        sourceAction(m_dataSetSource->menu(),QStringLiteral("DataSet 声明"),evidence->dataSet);
    } else {
        coverage = QStringLiteral("对象目录无单一 FC / 叶地址；展开属性查看数据集证据");
        m_dataSetSource->menu()->addAction(QStringLiteral("无已确认数据集声明；可看实例 LN / 类型"))->setEnabled(false);
    }
    m_objectSummary->setText(origin + " · CDC " + (evidence->cdc.isEmpty() ? QStringLiteral("未确定") : evidence->cdc)
        + (evidence->basicType.isEmpty() ? QString() : " / " + evidence->basicType) + QStringLiteral(" · 在线未验证\n") + coverage);
    m_objectSummary->setToolTip(evidence->limitation.toHtmlEscaped());
    m_compactSummary->setText(m_objectSummary->text()); m_objectCompact->setEnabled(true);
    const auto& instance = evidence->instance;
    const auto exact = instance.state == SclInstanceLocationState::Exact;
    sourceAction(m_instanceSource->menu(),exact ? QStringLiteral("精确实例配置")
        : QStringLiteral("实例回退：%1（停止段 %2）").arg(instance.reason,instance.stoppedAt),instance.target);
    m_instanceSource->setToolTip((instance.reason + " · " + Position(instance.target)).toHtmlEscaped());
    for (const auto& source : evidence->declarations)
        sourceAction(m_typeSource->menu(),QStringLiteral("类型路径 · ") + source.raw,source);
    for (const auto& source : evidence->typeLinks)
        sourceAction(m_typeSource->menu(),QStringLiteral("类型引用 / 定义 · ") + source.raw,source);
    if (m_typeSource->menu()->isEmpty()) m_typeSource->menu()->addAction(QStringLiteral("无唯一类型路径；保留声明与候选"))->setEnabled(false);
    for (const auto& value : m_detail->references) {
        if (!CanTraceSclReference(value.kind)) continue;
        auto* action = m_composition->menu()->addAction(SclReferenceLabel(value.kind) + " · " + value.value.left(100),this,[this,current,snapshot,value] {
            if (!current() || !m_tableInteraction->TraceHandler()) return;
            auto selection = std::make_shared<SclReferenceTraceSelection>();
            selection->snapshot = snapshot; selection->sourceView = m_table;
            selection->values = {value}; selection->limited = m_detail->limited;
            m_tableInteraction->TraceHandler()(std::move(selection));
        });
        action->setToolTip(value.value.toHtmlEscaped());
    }
    m_composition->setEnabled(!m_composition->menu()->isEmpty());
    emit GeometryChanged();
}
}
