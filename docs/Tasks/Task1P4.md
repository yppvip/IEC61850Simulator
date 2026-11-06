# Task1 P4 ADS 分析工作台验收

2026-11-06，`feature/Desktop`。P4 交付 SCL 浏览工作台，下一阶段为 P5。

打开文件后，现有 ADS 工作区切换至分析模式。「视图」菜单可在 SCL 和仿真之间切换。分析模式收起服务器及客户端列表，切回仿真时恢复列表。分析会话保留在内存中，重新进入工作台时可继续浏览。

| 区域 | P4 可用内容 |
|---|---|
| 导航树 | 从根元素按需展开 IED、AP、LD、LN、通信、类型模板和扩展。表格和关系跳转可定位到尚未展开的节点。 |
| 概览与问题 | 文件规模、模型计数、版本线索、规则覆盖和问题列表。双击问题定位对象和原文。 |
| 报告、数据集及成员 | 原始 rptID、BRCB/URCB、数据集、定义引用、有序 FCDA、解析状态和对象引用。 |
| 对象、通信、模板 | 实例及共享定义、FC、类型、索引、连接对象和常用地址参数。属性区保留 XML 原始字段，并独立显示派生映射。 |
| 引用与使用方 | 双向跳转、候选列表和当前文档内的返回操作。 |
| 原文 | 只读预览，目标行高亮，显示上下各 15 行。超长行标记截断。失败请求可打开自己的原文快照。 |

`SclBrowseIndex` 随后台快照发布，提供父子行、分类和有序成员索引。树和表格通过 Qt Item Model 按需取行。对象表同时显示实例和共享模板成员，模板展开后可看到 DO、DA、BDA 等定义。P5 继续实现搜索、筛选、完整前后退和问题导出。

本机 Debug 构建和 7 组 CTest 通过。自建样本覆盖报告、数据集、FCDA、模板定义、原文高亮、逐级返回、问题定位与仿真视图切换。真实 48.08 MiB SCD 在离屏主窗口中完成同一路径，从打开到可浏览约 8.92 秒，共 812,853 个元素、69,702 条引用和 687 条报告定义。磁盘文件验收前后 SHA-256 一致。界面截图只使用自建样本。人工高 DPI、多屏与原生文件选择器验收安排在后续阶段。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$env:QT_QPA_PLATFORM = 'offscreen'
$env:IEC61850_LOCAL_SCL = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalAcceptance -o 'build/windows-debug/SclWorkspaceAcceptance.txt,txt'
$before -eq (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
```
