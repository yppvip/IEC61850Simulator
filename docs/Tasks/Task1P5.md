# Task1 P5 搜索、导航与导出验收

2026-11-06，`feature/Desktop`。P5 已通过，下一阶段为 P6。

- 全局搜索覆盖当前快照的结构化名称和描述、对象引用、MMS 映射、报告标识、数据集、通信地址及诊断。支持字面子串、精确匹配、IED、对象类型、FC 和问题严重度。未展开的节点也可命中。对象按节点归并，诊断按问题保留。原文全文搜索属于单独的能力范围。
- 搜索输入防抖 200 ms，Enter 可立即发起，Esc 可取消。后台分批交付，界面显示搜索状态、已收到的命中数及完成后的总数。全局结果每页增加 512 条。当前表格筛选在后台生成行映射。新文档清理旧查询和结果。
- 前进和后退恢复对象、数据集上下文和对应页签。Ctrl+F 聚焦查询，Alt+左右方向键用于导航。图标采用 Qt 原生样式，搜索完成时的轻动效可关闭。
- 导出当前文档的完整诊断和规则覆盖。JSON/CSV 均包含文件 SHA-256、规则集版本、覆盖、严重度和原文位置。CSV 使用 UTF-8 BOM、引号转义和公式前缀防护。后台使用 QSaveFile 原子提交，源文件路径禁止作为导出目标。

Debug 构建及 7 组 CTest 通过。自建样本覆盖中文、特殊字符、未展开节点、空结果、搜索取消、切换快照、按 IED 和严重度筛选、双向历史及导出内容。CSV 验证了逗号、引号、换行和前置空白后的公式字符。键盘流程在离屏 Qt 100%、150% 和 200% 缩放下通过。实体屏幕、多屏及原生保存对话框的人工交互仍待验证。

本机真实 48.08 MiB SCD 只读验收通过，验收前后 SHA-256 一致。精确报告引用首批约 71 ms，精确 `DAI` 首批约 115 ms，约 519 ms 完成计数，共 340,428 条。时长从 Enter 发起计，包含离屏测试轮询，不含输入防抖。首屏时间对应这两个查询和本机 Debug 环境。分页从 512 条增加到 1,024 条，取消已发起的查询后没有发布迟到结果。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$env:QT_QPA_PLATFORM = 'offscreen'
$env:IEC61850_LOCAL_SCL = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalAcceptance -o 'build/windows-debug/SclSearchAcceptance.txt,txt'
$before -eq (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
foreach ($scale in @('1', '1.5', '2')) {
    $env:QT_SCALE_FACTOR = $scale
    ./build/windows-debug/bin/MainWindowBaselineTest.exe SclSearchFlow -o "build/windows-debug/SclSearchScale-$scale.txt,txt"
}
```
