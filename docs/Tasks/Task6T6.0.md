# T6.0 · MMS 基线、语义契约与自建样本

2026-11-19。状态：**T6.0 门槛通过**。通过范围为现有产品回归、指定样本的只读基线、原值/引用证据契约，以及下文限定的后续解释档案；不表示 MMS 新页面、实例推导或客户端解析已实现。按用户指令止于本阶段，T6.1–T6.5 未实施。

## 1. 路径、构建与运行身份

- 用户明确修订真实文件为 `D:/1Document/自动配置/电校scd/湖北武汉220kV电校培训变20261021.scd`，独立程序管理目录为 `D:/Code/IEC61850Simulator/tools/`，两者均可访问。该修订替代先前 E 盘路径，不触发 Redis 迁移或配置修改。
- 源码基线提交为 `4a4c5d309f0530609cad003739031666bbc0ca04`，另含本轮测试/CMake/文档修改。开工已有 Task6 文档改动和未跟踪的 `res/iec61850-qt-icons.zip`，全部保留。
- 开工及结束均观察到用户 PID 37080 运行 `build/windows-ucrt64-debug/bin/IEC61850Simulator.exe`；未关闭或覆盖。初次 CIM 查询被系统拒绝后，改用 Get-Process 读取路径，不以此阻断工作。
- 独立构建 `build/task6-t60`；Windows 11，MSYS2 UCRT64，GCC 16.2.0，Qt 6.12.0，CMake 4.4.5，Ninja 1.13.2；应用与测试为 Debug，系统 Qt 库本身为 release 构建。与旧 T5.6 的 Qt 6.11.1 不同，不直接计算跨历史构建的性能改善比例。
- 实际部署的主程序为 `D:/Code/IEC61850Simulator/build/task6-t60/bin/IEC61850Simulator.exe`，SHA-256：`104E2F4599FF4A048CE14624DC5625E35C915B23A4C854E823132522E2CE8308`。原生计时测试程序 `bin/MainWindowBaselineTest.exe` 的 SHA-256：`6EDB7E8916EC4D8ABB7CE019E250D634ABCAE74BC3B7E97F1FC1A9432F4A57B8`。
- 默认构建包含 DeployRuntime，ADS DLL、Qt 插件和递归依赖部署到独立 bin。测试运行 PATH 仅含该 bin、Windows/System32 与 Windows，不含 MSYS2 或 tools/QtADS。

本阶段不改 `src/`、`app/`、Redis 配置、库源码及工具程序。第三方来源和许可证记录没有新增条目。

## 2. 指定文件的只读核对

文件 50,418,958 字节（约 48.08 MiB）。读取前后 SHA-256 均为：

`E69733050C24F1E49DA0F5695AB804307838E549DFF36EFF64CAB4FF5E182F4B`

它与 T4.0 记录中的武汉培训变文件同指纹；本轮确实读取用户修订后的路径，没有拿旧文件替代。真实文件及工程字段原文均未上传、复制进仓库或制成测试夹具。

### 2.1 XML 声明盘点

新增本地只读脚本 [MeasureSclMmsInventory.ps1](../../tests/integration/MeasureSclMmsInventory.ps1)：禁止 DTD、外部解析与网络访问，限定文件/字符规模，按标准 namespace URI 统计，仅输出字段存在性、参数分布和指纹。报告的 TrgOps/OptFields 只统计 ReportControl 的直接子节点，避免把 44 个 LogControl 的 TrgOps 混入。

脚本已用真实文件、自建 MMS 和无报告样本复核；无报告时空集合正常输出，带外部实体声明的 DTD 输入被拒绝。测试输入仅保留在忽略的构建目录，不访问其声明的外部路径。

| 项目 | 结果 | 产品含义 |
| --- | ---: | --- |
| IED / AP / Server | 104 / 216 / 216 | 不是在线装置或连接数 |
| LD / LN0 / LN | 302 / 302 / 14,944 | 静态模型结构数量 |
| DataSet / FCDA | 1,036 / 42,688 | 数据集及成员出现次数，不是唯一信号数 |
| ReportControl / RptEnabled | 687 / 687 | 报告定义及其声明，不等于实例数 |
| buffered | true 535；false 151；缺失 1 | 不将缺失直接改写为原始 false |
| indexed | 全部 687 个缺失 | 不能仅凭 max 生成确定实例名 |
| rptID | 非空 573；空字符串 24；缺失 90 | 三种原始状态须保留 |
| datSet / confRev | 均为 687 个非空声明 | 是否解析正确另经产品引用检查 |
| TrgOps / OptFields | 均为 687 个 | 参数声明与真实报告行为分开 |
| bufTime / intgPd 缺失 | 45 / 37 | 缺省解释需要支持档案，不填伪造原值 |
| RptEnabled/max | 16：599 个；18：14 个；12：74 个 | 声明 max 合计 10,724，**不是已确认设备实例总数** |
| ClientLN / Association / ServerAt | 0 / 0 / 0 | 本文件不能验证客户端分配、关联专用实例或服务器别名 |
| Services / ReportSettings | 102 / 72 | 能力声明不表示当前连接/报告启用状态 |
| ConfReportControl / DynAssociation | 72 / 72 | 上限及能力需按版本解释，不与 RptEnabled 混为一数 |
| ConnectedAP / ExtRef | 182 / 3,194 | 所有 ExtRef 均缺 serviceType，不能默认 Report 或 GOOSE |

根 `version/revision/release` 全部缺失，标准 namespace 为 `http://www.iec.ch/61850/2003/SCL`。Header 虽有 version，不能代替根标准版本。该真实文件归入下文 **Unknown** 档案，不宣称已确认 Edition。

ConnectedAP/Address 中有 45 个 IP、40 个 IP-SUBNET、1 个 IP-GATEWAY；OSI AP-Title、AE-Qualifier、PSEL、SSEL、TSEL 各 10 个，另有 NSAP、AP-Invoke、AE-Invoke 各 1 个。只统计这一级 Address，不借用 GSE/SMV 的 MAC、APPID 或 VLAN。字段未覆盖所有 AP 不等于工程错误，部分服务器 AP 没有此类配置或文件未描述该信息。

### 2.2 实际产品解析

用现有 `SclAnalysisProbe <path> --references` 执行 Reader 与 ReferenceResolver：812,853 节点、1,171,432 属性，1,214,142 行；687 个报告定义。已有规则下，15,246 条 LN 类型、7,186 条成员类型、968 条控制块数据集、42,688 条 FCDA、182 条 ConnectedAP、238 条网络控制块、3,194 条 ExtRef 对象引用均解析成功；未生成 ExtRefControl 引用。

产品输出 VER001、EXT001 各一条。完整 XSD、ENG002～004 仍未实现；“现有引用成功”不能升级为完整工程校验通过。单次 CLI Reader/引用用时分别为 3,430/2,608 ms，该次与常规测试有时段重叠，仅用于解析核对，不作为性能比较基线。

## 3. 冻结的 MMS 语义与版本准入契约

### 3.1 数据与选择身份

| 信息 | 固定含义 |
| --- | --- |
| ReportControl name / 定义引用 | 文件定义身份与派生定义位置；现有 ReportMapping 不代表运行实例 |
| 预期实例 | 独立虚拟身份：定义 NodeId + 支持档案 + 实例/关联语境；不能伪造 XML NodeId |
| 原始 rptID | 缺失/空/显式值独立保存；原值不可被默认解释覆盖 |
| 默认报告标识 | 有适用依据时另列解释；与实际收到的 RptID 分开 |
| 信号地址 | LN 实例、类型路径、FC、数组访问、Domain/Item 与来源；同模板的不同实例不合并 |
| 数据集名称 | 声明 datSet、派生引用、MMS 命名变量列表名分列；不冒充标量变量 Item |
| 客户端关系 | ClientLN、Report ExtRef、Association、成员候选分别建边；配置证据不表示在线订阅 |
| 选择/历史/复制 | 同快照的原始节点/成员序号与字段身份；虚拟实例保留推导证据；不可用显示行号跨范围恢复 |

### 3.2 支持档案与命名表 v0.1

这是后续实现的**准入契约**，不是本阶段新增的产品解析功能。保守子集先行，扩大范围需补依据与正反例，不能在实现时静默套用最新规则。

| 档案/输入 | 允许确定表达 | 保留限制与反例 |
| --- | --- | --- |
| Unknown：根版本缺失/未知/冲突 | 原值、唯一作用域内引用、标明推导的定义映射 | 不依据 Header/文件名确认版本；不生成确定实例名/默认 RptID；真实样本属于此档 |
| 2007B4 Explicit Indexed：根明确 2007/B/4，buffered 显式合法，indexed=true，max 显式 1～99，作用域唯一 | 后续可按 01～max 的两位序号推导预期名称；BR/RP 与声明类型对应；始终标“离线推导” | 版本标签不是 XSD 合规证明；非法布尔/0/越界/歧义不进入该子集；实现与断言在 T6.2 |
| 显式 indexed=false、max=1 | 保存无索引声明、定义及命名规则待适用配置确认的状态 | 不从旧 Tissue 提案文字直接拼出统一实例地址；不套用索引后缀规则 |
| 非索引 URCB、max>1 | 表达关联语境及解释受限 | 不统一判错、不把所有客户端合并为一个全局实例，也不自动命名 N 个独立变量 |
| indexed、max、buffered 或相关子节点缺省 | 保留原始缺失与“默认解释尚未确认” | T6.0 未取得完整固定版本 schema 的本地证据包，不把一条默认规则扩展到所有版本；不据 max 合计宣称实例数 |
| ClientLN | 原顺序、NodeId、完整原始引用、目标候选与失败原因 | 索引分配只在对应规则核实时启用；不按名字排序、不把首个可解析客户端放到 01；缺失不代表没人订阅 |
| rptID 缺失/空/非空 | 分别保存原值及配置状态，非空自定义值不追加后缀 | NULL/空值的默认标识解释独立，不能把派生实例引用写回原始 rptID，实际值需在线核实 |

支持依据采用公开主资料，2026-11-19 核对：

- [Tissue 243：RCB Naming](https://iec61850.tissue-db.com/tissues/243)，已生效；其历史问题/提案/讨论必须区分。显式索引的两位编号有据，但不能把其中非索引提案文字当所有版本最终命名规则。
- [Tissue 198：Instantiation of URCB](https://iec61850.tissue-db.com/tissues/198)，已生效；用以核对定义与实例、ClientLN 原顺序及索引问题的来源，不直接覆盖未知版本。
- [Tissue 1771](https://iec61850.tissue-db.com/tissues/1771?workflow=show)，已生效的 Edition 2.1 修正；实例数与是否带名称索引并非同一含义，非索引 URCB 存在关联可见性语境。
- [Tissue 245：Attribute RptId](https://iec61850.tissue-db.com/tissues/245?workflow=show)，已生效；说明 NULL/缺失与空字符串的历史语义。显示原值仍须保留三态，不能以解释结果抹去差异。
- [Tissue 1919](https://iec61850.tissue-db.com/tissues/1919) 仍处于讨论，**不作为生效规则启用**；它提示了 ICD 阶段能力与最终工程实例配置的不同。
- [libiec61850 官方报告 API](https://support.mz-automation.de/doc/libiec61850/c/latest/group__IEC61850__CLIENT__REPORTS.html) 仅作 RCB/RptID/在线字段术语交叉核对，不替代选定版本的标准依据，也不代表本项目接入了在线服务。

本阶段没有下载或集成完整 XSD。默认值完整档案、非索引命名、关联能力限制和更多版本均留在受限范围；T6.2 若需要启用，须先固定合法来源/版本证据再写确定推导。由此，T6.1 可制作包含“未知/未声明/解释受限”的原型，不需要冒充已具备完整标准覆盖。

### 3.3 自建正反例与实际断言

新增 [Task6Mms.scd](../../tests/fixtures/scl/Task6Mms.scd) 与 [Task6MmsLimited.scd](../../tests/fixtures/scl/Task6MmsLimited.scd)，均为自建，已纳入 Manifest；**未作 XSD 校验**。前者混合正常与缺省/空值声明，后者故意包含未知版本、非法字面量和歧义，不能整体称为标准合规样本。

| 用例 | T6.0 已验证 | T6.2 待验证 |
| --- | --- | --- |
| brStatus，显式 BR/indexed/max=2、自定义 rptID | 原值、定义引用、Domain、MMS 定义 Item、同 LN 数据集引用 | 生成 brStatus01/02 的独立预期实例与证据，不给自定义 ID 加后缀 |
| ClientLN 顺序 SinkZ、SinkA | 原始 XML 顺序保留 | 独立解析目标及可适用的实例分配，不能按字母排序 |
| RP、显式非索引/max=1 | 现有 RP 定义映射和原字段 | 非索引命名/适用版本解释 |
| defaults 与 emptyId | 缺失和空 rptID、缺 indexed 的原始状态不同 | 缺省解释与默认标识，未知依据时保持受限 |
| 同一信号重复两次，加 DO 级成员 | 两个原始序号与相同目标并存；DA Item 与 DO Item 不混 | 新报告成员投影分页及图选择 |
| 未入数据集 Unlisted | 现有 SignalCatalog 仍建立对象目录；3 个 LN 各 2 个对象共 6 个实例 | 对属性按需生成完整地址及查询 |
| ServerAt Alias、多端点、显式 ldName | 两条 ConnectedAP 引用解析、Domain 使用显式名称 | MMS 服务器模型与端点投影去重/来源 |
| 非法 buffered、悬空与歧义 datSet | 非法类型不生成伪 RP 映射；REF001/KEY001、歧义状态；数据集引用不误绑定 | 新 MMS 参数值域与覆盖诊断 |
| Header 伪版本、非索引 URCB max=2、外部 ClientLN | VER001、原字段和定义保留 | 不全局判非法、不虚构订阅，按版本/关联显示限制 |

`SclReferenceResolverTest::mmsPreparationContract` 只冻结现有行为和原始证据，不伪造尚未实现的实例/客户端算法测试。既有数组、跨 LD、FC 和歧义测试继续保留。新增规则尚未启用，SclRules 的规则集版本与执行状态不变。

## 4. 构建和常规回归

独立 Debug 配置、编译和默认部署成功。首次清洁 PATH CTest 为 8/9：根目录的 `SclTaskPagesTest.exe` 没有找到部署在 bin/platforms 的离屏插件，停在 Qt 初始化；显式指定插件目录后 `-functions` 可立即完成。终止的仅为本构建目录下本次启动的两个测试进程，未操作用户程序。

CMake 已为该测试设置 `QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:IEC61850Simulator>/platforms` 和 30 s 超时，避免依赖 MSYS2 PATH 或无限停住。修复后完整 CTest **9/9 通过**，最终一轮 18.73 s；包含 Reader、引用、任务索引、夹具盘点、UI、大表、Redis 客户端、正常/不可用重复启动及配置回归。Redis 启动回归只管理它自己的测试进程，不改 tools/Redis。常规测试不依赖真实 SCD。

新增本地 `SclMmsBaseline`、`LocalMmsStages` 均需显式函数名和 `IEC61850_LOCAL_SCL` 才运行，普通 CTest 跳过；GUI 测试使用临时 QSettings，原生 Windows 平台与隔离设置，不覆盖用户布局。

## 5. 同机性能基线

### 5.1 原生现有报告/成员/原文路径，三次独立进程

Windows QPA、1280×800 逻辑像素、DPR 1、清洁 PATH；原生 Qt 控件，程序驱动选择，单轮各 100 个报告导航及原文切页。计时包含调用及正常事件派发，不含物理鼠标输入或 Windows DWM 呈现时间。Ready 是 GUI 观察到完整快照及其交付后的时刻；不能与纯 Reader 时间混用。

| 指标 | 第 1 轮 | 第 2 轮 | 第 3 轮 | 三轮中位数 |
| --- | ---: | ---: | ---: | ---: |
| Ready ms | 6,652 | 6,773 | 6,778 | 6,773 |
| Ready 时峰值工作集 B | 451,321,856 | 450,969,600 | 451,543,040 | 451,321,856 |
| Ready 后首报告可读 ms | 16.61 | 27.43 | 28.52 | 27.43 |
| 切数据集后首成员可读 ms | 12.20 | 11.17 | 14.91 | 12.20 |
| 报告导航 P95 ms | 13.94 | 13.54 | 13.52 | 13.54 |
| 切原文详情 P95 ms | 6.66 | 5.78 | 5.57 | 5.78 |
| 全程峰值工作集 B | 506,732,544 | 506,880,000 | 507,441,152 | 506,880,000 |
| 加载/交互最大心跳间隔 ms | 47 / 170 | 24 / 155 | 24 / 156 | 分别比较，不合成 P95 |

首个数据集为 3 条成员；这不代表大数据集已验收。报告导航各轮最大 16.09/16.34/16.01 ms，原文切页最大 155.90/144.23/151.77 ms；首次原文呈现的较慢值不能被 P95 隐藏。16 ms 心跳的最大值为实际相邻 tick 间隔，额外延迟约为间隔减 16 ms，不是全程 GUI 槽耗时。

现有路径达到本轮导航/局部详情的拟定目标，未观察到 >1 s 无响应。它不是新 MMS 查询或新任务页的验收，也不能关闭 Task3 或覆盖 Task5 的性能失败。

### 5.2 现有处理流水线，三次独立 Qt Core 测试进程

`SclTaskIndexTest::LocalMmsStages` 同步调用与应用后台相同的处理函数，准确测量各步骤；它自身不运行 GUI，也不测生产线程池排队。信号目录与描述/关系在 TaskIndex 一项内，尚未进一步分解其内部热点。

| 阶段 ms | 第 1 轮 | 第 2 轮 | 第 3 轮 | 中位数 |
| --- | ---: | ---: | ---: | ---: |
| Reader（含源索引/XML） | 2,462 | 2,535 | 2,551 | 2,535 |
| ReferenceResolver | 1,614 | 1,667 | 1,654 | 1,654 |
| BrowseIndex | 266 | 273 | 264 | 266 |
| BuildScopes | 146 | 130 | 133 | 133 |
| TaskIndex（含目录、描述、成员关系） | 1,871 | 1,889 | 1,860 | 1,871 |

每轮得到 190,666 个信号目录对象实例、7,138 个共享定义，目录/成员关系预算未触发。该数字不是完整 MMS 基本变量总数。GUI 测试还记录了现有 40 ms 进度轮询观察到的阶段边界，仅作交付定位，不当作精确后台分段。

### 5.3 Task3 相关补充复测

现有 `SclNativeTiming` 在同文件与清洁 PATH 下通过：树选择、详情隐藏、数据切页、筛选切页及检查器切页各 100 个热态样本，目标控件均记录到 100 次 Paint 事件。派发返回 P95 分别为 10.44、11.34、13.31、10.99、2.64 ms；加载 6,568 ms，加载/交互最大心跳间隔 23/103 ms，峰值工作集 465,866,752 B。

390 次 UpdateDetails 最大 5.18 ms，setHtml 最大 4.84 ms，Links Reset 最大 1.07 ms；174 次搜索交付最大 0.44 ms。这里只给热点证据，未实施 Task3 的刷新、索引或批次优化；隐藏页仍生成详情等源码风险及用户实际卡顿路径继续按 Task3 验收。

## 6. 可复现命令与本地证据

从仓库根目录在 PowerShell 执行；构建与运行 PATH 分开。构建不覆盖用户现有目录，真实文件只通过显式环境变量使用。

```powershell
$env:Path = 'D:/msys64/ucrt64/bin;D:/msys64/usr/bin;' + $env:Path
cmake -S . -B build/task6-t60 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64 -DCMAKE_C_COMPILER=D:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/ninja.exe -DBUILD_TESTING=ON
cmake --build build/task6-t60 --parallel 6
$env:Path = (Resolve-Path build/task6-t60/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
& D:/msys64/ucrt64/bin/ctest.exe --test-dir build/task6-t60 --output-on-failure

$env:IEC61850_LOCAL_SCL = 'D:/1Document/自动配置/电校scd/湖北武汉220kV电校培训变20261021.scd'
./tests/integration/MeasureSclMmsInventory.ps1 -Path $env:IEC61850_LOCAL_SCL
./build/task6-t60/SclAnalysisProbe.exe $env:IEC61850_LOCAL_SCL --references
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '1'
./build/task6-t60/bin/MainWindowBaselineTest.exe SclMmsBaseline
./build/task6-t60/SclTaskIndexTest.exe LocalMmsStages
$env:IEC61850_SCL_TIMING = '1'
./build/task6-t60/bin/MainWindowBaselineTest.exe SclNativeTiming
Remove-Item Env:IEC61850_SCL_TIMING
Remove-Item Env:IEC61850_LOCAL_SCL
```

三轮 MMS/后台基线每轮单独启动进程，计时期间不并行构建/测试。被保留的用户原有程序仍运行，存在外部负载可能；未来复测需保持该条件或注明变化。运行状态不因此次读取而修改。

本地忽略目录 `build/task6-t60/artifacts/` 保留 Configure/Build/CTestFinal、MmsInventory、Analysis、MmsBaseline1～3、WorkerStages1～3 和 NativeTiming 的日志/JSON。证据只含统计和计时；未来截图/转储若含工程字段仍限本地。程序/测试路径和指纹与上文对应。

## 7. 门槛结论与保留项

| T6.0 门槛 | 结果 |
| --- | --- |
| 独立 Debug、运行身份及部署 | 通过；用户运行实例保留 |
| 现有常规回归 | 通过；初次插件路径失败已修正，最终 9/9 |
| 指定真实样本可读、指纹与字段覆盖 | 通过；真实路径只读，版本未知如实保留 |
| 术语、保守版本档案、命名边界与正反例 | 通过；只允许有据子集确定解释，缺省/非索引复杂语义不在准入范围 |
| 同机加载、峰值内存、原生导航/详情/心跳及处理分段 | 通过；形成 T6.1 及以后对照基线 |
| 前序未完成项与后续边界 | 已记录；不宣称 Task5/Task3/P7 完成 |

Task5 的加载增量超目标、全部只读表交互推广、跨页图定位、成员单位/倍率/声明值字段、术语词典、真实鼠标复制反馈、动态跨屏 DPI、系统主题与三名用户走查等仍沿原记录。T6.0 不实施新导航，不组织 T6.1 原型走查，也不以原生自动化等同实体用户验收。

十轮会话、100%/150%/200% 与窄窗新流程、新图资源上限及完整 MMS 参数/实例/客户端检查仍由后续对应阶段执行。本轮没有新增在线连接或订阅，没有新增程序或图标资产。后续可按本契约开始 T6.1；本次不继续执行。
