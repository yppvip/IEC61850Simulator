# SCL 自建测试样本

这些样本由项目自行编写，没有复制真实工程 SCD 内容。所有名称、地址与报告标识均为合成数据。`Manifest.json` 是 P0 的固定清单，记录命名空间敏感的元素计数、XML 是否良构，以及后续规则的预期方向。

“Minimal”表示面向解析原型的精简输入，不表示已经通过 IEC XSD 或完整标准必需对象检查。所有样本均未执行 XSD 校验；`plannedRules` 不是当前测试已经产生的诊断，也不是完整诊断集合。

| 文件 | 用途 |
|---|---|
| Minimal.scd / Minimal.cid / Minimal.icd | 相同内容、不同后缀，准备文件入口和解析测试；不据后缀推定内容版本或合规性 |
| NamespaceAlias.scd | 全部标准元素改为 `s:` 前缀，语义计数保持不变 |
| NamespaceCollision.scd | Private 中包含扩展命名空间的 IED/DataSet/ReportControl，同名扩展不能计为标准对象 |
| MissingType.scd | LN 引用未定义的 lnType；P2 预期 TYP001 |
| DanglingDataSet.scd | 两条报告及 GOOSE/SV 控制块引用不存在的数据集；P2 预期 REF001 |
| DuplicateDataSet.scd | 相同 LN 下重复定义数据集，其中一个为空；P2 预期 KEY001、ENG001 |
| UnknownVersion.scd | 根缺 version/revision/release；Header 的版本不能替代标准版本 |
| UnknownDialect.ccd | 人为构造的非 SCL 根与命名空间，只测试“不识别”分支，不代表任何厂商的 CCD 格式 |
| Truncated.scd / Empty.scd | XML 不完整 / 零字节输入，预期 XML002 / XML001 |
| IedBrowsing.scd | 两个业务 IED 与空 IED、共享/未使用模板、通信归属、跨 IED ExtRef；用于浏览范围、Tab/成员选择和历史回归 |

正向基样包含显式 ldName、一个缺 daName 的 DO 级 FCDA、重复 FCDA（保留顺序）、前向类型引用、配置初值、BRCB/URCB、indexed 与缺省 rptID、GSE/SMV 配置。它不覆盖完整类型/数组/ExtRef/多 AP/编码矩阵。

P0 的 `SclFixtureBaselineTest` 使用 Qt 自带流式读取器检查清单、XML 状态、根和计数，尚未调用产品解析器或验证语义诊断。P1/P2 应复用这些输入建立独立的模型和规则断言，再补充结构数组、循环、作用域、编码、DTD、资源限制等样本。

真实工程文件只用于本地可选验收，不提交、上传或作为 CI 必需文件。可运行 `scripts/MeasureSclBaseline.ps1` 将本地统计写入被忽略的 `build/`，详见 [P0 验收记录](../../../docs/Tasks/Task1P0.md)。
