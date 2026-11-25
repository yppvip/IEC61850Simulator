# Task10 · T10.1 共用证据与独立数据集关系

2026-11-25。**T10.1 后端门槛通过。** 执行 [Task10](Task10.md) 的 T10.1，基线为 [T10.0](Task10T10.0.md)。本阶段仅交付后端投影、查询及测试，未接入 T10.2 的页面/按钮，未进入 T10.3–T10.5。用户已确认当前程序没有生成 `Vol.ang.f`，本轮不将其作为待修复的生成缺陷。

## 1. 实现范围

新增 `SclObjectEvidence`，并在现有 `SclMmsQuerySession` 上增加 `ObjectEvidence`、`DataSetRelations` 两类请求。不另建 XML 解析器，不创建第二套异步会话机制。数据和解析依据均来自当前不可变快照中的目录、唯一类型引用、FCDA 轨迹及源码索引。

| 交付 | 主要内容 |
| --- | --- |
| 对象证据 | 同快照文档身份/指纹、IED/AP/Server/LD/LN、DO/SDO 与 DA/BDA 分界、当前对象类型及 CDC、各级对象 CDC、叶基本类型、有效 FC 依据、数组选择和 alternate access |
| 来源与组成 | 文件声明、引用解析、类型展开、规则组合分别标记；根值与原完整复制投影一致；公式、组成部分和辅助依据分开，缺省属性与显式空属性保留区别 |
| 类型依据 | 源 `lnType/type` 与目标 `id` 的配对，解析状态和有界候选；缺失/歧义保持失败状态，不取首项 |
| 实例定位 | 精确实例、最深已匹配祖先、歧义、数组待选、定值组或不可定位；保留停止段、原因、已匹配链和候选，不用模板节点替代实例 |
| 成员证据 | 原始 doName/daName/fc/ix、daName 是否声明、原 FCDA 和 DataSet 的可信锚点；对象级成员仍停在对象层，不扩成虚构叶成员 |
| 独立数据集关系 | 一次成员出现一行，保留 DataSet、FCDA NodeId/序号、原始声明、精确/结构/对象覆盖、源范围及目标 LN；不要求有报告使用方 |
| 使用方 | ReportControl、GSEControl、SampledValueControl 单独分型；共享数据集的多个控制块不复制成员行，候选控制引用保留原解析状态，不暗示已收发 |

普通信号对象请求提供对象级元数据和实例依据，不给无 FC 的目录对象强造单一标量 Item。属性及结构地址请求包含其已证实的有效 FC。数据集关系请求以完整地址身份发起；目录对象整体跨 FC 汇总不等于某个叶属性的资格判断，后续 UI 应分别呈现。

## 2. 来源值和原文约定

`SclEvidenceSource` 携带节点、属性、原始值、是否存在、源码位置及定位精度。行号/字节位置直接使用本快照的可信元素开始标签锚点；属性跨行仍只标开始标签，未计算虚假的属性精确偏移。无可靠源码时标不可用。

`SclEvidenceValue` 保留根完整值、映射规则、公式和有序片段。类型链中的 DO/SDO/DA/BDA 名称与 `lnType/type → id` 引用分别给出。直接成员引用以 FCDA 原声明为组成输入，模板链为解析支持；按类型展开的独立属性以类型路径为组成输入，不虚构唯一 DataSet 父项。数组的 alternate access 组件与 Item 本身分开，数组后部路径不写成实际 Item 的后缀。

规则版本独立保存：新增证据投影为 `object-evidence-0.1`，基础解析与 MMS 映射仍分别使用快照原有版本。未改变原始 rptID、报告实例映射或 Task8 的完整复制值，也不把离线依据当在线结果或完整标准符合性证明。

实例定位只沿已确认模板链在同一 LN 下匹配 DOI/SDI/DAI。缺少某一段时立即停止，不搜索后代“近似名称”，不跳到另一对象。数组必须匹配轴和选择；同名属性跨 FC 在实例层不能区分时保留歧义。候选最多预览 64 项并标明截断。定位到 DAI 不代表选定某个 `Val/sGroup`，本投影不代选文件值。

## 3. 数据集范围与完整性

复用现有 `membersByLogicalNode` 语义，并在后台索引中增加两组信息：可识别 LN 的全部 FCDA（包含解析失败的声明），以及尚未识别 LN 的失败声明按所属 Server 分桶。控制块到数据集索引由已有 `ControlDataSet` 引用构建，保留唯一及候选解析状态。

报告反查与新数据集反查共用 `MatchSclDataSetMember`：核验 LN 实例、FC、逐段模板身份和数组轴/下标，避免两个入口各自拼名称或用字符串前缀升级覆盖关系。`PhV` 不匹配 `PhV2`，一个数组元素不匹配另一元素。无报告、仅 GOOSE/SV 使用或完全无使用方的数据集都能保留。

查询可通过 `target` 限定某个同快照 DataSet，否则查当前范围内该属性的全部适用成员。源 IED/AP/Server 与目标 LN 分别交付；ServerAt 别名沿用现有访问路径解释。无效 DataSet 身份、范围不匹配、数组未选或属性未解析时，不给“未引用”结论。

判断针对所查询层级的精确声明或上级覆盖。单独列出某个后代叶属性，不表示整个结构已经作为成员入集；不能把结构层级“未发现完整覆盖”写成“所有后代都未被使用”。后续对象目录的跨 FC/部分使用汇总应保留这个区别。

| 字段 | 含义 |
| --- | --- |
| `observedMatches` | 当前范围中已确认的成员出现次数；独立于文本筛选和单页条数 |
| `uncertainOccurrences / uncertainSources` | 可能影响该目标关系判断的未解析声明数及最多 64 项原文预览 |
| `scanComplete` | 成员范围检索是否完成，不等于每条引用都已解析，也不等于使用方无截断 |
| `Referenced` | 已确认至少一条精确或覆盖关系；仍可同时存在未确定关系/预算限制 |
| `NotReferenced` | 相关范围检索完整、无命中，且没有可能影响结论的未解析声明 |
| `Uncertain` | 没有确定命中且上下文、解析或预算不足，不能断言没有数据集 |

对失败声明，仅用已证实的不同 Server、LD、LN、FC、模板分支排除不相关项；其余保守保留。缺少报告使用不能作为负结论。文本筛选隐藏全部关系时，`observedMatches` 仍保留未筛选的关系事实，不将其改成未引用。

单页最多 200 行，整次请求最多 100,000 工作单元。引用/使用方索引分别有 500,000 条预算，使用方最多交付 512 条且保留观察数与完整性。相同 DataSet 的使用方在单次查询内共享，查询缓存最多 200 项，不按重复成员或报告实例复制。预算中断时只交付已观察的关系并明确受限，完整对象投影未完成则不发布部分对象证据。

## 4. 请求身份与调用方式

两类新请求必须携带 `identityDocument`，且与当前快照的文档对象一致。仅有碰巧相同的 NodeId/行号不构成身份。属性还按完整记录、字段、FC、声明、数组选择重新定位，成员核验原始序号，范围仍走现有作用域检查。

```cpp
SclMmsRequest request;
request.kind = SclMmsQueryKind::ObjectEvidence; // 或 DataSetRelations
request.identityDocument = snapshot->analysis.document;
request.key = deliveredRow.key;
request.scope = deliveredRow.addressScope;
session.SetContext(snapshot, request.scope);
session.Query(request);
```

列表、详情、数据集关系和来源查阅应分别使用独立会话。每个会话沿用一个工作任务加一个最新待处理请求；切文档/范围先 `SetContext`，隐藏/关闭 `Cancel`。交付仍核验快照对象、范围、独立代次。T10.2 的视图还需核验原始命中及模型代次，不应只保存根字符串或显示行号。

本轮没有向现有 GUI 表格自动附加逐行全量反查；新投影按显式查询读取，避免先给每个地址算全部关系后才接 UI。代码位于 `src/core/SclObjectEvidence.*` 和 `SclMmsQuery.*`，只使用 Qt Core/现有应用服务依赖，不增加 Widgets、浏览器、网络或独立程序依赖。

## 5. 验证记录

独立构建为 `build/task10-t101/`，工具链、选项及清洁 PATH 与 T10.0 相同。未回编或覆盖 `build/task10-t100/frozen-baseline/`。常规测试新增 `SclObjectEvidenceTest`，使用自建 `Task10Evidence.scd` 及测试内变体；真实样本不进入仓库或默认 CTest。

清洁 PATH 完整 CTest **17/17 通过，33.23 秒**（`CTestVerified.log`）。其中原有 16 项和新增领域测试均通过。新测试含 10 个用例，连同初始化/清理为 12 个通过项（`ObjectEvidenceFinal.log`），追加的“确定命中与未确定关系并存”断言也已通过。

首轮完整回归发现新增合成夹具未登记到 `Manifest.json`。已补齐新样本的文件名、良构状态和固定元素计数，并更新样本说明，未放宽清单检查。常规测试覆盖：MV/CMV 路径与根值一致性、类型/实例来源、精确/对象/结构覆盖、重复出现与共享使用方、无报告及 GOOSE/SV 数据集、空/缺省 daName、范围与别名、缺失/歧义类型、数组与越界、预算、候选预览、取消和独立会话。

真实样本使用 T10.0 相同的两个路径及指纹，经 `EvidenceDetails` 显式启用新后台查询，在 Windows 原生 Qt、1280×800、DPR=1 独立运行。日志、字段与截图仅保留于 `build/task10-t101/t101-evidence/`。所有已查目标的新投影根 Item 与现有完整值相同，没有预算截断或未确定成员。

| 本地查阅对象 | 新实例定位结果 | 独立数据集关系 |
| --- | --- | --- |
| SCD `PhV.phsA.cVal.mag.f` | 精确 DAI，第 7174 行 | 第 2789 行，精确属性声明 |
| SCD `PhV.phsA.cVal.ang.f` | 精确 DAI，第 7177 行 | 第 2790 行，精确属性声明 |
| SCD `PhV.res.cVal.mag.f` | 精确 DAI，第 7213 行 | 第 2795 行，精确属性声明 |
| SCD `PhV.res.cVal.ang.f` | 缺少 ang 段，回退 SDI cVal，第 7211 行 | 当前范围检索完整，未发现精确/覆盖关系 |
| ICD `PhV.phsA.cVal.mag.f` | 精确 DAI，第 5703 行 | 第 1435 行 DO 级覆盖；没有原始 daName |
| ICD `PhV.phsA.cVal.ang.f` | 缺少 ang 段，回退 SDI cVal，第 5701 行 | 同一第 1435 行 DO 级覆盖 |
| ICD `PhV.res` 的两叶属性 | 缺少 res 段，回退 DOI PhV，第 5699 行 | 当前范围检索完整，未发现精确/覆盖关系 |
| ICD `MMXN1.Vol.mag.f` | 精确 DAI，第 6611 行；CDC=MV | 第 1442 行 DO 级覆盖；完整属性查询仍无 ang.f |

两个文件读前后 SHA-256 与 T10.0 一致。上述为定向、只读、程序驱动核对，不是对整个工程进行完整标准校验，也不等于 T10.2 新 UI 已验收。

复测命令：

```powershell
& ./tests/integration/MeasureSclTask10Baseline.ps1 `
  -BuildDirectory build/task10-t101 `
  -ScdPath 'D:/1Document/SCD/实验室20190101.scd' `
  -IcdPath 'D:/1Document/SCD/PAC-813GA-GCN-V2.02-C09BAE31.icd' `
  -Rounds 1 -EvidenceDetails -CaptureScreenshots -OutputName t101-evidence
```

## 6. G5 返回可见性的定位证据

仅在测试探针记录原文前后 `CurrentLocation`、滚动条、视口高度、前十行高度及 `sectionResized` 时序；未改 `src/gui/`。两个样本的相角页均确认相同快照、相同选择、相同顶部原始记录和行内偏移。问题不来自跨快照 NodeId、旧范围或父报告被替换。

| 样本 / 相角 | 打开原文前 | 返回后 |
| --- | --- | --- |
| SCD | 顶部 `cVal.mag`、偏移 0，滚动值 30；当前行 Y=90，视口高 144 | 顶部与偏移仍相同，滚动值 30；当前行 Y=252，移出视口 |
| ICD | 顶部 `cVal`、偏移 6，滚动值 6；当前行 Y=114，视口高 144 | 顶部与偏移仍相同，滚动值 6；当前行 Y=276，移出视口 |

事件记录显示返回过程中前方多行由 30 px 重新测量为 84 px，约 10–11 ms 内发生；3 行各增加 54 px，正好解释当前行 Y 增加 162 px。`SclMmsView` 恢复顶部原始记录/偏移，`SclTableInteraction` 的按需测高随后改变前方几何，两者共同保住顶部锚点，却没有确保所选行最终可见。这是基于事件和代码路径的定位结论，不是仅凭截图猜测。

T10.2 应在同视图代次内协调延迟测高、锚点与所选身份的最终可见性，继续遵守可见行按需测高；不能改用固定行高或全表 `resizeRowsToContents()`，也不能重建另一套后退栈。需分别验证原文往返、短窄窗、非零行内锚点和所有祖先裁剪区。本阶段**未修复，仍记为未通过**。

## 7. 性能与阶段边界

复用同机 T10.0 冻结程序与当前程序作交替对照：每文件、每程序各 10 个全新进程，共 40 次。奇数轮 current→baseline，偶数轮 baseline→current；同一文件、工具链、Windows 原生平台、1280×800、DPR=1、隔离 QSettings 和清洁 PATH。不并行运行其他本任务测试。

正式采集关闭额外证据与截图，避免把新增诊断开销混入旧路径比较。P95 继续采用最近秩 `ceil(0.95×N)`；10 轮 Ready 的 P95 即该组最大值，不称为冷启动。内存为完整探针路径的进程峰值，非单纯解析模型占用。

| 指标 | SCD：T10.0 → T10.1 | 变化 | ICD：T10.0 → T10.1 | 变化 |
| --- | --- | --- | --- | --- |
| Ready 中位，ms | 1393 → 1424.5 | +2.26% | 115.5 → 116.5 | +0.87% |
| Ready P95，ms | 1504 → 1684 | +11.97% | 125 → 131 | +4.80% |
| 峰值工作集中位，字节 | 202,862,592 → 203,130,880 | +0.13% | 134,508,544 → 134,559,744 | +0.04% |
| 峰值工作集最大，字节 | 205,217,792 → 205,344,768 | +0.06% | 136,093,696 → 136,716,288 | +0.46% |
| 详情交付 P95，ms | 15.598 → 15.317 | -1.80% | 14.172 → 14.758 | +4.13% |
| 返回身份 P95，ms | 246.268 → 233.562 | -5.16% | 233.942 → 230.180 | -1.61% |
| 返回不可见次数 | 10/40 → 10/40 | 保持失败 | 10/50 → 10/50 | 保持失败 |

已测旧路径的加载、峰值工作集和详情交付满足 +15% 增量门槛；**返回身份的较快耗时不证明可见导航通过**。两程序的 A 相相角不可见均各 10/10 轮，保持 G5 的已知失败。新投影尚未接 UI，这组数据不代表 T10.2 新页面开销，也不是 Task3 性能归因或历史未通过项的替代结论。

逐轮 JSON 和日志保存于 `build/task10-t101/t101-comparison/baseline/`、`current/`，分别由原汇总脚本生成 `Summary.json`；变化率为 `(current / baseline - 1) × 100%`，汇总见 `t101-comparison/Comparison.json`。测量脚本同时核对两程序和源文件的读前/读后指纹，没有改写冻结目录。

```powershell
& ./tests/integration/MeasureSclTask10Baseline.ps1 `
  -BuildDirectory build/task10-t101 `
  -BaselineDirectory build/task10-t100/frozen-baseline `
  -ScdPath 'D:/1Document/SCD/实验室20190101.scd' `
  -IcdPath 'D:/1Document/SCD/PAC-813GA-GCN-V2.02-C09BAE31.icd' `
  -Rounds 10 -OutputName t101-comparison
& D:/msys64/ucrt64/bin/python.exe tests/integration/SummarizeSclTask10Baseline.py `
  build/task10-t101/t101-comparison/baseline
& D:/msys64/ucrt64/bin/python.exe tests/integration/SummarizeSclTask10Baseline.py `
  build/task10-t101/t101-comparison/current
```

T10.2 的三入口、字段/列表列名与默认说明尚未接入；T10.4 的粘贴校核尚未实现。独立人工及初学者验收、完整 XSD/CDC 合规性、历史性能缺口均保持原边界。无新第三方依赖，Redis 配置/迁移、ADS 布局不变。
