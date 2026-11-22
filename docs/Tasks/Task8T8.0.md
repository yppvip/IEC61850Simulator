# Task8 · T8.0 现状基线与术语

2026-11-21。T8.0 已通过。范围为记录现状、冻结表格与菜单矩阵、验证合成正反例。后续功能以 [Task8](Task8.md) 为准。

## 术语和文件边界

| 术语 | 本阶段口径 |
| --- | --- |
| 当前幅值、相位、品质、时间 | 设备运行中的量，由实际 MMS 读取或报告确认。当前工作台的地址和模板证据来自文件。 |
| 定值 | 具体装置声明的设定量。按实际对象、FC 及可用定值组证据查阅。 |
| 文件原值 | SCD 里的实例 `Val` 是工程文件记录。它可以表示配置/预置值，具体用途由字段和声明语境给出。 |
| 保护量 | 测量字段与保护输入的对应需要回路或明确关联证据。测量对象、保护 LN 同处一个 IED 只提供查阅线索。 |
| “赋值相位” | 按「幅值和相位」建立本阶段用例，另用同一文件里的独立相位定值验证区分。 |

## 合成样本与现有路径

样本 [Task8Voltage.scd](../../tests/fixtures/scl/Task8Voltage.scd) 为自建 SCD，用于离线功能回归。XML/对象与引用可由正式分析服务处理，完整 XSD 验收属于 Task1 P7。

| IED 与对象 | 预置证据 | 当前查阅基线 |
| --- | --- | --- |
| `DemoListed / MMXU1.PhV.phsA` | 显式 `ldName=ListedMeasurements`，幅值、相位、品质、时间都有声明。报告数据集含幅值 FCDA 和 DO 级 FCDA。 | 属性叶为 `cVal.mag.f`、`cVal.ang.f`，同属 `MX`。相位 Item 为 `MMXU1$MX$PhV$phsA$cVal$ang$f`。反查按对象列出两个已解析成员，结构成员与属性成员分型。 |
| `DemoUnlisted / MMXU1.PhV.phsA` | 同名路径和类型，独立 IED 实例，无 FCDA。 | 离线相位 Item 字面相同，经推导的 Domain 为 `DemoUnlistedLD0`。范围内对象报告反查为零。 |
| `DemoNoAngle / MMXU1.PhV.phsA` | A 相模板仅有幅值，报告数据集仍声明缺失的角度属性。 | 属性目录保留幅值，缺相位叶。悬空 FCDA 保留解析失败，地址留空。 |
| `DemoListed / MMXU1.AngSet` 与该 IED 的 `PTOV1` | 明确的独立 `SP` 定值实例，以及没有测量输入声明的合成保护 LN。文件含 `110`、`15` 和 `30` 的示意 `Val`。 | 可分别查到 `MX` 测量及 `SP` 定值属性。两类 LN 和静态值保持各自原文身份，保护输入对应关系仍无证据。 |

复现步骤为打开样本，在任务视图选择 `DemoListed`，点击「MMS 查阅 → 信号地址」，筛选 `PhV.phsA`，选择对象后点击「查看属性与地址」。在属性表选择相位叶，可核对 Domain、Item、FC，并从 MMS 字段表的 Item 原文入口定位同快照的 `BDA` 定义。高级视图在相同文件下展开数据集 `Voltage`，可查看两个原始 FCDA。

目前对象目录先定位 DO/SDO，再展开属性。报告反查以对象为入口。按业务词直达叶属性、叶属性与报告的精确匹配说明、跨表引用菜单列入后续阶段。

## 表格与菜单矩阵

矩阵按物理表格及其逻辑页面记录。已具名的当前字段可作为后续菜单提供方的证据。普通菜单指「复制单元格」「复制行」「查看完整值」「查看原文」，相应视图可附加已有动作。占位、加载或无法溯源的行保持原有行为。

| 视图和表格 | 当前字段与身份提供方 | 当前右键菜单与 T8.3 范围 |
| --- | --- | --- |
| 任务 · MMS 结果表 `SclMmsResults` | `SclMmsRow` / `SclMmsKey`，覆盖报告、实例、数据集成员、对象、属性、反查、端点、服务、搜索、图节点列表和检查结果，列随路线切换。 | 普通菜单。按记录类别提供有效引用。属性表作为首个试点。 |
| 任务 · MMS 字段表 `SclMmsFields` | `SclMmsPresentation` 的字段键、完整值及原文证据。定义引用、预期实例、Domain/Item、原始 `rptID`、数据集列表名各有自己的字段。 | 普通菜单。按字段语义提供引用。缺省值、在线未获取和说明行保留原菜单。 |
| 任务 · `SclTaskResults` 本机信号 | `SclTaskRow`、信号目录的 LN/对象路径、描述、类型及详情证据。 | 普通菜单。对象路径须按具体实例取得引用。 |
| 任务 · `SclTaskResults` 外部输入、GOOSE/SV 发布或输入、文件检查 | 同一个动态表。分别用 ExtRef 节点、流的控制块、问题原始序号与范围读取对象、源控制块、报文标识或原文证据。 | 这些路线未启用普通复制菜单，现有激活操作按页面打开证据。后续为有引用的记录提供菜单。 |
| 任务 · `SclConnectionMembers` 双向收发成员 | `SclConnectionDetail` 的分页成员与输入证据，区分已绑定和候选及其 MMS 投影。 | 普通菜单。按发布成员和接收输入区分，候选独立命名。 |
| 任务 · `SclPublicationMembers` GOOSE/SV 发布成员 | 同一收发详情组件的发布模式。保存原 FCDA、已解析对象、FC 及接收依据。 | 普通菜单。保持各个成员出现的原始身份。 |
| 任务 · `SclPublicationEndpoints` 网络端点 | `SclConnectionDetail::Endpoint` 与 ConnectedAP/Address/P 原文。 | 普通菜单。结构化地址或标识按实际字段提供，普通空白字段保持现状。 |
| 高级 · 数据集树表/平面表 `SclDataSets` | `SclHierarchyRow`，包括父数据集、FCDA 原始节点、序号、引用记录及状态。 | 普通菜单加拓扑。后续作为高级首个试点，加载行无工程身份。 |
| 高级 · 成员专页 `SclMembers` | 父数据集范围、FCDA 原始节点与 `ReferenceAnalysis`，可查声明、对象引用和 MMS Item。 | 普通菜单加拓扑。按当前父数据集和原始成员交付。 |
| 高级 · 报告 `SclReports` | `ReportMapping`、报告节点及 MMS 投影，当前表含声明的 datSet、原始 `rptID` 和定义引用。 | 可显示局部引用拓扑的记录有拓扑菜单，后续附加引用项。 |
| 高级 · 对象 `SclObjects` | 分类行与 NodeId，实例、模板节点及 outgoing/incoming 证据均按原始类别取值。 | 现有拓扑菜单依证据显示。类型定义与实例引用分别处理。 |
| 高级 · 通信 `SclCommunication` | 通信分类行及 GSE/SV/ExtRef、ConnectedAP 原始身份。 | 现有拓扑菜单依证据显示。数据集、源控制块、对象和标识按原证据提供。 |
| 高级 · 模板 `SclTemplates` | 模板 NodeId、类型 id 和复用关系。 | 现有拓扑菜单依证据显示。没有实例身份时只提供类型引用。 |
| 高级 · 问题 `SclFindings` | 问题的原始序号、节点及同快照证据。 | 未接入结构化右键复制，有问题依据的引用可在后续提供。 |
| 高级 · 搜索结果 `SclSearchResults` | `SclHit` 的命中字段及原始身份，列表展示文本片段。 | 未接入结构化右键复制。按命中依据读取完整值。 |
| 高级 · 引用 `SclLinks` 与候选 `SclCandidates` | 原始引用记录和候选 NodeId。检查器已有「复制声明」「复制推导引用」按钮，单独显示候选。 | 关系表无引用子菜单。后续保留方向、解析状态与候选语义。 |

工程 XML 树、任务导航、使用方列表窗口和 HTML 属性页是独立查阅控件。现有原文、拓扑、路径菜单与详情按钮继续单列。矩阵可通过 [通用表格菜单](../../src/gui/SclTableInteraction.cpp)、[MMS 表](../../src/gui/SclMmsView.cpp)、[任务表](../../src/gui/SclTaskPages.cpp)、[收发表](../../src/gui/SclConnectionView.cpp)、[高级表](../../src/gui/SclWorkspace.cpp) 与[搜索表](../../src/gui/SclWorkspaceSearch.cpp)复核。

## 验证记录

在独立目录 `build/task8-t80` 完成 Windows Debug 构建和运行库部署，使用 Qt 6.11.1 / GCC 16.2.0。隔离 QSettings。清洁 PATH 由本次部署目录及 Windows 系统路径组成。

- `SclMmsQueryTest::Task8VoltageBaseline` 通过。核对了正反例的完整 Item、显式/推导 Domain、结构与叶属性、FC、报告成员证据、跨 IED 范围和独立定值。
- `MainWindowBaselineTest::SclTask8VoltageBaseline` 的原生 Windows Qt 程序驱动通过。通过现有任务树进入信号地址，核对属性页、同快照的 `BDA` 原文和目标行可见性。实际打开 MMS 属性表与高级 FCDA 树表菜单，复现普通复制、完整值和原文动作，以及树表的拓扑动作。这两处当前均没有引用子菜单。
- 清洁 PATH 下 CTest 14/14 通过，耗时 41.03 秒，含夹具清单及显式启用的 MMS 原型测试。完整日志为 `build/task8-t80/T80CTest.txt`。
- 合成样本 SHA-256 为 `0831b51a72f948201bd5e228179c0ccad91feb87eedd0f9585fa9fdfc97cc762`。专项日志为 `build/task8-t80/T80Unit.txt` 和 `build/task8-t80/T80Native.txt`。

T8.3 接入新菜单时，在保留上述原有动作的前提下补充菜单断言。运行中切换引用项的行为在该阶段单独验收。
