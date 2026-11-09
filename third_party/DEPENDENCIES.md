# 第三方依赖记录

库源码随仓库保存在 `third_party/`；固定版本及许可证必须保留。构建产物位于 `build/`，独立运行的工具位于 `tools/`。

| 依赖 | 固定版本 | 仓库目录 | 许可证 | 集成方式 |
|---|---|---|---|---|
| Qt Advanced Docking System | 5.0.0 | `Qt-Advanced-Docking-System-5.0.0/` | 见该目录 `LICENSE` 与 `gnu-lgpl-v2.1.md` | CMake 子目录，动态库 |
| hiredis | 1.3.0 | `hiredis-1.3.0/` | BSD-3-Clause，见 `COPYING`；适配器另保留各文件声明 | CMake 子目录，静态库 |

## hiredis 来源与完整性

- 上游：[redis/hiredis](https://github.com/redis/hiredis)，标签 `v1.3.0`。
- 下载：[v1.3.0 源码归档](https://codeload.github.com/redis/hiredis/tar.gz/refs/tags/v1.3.0)。
- 下载归档的 SHA-256：`25cee4500f359cf5cad3b51ed62059aadfc0939b05150c1f19c7e2829123631c`。
- 导入完整归档源码，无功能补丁；仅将 `hiredis.targets` 的 UTF-8 BOM/CRLF 规范化为无 BOM/LF。原始文件名和版权声明保持不变。
- 关闭 SSL、上游测试、示例及 NuGet 打包；当前仅调用连接、PING 命令及响应解析 API。应用不依赖 hiredis DLL。
- Qt Concurrent 来自项目现有 Qt 安装，与 Qt Core/Widgets 使用同一工具链。
- 上游目前将 Windows 列为社区尽力兼容平台；本项目已在当前 MinGW64 工具链验证，升级时应重新验证，不能据此推定其他 Windows 工具链已支持。

`tools/Redis/` 为用户正在迁移的工具分发内容；本次不调整其版本、二进制、配置或 PID 跟踪状态。后续迁移完成时应补齐该工具包的来源、校验值与许可证记录。

## Windows Qt 运行时部署

Qt 和 MinGW/UCRT64 运行时来自本机所选工具链，未作为库源码新增到仓库。`DeployRuntime` 从同一 Qt/编译器安装复制 Qt 插件及递归 DLL（例如 ICU、FreeType、HarfBuzz、GLib、图片与压缩库），避免依赖开发终端的 PATH；实际版本随该 Qt 安装确定，不得混用 MinGW64 与 UCRT64 的二进制。生成文件仅保存在忽略的 `build/<构建目录>/bin/`。该开发部署步骤不生成包含全部许可证的正式发布包，正式分发仍需按实际工具链包补齐相应来源、版本和许可证。
