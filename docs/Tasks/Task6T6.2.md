# Task6 · T6.2 领域投影与检查

日期：2026-11-19。状态：**已通过 T6.2 领域层门槛**。本阶段只实现 Qt Core 投影与项目规则，不推进 T6.3 后台查询或 T6.4 正式工作台集成。T6.1 合成原型继续独立构建；现有正式程序页面和解析服务的交付内容未改变。

## 1. 交付结构

新增 `src/iec61850/scl/SclMmsProjection.hpp/.cpp`，由 `SclAnalysis` 构建，仅链接 Qt Core。入口为 `MmsProjector::Build(const ReferenceAnalysis &, TaskControl *, const MmsLimits &)`，复用 Reader、Document、SourceIndex、已有报告/数据集及 ExtRef/ConnectedAP 引用分析。不重复 XML、FCDA、类型、描述或数组解析。

| 投影 | 本阶段行为 |
| --- | --- |
| MmsReportDefinition | 定义 NodeId、归属 IED/Server/LN、原始字段、三类参数子元素及其重复项、同 LN 数据集状态/候选/原因、原始 FCDA 出现次数；定义引用、Domain/Item 与数据集 MMS 列表名分开 |
| MmsReportInstance | 独立虚拟身份 `(定义 NodeId, 原始序号)`；名称/地址及四项字段证据；不伪造 XML 节点，不复制数据集成员，不表示实际设备实例 |
| MmsClientDeclaration | ClientLN、Association、ReportExtRef 三类独立记录；原始字段及目标候选/状态/原因。ClientLN 的 XML 序号、预期实例位置、客户端解析目标独立存储 |
| MmsServer / MmsDomain | 每个 AP 的 Server/ServerAt 访问路径及解析结果；共享模型与多个访问入口分别保留，域名采用 ldName 或现有回退语义，重复域名不生成确认地址 |
| MmsEndpoint | ConnectedAP → AP/Server，Address → 按原顺序的每个 P；重复同 type 不覆盖。只取 ConnectedAP 的直接 Address，不借 GSE/SMV 地址，不填端口 |
| MmsCapability | IED/AP 的 Services 身份、直接声明及原始参数；不凭未确认版本选择覆盖优先级，不把上限当配置数量 |

`MmsField` 保留 rawPresent、raw、解释值、状态、来源节点/属性/规则、单位和限制。缺失、空字符串、非法词法不合并；缺省不擅自应用。来源是同快照的元素位置，未猜测属性字符偏移。投影自身持有文档、原文和 SHA-256；NodeId 离开该快照无效。

增量诊断规则集为 `mms-0.1`，与基础 `ReferenceAnalysis::ruleSetVersion=0.2` 分开。新增 MMS001～005 与 ENG004 子集的诊断/覆盖；MMS006、XSD 明确尚未实现。规则范围、依据、严重度、例外与未实现内容详见 [SclRules](SclRules.md)。REF001/002/003/004 的已有诊断不重复生成。

## 2. 保守语义与资源边界

仅在根版本恰为 2007/B/4、唯一可解释作用域、显式有效 buffered、indexed=true、单个 RptEnabled 且显式 max=1..99 时生成 01..99 两位索引预期实例。支持 BR/RP，词法 true/false/1/0 及首尾空白的解释；原值保留。名称采用保守的字母/数字/下划线子集，其他字符只保留原声明并限制地址推导。

未知版本、Header-only 版本、缺 indexed/max/buffered、重复 RptEnabled、非索引单实例与关联专用 URCB 均不生成确定实例。后者保留声明及限制，不全局判错或合并为一个对象。自定义 rptID 在各实例中保持原值；缺失/空 rptID 的默认解释仍未启用，不能用实例引用伪装实际报告标识。

ClientLN 使用 XML 原顺序，无法解析的第一项仍占位置 1，第二项不移到 01；位置分配不意味着客户端已经解析或订阅。外部 IED、缺项、多 AP/Server、重复候选与纯客户端 AP 未支持的模型分别保留状态。Association 解析其所声明的客户端 LN，归属 Server 单独记录，绝不隐式绑定报告实例。Report ExtRef 直接复用已有解析，显式控制块冲突不回退对象候选，缺 serviceType 不纳入 Report 声明。

默认预算为 250,000 个索引/字段/记录/诊断/访问路径工作单元、每条 ServerAt 路径 256 个 AP。全局预算不足不发布部分记录；覆盖为 Blocked 且带原快照身份。单条 ServerAt 环/路径受限保留分支状态，路径超预算同时置 resourceLimited。所有长循环检查取消；取消抛出 TaskCancelled，不交付半成品。域身份索引按 LD 直接查找，避免每个报告反复扫描全 Server 的 LD。

## 3. 验证

使用独立 `build/task6-t62` Debug 构建，沿用 T6.0 的 GCC 16.2.0、Qt 6.12.0、CMake 4.4.5、Ninja 1.13.2。启用合成原型回归选项，不覆盖用户旧构建。运行 PATH 仅含本次 bin 与 Windows 目录，不借 MSYS2 运行库搜索路径。

新增 `SclMmsProjectionTest`：自动用自建夹具及内存 XML 变体验证定义/实例分离、BR/RP、99 边界、缺省/空/非法/越界/重复参数、客户端顺序及分配溢出、多 AP/Server、ServerAt 链/环/歧义、重复域、共享服务器多端点、独立客户端种类、显式源冲突、无 serviceType、未知版本、原始 rptID 三态、作用域冲突、地址掩码、扩展隔离、取消、资源预算与失败快照。原有引用/目录测试继续覆盖跨 LD、重复 FCDA、DO 成员、FC/数组、嵌套类型及未入数据集信号；本阶段不新增信号地址展开器。

完整 CTest **11/11 通过**（含 T6.1 原型；普通构建不启用原型时为 10 套）。领域测试 29 个通过（含初始化/清理），0 失败；显式本地真实文件入口默认跳过，常规 CTest 不依赖工程文件。初轮测试通过后，复核时补齐了布尔空白解释、域索引、路径预算标记和 Address/P 层级；一次调整后遗漏测试字段导致编译失败，已修正并重建回归，不留在最终结果中。第三方已有 CMake 兼容性/ADS QFile 警告未在本阶段改动。

## 4. 指定真实文件只读核对

入口：`SclMmsProjectionTest LocalMmsProjection`，只在显式设置 IEC61850_LOCAL_SCL 时运行。文件为用户修订的 `D:/1Document/自动配置/电校scd/湖北武汉220kV电校培训变20261021.scd`。读取前后以及投影的 SHA-256 一致：

`e69733050c24f1e49da0f5695ab804307838e549dff36eff64cab4ff5e182f4b`

| 项目 | 实测 |
| --- | ---: |
| 报告定义 | 687 |
| 数据集唯一目标（报告使用） | 687 |
| 这些数据集的 FCDA 出现次数 | 29,189 |
| AP 访问路径 / ConnectedAP 端点 | 216 / 182 |
| Services 原始声明组 | 102 |
| ClientLN / Association / 显式 Report ExtRef | 均为 0 |
| 声明 max 合计 | 10,724，不是确认实例数 |
| 确定预期实例 | 0；687 个定义全部保留版本限制 |
| 增量提示 | ENG004 123 条：同 Server 不同定义的非空原始 rptID 疑似重复；不表示协议违法、地址冲突或在线订阅失败 |
| 已解释报告字段非法词法 | 0 |

一次独立 Core 进程实测 Reader 2,416 ms、ReferenceResolver 1,554 ms、新投影 304 ms。该结果只说明领域增量成本，不是正式 GUI Ready、完整信号地址、峰值内存或 T6.5 性能验收，不与 T6.0 的 GUI 指标混算。本地日志只输出聚合数与指纹，不输出工程原文、标识或地址。

## 5. 复现与证据

```powershell
$env:Path = 'D:/msys64/ucrt64/bin;D:/msys64/usr/bin;' + $env:Path
cmake -S . -B build/task6-t62 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64 -DCMAKE_C_COMPILER=D:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/ninja.exe -DBUILD_TESTING=ON -DIEC61850_BUILD_MMS_PROTOTYPE=ON
cmake --build build/task6-t62 --parallel 6
$env:Path = (Resolve-Path build/task6-t62/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
& D:/msys64/ucrt64/bin/ctest.exe --test-dir build/task6-t62 --output-on-failure

$env:IEC61850_LOCAL_SCL = 'D:/1Document/自动配置/电校scd/湖北武汉220kV电校培训变20261021.scd'
./build/task6-t62/SclMmsProjectionTest.exe LocalMmsProjection
Remove-Item Env:IEC61850_LOCAL_SCL
```

本地忽略目录 `build/task6-t62/artifacts/` 保存 Build.txt、CTest.txt、MmsProjectionFinal.txt、LocalMmsProjection.txt 和最终 LocalMmsProjectionFinal.txt。未新增第三方库、图标或独立程序；保留现有未跟踪图标压缩包、Redis 迁移与运行配置。真实文件没有上传、修改或加入夹具。

## 6. 门槛结论与下一步

T6.2 的领域/检查子集与 Qt Core 回归门槛通过，规则覆盖及不确定性明确保留。T6.3 再接不可变快照、后台分页/查询、反查、搜索及按需信号地址；T6.4 才接正式页面、历史、原文和统一检查入口。本次到 T6.2 为止。

能力适用/优先级/配置兼容性、默认 RptID、非索引复杂关联、完整其他 Services 语义、IPv6/OSI/selector 值域仍未实现，不把“保留原始声明”宣传成完整标准校验。Task1 P7、Task3 与 Task5T5.6 的未通过项及 T6.5 桌面/性能/初学者验收边界继续保留。
