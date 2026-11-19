# Task6 · T6.5 综合验收

2026-11-19。状态：**自动化、合成大表和本地真实文件验收已执行；整体验收未全部通过**。三名初学者、实体跨屏、系统级高对比/实际主题及同机 T6.0 增量对比仍缺验收证据，不以开发者或自动化测试替代。T6.4 功能集成记录见 [T6.4](Task6T6.4.md)。

## 构建与边界

独立目录 `C:/Code/IEC61850Simulator/build/task6-t65`；正式 EXE 为该目录 `bin/IEC61850Simulator.exe`，测试同一窗口组件的入口为 `bin/MainWindowBaselineTest.exe`。Debug / GCC 16.2.0 / Qt 6.11.1 / CMake 4.4.2 / Ninja 1.13.2。源码基点 `54213c9` 加本轮未提交修改，最终文件及 EXE 指纹记于忽略目录 `SourceManifest.json`。

配置/构建使用 C 盘 MSYS2。运行 PATH 仅包含本次 bin、Windows System32 和 Windows；不借 MSYS2 搜索运行库。没有新增依赖、网络协议、订阅、图标或独立工具。Qt/ADS 部署沿用既有规则；第三方翻译目录和可选 D3D12 提示没有改成必需依赖。

## 验收矩阵

| 项目 | 实际结论 |
| --- | --- |
| Debug、部署、常规回归 | 清洁 PATH CTest 13/13 通过，包含原有 ADS、GOOSE/SV、Redis、原型及新的 MMS 压力目标 |
| 输入/来源/领域边界 | Reader、ReferenceResolver、MmsProjection、MmsQuery 原有合成正反例通过：非法 XML、DTD、命名空间、类型/引用、BR/RP、缺省/非法/歧义、客户端独立语义、数组/DO、取消及预算；未实现的版本语义仍为限制 |
| 完整值与原文 | 正式 MMS 表双击一次复制且不导航；长值、中文/emoji/换行、字段所在元素、可信原文位置与返回通过 |
| 导航与并发 | 报告/成员/属性、共享数据集父报告、客户端方向、全工程搜索跨范围和历史、高级表原始身份一致、切范围/文件/取消/关闭通过 |
| 局部图与布局 | 有界图、箭头/候选边、节点列表键盘入口、详情收起/恢复、旧 ADS 布局回归通过；未验证实体跨屏拖动 |
| 1 千/5 万/50 万对象 | 真实共享类型结构生成 1/50/500 个 LN × 1,000 个 DO，经正式服务建立目录；GUI 每页最多 200 行，复制、分页和顶部记录恢复通过 |
| 大目录覆盖限制 | 5 万/50 万查询触发 100,000 工作单元上限，结果明确为受限下界；不是完整遍历或完整全工程搜索通过 |
| 真实文件 | 指定实验室 SCD 十轮只读会话通过，读取前后指纹一致，每轮关闭后快照释放 |
| 原生尺寸/DPI | 100%/150%/200%、窄窗和宽窗的程序驱动 Qt 交互通过，尺寸见下表 |
| 暗色/高对比 | 模拟窗口调色板通过；系统高对比、实际系统主题未验收 |
| 初学者 | 尚未组织三名实际参与者，未验收；记录表见 [用户走查](Task6UserWalkthrough.md) |
| 完整标准/工程完整性 | 完整 XSD 属 Task1 P7，在线核实与外部工程规范对照未执行；不把规则无发现称为工程完整 |

常规测试只使用自建/合成数据。`SclMmsStressTest` 为独立 CTest 目标，避免大规模用例挤入原窗口测试的超时预算。规模测试不把同一 FCDA 复制 50 万份，而是共享模板并建立实际 LN 实例目录；不按预期报告实例复制成员。

## 指定真实文件

文件：`D:/3Document/实验室20190101.scd`，9,061,162 字节。读取前后 SHA-256 均为：

`1e46b6fc5255407e0663d63d7f9ccb8bc07ccfa457906157bf125c7c6b0fd7fe`

聚合核对：24 个 IED，114 个报告定义（BR 82、RP 32），全部 114 个显式非空 rptID；报告使用 114 个唯一数据集、3,491 次原始成员出现，全工程 6,772 个 FCDA。可解析的 LN+DO 结构目录有 37,571 个唯一条目，目录未触及预算；不能将其等同实际业务信号完整性。Server/AP 访问路径 44、端点 34、Services 组 24。

根版本档案未知，声明 max 合计 1,654，确定预期实例 0，客户端声明 0；max 不是已确认实例数。真实文件没有覆盖缺 rptID、多个客户端等情况，这些由合成用例验证，不编造真实覆盖。文件未复制到夹具、上传或修改。

## 性能与释放

本机 T6.0 原基线来自另一设备和另一文件，不能计算本次相对 T6.0 的 +15% 结论。本轮保留了同机、同 Qt/GCC、同实验室 SCD、同隔离设置的冻结 T6.3 EXE 作为增量参照，三轮均走既有高级报告/原文路径。它只比较 T6.3→当前构建，不替代 T6.0 门槛。

| 指标 | 冻结 T6.3 三轮中位数 | 本轮同路径三轮中位数 |
| --- | ---: | ---: |
| Ready | 3,020 ms | 2,896 ms |
| 交互后峰值工作集 | 198,365,184 B | 201,957,376 B |
| 既有报告导航 P95 | 25.675 ms | 23.552 ms |
| 原文详情 P95 | 10.381 ms | 9.133 ms |

相对冻结 T6.3，Ready 为 -4.1%、交互后峰值为 +1.8%，在 +15% 参考范围内；这不是相对 T6.0 的结论，也不证明 Task3 卡顿已归因或修复。

新 MMS 主流程另测十轮：每轮打开、首报告/成员可读、24 次选择/详情/复制及四次原文往返，关闭后用 weak_ptr 证明完整快照已释放，再记录工作集。P95 使用 nearest-rank，与原 100 次导航的第 95 项口径一致；每轮原文样本仅四次，其 P95 等于最大值，保留小样本限制。最终指标从 `LocalTenCyclesFinal.txt` 汇总到 `SummaryFinal.json`，以最终汇总表为准。

| 新 MMS 流程指标 | 十轮实测 | 目标判断 |
| --- | ---: | --- |
| Ready 中位数 | 2,910.5 ms | 单列本机值，不与另一设备 T6.0 混算 |
| Ready 后首报告 / 首成员可读中位数 | 97 / 107 ms | 包含查询与事件派发机会 |
| 选择反馈 P95 中位数 / 最差轮 | 0.668 / 0.740 ms | ≤100 ms |
| 复制 P95 中位数 / 最差轮 | 10.564 / 11.263 ms | ≤100 ms |
| 局部详情 P95 中位数 / 最差轮 | 11.657 / 14.763 ms | ≤500 ms |
| 原文 P95 中位数 / 最差轮 | 10.391 / 221.238 ms | ≤500 ms；最差轮包含首次冷预览 |
| 列表交付 / 字段交付最大值 | 1.417 / 0.410 ms | 各自 ≤8 ms |
| 可见行测高批次最大值 | 1.400 ms | ≤8 ms |
| 事件循环心跳最大间隔 | 217 ms | 本工作负载未观察到 >1 s 间隔 |

十轮关闭后的工作集从 131,309,568 B 到 133,222,400 B，最高 133,361,664 B；没有持续无界增长迹象。每轮完整快照 weak_ptr 均过期，Qt/系统缓存没有被当成仍持有工程快照。释放后工作集与活动会话峰值不是同一个指标。

本地三轮 Core 分段探针分别记录 Reader、ReferenceResolver、Browse/Scope、TaskIndex、MmsProjector 和 MmsIndex，日志为 `Inventory-1.txt` 至 `Inventory-3.txt`；只输出聚合数与指纹。探针不是 GUI Ready，不能直接相加替代窗口呈现时延。

三轮分段中位数依次为 Reader 1,003 ms、引用 634 ms、浏览/范围 170 ms、任务目录 875 ms、MMS 投影 152 ms、MMS 索引 1 ms。真实全工程 MMS 搜索仍触及预算并明确不完整，限定 IED 后完成；`LocalQueriesFinal.txt` 再次验证成员、地址、图、范围和搜索，未把受限查询记为完整覆盖。

测试计时包含 Qt 事件派发/绘制机会，未测物理输入至 DWM 呈现时间。GUI 交付与可见行测高使用局部计时属性；工作集含 Qt/系统分配器缓存，关闭后不要求返回进程初始值。内存趋势、快照释放和峰值分别记录，不只看进程峰值一个数字。

## 原生窗口与视觉

| 场景 | 实际逻辑尺寸 | 结果 |
| --- | --- | --- |
| 100% | 1280×800 | 通过 |
| 150% | 1280×707 | 通过；可用桌面 1280×688，窗口尺寸受系统约束 |
| 200% | 962×531 | 通过；可用桌面 960×516，窗口尺寸受系统约束 |
| 窄窗 100% | 620×560 | 通过 |
| 窄窗 200% | 620×531 | 通过；请求高度 560 受桌面约束 |
| 宽窗 100% | 1920×1061 | 通过；请求 1920×1080 未按原尺寸完整验收 |

原生用例检查实际表格行、字段和原文锚点的可见性，不只检查模型有数据。程序驱动事件不等于三名初学者或实体跨屏操作。暗色/高对比仅为模拟调色板。

合成截图保存在 `native-1/`、`native-1.5/`、`native-2/`，包含 Reports、Members、Addresses、Source、Graph。截图检查发现并修正子页面任务选择器空白及图连线穿过无关节点；最终图采用间隙走线并检查实际描边不穿节点。地址页 Domain/Item 已位于首屏，详情与图的布局在后台交付后采集。真实工程截图未上传或用于模型视觉检查。

## 复现与待验收

```powershell
$env:Path = 'C:/msys64/ucrt64/bin;' + $env:Path
cmake -S . -B build/task6-t65 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe -DIEC61850_BUILD_MMS_PROTOTYPE=ON
cmake --build build/task6-t65 --parallel 6
$env:Path = (Resolve-Path build/task6-t65/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
& C:/msys64/ucrt64/bin/ctest.exe --test-dir build/task6-t65 --output-on-failure
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
$env:IEC61850_LOCAL_SCL = 'D:/3Document/实验室20190101.scd'
$env:IEC61850_MMS_CYCLES = '10'
./build/task6-t65/bin/MainWindowBaselineTest.exe SclMmsLocalAcceptance -o build/task6-t65/LocalTenCyclesFinal.txt,txt
./build/task6-t65/SclMmsQueryTest.exe LocalMmsInventory
Remove-Item Env:IEC61850_LOCAL_SCL,Env:IEC61850_MMS_CYCLES
```

常规 CTest 前应移除真实文件环境变量；T6.2 的旧指定样本专用断言不能拿来验收本次实验室文件。真实核对使用本轮通用 inventory/工作台入口。

剩余门槛：三名初学者无指导走查、实体跨屏动态 DPI、系统高对比/实际主题、受桌面限制的完整目标尺寸，以及严格同机同文件 T6.0 +15% 对比。Task3、Task5T5.6 未通过项及 P7 边界保留。README、AGENTS、DEVELOPMENT 同步记录本轮实际结论，不将 T6.5 整体标为通过；Redis 迁移和运行配置未改。
