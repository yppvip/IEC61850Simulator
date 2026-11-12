# IEC61850Simulator 开发约定

## 当前阶段：主程序 UI 骨架

- 使用 CMake 和 Qt Widgets；顶层窗口为 `QMainWindow`，停靠布局由 Qt Advanced Docking System（ADS）统一管理。
- 首期不实现复杂 QML/MDI 页面。服务器与客户端详情先作为 ADS Dock 的占位内容；需要富交互页面时，再由 `QQuickWidget` 承载 QML。
- 首期 UI 包含：SCL 文件打开与扩展名校验（`.icd`、`.scd`、`.cid`、`.ccd`）、新建服务器/客户端工具栏按钮、服务器/客户端侧边列表、详情工作区和状态栏。
- ADS 与 `QMainWindow::addDockWidget()` 不得管理同一批停靠窗口；布局保存与恢复通过 ADS 完成。

## 文件与调用边界

- 程序入口位于 `app/ui/`；UI 窗口、Dock 控制器和列表模型位于 `src/gui/`；Redis 配置与进程/连接状态管理位于 `src/core/`。
- 自有 C++ 文件使用 UpperCamelCase；CMake、JSON、文档使用 UTF-8（无 BOM）和 LF。
- UI 只调用 ViewModel 或应用服务，不直接访问 IEC 61850 协议实现或 Redis 客户端。
- 后续 Redis 订阅回调不得直接修改 GUI 对象，必须使用 queued signal/slot 投递给 GUI 线程。

## 依赖与说明文件维护

- 库源码、许可证及来源信息保存在 `third_party/`，固定版本并由 CMake 构建；本机构建产物不提交。独立运行的依赖程序及必要配套文件在 `tools/` 管理。
- Redis 客户端使用 `third_party/hiredis-1.3.0`，静态链接；命令与响应通过 hiredis 处理，不继续手写 RESP。封装位于 `src/core/RedisClient.*`，UI 不直接调用 hiredis。
- 当前 PING 在 Qt Concurrent 后台任务执行，每次独占 context；不得跨线程共享 context，不得在 GUI 线程执行阻塞 Redis I/O。订阅另行接入。
- 开发时同步维护 `README.md`、`AGENTS.md`、`docs/DEVELOPMENT.md` 及适用的第三方依赖记录。
- `.gitignore` 区分库构建产物与 `tools/` 中需要分发的 EXE/DLL，不得全局忽略 EXE/DLL。历史已跟踪的 Redis PID 本轮保留用户修改及跟踪状态。

## QtADS 构建与部署

- QtADS 源码固定在 `third_party/Qt-Advanced-Docking-System-5.0.0`，由根 CMake 通过 `add_subdirectory()` 构建并链接。
- 运行时 DLL 随可执行文件部署到 `build/<preset>/bin/`，由 CMake 的构建后步骤或 `install()` 规则完成。
- Windows 默认构建的 `DeployRuntime` 目标必须部署 Qt 插件及递归第三方 DLL；启动回归不得继承 MSYS2 的 PATH，以验证双击启动所需依赖完整。
- `tools/QtADS/` 仅可作为本机兼容缓存；其中的二进制文件不提交到版本库。

## Redis 生命周期

- 从 `config/Redis.json` 读取 `host`、`port`、`startupTimeoutMs`、`autoStart` 和 `redisWorkingDirectory`；`redisWorkingDirectory` 可为绝对路径，或相对 `bin/config/Redis.json` 的路径，控制 RedisService、`redis.conf` 与 Redis 工作目录的位置。首期不配置密码、数据目录或日志目录。
- 健康检查以当前 `host:port` 的 Redis `PING` 成功为准，而非 Windows 服务名。
- 若能连接现有 Redis 实例，直接使用；不要求由本程序创建。
- Redis 不可用时，主 UI 必须正常启动，并弹窗提示“Redis 数据库不可用”，同时在状态栏和日志中反映故障。
- 首期不自动停止 Redis，包括由 `RedisService.exe` 启动的实例。启动时必须显式传递 `tools/Redis/redis.conf`，并以 `tools/Redis/` 为工作目录，避免该工具误从主程序 `bin/` 目录查找默认配置。

## 实施与验证顺序

必须分阶段实施；每个阶段通过其验证门槛后，才能进入下一阶段。不得将未验证的 Redis 启动、ADS 部署和 UI 业务逻辑一次性合并实现。

1. **CMake 与 QtADS。** 补齐根 CMake、Preset 和 Qt/ADS 依赖目标。
   - 验证：Debug 配置成功；主程序能编译并启动空 `QMainWindow`；QtADS DLL 自动部署到 `build/<preset>/bin/`；启动时不依赖 `tools/QtADS/`。
2. **Redis 配置与可用性。** 实现 `Redis.json` 加载、目标 `host:port` 的 `PING` 检查和连接状态模型。
   - 验证：已有 Redis 时直接连接；Redis 不可达时 UI 仍启动、弹出“Redis 数据库不可用”并在状态栏反映故障；`RedisService.exe` 的参数、超时和退出码先通过独立试验确认，再接入 `autoStart`。
3. **UI 骨架。** 实现 ADS 主窗口、文件菜单、工具栏、服务器/客户端列表和详情占位 Dock。
   - 验证：仅可选择 `.icd`、`.scd`、`.cid`、`.ccd` 文件；两个新建按钮分别创建占位实体并同步更新列表和详情区；不执行 SCL 解析或 IEC 61850 通信。
4. **布局与最小测试。** 加入 ADS 布局保存/恢复、日志和最小自动化测试。
   - 验证：重启后可恢复有效 Dock 布局；无 Redis 场景和正常 Redis 场景均可重复启动；Debug 构建可运行。SCL 解析、IEC 61850 通信和 Redis 消息订阅另行接入。

不得修改或删除用户正在进行的 `tools/Redis/` 迁移，除非用户明确要求。

## 后续阶段：SCL 离线分析

- 设计与执行方案位于 `docs/Tasks/Task1.md`，P0 至 P6 已通过，记录于同目录的 `Task1P0.md` 至 `Task1P6.md`。完整 XSD 校验作为 P7 单列门槛。
- 规则目录为 `docs/Tasks/SclRules.md`，自建样本位于 `tests/fixtures/scl/`；P0 的 XML/计数检查不等于产品语义校验。UI 基线测试必须使用隔离的 QSettings 路径；Dock 的销毁回调必须在其访问的成员仍有效时结束。
- 本期扩展继续使用 Qt Widgets + ADS；只读 SCL 分析通过应用服务在后台执行，不依赖 Redis，不自动创建仿真实例，不接入设备通信。
- SCL 领域解析、类型与引用处理建议位于 `src/iec61850/scl/`；UI 只调用 ViewModel/应用服务，不在 GUI 线程解析大文件、全量搜索或校验。
- 区分 XML 格式检查、内部一致性和完整 XSD 校验；未执行的检查明确展示，不承诺发现所有工程缺失。区分原始字段、推导引用与运行时信息。
- 外部真实工程 SCD 仅作本地只读验收，不上传或直接提交仓库；自动化测试使用自建脱敏样本，原文预览和诊断必须对应同一文档快照。
- P1 的 `SclAnalysis` 仅依赖 Qt Core。NodeId 只在所属文档内有效；按 namespace URI 区分标准和扩展节点。失败结果不发布部分模型，原文字节偏移不得从 XML 解析器的字符偏移推算。
- P2 的引用结果保留原始声明、候选及失败原因。基于明确作用域解析，候选不唯一时保留歧义。报告的原始 rptID、派生定义引用及 MMS 映射使用独立字段。诊断及规则执行状态与同一文件快照绑定。
- P3 的 `SclAnalysisService` 在事件循环线程管理单文档会话。工作线程使用独立取消标记，替换请求时仅保留最新路径。GUI 仅接收完整不可变快照，失败、取消或过期结果都保留当前文档。
- P4 的工作台复用 ADS 工作区 Dock。后台快照提供浏览索引，GUI 通过按需模型读取行与节点。切换快照时清理选择和历史，失败文件的原文使用其独立快照。
- P5 在后台执行全局搜索和表格筛选，搜索批次属于发起请求的快照。切换文档时取消旧查询并清理结果。问题导出保留快照指纹、规则版本、覆盖状态及原文位置，CSV 文本字段须防公式执行。
- P6 以 ADS 用户布局版本和内容摘要验证持久化静态布局，兼容原版本 0。动态详情 Dock 和 SCL 会话只在本次运行中使用。真实样本十轮测试为显式本地验收，常规 CTest 使用自建样本。
- SCL 工作台视觉与交互优化按 [Task2](docs/Tasks/Task2.md) 的阶段门槛实施，继续沿用 Qt Widgets + ADS。导航和原文高亮仅使用当前操作对应的快照，完整 XSD 仍由 P7 独立交付。
- Task2 必须交付离线交互式引用拓扑。浏览器只消费应用服务从对应快照生成的有界图数据，逐段解析证据由现有 C++ 解析逻辑提供。工程数据及图页面不上传。
- Task2 后续回归使用 [T2.0](docs/Tasks/Task2T2.0.md) 的同机、同构建口径对比加载耗时、峰值工作集及离屏导航 P95。
- T2.1 的树表定位和历史恢复以快照节点及问题原始序号为依据，按当前成员范围和筛选结果重新定位。筛选遮挡时清除旧行高亮，保留树与详情。
- T2.2 的 XML 高亮只处理有限预览窗口。未知词法状态使用普通文本，直到同快照的可信解析位置。行号和截断提示独立于可复制文本，引用路径仅为有快照证据的节点提供跳转。
- T2.3 的同对象反查按 LN 实例、模板路径、FC 和数组访问识别。模板复用单独展示。拓扑 HTML 中的工程字段以编码数据传递，渲染时只作为文本，旧快照的生成结果不得回填当前工作台。
- T2.4 的视觉样式限定在 SCL 工作台。离屏缩放和真实文件性能结果见 [T2.4](docs/Tasks/Task2T2.4.md)，实体跨屏与浏览器内操作需单独复核。
- SCL 浏览卡顿按 [Task3](docs/Tasks/Task3.md) 分阶段定位与优化；当前仅为方案。先取得交互与 GUI 线程热点证据，再修改刷新、引用索引或批次交付；离屏少量报告导航通过不能替代真实桌面验收。保留快照身份与成员作用域语义，新增异步结果必须校验请求代次。
- 本轮图标、上下布局及 IED 浏览范围按 [Tasks/Task1](Tasks/Task1.md) 实施。原 `docs/Tasks/Task1.md` 保留。工作台内部使用独立 ADS 管理器与独立布局摘要，隐藏工作台时同步隐藏浮动窗；不把内部 Dock 交给主窗 ADS 或 QMainWindow 再次管理。
- IED 范围独立于选中节点，分类原始行索引在应用服务后台随快照构建。共享模板按唯一解析的类型引用闭包归属；ConnectedAP 按非空且唯一的 IED 名称归属，未知或歧义归属保留在工程范围。切换数据集必须清除旧成员页记忆，历史需恢复 IED、数据集及问题原始序号。
- 全局搜索明确使用整个工程；表格筛选受 IED/数据集及全部模板开关约束。范围变化取消旧筛选，沿用会话的独立取消标记屏蔽旧批次；不得把旧范围的原始行号应用于新视图。图标使用本地 Qt 资源与 Windows RC，不增加运行时外部服务依赖。
- 成员作用域在选择入口按当前数据集/FCDA 设置，离开后必须置空并取消旧成员筛选，不依赖详情刷新。业务 Tab 按当前范围内未应用文本筛选的内容数显示/隐藏；隐藏当前页时回到概览，抑制默认行导航，避免切页副作用。概览、搜索结果以及仅被筛选遮挡的业务页保留入口。

## Task4：IED 信号与通信查阅（方案阶段）

- 按 [Task4](docs/Tasks/Task4.md) 的 T4.0–T4.5 门槛逐阶段实施；本轮只交付方案与本地 XML 统计，未实现新业务页。继续使用 Qt Widgets + ADS，复用快照、解析器和后台会话；Task3 性能归因与 Task1 P7 完整 XSD 的验收边界保留。
- 四遥是可追溯的业务分类建议，不能仅按数据集名称或 FC 强制归类；信号目录按 LN 实例及类型结构建立，不只统计 FCDA/DOI/DAI。对象、附属属性、成员出现次数分开计数；不将静态配置当成实时值或工程完整性证明。
- GOOSE appID、SV smvID、数值 APPID、MMS Domain/Item、RCB 引用和原始 rptID 分列。ExtRef 缺少 serviceType 时保留证据状态，不能默认 GOOSE；仅对象匹配不能证明某个发布流的接收关系。
- 新业务索引/按需展开在后台执行，设置资源预算，结果校验快照、请求代次与范围。任务导航的空态规则独立于原高级表格的 Tab 隐藏规则；布局变化必须版本化并提供恢复默认布局。
- 用户本轮指定独立程序管理目录为 `E:/Code/IEC61850Simulator/tools/`，库源码仍为 D 盘仓库 `third_party/`。E 盘目录本次不可访问，Task4 不新增独立程序、不迁移现有 tools/Redis、不改运行配置；将来确需新增程序时先核对可访问路径，不能擅自改回其他目录。
