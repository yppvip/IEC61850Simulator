# Task6 · T6.3 后台查询与信号地址

日期：2026-11-19。状态：**已通过 T6.3 门槛**。本轮在 C 盘工作区继续另一设备已完成的 T6.0–T6.2，仅交付应用服务与自动化验证；正式 MMS 页面、导航、历史、统一检查/导出在 T6.4，综合桌面与初学者验收在 T6.5。

## 1. 快照与查询交付

`SclAnalysisService::Run` 在原后台流水线中构建 `MmsProjector` 和轻量 `SclMmsIndex`，与基础分析、浏览/任务索引一起发布 `shared_ptr<const SclSnapshot>`。MMS 保留自己的 mms-0.1 规则集、诊断和覆盖，不混成基础 0.2 已执行规则。失败/取消打开仍保留当前快照；MMS 全局预算失败时没有部分 MMS 记录，查询返回明确不可用状态，基本浏览仍可使用。

新增 `src/core/SclMmsQuery.hpp/.cpp`，链接现有 SclApplication / Qt Concurrent，无新依赖：

| 查询 | 行为与身份 |
| --- | --- |
| Reports / Instances / Members | 报告 NodeId、实例定义及原始序号、FCDA 节点及原始序号；成员保留父报告和共享数据集上下文，不按实例复制成员 |
| Clients / ReverseClients | 分别保留 ClientLN、Association、Report ExtRef 原语义；按目标 LN 及其 IED/AP/Server 祖先反查，候选仍为候选；明确源冲突不回退猜测 |
| Servers / Endpoints / Capabilities | IED/AP/Server 范围；ServerAt 别名访问共享模型，ConnectedAP 端点独立；原始 Services 可见性不等于覆盖优先级 |
| Signals / Addresses | 复用 SignalCatalog，未入数据集对象同样可查；仅选择对象后展开 DA/BDA，描述复用 Task5 投影，原文复用 SourceIndex |
| ReverseSignal | 原对象下已解析的 FCDA 出现及使用报告，保留 FC、DA 路径、数组访问；不等同客户端接收或实例绑定 |
| Search | 定义/原始 rptID/数据集、预期实例、客户端、成员对象引用及已有信号描述；带 `$` 或 `/` 的地址文本按预算触发属性展开，支持 Domain/Item 组合文本；保留现有基础全局搜索入口 |
| Graph | 报告→数据集、预期实例、客户端声明；数据集→成员→对象引用。Report ExtRef 指向源报告，边类型及解析状态独立；仅交付有界数据，不生成/上传页面，不推断在线收发 |

结果含文档/原文共享指针、SHA-256、请求身份、完整请求范围及记录/字段键。字符串为完整值，包含中文、换行和长字段；显示截断及复制交互留在 T6.4 适配。原文证据指向同文档元素，没有猜属性字符偏移。

## 2. 线程、取消与资源契约

`QuerySclMms` 是工作线程入口。`SclMmsQuerySession` 在所属事件循环线程管理请求，用 QtConcurrent 执行查询；每个会话最多一个工作任务及最新待处理请求。列表、详情、地址、搜索、图、复制分别使用独立会话，避免互相取消。`SetContext(snapshot, scope)` 切文件/范围；隐藏和关闭调用 `Cancel()`。即使旧工作任务已完成但尚未派发，也检查原快照对象、独立代次、范围和取消标记后才交付。关闭不等待工作线程访问 GUI，任务仅持有快照及独立取消标记。

- 单页最多 200 行，offset 为筛选结果中的偏移，返回键始终是原始记录身份。
- 单次查询最多 100,000 个工作单元；遍历和展开逐步检查取消。
- 局部图最多 128 节点、256 边；属性深度 32、路径长度 2048。
- 成员引用索引最多 500,000 项；客户端祖先反查最多 500,000 项，每条祖先链最多 256 层。领域投影沿用 T6.2 的独立预算。
- `limited=true` 时 `total` 是已观察下界，`hasMore` 不能解释为完整剩余量，也不保证增大 offset 能穿过工作预算。应缩小 IED/AP/Server 范围或更具体地查询，不能把受限搜索称为完整全工程结果。

IED 范围使用现有 `signalsByIed` 及 `browse.iedOwners`，不在 GUI 重建或扫描全部目录。地址仅在唯一 LN、DO/SDO 路径、已有唯一类型引用及明确 Domain/FC 下生成；重复声明、缺类型和未知名称保留限制。同名 DA 在不同 FC 下分别处理。结构、DO 成员、数组模板及 alternate access 分型；未选择数组元素不给出标量 Item，不默认下标 0。未实现在线实例核对或设备实际值。

## 3. 自动化与本地验收

独立构建：`C:/Code/IEC61850Simulator/build/task6-t63`，Debug / GCC 16.2.0 / Qt 6.11.1 / CMake 4.4.2 / Ninja 1.13.2，开启独立原型回归。正式 EXE 为该目录 `bin/IEC61850Simulator.exe`。初次配置时 GCC 的 cc1 在沙箱内返回 DLL 加载失败；沙箱外验证和构建成功，没有安装/替换编译器或修改系统运行库。第三方部署保留已有翻译目录与可选 D3D12 提示。

清洁 PATH（本次 bin + Windows System32/Windows）完整 CTest **12/12 通过**，含 T6.1 独立原型和原有 ADS、GUI、GOOSE/SV、Redis 回归。新增 `SclMmsQueryTest` 最终 13 项通过（含初始化/清理）、0 失败、显式本地用例默认跳过，使用自建夹具及内存变体，覆盖：

- 正式快照集成、失败打开保留快照、关闭后持有不可变结果。
- 报告/实例/重复 FCDA 原始身份、共享数据集父报告、253 成员的 200+53 分页及完整长值。
- IED/AP/Server 及 ServerAt 别名范围、连接端点原值、客户端 LN/IED 反查。
- 未入数据集信号、嵌套 BDA、FC、DO 级成员、数组模板及 alternate access；重复 LN、重复类型与非法地址名不伪造标量。
- 已有描述/成员对象引用、Domain/Item 搜索、图边语义及预算、预取消与投影预算失败。
- 连续替换查询、切范围/同字节新快照、取消/关闭、独立消费者及析构后工作任务存活。
- 中文多字节原文的可信字节位置及完整值不依赖 DisplayRole。

真实样本由本轮用户指定为 `D:/3Document/实验室20190101.scd`，9,061,162 字节。读取前后与快照 SHA-256 一致：

`1e46b6fc5255407e0663d63d7f9ccb8bc07ccfa457906157bf125c7c6b0fd7fe`

三轮显式 `LocalMmsQueries` 均通过：24 个 IED、6,772 个 FCDA、114 个报告定义、0 个确定预期实例、0 个客户端声明；首报告成员页 3 行、首对象地址页 4 行、首报告局部图 8 节点。全工程 MMS 搜索如实标资源受限，限定报告所属 IED 后完成且不受限。真实样本缺少的客户端/实例/数组边界由合成测试覆盖，未宣称真实工程覆盖这些情况。

最初 IED 搜索仍全量遍历信号目录，真实用例的“不受限”断言失败；修正为复用现有 IED 信号范围索引，并去掉无 AP/Server 约束时的父链遍历，随后三轮通过。最终 Ready 观测为 2,891 / 2,838 / 2,893 ms，中位数 2,891 ms；QCore 事件循环探针最大间隔为 102 / 70 / 92 ms，包含测试轮询及同步 Core 断言，不是原生输入/绘制时延。性能只作本机后台事件循环探针，不与另一设备/另一文件的 T6.0 比较，不代替 T6.5 的窗口绘制、内存、DPI、十轮释放及用户走查。日志见忽略的 `LocalMmsQuery-1.txt` 至 `LocalMmsQuery-3.txt`。

## 4. 复现与剩余范围

```powershell
$env:Path = 'C:/msys64/ucrt64/bin;' + $env:Path
cmake -S . -B build/task6-t63 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/msys64/ucrt64 -DCMAKE_C_COMPILER=C:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=C:/msys64/ucrt64/bin/ninja.exe -DIEC61850_BUILD_MMS_PROTOTYPE=ON
cmake --build build/task6-t63 --parallel 6
$env:Path = (Resolve-Path build/task6-t63/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
& C:/msys64/ucrt64/bin/ctest.exe --test-dir build/task6-t63 --output-on-failure
$env:IEC61850_LOCAL_SCL = 'D:/3Document/实验室20190101.scd'
./build/task6-t63/SclMmsQueryTest.exe LocalMmsQueries -o build/task6-t63/LocalMmsQuery.txt,txt
Remove-Item Env:IEC61850_LOCAL_SCL
```

日志保存在忽略的 `build/task6-t63/`：Build.txt、CTestFinal.txt、MmsQueryFinal.txt、LocalMmsQuery-1.txt 至 LocalMmsQuery-3.txt。常规 CTest 不要求外部工程。没有复制、修改、上传样本，没有新增第三方库/图标或独立工具，第三方依赖记录无新增条目；`tools/Redis/` 与运行配置未改。

T6.3 到此为止。T6.4 再消费这些会话/结果接入正式 Widgets + ADS 页面、历史、复制与原文交互、统一检查导出；T6.5 再执行综合验收。T6.2 未实现的标准语义、完整 XSD、Task3 及 Task5T5.6 未通过项继续保留。
