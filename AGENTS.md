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

## QtADS 构建与部署

- QtADS 源码固定在 `third_party/Qt-Advanced-Docking-System-5.0.0`，由根 CMake 通过 `add_subdirectory()` 构建并链接。
- 运行时 DLL 随可执行文件部署到 `build/<preset>/bin/`，由 CMake 的构建后步骤或 `install()` 规则完成。
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
