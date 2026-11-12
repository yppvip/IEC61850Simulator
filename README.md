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
