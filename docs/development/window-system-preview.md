# Muxora 窗口系统首个预览

日期：2026-10-12\
状态：首个交互预览，用于试用并继续改进窗口系统；完整 V0 的业务功能和验收尚未完成。

本阶段交付 Muxora 应用的首个预览。应用通过 WindowSystem 公开 API 组织界面，后续远程业务继续在这套代码中接入。入口为 `src/app/main.cpp`，占位内容由 `src/workspace_host/` 提供，用来试用布局、拖拽、窗口和键盘导航；SSH、SFTP、终端解析、远程桌面与会话管理留在后续阶段。

目前不添加应用 logo；顶部先使用菜单、工具和标题按钮，整体使用紫罗兰色系。窗口模块保留独立 API 与构建边界，复用验证通过测试夹具完成。

## 开发环境

首个预览针对 Windows x64，采用 C++20、Qt 6.12 Widgets 和匹配的 MinGW 工具链。当前 kit 所需的 GCC 15.1 工具链已对齐，原有编译器保留；脚本检查 Qt 编译元数据与实际编译器版本，避免误用不匹配工具。`-QtRoot` 指向具体 kit，例如 `E:/develop/sdk/Qt/6.12.0/mingw_64`。

```powershell
pwsh ./tools/build.ps1 -Configuration Debug -Test -Run
```

脚本通过 `-QtRoot` 或 `QT_ROOT` 绑定 kit，也会发现仓库旁的 SDK 和忽略提交的 `.local/sdk.json`。使用显式路径时，例如：

```powershell
pwsh ./tools/build.ps1 -Configuration Debug -QtRoot E:/develop/sdk/Qt/6.12.0/mingw_64 -Test -Run
```

| 参数 | 作用 |
| --- | --- |
| `-Configuration Debug` / `Release` | 选择构建类型 |
| `-QtRoot <path>` | 指定本机 Qt kit 目录，覆盖自动发现 |
| `-Test` | 构建后运行自动化测试 |
| `-Run` | 构建后启动 Muxora |
| `-Package` | 收集 Muxora 所需运行文件到本地部署目录 |

需要单独试用 Release 部署目录时：

```powershell
pwsh ./tools/build.ps1 -Configuration Release -Test -Package
./out/package/muxora-preview/muxora.exe
```

应用构建目标为 `muxora`。部署目录中的 Qt 运行文件保留其许可证；该目录是本机试用产物，正式分发仍需完成依赖清单与许可证检查。

完整验证入口：

```powershell
pwsh ./tools/verify.ps1
```

该脚本运行 Debug / Release 自动化测试，单独配置窗口模块并运行其测试，安装模块后构建外部宿主夹具，最后在移除 SDK 和编译器目录的 PATH 中检查部署应用启动。入口存在与验证通过分别记录，下文列出实际取得的结果。

完整窗口系统设计见 [窗口模块](../design/window-system-design.md)、[工作区](../design/workspace-design.md) 和 [浮动窗口](../design/floating-window-design.md)。本次以能交互的基础版本为起点，继续根据试用结果修订。

## 当前可试用的功能

| 范围 | 当前实现 |
| --- | --- |
| 模块 API | `WindowManager` 提供窗口、内容接入、布局与命令接口；`LayoutModel` 独立维护布局及不变量 |
| 标签与分割 | 中央区域可拆成多个标签组；每个标签内部可继续拆分窗格，分别导航与调整大小 |
| 四边停靠 | 内容可移到上下左右的自动隐藏边栏，点击展开，也可固定占用空间 |
| 两种窗口 | 默认拖出轻量浮窗，支持升级为完整窗口、降级回轻量窗口与逻辑窗口族 |
| 跨窗口移动 | 单标签拖拽、轻量标题栏整窗投放、合并或方向分割、返回所属窗口与目标唤起 |
| 命令与键盘 | 命令面板、窗口选择、标签组与窗格导航；图形编辑器可为已注册公开命令绑定或修改快捷键 |
| 图形行为设置 | 拖出窗口形态、完整窗口移空后的策略、拖拽唤起目标开关、悬停延迟 |
| 保存恢复 | 带版本的严格 JSON 布局，宿主通过 `QSaveFile` 原子写入；行为设置随布局保存 |

窗口设置与快捷键编辑分别可从菜单或命令面板打开。快捷键修改立即生效，当前只保留在本次运行内，重启后恢复默认绑定。

## 快捷键与试用步骤

| 操作 | 默认快捷键 |
| --- | --- |
| 命令面板 | `Ctrl+Shift+P` |
| 新建标签 / 关闭标签 | `Ctrl+T` / `Ctrl+W` |
| 上一个 / 下一个标签 | `Ctrl+PgUp` / `Ctrl+PgDown` |
| 标签组上下左右导航 | `Ctrl+Alt+方向键` |
| 窗格上下左右导航 | `Alt+方向键` |
| 向右 / 向下拆分窗格 | `Ctrl+Shift+E` / `Ctrl+Shift+O` |
| 调整标签组 / 窗格边界 | `Ctrl+Alt+Shift+方向键` / `Alt+Shift+方向键` |
| 当前标签移到轻量浮窗 | `Ctrl+Shift+D` |
| 选择窗口 / 返回所属完整窗口 | `Ctrl+Alt+W` / `Ctrl+Alt+Home` |
| 切换族内窗口 | `Ctrl+Alt+PgUp` / `Ctrl+Alt+PgDown` |
| 切换全部窗口 | `Ctrl+Alt+Shift+PgUp` / `Ctrl+Alt+Shift+PgDown` |
| 窗口行为设置 | `Ctrl+,` |
| 编辑快捷键 | 依次按 `Ctrl+K`、`Ctrl+S` |
| 保存布局 | `Ctrl+S` |

先把一个标签拖到另一个标签组的中心或方向指示上，比较合并与分组；再把标签拖到窗口外，试用轻量窗口与标题栏整窗回并。通过命令面板操作窗格、边栏和窗口转换，最后编辑快捷键或修改窗口行为。已注册命令的菜单和键盘入口使用同一执行接口。

布局状态默认位于 `QStandardPaths::StateLocation` 下的 `window-preview/window-layout.json`。程序自动合并短时间内的布局变化后保存；占位编辑器中的文字不会保存。通过命令面板选择“恢复初始工作区布局”，或启动 `muxora.exe --fresh`，可从初始布局重新试用；`--state-dir <path>` 可指定独立状态目录。

## 已取得的验证记录

本次最终代码通过 `tools/verify.ps1` 完整检查，取得以下结果：

| 检查 | 结果与证据 |
| --- | --- |
| Debug / Release | 模型、控件、拖拽交互三组 CTest 均通过，失败数为 0；报告为 `out/test-results/dev-debug.xml` 与 `dev-release.xml` |
| 模块独立构建 | 单独配置 `modules/window-system/`，三组 CTest 均通过；报告为 `out/test-results/standalone.xml` |
| 安装后的 API 复用 | 外部宿主夹具仅通过安装导出的 CMake 包引用模块，构建及运行契约测试通过；报告为 `out/test-results/external-host.xml` |
| 部署启动 | 从 PATH 移除 SDK 与编译器后，部署应用启动检查退出码为 0，并成功写入带空格的状态目录；记录为 `out/test-results/package-smoke.json` |

拖拽测试在 Windows 桌面上向 Qt 控件注入输入事件，同时检查 Windows 原生命中和独立进程的透明、非透明遮挡窗口。它验证事件排队、跨窗口投放和遮挡回归，不等同于全部系统输入、混合 DPI 或 Snap 行为的实机验收。

关闭确认期间增加标签、窗格或族内窗口，递归关闭请求，以及无效标签命令参数都有对应回归用例；通过后保留这些用例。部署启动记录与 `build-info.json` 记录同一个可执行文件的 SHA-256，结果只适用于此次本机部署目录。

测试采用 Qt Test / CTest。完整质量要求仍按 [测试规范](testing-design.md) 执行，失败时修复实现并保留回归用例。

## 待完成与验证范围

- 完整 VS Code 风格 JSONC 设置、用户注释保留、用户快捷键文件持久化、完整 `when` 条件以及多层规则和续键优先级仍待实现。当前快捷键编辑器与运行时续键能力不能等同于完整设计契约。
- 窗框已接入边缘缩放、最大化及 Snap 相关处理；系统 Snap 菜单、混合 DPI、多屏变更与其他原生窗口行为仍需逐项实机验收。
- ASan、长期资源压力、完整分发许可清单及 macOS / Linux 尚未完成验收。
- SSH、SFTP、Ghostty、X11 和终端 Normal / Visual 模式未接入，当前标签内容为占位控件。

构建、自动化测试与桌面试用分开记录。没有在本机复现的功能不得写为已通过；长期资源、完整安装包和其他系统仍须完成各自验收。

## 试用时记录

优先观察标签拖出、合并、分组、调整大小、窗格焦点、边缘停靠和多个窗口之间的移动。报告问题时附操作顺序、预期落点、实际落点、窗口形态、屏幕缩放及是否存在被遮挡的目标窗口。

完整要求与当前实现不一致时，记录缺口并继续完善代码，不能通过删设计或放宽测试标准使其看起来完成。
