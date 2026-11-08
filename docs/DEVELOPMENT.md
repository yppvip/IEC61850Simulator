# IEC61850Simulator 开发与架构设计

> 当前处于工程框架设计阶段。本文记录现阶段的软件结构、技术边界与待验证决策；具体依赖版本和协议能力将在原型验证后固定。

> 阅读范围：第 2～4 节的双进程、QML/MDI 与订阅为长期架构设想；当前实现以第 11～13 节的 Widgets + ADS 骨架为准。下一阶段 SCL 离线分析提案见第 14 节及 `docs/Tasks/Task1.md`，不以 Redis 或独立后台进程为打开文件的前提。

## 1. 设计目标

IEC61850Simulator 使用 C++、Qt/QML 和 CMake 开发，面向 Windows 与 Linux，目标是在统一的数据模型和协议适配层之上提供 IEC 61850 客户端与服务端仿真能力，并为后续 Python 自动化扩展预留接口。

设计原则：

- UI、应用服务、IEC 61850 协议实现相互隔离。
- 客户端、服务端复用 SCL、领域模型、配置、日志和诊断能力。
- 业务代码不直接散落调用 `libiec61850`，统一通过协议适配层访问。
- UI 不直接访问 IEC 61850 设备；设备通信和数据解析在后台服务中完成。
- Redis 是进程间数据分发设施，不作为 IEC 61850 领域模型的唯一事实来源。

## 2. 总体架构

```text
┌──────────────────────────────────────────────────────────┐
│ UI 进程（Qt Widgets + QML）                              │
│  QMainWindow / ADS                                       │
│  ├─ 工具停靠窗口（设备树、属性、日志、诊断等）           │
│  ├─ 自由画布 Dock（可创建多个）                          │
│  │  └─ QMdiArea                                          │
│  │     └─ 多种 QMdiSubWindow / QQuickWidget（QML 页面）  │
│  └─ RedisSubscriber → UI ViewModel → QML/Widgets         │
└──────────────────────────┬───────────────────────────────┘
                           │ Redis Pub/Sub + 状态读写
┌──────────────────────────▼───────────────────────────────┐
│ 后台服务                                                 │
│  连接管理 / SCL 与模型解析 / IEC 61850 数据读写与订阅    │
│  领域模型映射 / 数据质量与时间戳 / Redis 发布            │
└──────────────────────────┬───────────────────────────────┘
                           │
                    libiec61850 / IED
```

建议先采用两个进程：`simulator-ui` 和 `iec61850-service`。后台服务崩溃、重连或处理大模型时不会阻塞界面，也方便将来部署多个设备采集实例。

## 3. UI 设计

### 3.1 主窗口与 Qt Advanced Docking System

[Qt Advanced Docking System（ADS）](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System) 可以负责完整的停靠布局，本身不强制要求 `QMainWindow`，并且其布局理念没有传统 Qt Docking 的固定 central widget。因此，从控件能力上可以独立构成主工作区。

本项目仍建议使用 `QMainWindow` 作为顶层窗口，将 ADS 的 `CDockManager` 放入其中，原因是：

- 复用菜单栏、工具栏、状态栏和窗口级快捷键等标准桌面应用能力；
- 明确顶层窗口的生命周期、会话恢复和平台行为；
- ADS 只承担停靠布局，降低主窗口与第三方库的耦合。

这不是技术硬性要求，而是工程上的推荐组合。若主界面所有区域都由 ADS 管理，也可使用普通 `QWidget` 顶层窗口。不要同时用 `QMainWindow::addDockWidget()` 与 ADS 管理同一批停靠窗口。

本项目不设置固定的中央 MDI 工作区。应将“自由画布”封装为一种普通的 ADS `CDockWidget`，允许用户按需创建多个画布，并像其他 Dock 一样停靠、分屏、标签化、浮动或关闭。设备树、属性、日志和诊断等也是普通 Dock；ADS 统一管理这些顶层工作区组件。

### 3.2 可添加的自由画布与 QML MDI

自由画布采用 `QMdiArea`，并作为 ADS Dock 的内容，而不是主窗口中固定的 CenterWidget。每个画布内部可以添加多种类型的 `QMdiSubWindow`，用于自由移动、缩放、堆叠、层叠或平铺显示。

```text
ADS 主工作区
├─ 设备树 Dock
├─ 属性 Dock
├─ 日志 Dock
├─ 自由画布 Dock A
│  └─ QMdiArea
│     ├─ 数据表子窗口
│     ├─ 曲线子窗口
│     └─ QML 自定义子窗口
└─ 自由画布 Dock B
   └─ QMdiArea
      ├─ 模型子窗口
      └─ 诊断子窗口
```

QML 不能直接成为 QWidget/MDI 子窗口，推荐每个 QML 类型的子窗口以 `QQuickWidget` 承载：

```text
QMdiArea
└─ QMdiSubWindow
   └─ QQuickWidget
      └─ QML 页面根 Item
```

实施约束：

- 窗口框架、层叠/平铺、激活和关闭由 Widgets/MDI 管理；窗口内容和业务交互由 QML 实现。
- C++ ViewModel 通过注册类型、上下文属性或 QML singleton 暴露给 QML；QML 不直接持有 Redis 客户端。
- 每个子窗口使用清晰的文档/设备标识，统一管理创建、复用、关闭和状态恢复。
- ADS 负责画布、工具面板之间的停靠关系；`QMdiArea` 只负责当前画布内部子窗口的几何位置和堆叠顺序，两层职责不得混用。
- 用户可创建、命名和关闭多个自由画布；关闭画布时必须提示如何处理其中尚未关闭的子窗口。
- MDI 子窗口跨画布迁移不是 Qt 自动提供的能力。初期提供“移动到画布”菜单；验证稳定后再考虑自定义拖放协议实现直接跨画布拖动。
- 布局持久化需要同时保存两层状态：ADS 的 Dock 布局，以及各画布内子窗口的类型、业务标识、位置、尺寸、层叠顺序和配置。恢复时应先重建画布及子窗口，再恢复 ADS 布局。
- 先做原型验证多窗口缩放、拖动、显卡兼容性及内存占用。大量 `QQuickWidget` 会创建较重的渲染资源；如果常驻窗口很多，应考虑单个 QML 工作区自行实现标签页/窗口管理，而不是每个 MDI 子窗口一个 QML 场景。
- ADS 浮动窗口与 `QQuickWidget` 混用时，重点验证窗口脱离、跨屏幕和高 DPI 场景。

推荐的 C++ 抽象：

- `WorkspaceDock`：ADS 中可停靠、浮动的自由画布容器。
- `WorkspaceCanvas`：封装 `QMdiArea` 以及画布内窗口操作。
- `WorkspaceManager`：管理多个画布的创建、销毁、查找、窗口迁移和状态恢复。
- `DocumentWindowFactory`：根据窗口类型创建数据表、曲线、模型、诊断或 QML 子窗口。
- `DocumentDescriptor`：描述窗口类型、实例 ID、数据源、所属画布和可持久化业务配置。

这里的两级窗口模型是有意设计的：ADS 提供工作台级别的拖拽、分屏和浮动；MDI 提供单个自由画布内部的任意摆放与堆叠。它既满足 IDE 风格的布局能力，也满足仿真画面自由编排的需求。

### 3.3 UI 数据更新服务

UI 进程内设置独立的 `RedisSubscriber`，在非 GUI 线程订阅 Redis 通道。收到消息后先进行协议校验和反序列化，再通过 queued signal/slot 将不可变更新投递给 GUI 线程中的 ViewModel。

```text
Redis Pub/Sub → Subscriber 工作线程 → 校验/合并 → Qt ViewModel → QML/Widget
```

要求：

- Redis 回调不得直接修改任何 GUI/QML 对象。
- 对高频测点按 UI 刷新周期合并或限流，避免逐点触发界面重绘。
- 消息至少包含 schema 版本、数据源/设备 ID、对象引用、值、质量、源时间戳和序列号。
- UI 必须显示连接状态、数据新鲜度和质量，不能把“最后一次值”误当成当前有效值。
- Pub/Sub 不保存历史消息，也不保证断线补发。最新状态应另存 Redis Hash/JSON 等快照；UI 重连时先读取快照，再恢复订阅。
- 若业务要求可靠消费、回放或消费确认，事件通道改用 Redis Streams；Pub/Sub 仅用于可丢弃的实时通知。

## 4. Redis 中间件边界

Redis 用于 UI 与后台服务之间的进程解耦、实时通知、当前状态快照和可选的命令传递。建议按用途分离：

| 用途 | 推荐机制 | 说明 |
|---|---|---|
| 实时测点通知 | Pub/Sub | 低延迟，允许丢弃中间刷新 |
| 当前状态 | Hash 或 RedisJSON（可选） | 支持 UI 启动、重连后恢复 |
| 可靠事件/告警 | Streams | 支持回放、确认和消费组 |
| UI 控制命令 | Streams 或请求/响应通道 | 必须带命令 ID、超时、结果与幂等约束 |

通道和 Key 必须命名空间化，例如 `iec61850:<environment>:<service>:...`。序列化格式初期可使用带版本号的 JSON 方便诊断；性能数据证明有必要后，再评估 MessagePack 或 Protobuf。不得依赖 Redis 保存唯一的 SCL 模型文件或关键配置。

控制路径需要身份认证、权限校验、审计和显式结果返回。Redis 应启用认证并限制网络暴露；密码、证书和私钥不得写入仓库或消息正文。

## 5. 后台服务设计

后台服务负责：

- 加载、校验并解析 ICD/CID/SCD 等 SCL 文件，构建规范化领域模型；
- 管理 IED 连接、重连、超时、报告订阅、读写与控制；
- 将第三方库数据映射为统一的值、质量、时间戳和错误模型；
- 维护内存中的运行时状态，并把快照与增量更新写入/发布到 Redis；
- 接收经过授权的 UI 命令，执行后发布明确的成功或失败结果；
- 输出结构化日志、健康状态和诊断指标。

IEC 61850 网络回调、模型解析和 Redis I/O 不应运行在 UI 线程。后台服务内部也应将协议回调快速转交给队列，避免在第三方库回调中执行阻塞操作。

## 6. 源码与目录规划

优先按功能域组织源码，而不是只按技术类型堆放：

```text
IEC61850Simulator/
|-- app/                    # 各可执行程序入口与装配
|   |-- ui/
|   `-- service/
|-- cmake/                  # CMake 模块和公共编译选项
|-- config/                 # 默认/示例配置与 schema
|-- docs/                   # 架构、开发和发布文档
|-- include/                # 确需对外暴露的公共头文件
|-- qml/                    # 页面、组件、主题与 QML 模块
|-- resources/              # 图标、图片、翻译和资源清单
|-- src/
|   |-- core/               # 公共类型、配置、任务、错误与日志
|   |-- domain/             # 与传输和 UI 无关的 IEC 61850 领域模型
|   |-- iec61850/           # libiec61850 适配、SCL 与模型映射
|   |-- client/             # 客户端业务能力
|   |-- server/             # 服务端仿真能力
|   |-- middleware/redis/   # Redis 消息、快照和 schema
|   |-- gui/                # Widgets、ViewModel、QML 类型注册
|   `-- python/             # 可选 Python 接口
|-- tests/
|   |-- unit/
|   |-- integration/
|   |-- qml/
|   |-- python/
|   `-- fixtures/           # SCL、抓包和公开测试证书
|-- third_party/            # 必须随仓库管理的第三方源码/补丁
|-- tools/                  # 开发、生成、打包与诊断工具
|-- CMakeLists.txt
|-- CMakePresets.json
`-- README.md
```

构建产物放入 `build/<preset>/` 并由 Git 忽略；发布内容通过 CMake `install()` 进入独立的 `dist/<platform>/`。第三方源码与编译产物分离，不提交本机生成的 DLL、LIB 或 SO。

## 7. 合理性评估与优化建议

当前结构总体合理：后台协议服务与 UI 分进程能隔离耗时和故障；Redis 能降低模块耦合；Widgets/ADS 适合桌面工作台，QML 适合高质量内容页。

在正式实现前建议优先收紧以下设计：

1. **不要只依赖 Pub/Sub。** 增加当前状态快照；可靠告警或控制使用 Streams，否则 UI 断线期间的数据无法恢复。
2. **定义消息契约后再并行开发。** 先确定设备 ID、对象引用、值类型、质量、时间戳、序列号、schema 版本和错误格式，并建立兼容策略。
3. **控制 QML 场景数量。** 用原型测量 10、50、100 个子窗口的内存、拖动和刷新性能，再决定每窗一个 `QQuickWidget` 是否成立。
4. **区分数据频率与显示频率。** 后台保留原始变化，UI ViewModel 按帧或固定周期批量刷新；波形/历史曲线另走批量数据接口。
5. **把命令与遥测分开。** 控制操作必须可确认、可超时、可审计、可幂等，不能复用“发出即忘”的测点广播。
6. **先保持部署简单。** 初期一个后台服务可以管理多个 IED；只有在隔离、权限或吞吐数据证明需要时再拆分微服务。

## 8. 待定工程决策

- 首期 IEC 61850 范围：MMS、报告、控制、GOOSE、采样值分别支持到什么程度。
- SCL 类型、校验规则、模型版本与迁移策略。
- Qt、CMake、编译器、libiec61850、ADS、Redis 客户端及 Python 的固定版本与许可记录。
- RedisJSON 是否作为部署依赖、消息序列化格式；当前 C++ Redis 客户端已选定 hiredis 1.3.0。
- UI/服务的启动顺序、健康检查、服务发现、断线重连和优雅关闭。
- 配置、证书、密钥及用户设置的保存与权限策略。
- 日志、指标、报文记录、诊断包和敏感信息脱敏。
- Windows/Linux 构建、测试、安装包和升级策略。

重要架构选择应在验证后形成 ADR，至少记录背景、选项、决定和后果。

## 9. 推荐实施顺序

1. 固定工具链候选版本，搭建最小 CMake 构建和测试目标。
2. 定义领域模型、Redis 消息契约和错误模型，并完成无 UI 的 IEC 61850 → Redis 最小链路。
3. 实现 UI 订阅、快照恢复和 ViewModel，用简单 Widget 验证线程与刷新策略。
4. 制作 ADS + `QMainWindow` + `QMdiArea` + `QQuickWidget` 原型，完成性能与跨平台验证。
5. 加入控制命令、可靠事件、日志诊断和集成测试。
6. 最后引入可选 Python 扩展及正式打包发布流程。

## 10. 编码与质量约定

- 文本文件统一使用 UTF-8（无 BOM）和 LF 行尾。
- 自有源码文件使用 UpperCamelCase；工具链约定文件保留标准名称。
- 使用 `.editorconfig`、`.gitattributes`、`.clang-format`、`.clang-tidy` 和 QML lint 固化规则。
- 测试至少覆盖领域模型、协议适配、消息序列化兼容、Redis 重连、客户端/服务端回环及 QML 基础交互。
- 所有外部 SCL、配置和脚本输入均需校验；Python 扩展应显式启用并记录安全假设。

## 11. 已确认的首期 UI 与 Redis 决策

### 11.1 UI 范围

首期主程序采用 `QMainWindow + ADS`。ADS 管理服务器列表、客户端列表和详情工作区等 Dock；不得与 `QMainWindow::addDockWidget()` 混用来管理同一批 Dock。首期不引入复杂 QML/MDI 页面，服务器与客户端详情以 Widgets 占位，待领域模型稳定后再按需用 `QQuickWidget` 承载 QML。

首期提供文件菜单（仅选择并校验 `.icd`、`.scd`、`.cid`、`.ccd`）、新建服务器/客户端工具栏按钮、两类状态列表、详情占位区、状态栏及 ADS 布局保存/恢复。文件选择不等同于 SCL 解析或 IEC 61850 服务创建。

### 11.2 QtADS 构建产物

QtADS 源码保存在 `third_party/Qt-Advanced-Docking-System-5.0.0`，由项目 CMake 构建并链接。其 DLL 作为运行时依赖部署到 `build/<preset>/bin/` 或安装目录的 `bin/`；不提交构建产物。若需要 `tools/QtADS/`，该目录仅为本机兼容缓存并由 Git 忽略。

### 11.3 Redis 启动与连接边界

`config/Redis.json` 首期包含 `host`、`port`、`startupTimeoutMs`、`autoStart` 和 `redisWorkingDirectory`。健康检查以连接目标并成功执行 Redis `PING` 为准，不依赖 Windows 服务名；若已有实例在该端口正常响应，直接连接。运行时仅读取可执行文件同级的 `bin/config/Redis.json`；该文件仅在缺失时由默认配置创建，后续构建不会覆盖用户修改。

Redis 不可用时，UI 仍须启动，并弹窗提示“Redis 数据库不可用”，同时显示连接故障状态。应用不停止任何 Redis 实例。`redisWorkingDirectory` 可使用绝对路径或相对 `bin/config/Redis.json` 的路径；程序从该目录查找 `RedisService.exe` 与 `redis.conf`，并将其作为 RedisService 的工作目录。仅在 `autoStart` 启用且目标为本机地址时，才允许启动 RedisService；启动失败同样不得阻塞 UI。

## 12. 本轮已落地的基础实现

### 12.1 构建与运行时部署

- 根 CMake 已构建并链接 QtADS 5.0.0 动态库，Windows Debug 预设为 `windows-debug`。
- QtADS 与 Qt 运行时 DLL 部署到 `build/windows-debug/bin/`；默认 Redis 配置部署为 `bin/config/Redis.json`。
- 当前 Windows 验证环境为 MSYS2 MinGW64 + Qt 6；通过 `cmake --build --preset windows-debug` 构建，并使用 `ctest --preset windows-debug` 运行测试。

### 12.2 Redis 与日志

- `RedisAvailabilityChecker` 调用 `RedisClient`，通过 hiredis 发送 `PING` 并解析响应；Qt Concurrent 在后台执行连接与命令，完成后由 GUI 线程的 `QFutureWatcher` 回传状态。
- 本机 Redis 不可达且 `autoStart` 启用时，调用 `RedisService.exe run --config <redis.conf> --port <port>`，不停止任何 Redis 实例。
- 全局 Qt 消息处理器将运行日志写入 `bin/logs/IEC61850Simulator.log`。

### 12.3 UI 与测试

- `MainWindow` 使用 ADS 管理欢迎工作区、服务器侧栏、客户端侧栏与详情 Dock；新建实体目前仅为 UI 占位，不创建 IEC 61850 通信实体。
- 文件选择仅校验 SCL 扩展名，不解析文件内容。
- ADS 静态 Dock 布局通过 `QSettings` 保存与恢复；需要先持久化服务器/客户端实体，才能安全恢复动态详情 Dock。
- 已新增 `RedisConfigurationTest`，覆盖 Redis 配置的有效读取、相对工作目录解析和非法端口拒绝。

## 13. hiredis 集成与 Git 忽略规则（2026-11-06）

### 13.1 已实现边界

- hiredis 1.3.0 完整上游源码保存在 `third_party/hiredis-1.3.0`，来源、归档 SHA-256 与许可证记录于 `third_party/DEPENDENCIES.md`。源码无需在线下载即可构建。
- 根项目启用 C 与 C++，通过 `add_subdirectory()` 构建 hiredis 静态库；禁用其上游测试、示例、SSL 和 NuGet 打包，选项限定在依赖作用域，QtADS 保持动态链接。安装规则附带 hiredis 许可证。
- `RedisClient` 使用 hiredis 的连接和命令 API、RAII 释放 context/reply，每次 PING 独占连接，不维护持久业务连接。仅 `REDIS_REPLY_STATUS` 的 `PONG` 视为成功；错误、错误类型、格式异常、断线和超时均返回失败。
- 连接和命令分别设置 1000 ms 超时；该值不是整个操作的总截止时间，系统域名解析也可能超出此时间。后台任务不捕获 GUI 对象，销毁客户端不会等待正在进行的网络操作；应用退出时全局线程池可能等待任务结束。
- `RedisAvailabilityChecker` 保留配置、autoStart、本机地址限制、显式 RedisService 参数与工作目录、启动重试及状态信号。检查周期内重复 `Start()` 会忽略，结束后可再次检查。状态同时写入应用日志。
- UI 的“Redis 数据库不可用”弹窗和状态栏行为保持；不新增 SCL 解析、业务读写、订阅、认证或停止 Redis 的功能。

### 13.2 版本控制策略

- 忽略 `build/`、静态库/共享库构建产物、`tools/QtADS/` 本机缓存，以及 `tools/Redis/` 下的 PID、日志、RDB、AOF 与 `appendonlydir/`。
- 不全局忽略 EXE/DLL，保留 `tools/Redis/` 中可分发程序、配套 DLL 和配置；QtADS 与 hiredis 源码、构建脚本和许可证必须可被 Git 跟踪。
- `tools/Redis/redis_6379.pid` 已被历史提交跟踪且存在用户修改。本轮仅增加未来运行产物的忽略规则，未修改该文件、未取消跟踪，也未改动用户的 Redis 迁移。

### 13.3 分阶段验证

1. hiredis 独立 MinGW64 Debug 静态编译通过，再接入根 CMake。
2. `RedisClientTest` 覆盖分段 PONG、Redis 错误、非预期状态/类型、非法协议、连接拒绝、超时期间事件循环响应及操作中销毁。
3. `RedisStartupTest` 在 Windows/offscreen 环境运行真实应用：隔离配置和随机端口，在真实 Redis 与无响应端口两种场景各启动两次，检查日志及应用保持运行。真实 Redis 仅为测试子进程，关闭持久化并由测试清理；应用退出后验证 Redis 仍运行。
4. 原有 `RedisConfigurationTest` 保留。统一执行 `ctest --preset windows-debug`。启动测试不自动操作弹窗，也不验证视觉布局；autoStart 分支沿用原实现，本轮不重新启动 RedisService。

本节原始验收使用 Windows / MSYS2 MinGW64 / Qt 6；P1 已在 Windows / MSYS2 UCRT64 / Qt 6 复测。上游对 Windows 的支持为尽力维护，升级 hiredis 前需重复连接、超时和构建验证。

验证结果：`windows-debug` 与全新目录 `build/hiredis-clean-debug/` 均完成 Debug 配置、构建及三组测试（3/3 通过）。确认全部 74 个 hiredis 上游文件可被 Git 跟踪，除 `hiredis.targets` 的 BOM/行尾规范化外与下载归档一致；新增及修改文本通过 UTF-8/无 BOM/LF 检查，`git diff --check` 通过。构建仍有上游 hiredis 的旧 CMake 兼容警告和 QtADS 的既有编译警告，不影响本次通过。

## 14. SCL 离线分析（2026-11-06，P6 已通过）

主程序通过应用服务运行独立解析库。文件菜单提供打开、重载、取消和关闭，ADS 工作区支持浏览、搜索、筛选和校验摘要导出。完整方案及验收记录见 [Task1](Tasks/Task1.md) 和 [P6](Tasks/Task1P6.md)。

- 产品范围：只读打开、分层校验、IED/数据集/报告/对象/通信配置浏览、搜索、双向引用与原文预览；不包含编辑修复、设备通信、抓包或自动生成仿真实例。
- 架构：`src/iec61850/scl/` 共享领域模型与解析规则，经 `src/core/` 应用服务在后台分析，再向 GUI ViewModel 交付不可变快照；沿用 Widgets + ADS。
- 大文件策略：流式 XML、类型共享、按需展开、后台搜索和分窗口原文；行列/字符偏移需与原始字节编码正确映射，不能直接作文件 seek 偏移。
- 校验边界：先实现文件/XML、基础结构和引用一致性的明确子集，公开适用版本与未执行项；完整 XSD 引擎和 schema 获取/分发单列后续阶段。
- 本地真实 SCD 只读扫描约 48.08 MiB，P0 已按 namespace URI + localName 复核：104 个 IED、1,036 个数据集和 687 个 ReportControl；文件指纹未变。标准 P 元素为 1,405 个，扩展 P 为 2 个。统计不是合规结论，原始工程文件不纳入仓库。
- P0 已完成 windows-debug 配置/构建，五组测试通过；原有三组测试保留，新增 MainWindowBaselineTest 与 SclFixtureBaselineTest。前者验证列表/详情、正常退出、跨进程 ADS 布局；后者检查 12 份自建样本的 XML 和计数，不执行语义规则。
- P0 修复了详情 Dock 销毁回调访问已析构成员的问题；主窗先析构 ADS 管理器，并显式采用 QSettings 默认格式以支持测试隔离，保存失败输出警告。产品默认仍使用原 NativeFormat。
- `SclAnalysis` 位于 `src/iec61850/scl/`，只依赖 Qt Core。Reader 以 QXmlStreamReader 扫描原始快照，按文档顺序存储节点、属性、父子及兄弟关系；SourceIndex 独立映射起始标签的原始字节和行号，支持 UTF-8/UTF-16，预览单行最多读取 8 KiB。
- `ReferenceResolver` 消费 P1 的只读快照。先建立带作用域的定义索引，再解析类型、控制块到数据集、FCDA、通信和 ExtRef。结果包含引用的正反向索引、报告定义映射、诊断、快照 SHA-256 和规则覆盖状态。规则范围见 [SclRules](Tasks/SclRules.md)。
- `SclAnalysisService` 用 Qt Concurrent 执行读取和分析，QFutureWatcher 在拥有服务的线程交付结果。单个运行任务配合一个待处理路径，切换时取消旧任务并只保留最新请求。服务监视当前文档的磁盘变化，重载前可继续读取已发布的内存快照。
- `SclBrowseIndex` 在发布快照前构建父子行与分类索引。`SclWorkspace` 通过 Qt Item Model 按需读取视图数据，按 NodeId 联动导航、属性、关系及原文。原文预览从快照读取选中行前后各 15 行，长行显示截断标识。
- `SclSearchSession` 管理独立于加载会话的搜索取消标记和分批交付。全局匹配从 P2 映射、诊断和结构化字段取值，当前表格筛选使用行映射。`SclIssueExport` 后台写出完整校验摘要，通过 QSaveFile 原子提交。
- P6 按版本和 SHA-256 校验 ADS 静态布局，兼容版本 0 的旧布局。默认布局由 ADS 在创建静态 Dock 后捕获。退出保存前恢复仿真列表并移除动态详情 Dock。文档关闭会取消加载和搜索、清除快照及视图状态。

## 15. SCL 工作台视觉与交互优化（T2.4 已实施）

[Task2](Tasks/Task2.md) 规划在现有工作区内实现树表联动、可点击路径和有限窗口 XML 高亮。导航以快照内节点及目标视图为依据，区分筛选遮挡和无对应表行。保留现有后台分析与校验覆盖边界，分阶段验收后更新本节的实现状态。

交互式引用拓扑纳入本轮验收。Qt 应用服务从分析快照构造局部图与原文证据，独立本地 HTML 使用 SVG 呈现对象引用字段来源、报告与数据集、FCDA 成员及反向关系。

[T2.0](Tasks/Task2T2.0.md) 记录了操作基线、离屏导航计时及四态视觉小样。[T2.1](Tasks/Task2T2.1.md) 已实现树表双向定位、节点概览、诊断原文定位、筛选遮挡提示及历史恢复。视图在文档顺序索引中查找行，历史按快照内身份恢复。

[T2.2](Tasks/Task2T2.2.md) 已实现可点击祖先路径、引用分段、分组映射详情和原文高亮。`SclSourcePreview` 从有限窗口取文，按解析位置恢复高亮状态，并通过边栏独立绘制行号。

[T2.3](Tasks/Task2T2.3.md) 记录了离线引用拓扑及验收。解析器随快照保留 FCDA 字段证据，`SclTopology` 构造有界局部图并生成单文件页面，工作台异步生成后通过系统浏览器打开。

[T2.4](Tasks/Task2T2.4.md) 统一了树表和焦点样式，新增可收起详情栏及空表提示，并更新 F1。真实文件十轮加载关闭通过，实体跨屏和浏览器内部交互仍待复核。

卡顿反馈后的调整将树表恢复为原生绘制，原文高亮仅在原文页展开时生成。显示问题原文时直接使用诊断位置。

SCL 表格的描述列读取该行所关联节点的原始 `desc`。FCDA 的声明描述和唯一解析目标的描述分别展示。歧义引用的总行保留空描述，各候选行展示自己的描述。

FCDA 成员表同时展示 MMS Item 和所属 DO 的实例描述 `dU`。后台解析按已解析的 LN、DO/DA 路径和数组下标匹配 DOI、SDI、DAI。所属 DO 唯一匹配且模板声明 `dU` 为 DC 时，读取其唯一的非定值组 `Val`。详情中的实例及描述链接定位到同一快照的原文。

## 16. SCL 浏览卡顿排查规划（尚未实施）

[Task3](Tasks/Task3.md) 基于当前代码审查制定 T3.0–T3.3 门槛。现有树表已使用按需模型，搜索在后台执行，导航经快照 NodeId 和分类二分定位；未发现全量 UI 元素遍历检索。待测热点包括同步详情 HTML 与引用模型重置、高入度关系排序、表格筛选切页重置和无单次上限的搜索批次交付。

后续先记录真实桌面事件循环延迟与分段耗时，再按证据实施可见详情刷新、有界关系/批次和展示预算。解耦详情时必须保留数据集成员作用域设置，异步结果继续绑定快照与请求代次。本次仅交付方案，未确认卡死根因；用户给出的 E 盘样本和仓库路径在当前 C 盘工作区环境不可访问，真实文件验收仍待执行。
