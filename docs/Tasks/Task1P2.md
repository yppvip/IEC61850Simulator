# Task1 P2 引用解析与基础校验验收

2026-11-06，`feature/Desktop`。P2 已通过，下一阶段为 P3。

`SclAnalysis` 在 P1 快照上建立带作用域的索引，支持类型、控制块到数据集、FCDA、ConnectedAP、GSE/SMV 和 ExtRef。每条结果保留来源、候选和解析状态。FCDA 保留成员顺序与重复项。引用索引支持查找目标及其使用方。诊断包含规则、严重度、对象路径、原文行、建议和适用范围，分析结果保存规则版本和文件 SHA-256。

| 项目规则 | P2 覆盖和验收样本 |
|---|---|
| XML001～004 | 继承 P1 读取门槛。XML 导入失败后，后续规则标为前置失败。 |
| STR001、KEY001 | 检查可索引定义的空标识和作用域内重复键。测试同一 LN 内同名数据集产生歧义，也测试不同 LD 和不同 AP 的同名定义。 |
| TYP001～003 | 按目标类型解析前向引用，检查 LN 类别、缺失类型、类型环及超过 64 层的模板路径。共享模板可以复用。 |
| REF001 | 按所属 LN 查找非空 datSet。空值记为未静态绑定。 |
| REF002 | 在所属 Server 内按 LD、LN 和模板中的 DO/SDO/DA/BDA 查找，验证 FC 与单维数组下标。无 daName 的 DO 级成员通过模板判断 FC。测试跨 LD、嵌套、错误 FC、数组越界和缺失类型导致的前置失败。 |
| REF003 | 在 IED 下查 AP，按 Server 或 ServerAt 查 GSE/SMV 控制块。测试缺失 AP、缺失控制块及共享 Server。缺失的 IED 按文件外处理。 |
| REF004 | 区分未绑定、部分绑定、文件外、已知 IED 内悬空和歧义。显式控制块绑定支持 GOOSE、SMV、Report。测试多个 AP 和 ServerAt 共用模型。 |
| ENG001、VER001、EXT001 | 空数据集告警、根版本线索不全提示、外部命名空间汇总提示。 |

这些是项目规则子集。FCDA 数组按零基下标检查，支持路径内的单个选择器或单维时的 `ix`，多重选择和未覆盖的写法保留为覆盖受限。ExtRef 的控制块检查使用显式来源字段。报告定义使用显式 `ldName` 或 `IED name + LD inst`，分别保存原始 `rptID`、定义引用、数据集引用和 MMS domain/item。数组 MMS 映射另存下标和后续组件。P2 的报告映射粒度为定义，实例编号和运行时标识属后续范围。ENG002～004、厂商扩展语义、完整 XSD 和 GUI 仍属后续范围。

外部资料用于核对概念和映射格式。项目规则的适用范围以本页和规则目录为准。[IEC Code Components](https://iec61850.dvl.iec.ch/61850-resources/61850-code-components/) · [libIEC61850 报告 API](https://support.mz-automation.de/doc/libiec61850/c/latest/group__IEC61850__CLIENT__REPORTS.html) · [libIEC61850 模型 API](https://support.mz-automation.de/doc/libiec61850/c/latest/group__DYNAMIC__MODEL.html)

本机 UCRT64 Debug 构建和 7 组 CTest 通过。只读真实 SCD 验收前后 SHA-256 一致，与 P0/P1 相同。探针耗时约 5.07 秒读取并建立 P1 模型，另用 2.93 秒完成 P2 分析，峰值工作集约 389 MiB。

| 真实 SCD 指标 | 数量 |
|---|---:|
| 已解析类型引用（LN / 成员） | 15,246 / 7,186 |
| 已解析控制块到数据集 / FCDA | 968 / 42,688 |
| 已解析 ConnectedAP / GSE、SMV 通信映射 | 182 / 238 |
| 已解析 ExtRef 数据源 / 显式控制块来源 | 3,194 / 0 |
| 报告定义 | 687 |

真实样本共解析 69,702 条引用，诊断为一条版本线索提示和一条扩展命名空间提示。该样本没有触发显式 ExtRef 控制块解析，相关正反例由自建测试覆盖。统计对应本页列出的规则范围。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug
$scd = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $scd).Hash
./build/windows-debug/SclAnalysisProbe.exe $scd --references
$before -eq (Get-FileHash -LiteralPath $scd).Hash
```
