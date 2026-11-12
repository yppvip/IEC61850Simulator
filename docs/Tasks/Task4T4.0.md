# T4.0 · 基线与语义契约

2026-11-12，状态：基线与语义契约门槛通过。Task3 的性能优化及最终用户走查仍按各自门槛验收。

## 构建与样本

- 源码基线：`578beb5f28d9644964281c8a8efd1ed5cfb2ba7a`，产品源码未修改；新增本地计时入口、合成样本和阶段文档。
- 初查发现用户正在运行 `build/windows-ucrt64-debug/bin/IEC61850Simulator.exe`；本轮在 `build/task4-t40` 独立构建，不操作该实例。MSYS2 UCRT64、GCC 16.2.0、Qt 6.11.1、Ninja、Debug；运行时部署成功。主 EXE SHA-256：`BA28521431C6F3BB1680B6CC500AC8CBF3FE3918EB1AFD9F67684C9A74FC89D7`。
- 本次只读样本为 `D:/3Document/湖北武汉220kV电校培训变20261021.scd`，50,418,958 字节，SHA-256 前后均为 `E69733050C24F1E49DA0F5695AB804307838E549DFF36EFF64CAB4FF5E182F4B`。文件未复制到仓库或上传。
- `ctest --test-dir build/task4-t40 --output-on-failure`：7/7 通过，含隔离设置的 UI 回归和清洁 PATH 的 Redis 启动测试。使用 `SclAnalysisProbe.exe <本地文件> --references` 执行现有 Reader 与 ReferenceResolver；探针仅输出汇总。

| 能力 | 真实样本结果 | 解释边界 |
| --- | --- | --- |
| XML / 类型与成员 | 812,853 个节点；15,246 条 LN 类型、7,186 条成员类型及 42,688 条 FCDA 引用在现有规则下解析成功 | 不等于已构建 LN 实例信号目录 |
| 控制块与网络 | 968 条控制块数据集引用、182 条 ConnectedAP 引用、238 条网络控制块引用解析成功 | 关联数与控制块数是不同口径；不从数量差异判定异常 |
| ExtRef | 3,194 条数据对象引用解析成功，全部无 serviceType；现有解析器未生成 ExtRefControl 引用 | 不能确认任何特定发布流的接收关系；不能默认 GOOSE |
| 文件检查 | 现有规则输出 VER001、EXT001 各一条；根版本字段缺失 | 版本相关规则覆盖受限；完整 XSD、工程点表完整性仍未执行 |

历史文档引用的 `Tasks/Task1.md` 在当前 C 盘工作区缺失；本次以现有源码、自建样本和实际构建为准。

## 信号身份与分类契约 v0.1 草案

对象身份在单份不可变快照内由 IED/AP/Server/LD/LN 实例节点及已解析的 DO/SDO 声明路径和数组下标决定。路径的每一段保留模板节点和下标状态；未展开数组范围不是一个已确认元素。属性身份追加 DA/BDA 声明路径及适用 FC。显示名、文本路径和模板 NodeId 单独使用都不能作为身份。诊断和导航持有同快照标识；原文入口区分模板声明、LN 实例和可选 DOI/DAI，不能以模板行冒充实例行。

分别计对象身份数、已展开属性数和成员出现次数。成员位置使用 DataSet 节点加原始序号；相同对象在同一数据集出现两次仍计两次，DO 级成员与 DA 级成员分开。父级复合量保留为结构入口，不在子量汇总里重复计点。外部输入单独计，不能重复算成本机对象。未解析类型和预算截断分别列覆盖状态。

使用 [Task4Signals.scd](../../tests/fixtures/scl/Task4Signals.scd) 固定下列预期。现有探针已验证其 8 条 LN 类型、10 条成员类型和 2 条 FCDA 引用解析成功；分类列是供 T4.2 执行的预期。

| 例子 | 预期及反例 |
| --- | --- |
| A/L/XCBR1.Pos 与 A/L/XCBR2.Pos；B/L/XCBR1.Pos 同名 | 三个对象实例，均无 DOI；前者有两条成员出现，后两者未入数据集仍纳入目录。不能因共享模板或同显示路径合并 |
| XCBR.Pos 的 DPC、ST stVal、CO Oper | 位置反馈给遥信建议；CO 结构记录控制能力候选，ctlModel 未确认时单列证据不足；若实例明确为 status-only，不给遥控建议。品质 q 和时间 t 不计信号 |
| MMXU.A.phsA 的 CMV 与 MX cVal | 遥测建议，父 A 是复合入口；没有单位声明时显示未知，不由 LN 名或 SV 服务猜测 |
| GGIO.Limit 的 ASG 与 SP | 定值/配置，不能因 SP 自动判为遥调。首版只有确认的工程语义规则才给遥调建议；缺少规则保持未分类 |
| LLN0.Beh 即使有 SPS/ST | 状态结构本身不足以作为调度点；没有可解释 LN/DO 业务语义时保留其他/未分类 |
| Sink 的两条 ExtRef | 本工程对象解析成功与站外来源分开；均不生成发布流关系。控制块字段缺失或候选歧义需明示，不能仅用对象匹配、同名、MAC 或共享模板推断 |

规则输出分别记录规则 ID/版本、类别、证据位置、适用条件和限制。一个对象可有状态与控制两种能力；互相矛盾的证据标记冲突。未提供点表，不能标记为工程确认。名称来源依次展示可证实的实例 dU、实例 desc、模板 desc；保留冲突及来源，无描述则显示技术路径。

| 字段 | 界面含义 |
| --- | --- |
| GSEControl@appID / SampledValueControl@smvID | 各自的原始控制块文本标识，分别保留 |
| Address/P[type=APPID] / MAC / VLAN | 通信端点的独立配置值，保留原文与规范化状态 |
| MMS Domain / Item | 有解析依据的静态对象映射，保留其作用域和来源 |
| RCB 定义引用 / ReportControl@rptID | 派生定义引用和原始报告 ID 分列；无值不臆造运行实例或报文 invokeID |
| smpRate / smpMod / nofASDU | 静态采样声明；单位或频率依据不足时保留原值与未知状态 |

## 原生交互基线

在 Windows Qt 平台、1280×800 逻辑像素、DPR 1、独立 QSettings 和清洁 PATH 下，`MainWindowBaselineTest.exe SclNativeTiming` 打开真实文件。脚本驱动原生窗口中的选择和切页，记录同步调用加 Qt 事件派发，不调用截图强制绘制；尚未单独跟踪目标控件 Paint 完成及桌面合成。每类先走一轮不同目标，再采集 100 次热态操作。树包含 IED/LD/LN/报告/数据集/FCDA/DOI/DAI 及当前最大解析入度节点（5,472 条）。

| 操作 | 首轮最大 ms | 热态 P50 / P95 / P99 / 最大 ms |
| --- | ---: | --- |
| 树选择（混合目标） | 45.48 | 10.86 / 19.41 / 20.23 / 20.47 |
| 中央可见页切换 | 19.11 | 13.82 / 22.90 / 30.58 / 35.00 |
| 最大入度节点的详情页切换 | 16.76 | 2.41 / 3.14 / 3.68 / 3.71 |

该次打开至 Ready 10,363 ms，进程峰值工作集 442,298,368 字节。16 ms 心跳在加载及交互时测得最大间隔分别为 35 ms 和 154 ms。数据页切换是本轮热态 P95 最慢的操作；未测得长时间卡死，尚不能将延迟归因到具体槽、绘制或后台竞争。

同一 Windows 平台和清洁 PATH 下，`SclLocalCycles` 十轮打开/关闭均通过，每轮确认旧快照释放。首轮加载 10,226 ms，后九轮中位数 10,392 ms，全部轮次范围 10,226–10,797 ms；最高峰值工作集 445,702,144 字节。关闭后私有字节在约 61.6–66.8 MB 波动，末轮为 64.7 MB。保留此口径用于候选版本对比。

扩展复测使用 `IEC61850_SCL_TIMING=1`。原生窗口中每组均收到 100 次目标控件 Paint 事件。Paint 进入和本次 Qt 事件派发返回分别作为绘制完成的下界和上界。Windows 桌面合成时间未单独测量。

| 操作 | 同步 P95 ms | Paint 进入 P95 ms | 派发返回 P95 / P99 ms |
| --- | ---: | ---: | ---: |
| 树选择 | 5.32 | 17.10 | 19.09 / 20.17 |
| 树选择，详情收起 | 3.94 | 19.24 | 22.00 / 23.43 |
| 数据页切换 | 5.38 | 8.15 | 22.69 / 30.96 |
| 有筛选条件时切换报告与对象页 | 6.24 | 10.64 | 21.00 / 22.76 |
| 详情切页 | 0.28 | 0.70 | 3.39 / 3.68 |

筛选操作从查询已启动时开始切页。大搜索完成后，另一查询在启动后取消，待工作线程退出再检查取消状态仍有效。扩展测试通过，最大心跳间隔 147 ms。390 次详情刷新中，`UpdateDetails` 最大 15.65 ms，`setHtml` 最大 15.38 ms，Links Reset 最大 1.41 ms。大搜索最大单批 132,374 行，交付 9.62 ms，超过 8 ms 的拟定目标。收起详情时仍有详情构造，且同步耗时下降未降低绘制延迟。这两处是已测得的优化线索。该路径没有复现此前的卡死，根因仍由 Task3 复核。扩展测试构建的主 EXE SHA-256 为 `C7851F105B4374666292F9F5E240CE84933672156ED287E307B3DC68A2C9B11E`。

复现命令（从仓库根目录，配置与构建先使用 UCRT64 的 PATH）：

```powershell
cmake --preset windows-debug -B build/task4-t40 -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe
cmake --build build/task4-t40 --parallel 6
ctest --test-dir build/task4-t40 --output-on-failure
$env:IEC61850_LOCAL_SCL = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$env:QT_QPA_PLATFORM = 'windows'
$env:Path = (Resolve-Path build/task4-t40/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
$env:IEC61850_SCL_TIMING = '1'
& build/task4-t40/bin/MainWindowBaselineTest.exe SclNativeTiming -o build/task4-t40/Task4Native.txt,txt
Remove-Item Env:IEC61850_SCL_TIMING
& build/task4-t40/bin/MainWindowBaselineTest.exe SclLocalCycles -o build/task4-t40/Task4Cycles.txt,txt
```
