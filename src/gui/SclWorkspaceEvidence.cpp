#include "SclWorkspace.hpp"
#include <QCheckBox>
#include <QStackedWidget>
#include <QTabWidget>
#include <DockWidget.h>
#include <QTextBrowser>
#include <QScrollBar>

namespace iec61850sim {
void SclWorkspace::QueryAdvancedEvidence()
{
    m_advancedEvidence->Cancel(); m_advancedEvidenceId = 0;
    if (!m_snapshot || m_selected == Scl::NoNode || !isVisible() || m_tableModes->currentIndex() != 0
        || m_inspectorDock->isClosed() || m_inspector->currentWidget() != m_properties) return;
    if (!m_snapshot->browse.objects.contains(m_selected)) return;
    SclMmsScope scope; scope.ied = m_ied;
    if (m_allTemplates->isChecked() && m_snapshot->browse.iedOwners[m_selected] == Scl::NoNode) scope.ied = Scl::NoNode;
    m_advancedEvidence->SetContext(m_snapshot,scope);
    SclMmsRequest request; request.kind = SclMmsQueryKind::NodeEvidence; request.target = m_selected;
    request.identityDocument = m_snapshot->analysis.document;
    m_advancedEvidenceId = m_advancedEvidence->Query(request);
}

void SclWorkspace::ApplyAdvancedEvidence()
{
    const auto result = m_advancedEvidence->result();
    if (!result || !m_snapshot || !isVisible() || m_tableModes->currentIndex() != 0
        || result->requestId != m_advancedEvidenceId || result->document != m_snapshot->analysis.document
        || result->request.target != m_selected) return;
    QString html = QStringLiteral("<h3>对象结构与来源（离线）</h3>");
    const auto field = [&html](const QString& label, const QString& value) {
        html += QStringLiteral("<p><b>%1</b>：%2</p>").arg(label.toHtmlEscaped(),value.toHtmlEscaped());
    };
    if (const auto e = result->objectEvidence) {
        field(QStringLiteral("解析状态"),SclMmsStateText(e->state));
        field(QStringLiteral("DO / SDO 路径"),e->objectPath);
        field(QStringLiteral("DA / BDA 路径"),e->attributePath);
        field(QStringLiteral("所属对象 CDC"),e->cdc.isEmpty() ? QStringLiteral("尚未确定 / 无实例上下文") : e->cdc);
        field(QStringLiteral("DOType"),e->objectType == Scl::NoNode ? QString() : m_snapshot->analysis.document->attribute(e->objectType,"id"));
        field(QStringLiteral("基本类型 / FC"),e->basicType + " / " + e->fc);
        int remainingSources = 64;
        const auto link = [this,&html,&remainingSources](const QString& label, const SclEvidenceSource& source) {
            if (source.node == Scl::NoNode || source.precision != SclEvidencePrecision::OpeningTag) return;
            if (remainingSources-- <= 0) return;
            const auto element = m_snapshot->analysis.document->nodes[source.node].name;
            html += QStringLiteral("<p><a href='scl-evidence://request-%1/%2'>%3 · %4%5 · 标签起始行 %6</a></p>")
                .arg(m_advancedEvidenceId).arg(source.node).arg(label.toHtmlEscaped(),element.toHtmlEscaped(),
                    source.attribute.isEmpty() ? QString() : '/' + source.attribute.toHtmlEscaped()).arg(source.location.line);
        };
        if (e->member.node != Scl::NoNode) {
            field(QStringLiteral("原始 doName"),e->rawDoName);
            field(QStringLiteral("原始 daName"),!e->daNamePresent ? QStringLiteral("未声明") : e->rawDaName.isEmpty() ? QStringLiteral("已声明空值") : e->rawDaName);
            field(QStringLiteral("成员层级 / 覆盖"),SclMemberLevel(*m_snapshot->analysis.document,
                e->declarations.isEmpty() ? Scl::NoNode : e->declarations.back().node));
            field(QStringLiteral("证据边界"),QStringLiteral("文件 FCDA 声明 + 引用解析；展开叶不会增加成员。精确 / 覆盖结论需对具体属性核对 FC 与数组。"));
            link(QStringLiteral("数据集声明"),e->dataSet); link(QStringLiteral("原始成员声明"),e->member);
        } else field(QStringLiteral("数据集证据"),QStringLiteral("对象 / 模板结构不代表数据集清单；到属性页查看独立数据集引用与覆盖。"));
        field(QStringLiteral("实例配置"),e->instance.state == SclInstanceLocationState::Exact ? QStringLiteral("精确实例配置") : e->instance.reason);
        link(e->instance.state == SclInstanceLocationState::Exact ? QStringLiteral("实例配置") : QStringLiteral("实例回退（最深已证实祖先）"),e->instance.target);
        for (const auto& source : e->declarations) link(QStringLiteral("类型声明"),source);
        for (const auto& source : e->typeLinks) link(QStringLiteral("类型引用 / 定义"),source);
        link(QStringLiteral("CDC 依据"),e->cdcEvidence);
        if (remainingSources < 0) field(QStringLiteral("来源预览受限"),QStringLiteral("最多展示 64 个原文入口；可沿类型定义继续查阅。"));
        if (!e->limitation.isEmpty()) field(QStringLiteral("限制"),e->limitation);
    }
    if (result->limited) field(QStringLiteral("资源限制"),QStringLiteral("查询未完成；不能判定不存在或未引用。") + result->limitation);
    if (IsSclMmsAttributeTarget(*m_snapshot,m_selected))
        html += QStringLiteral("<p><a href='scl-attributes://request-%1/%2'>到任务视图查阅属性与数据集（当前 IED 范围；复用候选分别保留）</a></p>")
            .arg(m_advancedEvidenceId).arg(m_selected);
    field(QStringLiteral("覆盖边界"),QStringLiteral("在线未验证；完整 XSD 与完整标准符合性未执行。"));
    const auto scroll = m_properties->verticalScrollBar()->value();
    m_properties->setHtml(html + m_propertyHtml);
    m_properties->verticalScrollBar()->setValue(scroll);
}
}
