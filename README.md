# IEC61850Simulator

IEC61850Simulator 是一个基于 C++、Qt/QML 和 CMake 开发的 IEC 61850 仿真工具，目标是在同一程序中同时提供客户端与服务端仿真能力，并为后续 Python 脚本扩展预留接口。

项目支持在 Windows 和 Linux 上编译与运行。跨平台代码应优先使用 C++ 标准库及 Qt 提供的抽象，平台专用实现需要隔离在独立模块中。

> 项目当前处于工程骨架设计阶段，协议能力、依赖版本及交付方式尚需逐步落实。

## 当前开发阶段

主程序 UI 骨架已完成：使用 `QMainWindow` 与 Qt Advanced Docking System（ADS）提供可自由停靠的服务器、客户端和详情工作区。当前已落地的功能包括：

- 文件菜单支持打开和重新加载 ICD、SCD、CID、CCD 文件，并可取消后台分析。状态栏显示处理阶段、结果或失败原因。
- 工具栏可创建服务器和客户端占位实体，并在侧栏和详情 Dock 中显示状态。
- Redis 通过 hiredis 在后台线程执行 `PING` 检查；不可用时不阻止 UI 启动并给出提示。
- ADS 静态布局在退出时保存，启动时校验版本与内容后恢复。「视图 → 恢复默认布局」可重新排列停靠区。
- 日志输出到可执行文件同级的 `logs/IEC61850Simulator.log`。

Redis 连接配置位于 `config/Redis.json`。应用以其中的 `host:port` 执行 Redis `PING` 检查；Redis 不可用不会阻止 UI 启动，但会显示“Redis 数据库不可用”。详细的调用边界和实施顺序见 [AGENTS.md](AGENTS.md)。

### SCL 离线分析（P6 已通过）

打开文件后，ADS 工作区显示 SCL 概览、导航树、报告、数据集及成员、对象、通信、模板和问题。选中对象即可查看属性与原文，通过引用及使用方可双向跳转。「视图」菜单可切回仿真工作区。设计与执行方案见 [Task1](docs/Tasks/Task1.md)。

分析工作台支持全局搜索、当前表格筛选、前后退和 JSON/CSV 问题导出。按 F1 查看应用内帮助，或在「文件」菜单关闭当前 SCL 文档。本机 MSYS2 UCRT64 的 Debug 构建和 7 组 CTest 均通过。十次真实 SCD 加载与关闭及复现命令见 [P6 验收记录](docs/Tasks/Task1P6.md)。

工作台默认左侧工程树、右上概览与业务表、右下属性/引用/原文。拖动 ADS 标题栏可重新停靠或浮动，布局在退出后保存；「详情」控制详情显示，「重置工作台布局」恢复默认排列。主窗口及 Windows EXE 使用 `res/pic/函数发生器.png` 图标，资源已嵌入程序。

没有实际内容的业务 Tab 自动隐藏；离开数据集/FCDA 节点时清空并隐藏成员页，回到有成员的数据集后恢复。隐藏当前页时返回概览并保持树中节点。仅因搜索条件筛成零行的 Tab 保留，方便清除筛选；概览和搜索结果入口始终保留。

选择 IED 或其子节点后，业务表默认只显示该 IED 的内容。切换 Tab 恢复该页选择，首次进入定位首项；成员表始终跟随当前数据集。共享模板默认只显示当前 IED 经唯一解析引用使用的定义，可勾选「显示全部模板」；全局通信网络、无法归属的问题及歧义归属内容可切回「整个工程」查看。引用表标识跨 IED 目标，显式跳转后可返回原 IED。「全局搜索（整个工程）」保持工程范围，「筛选当前表格」遵守当前 IED 和数据集范围。执行方案及验收边界见 [本轮 Task1](Tasks/Task1.md)。

SCL 工作台优化见 [Task2](docs/Tasks/Task2.md)。树表双向定位和历史见 [T2.1](docs/Tasks/Task2T2.1.md)，可点击引用路径、分组详情与 XML 原文高亮见 [T2.2](docs/Tasks/Task2T2.2.md)。

选中有引用关系的对象，在「引用与使用方」页打开离线拓扑，可追查 FCDA 字段来源、同对象的使用方和共享模板。见 [T2.3](docs/Tasks/Task2T2.3.md)。

窄窗可点「详情」收起右栏，按 F1 查看导航和拓扑操作。测试与人工复核进度见 [T2.4](docs/Tasks/Task2T2.4.md)。

树节点选择和表格切换卡顿的排查与优化方案见 [Task3](docs/Tasks/Task3.md)。目前完成源码风险分析与阶段规划，尚未复现本次卡死或实施修复；真实桌面验收需核对实际运行版本与可访问的本地样本。

### IED 信号与通信查阅（Task4）

后续表格与详情优化见 [Task5](docs/Tasks/Task5.md)：规划双击复制完整单元格、非省略换行、共享信号/成员描述来源和统一选中对象详情，并评估图投影与 QtNodes 只读适配。[T5.0 基线记录](docs/Tasks/Task5T5.0.md) 包含表格交互盘点与原生滚动复测。[T5.1](docs/Tasks/Task5T5.1.md) 已接入共享描述投影及来源原文。[T5.2](docs/Tasks/Task5T5.2.md) 试点信号与收发成员表的复制和换行。[T5.3](docs/Tasks/Task5T5.3.md) 将信号详情集中到「属性与依据」，保留独立原文浏览。[T5.4](docs/Tasks/Task5T5.4.md) 提供 GOOSE/SV 发布成员分页和网络端点表。[T5.5](docs/Tasks/Task5T5.5.md) 提供有界只读关系图。[T5.6](docs/Tasks/Task5T5.6.md) 记录综合验收和未通过项。

新增「收发关系」：选 IED 后按“发送到/接收自 → 对端装置 → 发布流”展开，直接查 APPID、MAC、VLAN 与成员到 `intAddr` 的映射。成员表、可点击路径、详情和内嵌关系图可相互配合；图支持平移、缩放、切换成员及双击原文。缺省源控制块时展示成员匹配候选，未见接收方时仍保留完整发送成员。设计与本地验收见 [Task4 第 12 节](docs/Tasks/Task4.md) 和 [T4.6](docs/Tasks/Task4T4.6.md)。本轮候选程序为 `build/windows-relations-debug/bin/IEC61850Simulator.exe`。

打开 SCL 后，任务视图提供概览、信号查阅、GOOSE、SV 和文件检查。选 IED 后可筛选本机对象或查阅外部输入。选中一行可查看分类证据、数据集用法、接收声明和原文。顶部保留全工程搜索，可用「高级视图」进入原有技术表格与引用拓扑。完整 XSD 由 Task1 P7 独立交付。阶段记录见 [Task4](docs/Tasks/Task4.md)。

当前仓库为 `D:/Code/IEC61850Simulator`。独立依赖程序管理路径已由用户修订为 `D:/Code/IEC61850Simulator/tools/`，库源码在仓库 `third_party/` 中管理。

### MMS 离线查阅规划（Task6）

[Task6](docs/Tasks/Task6.md) 规划 MMS 报告、信号地址、客户端声明与连接配置，主流程按“装置 → 报告 → 数据集成员 → 信号/原文”逐级查阅，技术表保留在高级视图。[T6.0](docs/Tasks/Task6T6.0.md) 已完成独立 Debug、9/9 CTest、自建正反例及修订后 D 盘 SCD 的只读基线；新 MMS 任务页尚未实施。文件中 687 个报告均未声明 indexed，根版本也缺失，因此不会把定义映射当成已确认的设备实例。完整 XSD 仍属 Task1 P7，Task5 未通过项继续保留。

[T6.1](docs/Tasks/Task6T6.1.md) 已完成独立合成数据原型，可运行 `build/task6-t61/bin/MmsInteractionPrototype.exe` 评审任务导航、报告/成员、信号地址、客户端反查、统一详情及原文返回。构建时显式开启 `IEC61850_BUILD_MMS_PROTOTYPE=ON`（默认关闭）；清洁 PATH CTest 10/10 和原生 100%/150%/200% 流程通过。原型不读取真实 SCD，也未接入正式主程序，其预置数据不作为领域解析结果。

[T6.2](docs/Tasks/Task6T6.2.md) 已交付纯 Qt Core 的 MMS 领域投影：报告参数/数据集、保守版本化预期实例、客户端声明、ServerAt/端点、能力原文及增量检查。该阶段另一设备的清洁 PATH 回归 11/11（含独立原型）；当时指定真实文件只读核对得到 687 个报告定义，未知版本下确定实例为 0。能力一致性、默认 RptID、非索引关联及完整 XSD 等明确保留未实现。

[T6.3](docs/Tasks/Task6T6.3.md) 已接入正式不可变快照，提供后台报告/成员分页、客户端与信号反查、IED/AP/Server 范围、搜索、有界局部图及未入数据集信号的按需地址。分页最多 200 行、图最多 128 节点，耗尽工作预算时明确标记结果不完整。

[T6.4](docs/Tasks/Task6T6.4.md) 已接入正式 MMS 页面、现有详情 Dock、完整值复制、原文、搜索、历史和基础/MMS 统一问题导出。[T6.5](docs/Tasks/Task6T6.5.md) 已执行：Debug 清洁 PATH 13/13 CTest，1千/5万/50万对象目录、原生三档 DPI/窄窗与指定 `D:/3Document/实验室20190101.scd` 十轮只读验证通过。整体仍待三名初学者、实体跨屏、系统级主题及严格同机 T6.0 增量对比；[用户走查表](docs/Tasks/Task6UserWalkthrough.md) 保留待测状态。Task3、Task5T5.6 和完整 XSD 的既有边界不变。

### MMS 导航与高级视图关系展示（Task7）

[Task7](docs/Tasks/Task7.md) 的 [T7.1](docs/Tasks/Task7T7.1.md) 已实现 MMS 页内后退/前进、业务上一级、可点击路径及完整路径菜单，复用工作台统一历史。共享数据集保留来源报告，返回恢复范围、字段和滚动锚点。另一设备的清洁 PATH CTest 13/13 和五档程序驱动导航通过，最终三对性能有 4 条路径超过 +15%，该阶段未标整体通过。

[T7.2](docs/Tasks/Task7T7.2.md) 交付后台数据集、FCDA 子页、使用方和原始节点定位。[T7.3](docs/Tasks/Task7T7.3.md) 接入数据集树表、成员专页与概览，历史按原始节点恢复选中项和滚动位置。[T7.4](docs/Tasks/Task7T7.4.md) 统一包含、引用、候选与查阅来源，支持使用方筛选、键盘定位、跨 IED 往返及本地局部拓扑。

[T7.5](docs/Tasks/Task7T7.5.md) 记录本机 Debug 回归、原生三档 DPI 与短窄窗、模拟主题及指定武汉 SCD 十轮只读验收。正式整体验收仍待 T7.0 冻结构建同机对照、内存与原文慢路径归因、系统主题、实体跨屏和三名初学者走查。

### MMS 信号查阅与引用复制（Task8）

[Task8](docs/Tasks/Task8.md) 规划按 IED 查阅文件中的 MMS 属性与报告使用，并在任务和高级表格提供两级引用复制菜单。[T8.0](docs/Tasks/Task8T8.0.md) 已建立 A 相电压合成基线，记录现有属性路径、文件原值与定值边界，以及表格和菜单矩阵。

[T8.1](docs/Tasks/Task8T8.1.md) 增加了后台属性检索、精确属性与结构覆盖的报告反查，以及带原文依据的类型化引用值。[T8.2](docs/Tasks/Task8T8.2.md) 可从「MMS 查阅 → 信号地址 → 查找属性」直接查找幅值和相位，也可从高级视图的对象、FCDA 或其引用进入同一查阅页。[T8.3](docs/Tasks/Task8T8.3.md) 可在任务及高级表格右键展开引用名称，选择第二级中的值复制完整文本。

[T8.4](docs/Tasks/Task8T8.4.md) 在指定的本地 SCD 验证了 A 相幅值、相位、DO 级报告覆盖、原文和跨视图复制。大范围搜索受查询预算约束，界面显示观察下界。在线状态单列为「未验证」。

### 引用组成与原文溯源（Task9）

[Task9](docs/Tasks/Task9.md) 在两级引用菜单中加入「查看关系」。从对象引用、功能约束、MMS Domain、MMS Item 或类型引用出发，逐层查看名称怎样组成、使用了哪些 SCD 声明，以及对应的原文行号。可切到「证据树」用键盘展开，再打开所选节点的原文。「返回来源」恢复原表位置。

### 对象来源与 MMS 地址解释规划（Task10）

[Task10](docs/Tasks/Task10.md) 统一展示文件声明、类型展开、规则组合，以及数据集精确声明、上级覆盖和未引用的区别。[T10.0](docs/Tasks/Task10T10.0.md) 冻结基线与 [T10.1](docs/Tasks/Task10T10.1.md) 后端记录保留。[T10.2](docs/Tasks/Task10T10.2.md) 将来源及三类原文入口接到任务页；属性可独立查数据集覆盖与报告/GOOSE/SV 静态使用方。[T10.3](docs/Tasks/Task10T10.3.md) 在高级对象、FCDA 与模板中补齐 CDC、类型、成员层级和共享来源，保留模板未绑定状态，并按原始身份恢复跨视图筛选、字段及滚动位置。沿用组成图和统一历史，ADS 布局仍为版本 2。已确认程序未生成 `Vol.ang.f`；粘贴校核留在 T10.4。本次使用 D 盘工作区与 MSYS2、D:/1Document/SCD 下两份指定样本；T10.2 的 C 盘环境及 D:/3Document 路径保持历史含义，验证与待测项分别见阶段记录。

## 技术栈

- C++：应用主框架、IEC 61850 报文与业务处理、Qt 集成。
- QML：页面和可视化交互。
- Python：后续加入的自动化及扩展脚本能力。
- CMake：构建与依赖组织。
- Visual Studio Code：主要开发 IDE。
- IEC 61850 协议库：[mz-automation/libiec61850](https://github.com/mz-automation/libiec61850)。正式集成时应固定经过验证的版本，并记录许可证及 CMake 集成方式。

## 源码规范

- 所有文本文件统一使用 UTF-8 编码且不带 BOM。
- 所有平台统一使用 LF（`\n`）行尾，不在 Windows 工作区转换为 CRLF。
- 项目自有源码文件名统一使用大驼峰（UpperCamelCase），例如 `ClientManager.cpp`、`ClientManager.hpp`、`MainWindow.qml` 和 `ProtocolAdapter.py`；同一 C++ 类的头文件与实现文件使用相同主文件名。
- 工具链规定或生态约定的文件名不强制改名，例如 `CMakeLists.txt`、`README.md`、`.gitignore`、`.editorconfig`、`pyproject.toml` 和 `requirements.txt`。
- `.editorconfig` 约束编辑器保存格式，`.gitattributes` 在 Git 层统一文本文件行尾。
- 提交前应确保新增源码、QML、CMake、配置和文档均符合上述约定。

## 建议架构

客户端和服务端应复用数据模型、配置、日志和协议适配层，避免在两套实现中重复解析 SCL 或维护连接状态。

```text
QML UI
  |
Qt/C++ 应用服务层
  |-- 客户端仿真
  |-- 服务端仿真
  |-- 脚本服务
  |
领域模型与协议适配层
  |-- IEC 61850 库
  |-- SCL/配置
  |-- 日志与诊断
```

建议优先按功能域划分 `src/`，而不是仅按技术类型堆放文件：

- `src/core/`：公共类型、错误处理、配置、任务调度和应用服务。
- `src/iec61850/`：第三方协议库的薄封装、数据模型映射和 SCL 处理。
- `src/client/`：客户端连接、读写、控制、报告与订阅逻辑。
- `src/server/`：服务端模型、数据更新、控制处理与连接管理。
- `src/gui/`：暴露给 QML 的模型、控制器及类型注册。
- `src/python/`：C++ 与 Python 之间的接口；未启用脚本功能时应可独立关闭。

业务代码不应直接散落调用第三方 IEC 61850 API，应经由 `src/iec61850/` 适配。这样便于测试、升级依赖并统一资源释放和错误转换。

## 目录规划

```text
IEC61850Simulator/
|-- app/                 # main.cpp、Qt 初始化、QML 加载和程序装配
|-- cmake/               # 自定义 CMake 模块、查找脚本和公共编译选项
|-- config/              # 默认配置、示例配置及配置 schema
|-- docs/                # 架构、协议范围、开发和发布文档
|-- include/             # 仅存放确需对外暴露的公共头文件
|-- python/              # Python 包、脚本和 requirements/pyproject 配置
|-- qml/                 # QML 页面、组件、主题及 QML 模块
|-- resources/           # 图标、图片、翻译和 Qt 资源清单
|-- src/                 # C++ 业务实现
|   |-- core/
|   |-- iec61850/
|   |-- client/
|   |-- server/
|   |-- gui/
|   `-- python/
|-- tests/               # C++、QML、Python 测试及协议测试数据
|   |-- unit/
|   |-- integration/
|   |-- qml/
|   |-- python/
|   `-- fixtures/        # SCL 文件、抓包和可公开的测试证书等
|-- third_party/         # 必须随源码管理的第三方源码或子模块
|-- tools/               # 开发、生成、打包和诊断工具
|-- .editorconfig        # 编辑器编码、行尾及基础格式约定
|-- .gitattributes       # Git 文本行尾规范
|-- CMakeLists.txt
|-- CMakePresets.json
`-- README.md
```

`build/` 是本地构建产物，不属于源码目录，必须保持在 Git 忽略列表中。项目使用的依赖库应在 `third_party/` 备份固定版本源码、许可证和来源信息；独立运行的依赖程序在 `tools/` 管理。不要依靠构建时临时联网下载来替代依赖备份。

## 第三方库产物放置

第三方库源码与编译产物应分开管理：`third_party/` 只放必须随仓库管理的源码、补丁或子模块，不放编译生成的 DLL、LIB、SO 等文件。第三方库产物应位于当前 CMake 构建目录中，并随整个 `build/` 一起被 Git 忽略。

建议每个预设使用独立构建目录：

```text
build/<preset>/
|-- _deps/               # FetchContent 下载及编译内容
|-- third_party/         # 手动纳入 CMake 的第三方构建目录
|-- bin/                 # 可执行文件及 Windows 运行时 DLL
`-- lib/                 # Windows 导入/静态 LIB、Linux SO 及其他库产物
```

- Windows：链接所需的 `.lib` 放在构建树的 `lib/`，运行所需的 `.dll` 由构建或部署步骤复制到可执行文件所在的 `bin/`。
- Linux：构建期 `.so` 可放在 `lib/`；发布时安装到应用私有的 `lib/`，并通过安装 RPATH（通常为 `$ORIGIN/../lib`）让可执行文件定位依赖。
- 正式打包不要直接复制整个构建目录。使用 CMake `install()` 规则生成独立暂存目录，例如 `dist/<platform>/bin`、`dist/<platform>/lib` 和 `dist/<platform>/qml`；`dist/` 同样不提交 Git。
- 不建议将本机编译出的第三方二进制提交到仓库。若将来确实需要分发预编译依赖，应建立带平台、架构、编译器和版本信息的制品仓库，而不是混入源码目录。

## 开工前需补充的关键事项

当前目录方案总体合理，但下列内容属于重要遗漏或尚未定案的工程决策：

1. **协议范围**：明确首期支持 MMS、报告、控制、GOOSE、采样值中的哪些能力，以及客户端和服务端各自边界。不同能力对网络权限、实时性和测试环境要求差异很大。
2. **数据模型与 SCL**：确定 ICD/CID/SCD 等文件的导入、校验、运行时模型和版本迁移策略，并准备可公开的测试样本。
3. **依赖管理**：固定 Qt、CMake、编译器、`libiec61850` 及 Python 版本；记录许可证，选择 vcpkg、Conan、FetchContent 或系统包中的一种主路径。
4. **配置与持久化**：定义连接参数、服务端模型、场景数据及用户设置的格式、schema、默认值和敏感信息处理方式。
5. **并发与生命周期**：提前约定 Qt 主线程、协议工作线程、网络回调和 Python 解释器之间的边界，避免阻塞 UI 或产生跨线程对象问题。
6. **日志与诊断**：规划分级日志、协议事件、连接状态、可选报文记录和问题导出；避免在日志中泄露口令或私钥。
7. **错误模型**：统一第三方库错误、协议错误和 UI 提示之间的转换，明确重连、超时、取消和资源清理策略。
8. **测试体系**：至少覆盖单元测试、客户端/服务端回环集成测试、SCL 测试数据、QML 测试和 Python 测试；后续补充与真实 IED 或参考实现的互操作测试。
9. **质量与自动化**：增加格式化和静态检查配置，例如 `.clang-format`、`.clang-tidy`、QML lint，并在 CI 中执行配置、构建和测试。
10. **打包发布**：分别建立 Windows 和 Linux 的构建、测试及打包流程，处理 Qt 插件、QML 模块、第三方动态库、Python 运行时、安装包及版本号策略。
11. **安全边界**：验证所有外部 SCL/配置/脚本输入；脚本执行属于高权限扩展能力，需要显式启用并记录安全假设。
12. **项目治理**：后续补充 `LICENSE`、`CONTRIBUTING.md`、变更记录和架构决策记录（ADR）。

## 推荐实施顺序

1. 固定工具链和 IEC 61850 库，完成最小 CMake 构建与测试目标。
2. 建立协议适配层及统一数据模型，实现无 UI 的客户端/服务端最小回环。
3. 加入配置、SCL、日志、错误处理和集成测试。
4. 通过 Qt 模型将稳定的应用服务暴露给 QML，再实现页面。
5. 最后引入可选 Python 扩展、打包和发布流程。

## 构建

当前 Windows 开发环境使用 **MSYS2 MinGW64** 工具链。必须使用 MSYS2 安装的 Qt 和 MinGW，不要将其与 Qt Online Installer 提供的 MinGW 或 Qt 二进制混用，以避免 C++ ABI 与运行时不兼容。

在 **MSYS2 MinGW64** 终端安装依赖：

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-x86_64-toolchain \
  mingw-w64-x86_64-cmake \
  mingw-w64-x86_64-ninja \
  mingw-w64-x86_64-qt6-base
```

如需使用 UCRT64 工具链，请在 **MSYS2 UCRT64** 终端安装对应依赖：

```bash
pacman -Syu
pacman -S --needed \
  mingw-w64-ucrt-x86_64-toolchain \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base
```

从同一个 MSYS2 MinGW64 终端配置并构建 Debug 版本：

```bash
cd /e/Code/IEC61850Simulator
cmake --preset windows-debug -DCMAKE_PREFIX_PATH=/mingw64
cmake --build --preset windows-debug
```

可执行文件输出到 `build/windows-debug/bin/`。Windows 默认构建会执行 `DeployRuntime`：使用当前 Qt 安装的 `windeployqt` 部署平台插件，再递归复制 Qt、插件和编译器所需的第三方 DLL（包括 ICU、字体、图片和压缩库）。部署完成后可直接双击 EXE，无需依赖 MSYS2 的 PATH。分发时需要保留整个 `bin/` 目录。

`scripts/BuildMinGW64.sh` 和 `scripts/BuildUcrt64.sh` 共用该 CMake 部署流程。若只构建了特定目标或需要补齐已存在的输出目录，请执行 `cmake --build <构建目录> --target DeployRuntime`；UCRT64 示例为 `cmake --build build/windows-ucrt64-debug --target DeployRuntime`。仅运行 `windeployqt` 可能遗漏 MSYS2 提供的递归第三方依赖。

运行时 Redis 配置固定从可执行文件同级的 `config/Redis.json` 读取，例如 `build/windows-debug/bin/config/Redis.json`。该文件只会在首次构建时由仓库中的默认配置创建；之后可直接修改它，后续构建不会覆盖本地修改。修改后重启程序使新连接参数生效。

`redisWorkingDirectory` 控制 `RedisService.exe`、`redis.conf` 和 Redis 的工作目录。它支持绝对路径，也支持相对 `bin/config/Redis.json` 的路径；默认的 `../../../../tools/Redis` 对应本项目的 Windows Debug 构建目录。

若在 VSCode 中使用 CMake Tools，需从 MSYS2 MinGW64 终端启动 VSCode，或显式为 CMake Tools 配置同一套 MinGW64 环境，使其能找到 `/mingw64` 下的编译器、Ninja 和 Qt。

Redis 配置、hiredis 客户端和 Windows 应用启动测试通过 `CMakePresets.json` 统一执行：

```bash
ctest --preset windows-debug
```

也可在对应的 MSYS2 终端直接运行 `./scripts/BuildMinGW64.sh` 或
`./scripts/BuildUcrt64.sh`。脚本使用相互独立的构建目录，避免两种工具链的产物混用。

## 第三方依赖与版本管理

- 库源码保存在 `third_party/` 并随仓库管理。hiredis 固定为 1.3.0，由根 CMake 静态构建，不需要额外部署 hiredis DLL，也不需要构建时联网下载。
- 独立运行的依赖程序保存在 `tools/`。当前 Redis 的 EXE、配套 DLL 和默认配置继续纳入版本控制；本机生成的库、构建目录和 Redis PID、日志、数据文件应忽略。
- `tools/QtADS/` 仅为本机缓存，整体忽略；QtADS 运行时由 CMake 构建并部署。
- hiredis 的来源、校验值与许可证见 [third_party/DEPENDENCIES.md](third_party/DEPENDENCIES.md)，架构及验证边界见 [docs/DEVELOPMENT.md](docs/DEVELOPMENT.md)。
- `tools/Redis/redis_6379.pid` 是历史已跟踪的运行文件。本轮保留其用户修改及跟踪状态，避免干扰正在进行的迁移；新增忽略规则只作用于未跟踪文件。

Windows 启动测试在临时目录运行实际应用，通过日志及进程存活检查验证 Redis 正常与超时场景各连续启动两次。测试使用关闭持久化的独立 Redis 子进程，不启动 RedisService、不修改 `tools/Redis/`，仅清理测试自己创建的进程。缺少 Redis 可执行文件时跳过真实 Redis 场景；Qt offscreen 插件可用时，CMake 自动部署该测试插件。弹窗文字与状态栏的视觉效果仍需人工检查。
