# SCL 首批规则目录 v0.2

日期为 2026-11-06。P2 启用了下文列出的项目规则，覆盖 XML 导入和部分文件内部一致性。

本目录固定规则 ID 与诊断边界，供 P1/P2 实施。XML 检查由 P1 准备，引用规则由 P2 实施，工程提示可在后续阶段接入；XSD 留在 P7。每条启用的规则必须补充版本/条款依据、适用配置、正反例及例外，未核验的版本不输出“标准要求”的断言。基础内部一致性可独立执行，但必须标明依据为项目规则。

## 执行契约

- 文件类型入口：ICD/CID/SCD；CCD 只有识别到明确方言后才应用对应规则，不识别时止于 XML003。不按扩展名推断精确版本。
- 版本线索：根属性、声明及已确认配置；不把 Header 文档版本当作标准版本。版本未知时输出 VER001，结构必需项按保守子集检查；版本相关规则标记未执行。
- 同名节点必须按 namespace URI 识别；扩展节点不混入标准对象索引。对于标准命名空间中的未知元素也应保留并报告覆盖不足，不自动接受为已支持对象。
- 每条诊断至少有 ruleId、规则集版本、严重度、文档指纹、NodeId、对象路径、原文位置、发现原因、检查建议和适用范围。未执行/不适用/受前置失败阻断与“执行后无问题”分别计数。
- 发生 XML 错误不继续输出完整模型或语义“成功”；缺少依赖类型时抑制由它引发的大量重复引用错误，保留根因和被影响数量。
- 作用域未知或存在多个候选时返回歧义，不取第一个候选。结构可解释性错误不等于现场设备一定不能运行。

## 规则定义

| ID | 检查与适用范围 | 级别、前置及例外 | 已准备样本 / 后续补充 |
|---|---|---|---|
| XML001 | 所有输入的可读性、空文件 | 错误；打开失败或零字节停止分析；文件访问失败与空文件使用不同原因码 | Empty.scd；后补无权限/文件消失 |
| XML002 | XML 格式与编码解码 | 错误；良构性失败阻断语义；行列可用时定位原文 | Truncated.scd；后补编码失败/多行标签/CDATA |
| XML003 | 根 SCL 与受支持命名空间/方言 | 错误；后缀正确仍可能不支持；不能把未知 CCD 当标准 SCD | UnknownDialect.ccd；后补无命名空间/错误后缀 |
| XML004 | DTD/外部实体及导入资源上限 | 错误并停止；不跟随 schemaLocation；记录具体超限原因 | 后补 DTD、外部实体、深度/节点/大小边界；阈值在 P1 固定 |
| STR001 | 可索引对象的关键标识（IED name、LD inst、LN 组合、DataSet name 等） | 已确认必需项缺失为错误；LN0/LLN0 特殊实例命名和可选 prefix/inst 不一概要求非空；精确集合需版本核验 | 后补缺失命名属性与缺省合法反例 |
| KEY001 | 合法作用域内重复 IED、LD/LN、DataSet/控制块、同类模板 id | 产生歧义为错误；跨 IED、跨合法作用域或不同模板类别不按全局名字粗暴去重；保留所有定义 | DuplicateDataSet.scd；后补跨 IED/AP/模板类别同名 |
| TYP001 | lnType 与适用模板种类引用不存在 | 错误；模板缺失为根因，保留受影响对象；前向定义合法 | MissingType.scd / Minimal.scd（前向反例） |
| TYP002 | DO/SDO/DA/BDA/Enum 的 type 与种类不匹配或不存在 | 错误；并非所有 bType 必须带 type；只有结构/枚举等适用类型才查对应模板 | 后补嵌套、结构和枚举缺失/正确样本 |
| TYP003 | 类型引用环或展开深度超过上限 | 错误并停止该分支展开，按类型图检测；多个实例共用同一模板合法，不误判为循环 | 后补循环、共享 DAG、深度边界 |
| REF001 | RCB/GSE/SV 的非空 datSet 无法解析 | 明确作用域下为错误；空/缺省是否允许需按能力/版本单独判断，不强制所有控制块静态绑定 | DanglingDataSet.scd / Minimal.scd |
| REF002 | FCDA 的 LD/LN、DO/SDO、DA/BDA、FC、数组索引不能解析 | 错误或覆盖不足；允许 DO 级引用缺 daName；不依赖 DOI/DAI 是否出现；相同成员保留原始序号 | Minimal.scd 含缺 daName、重复成员；后补 FC/数组/跨 LD |
| REF003 | ConnectedAP、GSE/SMV 对 AP/控制块的关联失效 | 文件覆盖范围明确时错误；候选不唯一报告歧义；不能把同名多 AP 合并 | Minimal.scd 正向；后补错误名称、多 AP、冗余网络 |
| REF004 | ExtRef 源定位或服务类型关联失败 | 区分未绑定、部分绑定、文件外来源与确实悬空；按版本/范围定级，不统一报错 | 后补绑定/未绑定/文件外/歧义样本 |
| ENG001 | 空数据集 | 默认警告；未使用或工程中预留可能合理，不归为 XML 错误 | DuplicateDataSet.scd |
| ENG002 | 未被使用的定义 | 提示；仅针对当前文件内可解析引用，不推断站外使用情况 | 后补使用/未使用对照 |
| ENG003 | IP/MAC/VLAN/APPID 值域或疑似冲突 | 默认警告；区分 MMS、GOOSE、SV，按网络及冗余配置定作用域，不把 APPID 当 MMS ID | Minimal.scd 正向；后补值域、冗余和冲突 |
| ENG004 | 报告标识疑似冲突 | 默认警告；先区分 RCB 定义/实例和适用作用域，缺省 rptID 不直接报错 | Minimal.scd 缺省反例；后补跨 IED 同名和同域冲突 |
| VER001 | 标准版本识别不足 | 提示并列明受影响规则；Header version 不代替根属性；不猜测或自动下载 schema | UnknownVersion.scd / 本地真实 SCD |
| EXT001 | 扩展或未知节点未覆盖 | 提示，保留原文和来源；可按命名空间/类型汇总，不能逐个重复淹没问题表 | NamespaceCollision.scd；后补标准命名空间未知元素 |

## 验证状态与实施顺序

P0 仅验证了样本清单、命名空间敏感的计数和 XML 良构性预期。没有执行上述语义规则，Manifest 的 plannedRules 只列预期方向。

P1 的安全、编码和原文映射测试见 [P1 验收记录](Task1P1.md)。

P2 的 `ReferenceResolver` 执行 XML001～004、STR001、KEY001、TYP001～003、REF001～004、ENG001、VER001 和 EXT001 的项目规则子集。规则区分已执行、不适用、前置失败及尚未实现。已执行的规则单列覆盖受限的个案。ENG002～004 和完整 XSD 标记为尚未实现。规则范围、明确的作用域假设和正反例见 [P2 验收记录](Task1P2.md)。

## Task6 增量：MMS 投影规则集 mms-0.1

2026-11-19 的 [T6.2](Task6T6.2.md) 已在纯 Qt Core 的 `MmsProjector::Build` 实施下表。**它尚未接入正式快照服务、检查页或导出**；上文基础解析器的 `0.2` 覆盖状态不变。调用方后续须同时保留两份规则集身份及覆盖，不能只因增量实现而把旧解析器的 ENG004 标成已执行。

投影与诊断共同持有源文档、SourceIndex、SHA-256 和规则集版本；Finding 的节点、路径、元素位置及字段证据均属于该快照。虚拟实例只有 `(ReportControl NodeId, ordinal)`，其原文证据仍是定义/RptEnabled 节点。全局记录预算失败返回 Blocked 且不发布部分记录；ServerAt 路径预算受限保留声明及分支 Blocked，并置 `resourceLimited`。取消抛出 TaskCancelled，不交付结果。

| ID | 本阶段已执行范围 | 依据、级别及限制 |
| --- | --- | --- |
| MMS001 | 显式 buffered/indexed、TrgOps/OptFields 已识别布尔字段；confRev/bufTime/intgPd、RptEnabled/max 及 ConfReportControl/DynAssociation 的显式 max 整数词法；重复参数子元素 | 项目词法检查，警告。接受 XML boolean true/false/1/0；解释时去首尾空白，原值不改。数字解释为 0..4294967295；缺失/空/非法分别记录，不应用缺省值。这里不是全部 SCL 属性或完整 XSD 校验；能力 max 的词法成功不表示能力适用性已确认。bufTime/intgPd 的展示单位为 ms。 |
| MMS002 | 根属性恰为 2007/B/4，唯一模型/LD 域/LN/报告名、显式有效 buffered、indexed=true、单个 RptEnabled 且显式 max=1..99 时生成两位后缀预期实例；已知索引档案的 max 越界提示 | 沿用 [T6.0 准入档案](Task6T6.0.md)。两位索引依据 [IEC Tissue 243](https://iec61850.tissue-db.com/tissues/243)，ClientLN 位置依据 [Tissue 198](https://iec61850.tissue-db.com/tissues/198)。只启用已确认的索引子集，不照搬历史提案中的非索引描述。未知版本、缺省字段、重复 RptEnabled、非索引及关联专用 URCB均不生成确定实例。名称含未支持字符时受限。 |
| MMS003 | ClientLN 按 IED/AP/Server/LD/LN 定位，保留外部、未绑定、歧义及阻断；重复客户端、已知索引配置的列表位置超过 max 提示；Association 独立解析客户端 LN；Report ExtRef 复用已有对象/控制块证据 | 项目作用域检查，明确缺项/文件内悬空/歧义/重复/分配溢出为警告；外部或纯客户端 AP 未覆盖保留限制。ClientLN 原始位置从 1 开始；前一项无法解析也不将后一项移到 01。位置分配与目标解析分开。Association 不绑定报告实例。缺 serviceType 不计为 Report；显式源冲突不回退成员猜测，不重复 REF004 诊断。 |
| MMS004 | ServerAt 链、环、目标歧义及路径预算；ConnectedAP 只消费现有 REF003 的 AP 引用；只检查直接 Address/P 中 IP/IP-SUBNET/IP-GATEWAY 的 IPv4 格式及掩码连续性 | 项目检查，警告。保留多端点、重复 P、原始 OSI/selector/未知类型；IPv6 和其他类型值域受限。不读取 GSE/SMV Address，不填端口。不因缺 Communication 或纯客户端 AP 无 Server 而报工程缺失；不重复 REF003 根因。 |
| ENG004（增量子集） | 已确认预期实例地址按 Server 比较；可唯一定位的不同报告定义在同一 Server 重复声明非空原始 rptID 时给“疑似重复”提示 | 默认警告，**不是协议唯一性或在线订阅错误的证明**。同定义的多实例只检查一次原始 rptID；跨 Server/AP/IED 复用不报；空/缺失 rptID 不报；不确定实例地址不参与地址冲突。非索引关联语境不纳入确认比较。 |
| MMS005 | 汇总报告未解释字段、未知标准子元素/扩展属性、参数缺省及原始能力声明的覆盖限制 | 覆盖计数，不制造每条默认值的错误。Services 的 IED/AP 原始归属与全部直接声明保留；不选择全工程第一条或臆定优先级。原始字段仍可凭节点查阅。 |
| MMS006 | **NotImplemented**：能力适用性/覆盖优先级、能力与配置兼容性、跨版本参数组合、默认 RptID、非索引关联规则及其他 Services 的语义校验 | 不根据能力上限推断实际实例或连接数量；不强制 intgPd 非零必须 period=true。未知/缺省保持限制。[Tissue 1771](https://iec61850.tissue-db.com/tissues/1771?workflow=show) 说明后续版本中数量与索引及关联的区别，本档案未启用其全部条件。 |
| XSD | **NotImplemented** | 完整 XSD 仍归 Task1 P7，无在线 schema 下载或校验。 |

缺省/原始字段的状态至少区分 Missing、Empty、Explicit、Invalid、Limited；Derived 只用于有证据的派生映射。自定义 rptID 不追加实例后缀；缺失/空 rptID 即使能推导实例也不给伪造的有效默认值。报告定义映射与实例分别存储，数据集 NodeId、原始 FCDA 数量共享，不按实例复制成员。FCDA 类型/数组/FC 仍由 REF002 提供，不另写解析器。

正反例由 `Task6Mms.scd`、`Task6MmsLimited.scd` 与 `SclMmsProjectionTest` 的自建内存变体覆盖，详见阶段记录。新规则本阶段仅供领域调用；信号地址按需展开、查询/搜索、导出与正式 UI 接入依 T6.3/T6.4 门槛后实施。
