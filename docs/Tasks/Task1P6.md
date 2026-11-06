# Task1 P6 收尾与发布验收

2026-11-06，`feature/Desktop`。P0 至 P6 完成，完整 XSD 校验保留为 P7 独立门槛。

「视图 → 恢复默认布局」可恢复 ADS 工作区、服务器和客户端的默认停靠位置，当前分析快照及本次运行中已创建的仿真详情仍可访问。关闭窗口时保存静态布局。启动读取 ADS 用户版本和 SHA-256，兼容旧版本 0。未知版本、摘要不符或 ADS 数据损坏时回到默认布局。重启后的仿真工作区从空会话开始，动态详情和 SCL 快照按需创建。

「帮助 → SCL 使用帮助」或 F1 可查看离线操作指引。「文件 → 关闭 SCL 文档」可释放文档，取消仍在进行的读取或搜索，并清理树、表格、原文、历史和重载路径。随后可在同一窗口打开新文档。

| 验收项 | 结果 |
|---|---|
| Debug 配置与构建 | Windows / MSYS2 UCRT64，GCC 16.2.0，Qt 6.11.1。构建成功，7 组 CTest 通过。 |
| 跨进程布局 | 自定义停靠关系可恢复，旧版布局可读取并升级。重启跳过动态详情和 SCL 会话。未知版本、摘要不符、摘要正确但 ADS 数据损坏均回退默认。 |
| 应用内操作 | 默认布局恢复保留当前文档和已创建的仿真详情。离线帮助可打开。加载中关闭文档后，旧任务结果不会回填。 |
| Redis 启动 | 使用隔离配置，已有 Redis 与不可用目标各启动实际应用两次。应用按预期记录状态，测试 Redis 在应用退出后仍运行。 |
| 真实 SCD | 同一离屏主窗口连续打开、关闭十轮。每轮均得到 812,853 个元素和 69,702 条引用，关闭后弱引用失效，树和报告表清空。验收前后原文件 SHA-256 一致。 |

48.08 MiB 样本的十轮加载用时 8.81–9.05 秒，关闭并完成快照释放用时 190–208 ms。Windows 进程峰值工作集约 397.1 MiB，启动基线约 39.7 MiB。关闭后的私有内存为 51.8–57.4 MiB，第十轮约 54.5 MiB。测量在离屏 Debug 测试进程中完成，包含 Qt 和测试宿主。

常规 CTest 使用自建样本。真实工程文件只在本机只读使用。原生文件选择器和保存对话框、实体高 DPI 屏幕及多屏布局仍待人工交互验收。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$env:QT_QPA_PLATFORM = 'offscreen'
$env:IEC61850_LOCAL_SCL = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalCycles -o 'build/windows-debug/SclLocalCycles.txt,txt'
$before -eq (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
```
