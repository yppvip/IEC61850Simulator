# T6.1 · MMS 合成数据交互原型

2026-11-19。状态：**T6.1 交互原型门槛通过**。独立 Qt Widgets + ADS 原型已编译，四条主查阅路径、复制/导航分离、历史和紧凑窗口经过自动化及原生窗口验证。止于本阶段；T6.2 的领域投影、T6.3 的后台查询和 T6.4 的正式工作台集成未实施。

## 1. 预览入口与隔离范围

本地预览程序：`D:/Code/IEC61850Simulator/build/task6-t61/bin/MmsInteractionPrototype.exe`。直接启动即可查看合成 Demo.scd；无真实文件导入、设备连接或 Redis 启动，也不读写用户 QSettings。程序持续显示“合成示例 / 离线交互原型”，所有实例、地址和检查结果都是明确标注的预置展示数据。

独立 Debug 目录为 `build/task6-t61`，Windows 11 / MSYS2 UCRT64 / GCC 16.2.0 / Qt 6.12.0 / CMake 4.4.5 / Ninja 1.13.2，沿用 T6.0 工具链。源码基线提交 `4a4c5d309f0530609cad003739031666bbc0ca04` 加未提交的 T6.0/T6.1 改动；保留用户已有修改和未跟踪图标压缩包。

预览程序 SHA-256：`30AB68D2F612A6F6E77E49B23179FC470E4D0CC9834F5C0B9CFE5274EF6E6DAE`。

`MmsPrototypeTest.exe` SHA-256：`2D5F8938A8402F6A43A2CEA923A400A091745316A0F8FC8317F62575395EC7EA`。

原型通过 `IEC61850_BUILD_MMS_PROTOTYPE=ON` 显式启用，默认 OFF。另用独立配置目录确认默认值为 OFF，目标图中没有原型目标。它不链接进主程序，也不把合成数据接入正式 SCL 会话；没有安装规则。原型是项目自己的验证程序，不是新增第三方依赖工具，不放入 tools/。

用户原有 PID 37080、`build/windows-ucrt64-debug/bin/IEC61850Simulator.exe` 始终保留。本轮没有读取真实 SCD、修改 `src/`/`app/` 运行代码、Redis 配置或 tools/Redis，不新增库或图标，第三方记录无新增条目。

## 2. 实现与选择契约

| 文件 | 职责 |
| --- | --- |
| [MmsPrototypeSession.hpp](../../tests/prototypes/MmsPrototypeSession.hpp)、[实现](../../tests/prototypes/MmsPrototypeSession.cpp) | 有界合成快照、展示字段/原值/来源、任务路线、作用域、筛选、历史与动作的 ViewModel；不读取或解析工程文件 |
| [MmsPrototypeWindow.hpp](../../tests/prototypes/MmsPrototypeWindow.hpp)、[实现](../../tests/prototypes/MmsPrototypeWindow.cpp) | QMainWindow、单一 ADS 管理器、任务/中央/统一详情三个 Dock；列表与原文在当前对象上下文中切换 |
| [MmsPrototypeMain.cpp](../../tests/prototypes/MmsPrototypeMain.cpp) | 独立预览入口 |
| [原型 CMake](../../tests/prototypes/CMakeLists.txt) | 开关、可选构建、运行 DLL 与测试插件部署；不更改正式入口 |
| [MmsPrototypeTest.cpp](../../tests/integration/MmsPrototypeTest.cpp) | 五组有行为断言的 UI 测试及原生截图采集 |

沿用 finesse-ui 的产品信息层级与现有原生控件风格；系统字体/调色板、文字状态、短操作提示，无新图片、装饰动画或 Web 运行时。表格直接复用 `SclTableInteraction` 的完整值复制、显式 Enter 原文、按可见行测高和超长值入口，不复制 DisplayRole 中的预览提示。

固定的选择身份为 **快照 + IED 范围 + 任务路线 + 原始记录键 + 所属报告/对象 + 字段键**。两个成员指向同一信号时，仍分别使用 member-1/member-2；两个报告共用 Events 时，成员内容共享但父报告上下文独立。属性/模板来源不冒充当前工作对象。

历史保存原始身份、当前单元格列、筛选、字段及“顶部记录键 + 行内偏移”，不用旧可见行号或总滚动像素恢复到另一个视图。切换快照清空历史和选择；切范围清空旧父对象/成员/筛选。仅被筛选遮挡时清除旧行高亮，保留所选对象详情并标明遮挡，清除筛选可重新定位。

## 3. 四条主查阅路径

| 用户任务 | 原型操作 | 已验证结果 |
| --- | --- | --- |
| 装置上送什么 | DemoIED → 报告上送 → brStatus → 查看数据集成员 | 两条成员保留原始序号；可从成员查看属性/MMS 地址和同快照原文，并返回原成员 |
| 未入数据集信号的地址 | 全局搜索 Unlisted → 定位对象 → 查看属性 → 技术字段 | 未入数据集对象仍有入口；Domain/Item、FC、类型、声明值状态分列；可返回原对象 |
| 报告配置给谁 | 报告 → 客户端配置，或任务导航直接选客户端 → 定位关联报告 | SinkZ、SinkA 保留声明顺序；不显示“在线订阅”；urUnknown 无客户端时仍可查报告，实际客户端未知 |
| 问题和原文在哪里 | 文件检查 → REF001 示例 → 查看原文 / 定位关联报告 | broken/Absent 的预置错误与来源对应；返回恢复原问题；XSD 明确“未执行”，没有执行检查的虚假承诺 |

连接配置入口展示 AP1、IP 和未声明端口，不用常见端口替代原字段。高级入口使用同一组报告记录展示技术字段，明确为高级字段示例；正式高级表、XML 结构树和全工程引用拓扑留在 T6.4 复用，原型没有再做一套解析器。

报告列表、数据集成员、信号对象/属性、客户端、连接、问题及搜索都通过中央页路由逐级查阅，不新增一排协议 Tab。面包屑和统一详情始终显示当前对象；从字段打开原文，标题注明工作对象及该字段，普通对象原文不会沿用上一次字段标题。

## 4. 空态、未知、错误与交互

- EmptyIED 下保留任务入口，明确“当前范围没有此类声明；这不表示不支持 MMS”。不会用空表暗示全工程缺失。
- urUnknown 保留缺 indexed、缺 rptID 和客户端未知；broken 保留空 rptID、非空但未解析的数据集。原始空字符串与属性缺失在展示和记录中分开，均不把提示文本复制到剪贴板。
- 表格单击选对象；双击/Ctrl+C 只复制完整单元格，Ctrl+Shift+C/右键复制行沿用共享组件；原文使用按钮或 Enter。测试先设置剪贴板哨兵，确认本次操作实际写入一次，避免旧剪贴板值造成假通过。
- 字段表可选择来源；“查看所选字段原文”和 Enter 打开其对应证据。原文为同一合成快照的最多七行上下文，行号/截断范围放在可复制文本外，自动滚到证据锚点。来源不存在的在线字段禁用原文动作。
- 全局搜索忽略当前 IED，结果选择后“定位对象”显式进入目标范围；当前表格筛选遵守范围及父对象。合成数据量有界，筛选同步执行仅为交互验证，不作为真实工程大文件搜索方案。
- 窄窗隐藏左侧任务 Dock，用顶部任务选择器访问同一路由。成员/属性/搜索等当前子层级显示明确名称，不出现空选择器；返回/前进、Ctrl+F/Ctrl+L 和原生 Tab/Space/方向键保留。
- 详情可收起、重开、拖动 ADS 标题停靠/浮动；重置布局只影响当前原型窗口，不保存到用户布局。单个 ADS 管理器管理所有原型 Dock，没有混用 QMainWindow::addDockWidget。

## 5. 验证结果与修正

### 5.1 构建与回归

独立 Debug 配置和构建成功，默认 DeployRuntime 部署 Qt/ADS 与递归依赖。清洁 PATH（原型 bin + Windows/System32 + Windows）完整 CTest **10/10 通过**，包含原有九组和新 MmsPrototypeTest；一轮 23.22 s。最后原文锚点、数据集面包屑及退出回调改动后，补跑新原型的离屏用例及全部原生缩放矩阵通过。退出时先断开详情开关与 Dock 回调，再销毁 ADS 管理器，避免成员销毁后的访问。主程序不链接原型；默认关闭配置已独立核对。

五组 UI 用例：FourRoutes、StatesAndHistory、CopyAndFieldEvidence、NarrowAndKeyboard、FullValuesAndAnchors。覆盖主路径、空/未知/错误、缺省值不复制提示、复制与激活不串联、字段原文/锚点、返回恢复范围和成员、切快照清理、未入数据集对象、超过 6 千字符的中文/emoji/换行原值复制，以及额外 80 行合成记录下按记录身份恢复滚动锚点。

没有编写“新 MMS 解析正确”的断言；原型的实例、地址和诊断全部来自合成 ViewModel。领域语义、版本准入和异步交付仍按 T6.2/T6.3 单独验收。

### 5.2 Windows 原生窗口

Windows QPA 下各缩放均完成五组用例，使用程序驱动的原生控件事件；截图另作视觉检查。它不是物理鼠标、实体跨屏或目标用户走查的替代。

| 缩放 / DPR | 主流程实际逻辑尺寸 | 窄窗实际逻辑尺寸 | 结果 |
| --- | --- | --- | --- |
| 100% / 1 | 1280×800 | 620×560 | 通过 |
| 150% / 1.5 | 1280×707 | 620×560 | 通过；主窗口高度受桌面工作区约束 |
| 200% / 2 | 962×531 | 620×531 | 通过；主/窄窗高度受桌面工作区约束 |

200% 初跑曾暴露默认 Dock 分配使列表只剩表头。ADS 中央区会伸缩、详情区保留请求的像素高度，不能用 700/300 这样的常量冒充比例。现改为在短窗口读取实际可分配高度后均分，配合精简提示、详情收起和可滚动区域；重置/重开详情也应用同一规则。测试进一步断言列表及字段至少可见一行、原文至少可见两行并且证据锚点在视口内，未以“模型里有数据”代替可操作性。

另外修正了子层级任务选择器空白、属性页返回目标和字段/对象原文标题混用。短窗口的列表仍可能只显示少量行，用户可收起或拖大详情区；横向技术字段通过滚动查阅，普通单元格继续完整换行。

原生截图和日志位于忽略的 `build/task6-t61/artifacts/`：`native-100/`、`native-150/`、`native-200/` 包含 Reports、Members、Address、Source、Error、Empty、NarrowCollapsed、NarrowSearch。数据全部合成，可直接用于设计评审。

## 6. 复现命令

在仓库根目录的 PowerShell 执行。构建和启动 PATH 分开，独立目录不覆盖用户运行版本。

```powershell
$env:Path = 'D:/msys64/ucrt64/bin;D:/msys64/usr/bin;' + $env:Path
cmake -S . -B build/task6-t61 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=D:/msys64/ucrt64 -DCMAKE_C_COMPILER=D:/msys64/ucrt64/bin/gcc.exe -DCMAKE_CXX_COMPILER=D:/msys64/ucrt64/bin/g++.exe -DCMAKE_MAKE_PROGRAM=D:/msys64/ucrt64/bin/ninja.exe -DBUILD_TESTING=ON -DIEC61850_BUILD_MMS_PROTOTYPE=ON
cmake --build build/task6-t61 --parallel 6
$env:Path = (Resolve-Path build/task6-t61/bin).Path + ';' + $env:SystemRoot + '\System32;' + $env:SystemRoot
& D:/msys64/ucrt64/bin/ctest.exe --test-dir build/task6-t61 --output-on-failure
./build/task6-t61/bin/MmsInteractionPrototype.exe

# 原生自动化；不读取真实工程，也不使用用户布局设置。
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_SCALE_FACTOR = '2'
$env:MMS_PROTOTYPE_ARTIFACT_DIR = (Resolve-Path build/task6-t61/artifacts).Path + '/native-200'
./build/task6-t61/bin/MmsPrototypeTest.exe
Remove-Item Env:QT_QPA_PLATFORM,Env:QT_SCALE_FACTOR,Env:MMS_PROTOTYPE_ARTIFACT_DIR
```

独立原型使用同一编译后的窗口/会话组件完成测试；预览 EXE 供人工交互评审，不会在后台替换正式应用。退出后不留下持久化工程或会话文件。

## 7. 后续边界

T6.1 门槛的四条查阅路径、选择契约、空/未知/错误、复制/原文分离和紧凑原生窗口已验证，可作为 T6.2 的界面契约。真实文件导入、新 MMS 实例/客户端解析、按需属性展开、后台搜索/分页/图、取消与代次、正式 ADS 布局持久化等均未接入。

三名初学者走查、系统高对比/主题、实体跨屏动态 DPI 和真实工程性能继续由后续门槛执行。没有将 T6.0 未确认的版本默认值变为确定结论；Task3、Task5 未通过项及 P7 完整 XSD 的边界保持。此次同步 README、AGENTS、DEVELOPMENT 和 Task6 进度，不自动进入 T6.2。
