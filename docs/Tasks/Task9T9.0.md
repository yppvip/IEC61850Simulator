# Task9 · T9.0 菜单和证据基线

2026-11-22。T9.0 通过。沿用 [T8.0 的表格清单](Task8T8.0.md) 和 [T8.4 的集成口径](Task8T8.4.md)。

| 入口 | 五类字段的来源 | 图需要补齐的依据 |
| --- | --- | --- |
| MMS 属性结果及详情字段 | `SclMmsRow` 与 `SclMmsKey` 提供对象引用、FC、Domain、Item 和路径上的类型 ID。详情按原始字段键投影。 | 保留行已有的模板路径、有效 FC、地址形态和 AP/Server，避免丢失为一个叶节点。 |
| 高级数据集树表、成员专页，以及 MMS 成员、报告使用、发布与收发成员 | 按 FCDA NodeId、成员出现次序及 `ReferenceAnalysis` 查询。已有对象引用、声明 FC、Domain 和 Item。 | FCDA 的逐段解析记录可重用。声明 FC 与有效 FC 分列；DO 级停止于对象路径。 |
| MMS 报告与预期实例、高级报告 | 定义或实例投影提供 Domain 和 Item。 | 沿报告真实所属 LN 与 LD 补命名依据。报告类别与实例名按已有映射展开。 |
| 任务信号、高级对象及实例 DAI | 信号目录或定向属性查询提供对象引用、Domain、类型及适用属性地址。 | 共享模板保留具体 IED/LN；对象级引用只保留对象路径。 |
| 高级模板、引用与候选、问题、通信、全局搜索和适用任务行 | 由实际命中节点、字段或解析记录决定。类型声明、FC、可解析对象和地址按具体结果出现。 | 类型从实际声明追到定义；同值不同声明需并列。仅有模板标识、网络参数、占位或候选路径时保留各自原动作。 |

[Task8Voltage.scd](../../tests/fixtures/scl/Task8Voltage.scd) 冻结以下口径。显式 LD 名为 `ListedMeasurements`，跨 IED 的同名信号使用推导名 `DemoUnlistedLD0`。普通 LN 的名称由 `MMXU` 和 `1` 组成，报告所有者为 `LLN0`。相位 Item 与报告中的 DO 级成员引用分列，幅值有额外精确属性 FCDA。文件中缺相位及未入报告的两组正反例保持原义。

Task8 冻结构建及独立 `build/task9` Debug 基线均在清洁 PATH 下通过 `SclMmsAttributeWorkflow` 和 `SclReferenceMenus`（各 2/2）。独立构建使用 GCC 16.2.0，部署了原有 Qt 插件和运行库。原有任务和高级入口的完整值、原文及点击复制可作为对照。
