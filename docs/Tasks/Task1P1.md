# Task1 P1：流式解析与原文索引验收

日期：2026-11-06。分支：`feature/Desktop`。P1 通过；P2 尚未开始。

## 本机环境和前置门槛

本次工作区为 `C:/Code/IEC61850Simulator`。本机没有 `E:/Code/IEC61850Simulator`；QtADS 5.0.0 和 hiredis 1.3.0 源码已在当前工作区的 `third_party/`，Redis 独立程序和配套文件已在 `tools/Redis/`。未新增依赖、未修改 Redis 工具目录。

Windows / MSYS2 UCRT64，GCC 16.2.0、Qt 6.11.1、CMake 4.3.1、Ninja 1.13.2；Intel Core i5-9400F、16 GiB RAM。P1 开始前先完成 Debug 配置、构建与 5 组 P0 CTest；列表、详情、布局及两类 Redis 启动基线通过，QtADS DLL 已部署到 `build/windows-debug/bin/`。配置提示缺少可选 Vulkan 头文件、hiredis 旧 CMake 兼容警告，未阻碍构建。

## 交付和验证

- Qt Core 静态库 `SclAnalysis` 使用 QXmlStreamReader 流式读取原始字节快照。保留元素与属性的 localName/namespace URI、父子及兄弟关系、直接文本和原始起始位置。数据集成员按文档顺序保存，重复成员和缺省属性保留。模板、通信、扩展节点均可通过同一模型访问。
- 原文索引按 CRLF、LF 和 CR 建行，支持 UTF-8（含 BOM）、ASCII 和 UTF-16 LE/BE。单行预览上限为 8 KiB，截断时标记，避免从超长行产生大块 UI 文本。元素起点为原始字节偏移及一基行号；未实现精确属性跨度。
- 拒绝 DOCTYPE，未设置实体解析器，不跟随 schemaLocation。默认上限：256 MiB 文件、256 层、200 万元素、600 万普通属性、单个属性值或元素直接文本 1 Mi UTF-16 代码单元、800 万行。文件读取、编码、XML 格式、根命名空间和超限有区分的诊断；失败不发布部分模型。
- 新增 `SclReaderTest`，覆盖模型内容、同名扩展、命名空间别名、空/坏 XML、未知 CCD、DTD、资源边界、中文和非 BMP 字符、混合换行、多行标签、注释、CDATA 及快照所有权。Debug 构建和全部 6 组 CTest 通过。

## 本地真实 SCD

使用 `D:/3Document/湖北武汉220kV电校培训变20261021.scd` 只读验收。原文件 50,418,958 字节（48.08 MiB），验收前后 SHA-256 与 P0 一致。Qt 解析器与独立 .NET XmlReader 扫描的 78 类标准命名空间元素计数逐项一致；总元素数（含扩展）为 812,853，最后一个元素起始行同为 1,214,138。标准 `P` 为 1,405；扩展 `P` 另有 2 个。模型还保留 1,171,432 个普通属性。

| Debug 探针指标 | 实测 |
|---|---:|
| 读取快照、建行索引及构建模型 | 5,268 ms |
| 进程峰值工作集（含运行时） | 396,922,880 字节（约 379 MiB） |

该测量不含 GUI、引用解析或语义校验。根未声明 version/revision/release，不能据命名空间推断精确标准版本。真实工程未复制入仓库，也未上传。

```powershell
cmake --preset windows-debug
cmake --build --preset windows-debug --parallel 6
ctest --preset windows-debug

$scd = 'D:/3Document/湖北武汉220kV电校培训变20261021.scd'
$before = (Get-FileHash -LiteralPath $scd).Hash
./scripts/MeasureSclBaseline.ps1 -InputPath $scd -OutputPath build/windows-debug/p1-artifacts/IndependentCounts.json
./build/windows-debug/SclAnalysisProbe.exe $scd
$before -eq (Get-FileHash -LiteralPath $scd).Hash
```

下一阶段是 P2：类型与引用解析、基础内部一致性校验及映射。当前没有接入 GUI、取消和重载、设备通信或完整 XSD 校验；没有验证其他厂商编码或 CCD 方言。
