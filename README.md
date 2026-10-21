# Muxora

Muxora 是一款面向远程工作的原生桌面应用，计划整合 SSH 终端、会话管理、文件传输和远程图形应用。首发平台为 Windows，采用 C++20 与 Qt 6 Widgets；后续支持 macOS 和 Linux。

当前交付的是 **Muxora 应用的首个窗口系统预览**，后续功能继续在这套应用代码上开发。已接入标签组与窗格分割、四边停靠、轻量和完整窗口、跨窗口拖拽、顶部／底部工具区、独立栏显隐、三种全屏与禅模式、命令面板和布局保存。标签中的占位内容用于试用布局，SSH、SFTP 和终端协议尚未实现。

Lua 脚本可以调用窗口和布局 API、注册命令与快捷键、定制工具条和状态项，并接收宿主事件与计时回调。配置入口为“工具 → Lua”，用法见 [Lua 使用说明](docs/development/lua-guide.md)。真实终端输出源和按月／年的日历触发器留待业务接入。

## 下载试用

Windows x64 已发布的便携版见 [GitHub 预发布](https://github.com/UniversesAurora/Muxora/releases/tag/v0.1.0-preview.1)。下载 `Muxora-preview-windows-x64.7z`，完整解压后运行 `Muxora/muxora.exe`，无需安装 Qt。该发布包的功能以对应 Release 说明为准；后续源码改动不会自动进入旧包。

同页的 `.sha256` 文件用于核验下载。Qt 源码 ZIP 是可选的依赖源码，运行程序无需下载；自有代码与各项依赖的许可证见 [LICENSE](LICENSE) 和 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 构建与运行

需要 PowerShell 7、Git、CMake、Ninja，以及包含匹配 C++ 编译器和 Linguist 翻译工具的 Qt 6 SDK。当前 Windows 预览使用 Qt 6.12 的 MSVC 2022 64-bit 套件和本机 Visual Studio 2022 的 MSVC 编译器（[ADR 0009](docs/adr/0009-msvc-toolchain.md)）。Qt 安装在仓库外，不提交 SDK 或构建产物。

首次构建会从 Lua 官方下载固定的 5.5.1 源码，在构建目录编译并静态链接；无需另外安装 Lua。嵌入版本的构建方式及垃圾回收 hook 适配见 [依赖说明](third_party/lua/README.md)。

```powershell
pwsh ./tools/build.ps1 -Configuration Debug -Test -Run
```

脚本支持 `-QtRoot` 指定 Qt kit 目录，或读取 `QT_ROOT`；也会发现仓库旁的 SDK 和忽略提交的 `.local/sdk.json`。完整检查入口为 `pwsh ./tools/verify.ps1`。构建、运行、测试和部署参数见 [窗口系统预览说明](docs/development/window-system-preview.md)。这是供试用与改进的首个版本，功能范围、验证记录和待完成项以该说明为准。

交付 Windows x64 便携预览时，安装 7-Zip 后运行 `pwsh ./tools/package-portable.ps1`。脚本构建并测试 Release，将运行库和许可证打包到 `out/dist/*.7z`，随后解压检查启动与布局恢复。用户解压整个文件夹即可运行 `muxora.exe`，状态保存在相邻的 `data/` 中。

## 仓库结构

| 目录 | 内容 |
| --- | --- |
| `modules/window-system/` | 独立窗口系统、公开 API 和模块测试 |
| `modules/lua-runtime/` | 在工作线程运行 Lua 的异步运行时（Qt Core） |
| `modules/window-system-lua/` | 窗口 API 的 Lua 适配与组件服务入口 |
| `src/app/` | Muxora 应用入口，后续产品继续使用 |
| `src/workspace_host/` | 宿主内容接入；目前提供布局试用的占位内容 |
| `tools/` | 开发与构建脚本 |
| `docs/design/` | 产品行为、模块边界和接口设计 |
| `docs/development/` | 工程规范、测试要求、预览说明、交接记录和开发经验 |
| `docs/roadmap/` | 实现计划、功能对标和未来规划 |
| `docs/adr/` | 架构决定记录 |
| `third_party/` | 固定版本的依赖构建定义与许可证 |
| `out/` | 本地构建与部署产物，忽略提交 |

文档入口见 [docs/README.md](docs/README.md)。设计文档描述目标；预览说明记录已实现范围，二者不能混同。

## 参与开发

请先阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。自动化代理还需遵守 [AGENTS.md](AGENTS.md)。

项目自有代码采用 [MIT 许可证](LICENSE)。第三方组件保留各自许可证；当前 Windows 预览的清单见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)，选型约束见 [依赖与许可约束](docs/development/engineering-design.md#62-自有代码与第三方许可)。
