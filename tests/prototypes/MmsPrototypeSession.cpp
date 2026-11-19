#include "MmsPrototypeSession.hpp"

#include <algorithm>

namespace iec61850sim::prototype {
const Record* Snapshot::Find(const QString& id) const
{
    for (const auto& record : records)
        if (record.id == id) return &record;
    return nullptr;
}

std::shared_ptr<const Snapshot> Snapshot::Demo(const QString& id)
{
    auto data = std::make_shared<Snapshot>();
    data->id = id;
    const auto line = [&](const QString& key, const QString& text) {
        data->xml.push_back(text);
        if (!key.isEmpty()) data->evidenceLines.insert(key, data->xml.size());
    };
    line({}, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    line({}, "<!-- 合成片段，仅验证查阅交互；未进行 XSD 校验 -->");
    line({}, "<SCL xmlns=\"http://www.iec.ch/61850/2003/SCL\" version=\"2007\" revision=\"B\" release=\"4\">");
    line({}, "  <Communication><SubNetwork name=\"DemoBus\">");
    line("endpoint", "    <ConnectedAP iedName=\"DemoIED\" apName=\"AP1\"><Address><P type=\"IP\">192.0.2.10</P></Address></ConnectedAP>");
    line({}, "  </SubNetwork></Communication>");
    line("server", "  <IED name=\"DemoIED\"><AccessPoint name=\"AP1\"><Server><LDevice inst=\"LD0\" ldName=\"DemoDomain\">");
    line({}, "    <LN0 lnClass=\"LLN0\" lnType=\"Zero\">");
    line("dataset", "      <DataSet name=\"Events\">");
    line("member-1", "        <FCDA ldInst=\"LD0\" lnClass=\"GGIO\" lnInst=\"1\" doName=\"Ind1\" daName=\"stVal\" fc=\"ST\"/>");
    line("member-2", "        <FCDA ldInst=\"LD0\" lnClass=\"GGIO\" lnInst=\"1\" doName=\"Ind1\" daName=\"stVal\" fc=\"ST\"/>");
    line({}, "      </DataSet>");
    line("report", "      <ReportControl name=\"brStatus\" rptID=\"DemoStatus\" datSet=\"Events\" buffered=\"true\" indexed=\"true\" confRev=\"1\" bufTime=\"20\" intgPd=\"1000\">");
    line("trigger", "        <TrgOps dchg=\"true\" qchg=\"true\" period=\"true\"/><OptFields seqNum=\"true\" dataSet=\"true\"/>");
    line("instances", "        <RptEnabled max=\"2\">");
    line("client-z", "          <ClientLN iedName=\"SinkZ\" apRef=\"Client\" ldInst=\"C\" lnClass=\"GGIO\" lnInst=\"1\"/>");
    line("client-a", "          <ClientLN iedName=\"SinkA\" apRef=\"Client\" ldInst=\"C\" lnClass=\"GGIO\" lnInst=\"1\"/>");
    line({}, "        </RptEnabled>");
    line({}, "      </ReportControl>");
    line("unknown", "      <ReportControl name=\"urUnknown\" datSet=\"Events\" buffered=\"false\" confRev=\"1\"><RptEnabled max=\"2\"/></ReportControl>");
    line("broken", "      <ReportControl name=\"broken\" datSet=\"Absent\" rptID=\"\" buffered=\"true\" confRev=\"1\"/>");
    line({}, "    </LN0><LN lnClass=\"GGIO\" inst=\"1\" lnType=\"Signals\"/>");
    line({}, "  </LDevice></Server></AccessPoint></IED>");
    line("empty", "  <IED name=\"EmptyIED\"/>");
    line({}, "  <DataTypeTemplates><LNodeType id=\"Signals\" lnClass=\"GGIO\">");
    line("signal", "    <DO name=\"Ind1\" type=\"Status\" desc=\"开关状态（合成说明）\"/>");
    line("unlisted", "    <DO name=\"Unlisted\" type=\"Status\" desc=\"未入数据集的备用状态\"/>");
    line({}, "  </LNodeType><DOType id=\"Status\" cdc=\"SPS\">");
    line("attribute", "    <DA name=\"stVal\" fc=\"ST\" bType=\"BOOLEAN\" dchg=\"true\"/>");
    line({}, "  </DOType></DataTypeTemplates>");
    line({}, "</SCL>");
    const auto fields = [](QString title, QString evidence) {
        return QList<Field>{{QStringLiteral("工作对象"), Cell(title), QStringLiteral("合成示例"), evidence}};
    };
    Record report{"report", Page::Reports, "DemoIED", "报告定义 · brStatus", "report",
        {Cell("brStatus"), Cell(QStringLiteral("开关状态上送")), Cell(QStringLiteral("缓冲报告 BR")), Cell("Events"), Cell("2"),
         Cell(QStringLiteral("配置可查阅")), Cell("DemoStatus"), Cell("DemoDomain/LLN0.BR.brStatus")}, fields("brStatus", "report")};
    report.target = "dataset";
    report.fields += QList<Field>{
        {QStringLiteral("原始 rptID"), Cell("DemoStatus"), QStringLiteral("ReportControl/@rptID"), "report"},
        {QStringLiteral("定义引用（派生）"), Cell("DemoDomain/LLN0.BR.brStatus"), QStringLiteral("定义层映射"), "report"},
        {QStringLiteral("预期实例（示意）"), Cell(QStringLiteral("brStatus01 / brStatus02")), QStringLiteral("预置推导示意；非设备目录"), "instances"},
        {QStringLiteral("数据集 / 原始成员数"), Cell("Events / 2"), QStringLiteral("DataSet，重复成员保留"), "dataset"},
        {QStringLiteral("触发配置"), Cell(QStringLiteral("数据变化、品质变化、周期")), QStringLiteral("TrgOps 原始声明"), "trigger"},
        {QStringLiteral("bufTime / intgPd"), Cell("20 ms / 1000 ms"), QStringLiteral("事件合并等待 / 完整性周期"), "report"},
        {QStringLiteral("客户端配置"), Cell("SinkZ / SinkA"), QStringLiteral("ClientLN 声明顺序；不代表已订阅"), "client-z"},
        {QStringLiteral("在线 RptID / RptEna / Owner"), Cell(QStringLiteral("离线未获取"), {}), QStringLiteral("本原型不连接设备"), {}}};
    data->records.push_back(report);
    Record unknown{"unknown", Page::Reports, "DemoIED", "报告定义 · urUnknown", "unknown",
        {Cell("urUnknown"), Cell(QStringLiteral("默认解释受限示例")), Cell(QStringLiteral("非缓冲报告 RP")), Cell("Events"), Cell("2"),
         Cell(QStringLiteral("indexed 未声明")), Cell(QStringLiteral("未声明"), {}), Cell("DemoDomain/LLN0.RP.urUnknown")}, fields("urUnknown", "unknown")};
    unknown.target = "dataset";
    unknown.fields += QList<Field>{{QStringLiteral("原始 rptID"), Cell(QStringLiteral("未声明"), {}), QStringLiteral("属性缺失"), "unknown"},
        {QStringLiteral("indexed / max"), Cell(QStringLiteral("未声明 / 2")), QStringLiteral("不生成确定实例名"), "unknown"},
        {QStringLiteral("客户端声明"), Cell(QStringLiteral("未声明；实际客户端未知"), {}), QStringLiteral("没有 ClientLN 不表示无人订阅"), "unknown"}};
    data->records.push_back(unknown);
    Record broken{"broken", Page::Reports, "DemoIED", "报告定义 · broken", "broken",
        {Cell("broken"), Cell(QStringLiteral("引用错误示例")), Cell(QStringLiteral("缓冲报告 BR")), Cell("Absent"), Cell(QStringLiteral("未知"), {}),
         Cell(QStringLiteral("数据集未找到")), Cell(QStringLiteral("空字符串"), {}), Cell("DemoDomain/LLN0.BR.broken")}, fields("broken", "broken")};
    broken.fields += QList<Field>{{QStringLiteral("原始 datSet"), Cell("Absent"), QStringLiteral("非空但无目标"), "broken"},
        {QStringLiteral("原始 rptID"), Cell(QStringLiteral("空字符串（显式声明）"), {}), QStringLiteral("与缺失分别保存"), "broken"}};
    data->records.push_back(broken);
    for (int ordinal = 1; ordinal <= 2; ++ordinal) {
        const auto key = QStringLiteral("member-%1").arg(ordinal);
        Record member{key, Page::Members, "DemoIED", QStringLiteral("成员 #%1 · Ind1.stVal").arg(ordinal), key,
            {Cell(QString::number(ordinal)), Cell("Ind1"), Cell(QStringLiteral("开关状态（模板说明）")), Cell("stVal"), Cell("ST"),
             Cell(QStringLiteral("唯一引用 · 合成")), Cell("DemoDomain/GGIO1.Ind1.stVal"), Cell("GGIO1$ST$Ind1$stVal")}, fields(key, key)};
        member.report = "dataset";
        member.target = "attribute";
        member.targetPage = Page::Attributes;
        member.fields += QList<Field>{{QStringLiteral("原始成员序号"), Cell(QString::number(ordinal)), QStringLiteral("Events 中的出现位置"), key},
            {QStringLiteral("对象说明"), Cell(QStringLiteral("开关状态（合成说明）")), QStringLiteral("模板 DO/@desc"), "signal"},
            {QStringLiteral("MMS Domain（预置派生）"), Cell("DemoDomain"), QStringLiteral("LDevice/@ldName"), "server"},
            {QStringLiteral("MMS Item（预置派生）"), Cell("GGIO1$ST$Ind1$stVal"), QStringLiteral("实例路径 + FC"), key}};
        data->records.push_back(member);
    }
    for (const auto& key : {QStringLiteral("signal"), QStringLiteral("unlisted")}) {
        const auto name = key == "signal" ? QStringLiteral("Ind1") : QStringLiteral("Unlisted");
        const auto description = key == "signal" ? QStringLiteral("开关状态（合成说明）") : QStringLiteral("未入数据集的备用状态");
        Record signal{key, Page::Signals, "DemoIED", QStringLiteral("信号对象 · ") + name, key,
            {Cell(name), Cell(description), Cell(key == "signal" ? QStringLiteral("Events · 2 次出现") : QStringLiteral("未入数据集")),
             Cell(QStringLiteral("结构入口")), Cell("DemoDomain/GGIO1." + name)}, fields(name, key)};
        signal.target = key == "signal" ? "attribute" : "unlisted-attribute";
        signal.targetPage = Page::Attributes;
        data->records.push_back(signal);
        Record attribute{signal.target, Page::Attributes, "DemoIED", name + ".stVal", "attribute",
            {Cell("stVal"), Cell(description), Cell("ST"), Cell("BOOLEAN"), Cell(QStringLiteral("未声明"), {}),
             Cell(QStringLiteral("离线地址示意")), Cell("DemoDomain"), Cell("GGIO1$ST$" + name + "$stVal")}, fields(name + ".stVal", "attribute")};
        attribute.target = key;
        attribute.targetPage = Page::Signals;
        attribute.fields += QList<Field>{{QStringLiteral("MMS Domain（预置派生）"), Cell("DemoDomain"), QStringLiteral("LDevice/@ldName"), "server"},
            {QStringLiteral("MMS Item（预置派生）"), Cell("GGIO1$ST$" + name + "$stVal"), QStringLiteral("实例 + FC + 类型路径"), "attribute"},
            {QStringLiteral("属性说明"), Cell(QStringLiteral("未声明；不借用对象说明"), {}), QStringLiteral("DA/@desc 缺失"), "attribute"},
            {QStringLiteral("文件声明值"), Cell(QStringLiteral("未声明；不是实时值"), {}), QStringLiteral("未声明 Val"), "attribute"}};
        data->records.push_back(attribute);
    }
    for (const auto& pair : {qMakePair(QStringLiteral("client-z"), QStringLiteral("SinkZ")), qMakePair(QStringLiteral("client-a"), QStringLiteral("SinkA"))}) {
        Record client{pair.first, Page::Clients, "DemoIED", QStringLiteral("客户端声明 · ") + pair.second, pair.first,
            {Cell(pair.second), Cell("ClientLN"), Cell("brStatus"), Cell(QStringLiteral("文件声明 · 非在线订阅")), Cell(pair.second),
             Cell(QStringLiteral("实例分配未核实"), {})}, fields(pair.second, pair.first)};
        client.report = "report"; client.target = "report"; client.targetPage = Page::Reports;
        client.fields += QList<Field>{{QStringLiteral("声明目标"), Cell(pair.second + "/Client/C/GGIO1"), QStringLiteral("ClientLN 原始字段"), pair.first},
            {QStringLiteral("订阅/连接状态"), Cell(QStringLiteral("离线未获取"), {}), QStringLiteral("仅声明关系"), {}}};
        data->records.push_back(client);
    }
    Record endpoint{"endpoint", Page::Connections, "DemoIED", "连接配置 · AP1", "endpoint",
        {Cell("AP1"), Cell("192.0.2.10"), Cell(QStringLiteral("未声明"), {}), Cell(QStringLiteral("文件配置 · 未连接")),
         Cell("DemoIED/AP1/Server"), Cell("DemoBus")}, fields("AP1", "endpoint")};
    endpoint.fields += QList<Field>{{QStringLiteral("IP"), Cell("192.0.2.10"), QStringLiteral("ConnectedAP/Address/P"), "endpoint"},
        {QStringLiteral("端口"), Cell(QStringLiteral("未声明"), {}), QStringLiteral("不以常见值冒充原值"), "endpoint"}};
    data->records.push_back(endpoint);
    Record issue{"issue", Page::Checks, "DemoIED", QStringLiteral("示例问题 · 数据集引用未找到"), "broken",
        {Cell("REF001"), Cell(QStringLiteral("broken 引用了不存在的数据集 Absent")), Cell(QStringLiteral("预置错误示例")), Cell("broken")}, fields("REF001", "broken")};
    issue.target = "broken"; issue.targetPage = Page::Reports;
    data->records.push_back(issue);
    data->records.push_back({"xsd", Page::Checks, {}, QStringLiteral("检查覆盖 · 完整 XSD"), {},
        {Cell("XSD"), Cell(QStringLiteral("完整 XSD 由 P7 独立交付")), Cell(QStringLiteral("未执行"), {}), Cell(QStringLiteral("不适用本原型"), {})},
        {{QStringLiteral("覆盖状态"), Cell(QStringLiteral("未执行"), {}), QStringLiteral("本原型无检查执行器"), {}}}});
    return data;
}

QString MmsPrototypeSession::PageName(Page page)
{
    switch (page) {
    case Page::Overview: return QStringLiteral("工程概览");
    case Page::Reports: return QStringLiteral("报告上送");
    case Page::Members: return QStringLiteral("数据集成员");
    case Page::Signals: return QStringLiteral("信号地址");
    case Page::Attributes: return QStringLiteral("对象属性");
    case Page::Clients: return QStringLiteral("客户端配置");
    case Page::Connections: return QStringLiteral("连接配置");
    case Page::Checks: return QStringLiteral("文件检查");
    case Page::Search: return QStringLiteral("全局搜索（整个工程）");
    case Page::Advanced: return QStringLiteral("高级字段示例");
    }
    return {};
}

MmsPrototypeSession::MmsPrototypeSession(QObject* parent) : QObject(parent) { SetSnapshot(Snapshot::Demo()); }
void MmsPrototypeSession::SetSnapshot(std::shared_ptr<const Snapshot> snapshot)
{
    if (!snapshot) return;
    m_snapshot = std::move(snapshot);
    m_location = {};
    m_location.snapshot = m_snapshot->id;
    m_history = {m_location}; m_cursor = 0;
    emit Changed(true);
}
QList<const Record*> MmsPrototypeSession::Rows() const
{
    QList<const Record*> rows;
    for (const auto& row : m_snapshot->records) {
        if (m_location.page == Page::Search) {
            if (m_location.query.isEmpty()) continue;
        } else {
            const auto page = m_location.page == Page::Advanced ? Page::Reports : m_location.page;
            if (row.page != page) continue;
            if (!m_location.ied.isEmpty() && !row.ied.isEmpty() && row.ied != m_location.ied) continue;
            if (page == Page::Members) {
                const auto* report = m_snapshot->Find(m_location.parent);
                if (!report || row.report != report->target) continue;
            }
            if (page == Page::Clients && !m_location.parent.isEmpty() && row.report != m_location.parent) continue;
            if (page == Page::Attributes && row.target != m_location.parent) continue;
        }
        QStringList values{row.title, row.ied};
        for (const auto& cell : row.cells) values << cell.display << cell.value;
        const auto text = values.join('\n');
        const auto needle = m_location.page == Page::Search ? m_location.query : m_location.filter;
        if (!text.contains(needle, Qt::CaseInsensitive)) continue;
        rows.push_back(&row);
    }
    return rows;
}
void MmsPrototypeSession::Commit(Location location, bool rowsChanged)
{
    if (location.snapshot != m_snapshot->id) return;
    m_history[m_cursor] = m_location;
    m_history = m_history.mid(0, m_cursor + 1);
    m_location = std::move(location);
    m_history.push_back(m_location); ++m_cursor;
    emit Changed(rowsChanged);
}
void MmsPrototypeSession::Navigate(Page page, const QString& parent)
{
    auto next = m_location;
    next.page = page; next.parent = parent; next.selected.clear(); next.source.clear(); next.field.clear();
    next.filter.clear(); next.topId.clear(); next.topOffset = 0; next.column = 0;
    Commit(next, true);
}
void MmsPrototypeSession::SetScope(const QString& ied)
{
    auto next = m_location;
    next.ied = ied; next.selected.clear(); next.parent.clear(); next.source.clear(); next.field.clear();
    next.filter.clear(); next.topId.clear(); next.topOffset = 0;
    if (next.page == Page::Members || next.page == Page::Attributes) next.page = Page::Reports;
    Commit(next, true);
}
void MmsPrototypeSession::SetFilter(const QString& text)
{
    m_location.filter = text; m_location.topId.clear(); m_location.topOffset = 0; m_location.source.clear();
    m_history[m_cursor] = m_location;
    emit Changed(true);
}
void MmsPrototypeSession::Select(const QString& id, int column)
{
    const auto rows = Rows();
    if (std::none_of(rows.cbegin(), rows.cend(), [&](const Record* row) { return row->id == id; })) return;
    if (m_location.selected == id) {
        m_location.column = column; m_history[m_cursor] = m_location; return;
    }
    auto next = m_location;
    next.selected = id; next.source.clear(); next.field.clear(); next.column = column;
    Commit(next, false);
}
void MmsPrototypeSession::SetField(const QString& name) { m_location.field = name; m_history[m_cursor] = m_location; }
void MmsPrototypeSession::SetAnchor(const QString& topId, int offset)
{
    m_location.topId = topId; m_location.topOffset = offset; m_history[m_cursor] = m_location;
}
void MmsPrototypeSession::Drill()
{
    const auto* row = Selected();
    if (!row) return;
    auto next = m_location;
    if (m_location.page == Page::Search) {
        next.page = row->page; next.ied = row->ied; next.selected = row->id;
        next.parent = row->page == Page::Members || row->page == Page::Clients ? row->report
            : row->page == Page::Attributes ? row->target : QString{};
    } else if (row->page == Page::Reports) {
        if (row->id == "broken") return;
        next.page = Page::Members; next.parent = row->id;
        next.selected.clear();
    } else {
        if (row->target.isEmpty()) return;
        const auto* target = m_snapshot->Find(row->target);
        if (!target) return;
        next.page = row->targetPage; next.ied = target->ied; next.selected = target->id;
        next.parent = target->page == Page::Attributes ? target->target : QString{};
    }
    next.source.clear(); next.field.clear(); next.filter.clear(); next.topId.clear(); next.topOffset = 0;
    Commit(next, true);
}
void MmsPrototypeSession::ShowClients()
{
    if (const auto* row = Selected(); row && row->page == Page::Reports) Navigate(Page::Clients, row->id);
}
void MmsPrototypeSession::OpenSource(const QString& evidence)
{
    const auto* row = Selected();
    if (!row) return;
    const auto key = evidence.isEmpty() ? row->evidence : evidence;
    if (!m_snapshot->evidenceLines.contains(key)) return;
    auto next = m_location; next.source = key;
    Commit(next, false);
}
void MmsPrototypeSession::CloseSource()
{
    if (m_location.source.isEmpty()) return;
    if (CanBack() && m_history[m_cursor - 1].selected == m_location.selected) Back();
    else { auto next = m_location; next.source.clear(); Commit(next, false); }
}
void MmsPrototypeSession::Search(const QString& text)
{
    auto next = m_location;
    next.page = Page::Search; next.query = text; next.filter.clear(); next.parent.clear(); next.selected.clear();
    next.source.clear(); next.field.clear(); next.topId.clear(); next.topOffset = 0;
    Commit(next, true);
}
void MmsPrototypeSession::Back() { if (CanBack()) { m_history[m_cursor] = m_location; m_location = m_history[--m_cursor]; emit Changed(true); } }
void MmsPrototypeSession::Forward() { if (CanForward()) { m_history[m_cursor] = m_location; m_location = m_history[++m_cursor]; emit Changed(true); } }
}
