# T4.6 · 双向虚端子关系查阅

2026-11-13。对应 [Task4 第 12 节](Task4.md)。本轮已实现只读的收发关系任务页、成员映射与工作台内局部关系图；未实现接线编辑或在线收发检测。

## 1. 基线与原因

修改前 `build/windows-ucrt64-debug` 的 CTest 9/9 通过。旧实现只有在源控制块字段和成员均匹配时才记录发布流。指定样本缺少这些字段，因此用户在 SV/GOOSE 页看不到已经声明的输入来源，也无法从发送端追到接收端。

样本位于 `D:/1Document/自动配置/20260920/自动测试/公司测试工程/实验室20190101.scd`，9,061,162 字节。SHA-256 `1E46B6FC5255407E0663D63D7F9CCB8BC07CCFA457906157BF125C7C6B0FD7FE`，本轮读取前后一致。仅本机读取，未复制为仓库夹具、未上传工程文件。Private 节点中出现的虚端子 CRC 并不提供标准成员映射，本轮不猜测其厂家语义。

## 2. 实现

- `SclTaskIndex::MemberLink` 独立保存 FCDA–ExtRef 映射。明确绑定与成员候选分开；按唯一源 LN/对象/属性匹配，约束服务、FC、数组及显式控制块声明；同成员多输入、重复成员和多个发布流不合并。预建去重候选列表，避免 GUI 为每个单元格扫描全部成员关联。
- `SclConnections` 后台查询按发布流和接收 IED 分组，从发端和收端查到同一关系。保留没有接收声明的发布配置、未定位到流的输入以及各通信端点的 APPID/MAC/VLAN。
- `SclConnectionView` 通过树、可点击路径、分页多列表、成员详情和 Graphics View 表达关系。图支持成员切换、平移、缩放、适应窗口及有证据节点的原文跳转。用颜色、图标、箭头、线型和文字共同区分服务与证据。
- 查询与详情分别使用取消标记及快照/请求身份；关闭、重载、切范围、换服务、快速过滤不能回填旧结果。源跳转复用现有历史，返回恢复关系及成员页。关系树最多 512 个关系条目、成员每页 200 行，覆盖受限与页码可见。
- 沿用 Widgets + ADS，新增页放入中央任务区，不新增 ADS Dock，内部布局版本保持 2。新页外层滚动区域隔离隐藏页面的最小尺寸，窄窗和高缩放时可滚动访问动作。
- 原 GOOSE/SV 表增添通信参数列，接收列表纳入有成员证据的候选。F1、README、AGENTS、DEVELOPMENT 和 Task4 同步更新。未增加第三方库、外部图标资产、QML、WebView 或在线服务；Redis 与工具迁移保持原状。

## 3. 本地验收

| 关系 | 结果 |
| --- | --- |
| ML2201B → PL2201B，SV | APPID 400B；14 行候选成员到内部地址的映射，源控制块绑定未确认；源/收两侧查询均有断言 |
| IL2201B → PL2201B，GOOSE | APPID 0132；5 行候选；原生 UI 从接收侧和发送侧均可找到同一映射 |
| PL2201B 自身 GOOSE | APPID 0131；19 条原始发送成员；当前文件未见引用它的接收声明，界面不虚构接收 IED |

本地索引汇总为 24 个 IED、47 条可展示发布流、18 个关联接收 IED、190 条候选成员映射，明确源控制块成员绑定为 0。候选映射数不是去重信号数或已确认订阅数；单个 FCDA 可对应多个内部地址。

自建单元样本覆盖明确控制块、缺省控制块、GOOSE/SV 多候选、显式冲突不回退、服务约束、重复 FCDA、DO/DA 包含候选、取消以及超过一页的 223 条成员（200 + 23，第二页从原始序号 201 开始）。可选真实样本断言使用环境变量显式启用；常规 CTest 不依赖该文件。

主窗口测试 `SclConnectionsFlow` 验证当前 IED/服务筛选、映射内容、APPID 摘要、原文信号、真实工作台历史恢复、发端/收端查阅、未见接收方及快速替换/关闭旧查询。另保留原有布局、成员范围、XML 原文、拓扑、搜索、Redis 等回归。

完整 CTest 9/9 通过。一次中间回归出现旧 `SclWorkspaceFlow` 的详情收起布局断言等待超时：新页面的最小尺寸影响了隐藏页所在的 stacked layout。已通过滚动容器隔离尺寸约束修复，原测试及全套回归通过；未放宽测试超时或删除断言。

Windows 原生平台、仅部署目录和 Windows 系统目录的 PATH，指定真实样本流程在 100%、150%、200% 缩放均通过。成员/图截图已检查，150%/200% 下页面需要滚动；这不等于实体跨屏或操作系统原生高对比验收。图在隐藏页首次生成后显示时会重新适配，修复了隐藏 viewport 尺寸造成图缩成小点的问题。

截图与日志均在忽略的构建目录中：

- `relations-members.png`、`relations-graph.png`：100% 的真实样本界面。
- `relations-150-*.png`、`relations-200-*.png`：缩放检查；高缩放首屏不保证展示全部动作，外层滚动条提供访问路径。
- `member-index.txt`：启用本地样本的索引测试，8 passed。
- `relations-ui.txt`、`relations-ui-150.txt`、`relations-ui-200.txt`：原生界面流程。
- `Testing/Temporary/LastTest.log`：常规 CTest 结果。

## 4. 构建与复测

独立目录 `build/windows-relations-debug`，MSYS2 UCRT64 / GCC 16.2.0 / Qt 6.12.0 / CMake Debug / Ninja。保留用户原构建，默认 `DeployRuntime` 已部署 Qt 平台插件及递归 DLL。程序：`build/windows-relations-debug/bin/IEC61850Simulator.exe`，SHA-256 `7E29F2F66F77F22526EC7B3ADC8D13B2E7615C251927A76EDE383C8078A76A18`。

从仓库根目录使用 PowerShell：

```powershell
$env:PATH = 'D:/msys64/ucrt64/bin;C:/Windows/System32;C:/Windows'
cmake -S . -B build/windows-relations-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64
cmake --build build/windows-relations-debug -j 6
ctest --test-dir build/windows-relations-debug --output-on-failure
```

显式本地只读索引验收：

```powershell
$env:IEC61850_RELATION_SCL = 'D:/1Document/自动配置/20260920/自动测试/公司测试工程/实验室20190101.scd'
./build/windows-relations-debug/SclTaskIndexTest.exe -o build/windows-relations-debug/member-index.txt,txt
```

清洁 PATH 原生流程：

```powershell
$env:PATH = 'D:/Code/IEC61850Simulator/build/windows-relations-debug/bin;C:/Windows/System32;C:/Windows'
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
$env:IEC61850_RELATION_ARTIFACT = 'D:/Code/IEC61850Simulator/build/windows-relations-debug/relations'
./build/windows-relations-debug/bin/MainWindowBaselineTest.exe SclConnectionsFlow -o build/windows-relations-debug/relations-ui.txt,txt
```

150%/200% 分别用 `QT_SCALE_FACTOR=1.5` 和 `2` 并更换日志/截图前缀。运行时无需 MSYS2 PATH；命令行测试 harness 的其他可执行文件位于构建根目录，常规 CTest 按前述开发工具链 PATH 运行。

## 5. 边界

本轮完成指定样本与合成样本的关系功能验证，未将缺省声明推断升级为现场接收确认。没有新增全站自由布线编辑或全工程无界关系图；对象/DA 包含、未知数组和自定义 Private 的覆盖限制保持可见。完整 XSD、Task3 全场景性能基线、真实工程十轮性能对比、实体跨屏及目标用户任务计时仍按原独立门槛执行，不以本次关系截图替代。
