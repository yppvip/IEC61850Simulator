# Task2 T2.0 · 基线与样式小样

2026-11-06。基线与小样门槛通过。Qt 产品界面的交互与样式改造留给后续阶段。

## 现有工作台

| 操作 | 基线结果 |
| --- | --- |
| 表选报告 | 表格选行后同步树节点，详情出现 `rptID`。自建样本及本地 SCD 均通过工作台流程。 |
| 树选报告 | 在本地 SCD 中先通过报告表取得前两条报告的树节点。选择第二条表格行后切到概览，再从树选第一条。中央仍停在概览，报告表的旧行索引仍为 1。 |
| 激活问题 | 问题激活后定位树并切到原文页。自建样本的现状截图由测试生成于 `build/windows-debug/p0-artifacts/SclWorkspace.png`。截图显示树标签和问题列在默认宽度下容易截断，原文以带行号的纯文本呈现。 |
| 搜索原始 `rptID` | 自建样本精确搜索 `TEST_EVENTS` 有结果。本地 SCD 选取一个非空原始 `rptID` 进行精确搜索，有结果。 |
| 空、失败、无匹配 | 关闭后树表与原文清空，概览提示「尚无可浏览的文档」。加载错误后保留上一个有效快照并报告失败文件。当前报告表用不存在的 IED 筛选得到 0 行，改回有效 IED 恢复 2 行。 |

## 性能基线

Windows 11、MSYS2 UCRT64、GCC 16.2.0、Qt 6.11.1，Debug，`QT_QPA_PLATFORM=offscreen`，隔离 QSettings。用户指定的本地 SCD 约 48.08 MiB。每轮得到 812,853 个元素、69,702 条引用，关闭后快照释放。十轮前后文件 SHA-256 一致。

| 指标 | 结果 |
| --- | --- |
| 第一次加载 | 8,812 ms |
| 十轮加载 | 8,812–9,237 ms，中位数 8,932 ms |
| 十轮关闭及快照释放 | 190–200 ms，中位数 192 ms |
| 十轮进程峰值工作集 | 398.95 MiB，启动基线 40.74 MiB |
| 关闭后私有内存 | 50.51–59.53 MiB，第十轮 54.46 MiB |
| 连续树选到离屏画面更新 P95 | 15.70 ms |

导航在独立验收进程中交替选择已展开的前两条报告 40 次，原文页保持打开。计时从树索引变更开始，经事件处理和窗口抓取后结束，取排序后的第 38 个样本。该口径包含离屏光栅绘制，当前基线未包含后续新增的报告表路由，也不代表实体屏幕的呈现延迟。

## 小样与视觉决定

[浏览器小样](Task2T2.0Mock.html)与现状截图组成对照。小样仅用自建样本数据，可切换正常、尚未打开、加载失败、筛选无匹配四种状态。以 22/45/33 三栏为初始比例，中央列表保留最大空间。统一使用浅灰画布、白色工作面、蓝色选中，LD 蓝、LN/LN0 青、DO 紫、DA 灰。保留类型文字。失败提示与类型颜色分开。窄窗小样将详情下移，正式 Widgets 实现按 Task2 的可收起详情区设计。

已核看现状离屏截图，并通过脚本验证小样的四种状态切换。浏览器自动化拒绝访问本地 `file:` URL，小样的浏览器渲染尚未人工复核。

## 复现

```powershell
$env:PATH = 'C:\msys64\ucrt64\bin;' + $env:PATH
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$env:QT_QPA_PLATFORM = 'offscreen'
# 将 IEC61850_LOCAL_SCL 设为指定的本地验收样本路径
$before = (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalCycles -o build/windows-debug/T2LocalCycles.txt,txt
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalAcceptance -o build/windows-debug/T2LocalAcceptance.txt,txt
$before -eq (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
```

Debug 配置及构建通过，CTest 7/7，两项本地验收通过。
