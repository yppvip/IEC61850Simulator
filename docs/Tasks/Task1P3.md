# Task1 P3 异步加载与文档会话验收

2026-11-06，`feature/Desktop`。P3 已通过，下一阶段为 P4。

文件菜单可打开、重载和取消 SCL 分析。状态栏分别显示 SCL 处理状态与 Redis 状态。SCL 状态涵盖读取、原文索引、XML 解析、引用索引和校验。快速完成的阶段可能略过显示。加载失败时显示失败文件和可用的原文行，状态栏提示仍在使用的文档。重新加载针对当前文档，首次打开未成功时重试最近请求。

`SclAnalysisService` 使用 Qt Concurrent 执行 P1/P2。服务在事件循环线程管理待处理请求和当前快照，后台任务只持有路径和独立的协作取消标记。新请求替换待处理路径，旧任务结束后才运行最新请求。只有成功且仍有效的结果能发布不可变快照。成功替换前，旧快照的原文字节、解析模型和校验结果仍可一起访问。文件系统通知提示磁盘变化，读取前后也检查文件大小和修改时间。取消立即更新界面，后台在检查点停止。单次系统文件调用及 XML 读取器处理单个 token 的耗时受底层操作影响。

Debug 构建和 7 组 CTest 通过。新增主窗口集成场景使用 offscreen 和隔离的 QSettings。

- 快速连续请求及 XML 解析中切换文件，仅发布最新结果。
- 截断 XML 和错误扩展名保留旧文档，解析失败的字节和诊断仍对应失败请求。
- 文件写入触发重载提示，重载成功后新旧快照保留各自原文。
- 加载期间事件循环定时器持续触发。取消和关闭窗口操作低于 500 ms，后台任务在测试规定的 3 秒内退出。
- SCL 状态与 Redis 不可用消息同时保留。已有 Redis 启动集成测试另行验证真实 Redis 和不可达端口的启动行为。

本机真实 48.08 MiB SCD 通过主窗口的应用服务只读验收，从发起加载到快照发布耗时 8,171 ms，共 812,853 个元素和 69,702 条引用。快照 SHA-256 与 P2 一致，验收前后磁盘 SHA-256 相同。该数据来自 offscreen 的主窗口集成测试。报告、导航和原文浏览面板进入 P4。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$env:QT_QPA_PLATFORM = 'offscreen'
$env:IEC61850_LOCAL_SCL = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
./build/windows-debug/bin/MainWindowBaselineTest.exe SclLocalAcceptance -o 'build/windows-debug/SclLocalAcceptance.txt,txt'
$before -eq (Get-FileHash -LiteralPath $env:IEC61850_LOCAL_SCL).Hash
```
