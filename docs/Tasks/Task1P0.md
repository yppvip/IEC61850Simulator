# Task1 / P0 基线与规则准备验收记录

日期：2026-11-06（Asia/Shanghai）。结论：P0 通过，可在单独指令下进入 P1。本轮未实现产品 SCL 解析、引用校验或新工作台。

## 1. 交付与范围

- 固定当前 Debug 构建、原有测试与真实应用 Redis 启动基线。
- 新增 `MainWindowBaselineTest`：使用真实 MainWindow、ADS 和 Qt offscreen，验证列表、详情、正常退出及独立进程间的布局恢复。
- 新增 12 份自建微型 SCL 输入与 `Manifest.json`；`SclFixtureBaselineTest` 检查清单、XML 状态、根和命名空间敏感的计数。没有拿未实现的语义校验结果做断言。
- 建立 [SclRules.md](SclRules.md) v0.1：固定首批规则 ID、作用域、前置条件、反例及待补样本，明确所有产品规则尚未实现。
- 新增 `scripts/MeasureSclBaseline.ps1`：本地只读流式统计、SHA-256 前后核对、标准/扩展命名空间分开计数；原始工程文件不复制进仓库。
- README、AGENTS、DEVELOPMENT 和 Task1 同步记录 P0；长期 QML/MDI、双进程/订阅规划与当前 Widgets 骨架区分。

无新增第三方库/独立程序，无需改变依赖记录。没有修改 `tools/Redis/` 或取消其历史 PID 跟踪。

## 2. 环境与复现

Windows 11 x64，MSYS2 MinGW64；CMake 4.4.0、Ninja、GCC 16.1.0、Qt 6.11.1，应用构建类型为 Debug。Qt 安装包自身为 release 构建，不代表应用未使用 Debug。QtADS 5.0.0 动态链接，hiredis 1.3.0 静态链接。

CPU 环境标识：`Intel64 Family 6 Model 183 Stepping 1, GenuineIntel`。系统硬件详细信息的 CIM 查询被当前环境拒绝，未获得处理器商品名和物理内存总量；不将磁盘容量当作内存指标。

PowerShell（仓库根目录）：

```powershell
$env:PATH = 'C:/msys64/mingw64/bin;C:/msys64/usr/bin;' + $env:PATH
& C:/msys64/mingw64/bin/cmake.exe --preset windows-debug -DCMAKE_PREFIX_PATH=C:/msys64/mingw64
& C:/msys64/mingw64/bin/cmake.exe --build --preset windows-debug
& C:/msys64/mingw64/bin/ctest.exe --preset windows-debug --output-on-failure

./scripts/MeasureSclBaseline.ps1 `
  -InputPath 'E:/Document/项目/自动配置/湖北武汉220kV电校培训变20261021.scd' `
  -OutputPath 'build/windows-debug/p0-artifacts/RealScdBaseline.json'
```

统计脚本输出包含本地文件名及根声明，默认放在被忽略的 `build/`，不上传。该脚本是 P0 测量工具，不是产品解析器，也不要求构建机/CI 存在真实文件。跨平台和其他工具链尚未复验。

## 3. 构建与测试结果

变更前：当前 Debug 配置/构建成功，原有三组测试 3/3 通过。变更后：五组测试通过，无 SCL 业务接入。

| 验证项 | 结果和证据边界 |
|---|---|
| windows-debug 配置、构建 | 通过；QtADS DLL 构建后部署到 `build/windows-debug/bin/`，启动不需改 tools/QtADS |
| RedisClientTest | 通过；hiredis 的响应、错误、连接/超时和后台生命周期测试继续保留 |
| RedisConfigurationTest | 通过；配置及工作目录规则无回归 |
| RedisStartupTest | 通过，未跳过；不可用 TCP 端口和已有真实 Redis 两种场景各启动实际应用两次，日志和进程存活正常，应用退出后测试 Redis 仍存活 |
| MainWindowBaselineTest | 通过；创建服务器/客户端分别更新列表、详情及状态；点击列表激活已有详情且不重复创建；额外服务器不改变客户端数；创建详情后正常销毁主窗不崩溃 |
| 跨进程布局 | 通过；进程 A 将客户端与服务器 Dock 合并为同一区域并关闭保存，进程 B 重建主窗后检查同区关系及静态 Dock 数量；不是仅比较一次内存中的 save/restore |
| SclFixtureBaselineTest | 通过；12 个输入与清单一致，10 个 XML 良构、2 个预期失败，标准 namespace 计数排除扩展同名节点 |
| 截图检查 | 已检查 offscreen 的 `p0-artifacts/MainWindow.png`；列表、详情及状态文字可见，记录当前 UI 外观，不作为后续美化验收 |

Redis 启动测试使用临时目录、独立配置、随机端口、`autoStart=false` 和关闭持久化的测试 Redis 子进程；不启动 RedisService、不改工具目录、不停止用户 Redis。临时应用靠复制到运行目录的 QtADS DLL 启动，Qt 平台插件从构建部署路径指定。

基线测试不依赖真实工程文件。UI 测试显式将 QSettings 默认格式设为 IniFormat，User/System 路径均指向临时目录；主窗使用显式 defaultFormat 的构造方式，应用正常运行时仍默认使用原 NativeFormat 位置。不会用测试布局覆盖用户持久化布局。

## 4. P0 发现并修复的缺陷

新增“创建详情后正常退出”测试在修复前触发访问冲突：MainWindow 的 `m_detailDocks` 已随派生类成员析构，而 QObject 基类后续删除 Dock 子对象时，destroyed 回调仍访问该成员。原有启动测试调用 kill，因此未覆盖正常退出。

修复：MainWindow 显式析构 ADS 管理器，使 Dock 及其回调在成员仍有效时结束。新测试已在修复前复现崩溃、修复后正常退出。未改第三方源码。

布局测试初版也揭示了测试隔离问题：`QSettings(organization, application)` 固定使用 NativeFormat，setDefaultFormat 不生效。依据 [Qt 官方构造器说明](https://doc.qt.io/qt-6/qsettings.html)，改用带显式 format 的构造器，并保持生产默认行为；避免测试依赖注册表。保存布局后检查 status，失败时输出警告，避免写入失败仍记录“已保存”。

## 5. 真实 SCD 统计基线

本地只读输入为 Task1 指定文件，50,418,958 字节，扫描前后 SHA-256 一致：

```text
e69733050c24f1e49da0f5695ab804307838e549dff36eff64cab4ff5e182f4b
```

根为 SCL，namespace 为 `http://www.iec.ch/61850/2003/SCL`；schemaLocation 指向相对 `SCL.xsd`；根未声明 version/revision/release。仍标为“标准版本未明确”，不根据 Header version 或命名空间猜测精确版本。

| 标准 namespace 元素 | 数量 |
|---|---:|
| IED / AccessPoint / Server | 104 / 216 / 216 |
| LDevice / LN0 / LN | 302 / 302 / 14,944 |
| DataSet / FCDA | 1,036 / 42,688 |
| ReportControl / GSEControl / SampledValueControl | 687 / 205 / 32 |
| ExtRef / Private | 3,194 / 5,131 |
| LNodeType / DOType / DAType / EnumType | 476 / 119 / 19 / 21 |
| DOI / SDI / DAI | 118,348 / 46,290 / 340,428 |
| P | 1,405 |

最后一个元素位于第 1,214,138 行。扩展 namespace 中另有 Input 3,621、Output 2,305、ReferedLD 51、P 2、privatecfg 2。原设计阶段仅按 localName 统计的 P=1,407 在本次拆为标准 1,405 + 扩展 2，证明正式实现必须区分命名空间。

一次 P0 扫描耗时 1,492 ms（不含前后 SHA-256），PowerShell/.NET 进程峰值工作集 102,203,392 字节。该数值包含宿主，不是 Qt 产品解析器的性能结果；不据此宣称 P1 性能门槛已通过。完整 JSON 在本地 `build/windows-debug/p0-artifacts/RealScdBaseline.json`。

XML 良构性扫描成功；未执行 XSD、类型/引用完整性或工程规则验证，不能推出该文件合规或没有配置缺失。

## 6. 已知限制及下一阶段入口

- Redis 弹窗文字/状态栏实际视觉、原生文件选择器、鼠标拖动、高 DPI/多屏尚未做人工交互验收；自动化已覆盖 P0 所需启动、列表联动和布局保存/恢复路径。
- Qt offscreen 有 propagateSizeHints 等能力提示，不代表原生平台视觉行为；现有 ADS 详情区排列属于基线，不在 P0 重做设计。
- 原有 hiredis 旧 CMake 兼容警告保留，未为消除警告修改上游依赖。
- 本期样本尚不覆盖完整版本/数组/多 AP/ExtRef/循环/编码矩阵；规则目录逐项列出 P1/P2 所需补充。CCD 尚无真实厂家样本，不宣称兼容。
- 未验证 RedisService autoStart 分支；本轮沿用既有实现，不重新进行服务启动试验或干预 Redis 迁移。

P1 可开始实现独立的 SclReader/SclDocument/SourceIndex 及其有意义的模型断言，并补齐安全、编码与原文映射样本；不同时接入新 UI 或设备协议。P0 不自动进入 P1。
