# 本地终端设计

状态：设计稿，2026-10-19。实现步骤见 [阶段 7.3](../roadmap/implementation/phase-7-terminal-and-sessions.md#73-本地终端conpty)。

Muxora 除了 SSH 会话，还要像 Windows Terminal 一样打开本机的 shell：Windows PowerShell、PowerShell 7、命令提示符（cmd）、WSL 各发行版、Git Bash，以及 Visual Studio 开发者命令行等。这些都**不是 SSH 连接**：它们是本机进程，通过 Windows 伪控制台（ConPTY）与终端交换字节。WSL 也是这样：`wsl.exe` 在本机通过 ConPTY 运行，由 WSL 自己把输入输出转到 Linux 发行版里，不经过 SSH。

终端核心（解析、屏幕、历史、Normal／Visual 模式）、显示、快捷键、标签与窗口行为与 SSH 终端完全相同；不同的只是“字节从哪里来”。

## 1. 概念

| 概念 | 说明 |
| --- | --- |
| 本地终端配置（profile） | 启动一个本地 shell 所需的信息：命令行、起始目录、环境变量、图标、标签标题，以及终端外观（字体、配色等，与 SSH 会话共用同一组终端设置）。对应 Windows Terminal 的 profile。 |
| 配置来源 | 配置从哪里来：应用自建、自动检测、Windows Terminal 设置文件。不同来源的可编辑性不同（§3）。 |
| 本地进程传输 | 用 ConPTY 启动进程并收发字节的传输层，地位与 SSH 通道相同。 |
| 默认配置 | 新建终端时默认使用的配置。设置项 `terminal.defaultProfile`。 |

## 2. 支持的 shell

| shell | 检测方式 | 默认命令行 | 说明 |
| --- | --- | --- | --- |
| Windows PowerShell | 固定位置 `%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe` | `powershell.exe -NoLogo` | 系统自带 5.1 |
| PowerShell 7+ | `%ProgramFiles%\PowerShell\<版本>\pwsh.exe`；Microsoft Store／winget 安装的应用执行别名；`PATH` 中的 `pwsh.exe`；按版本号只保留最高的稳定版和预览版各一个 | `pwsh.exe -NoLogo` | 检测范围参照 Windows Terminal |
| 命令提示符 | 固定位置 `%SystemRoot%\System32\cmd.exe` | `cmd.exe` | |
| WSL 发行版 | 注册表 `HKCU\Software\Microsoft\Windows\CurrentVersion\Lxss\{GUID}` 下的 `DistributionName`（不运行 `wsl.exe -l`，避免在检测时唤醒 WSL 虚拟机）；跳过 `docker-desktop`、`docker-desktop-data` | `wsl.exe -d <发行版> --cd ~` | 每个发行版一个配置；默认发行版排在前面 |
| Git Bash | 注册表 `HKLM\SOFTWARE\GitForWindows` 或 `HKCU\SOFTWARE\GitForWindows` 的 `InstallPath`；回退 `%ProgramFiles%\Git` | `<安装目录>\bin\bash.exe -i -l` | 必须用 `bin\bash.exe`，不能用 `git-bash.exe`：后者是 mintty 启动器，会另开一个自己的窗口 |
| Visual Studio 开发者命令行 | `vswhere.exe -products * -format json`（位于 `%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\`） | cmd：`cmd.exe /k "<安装目录>\Common7\Tools\VsDevCmd.bat"`；PowerShell：加载 `Microsoft.VisualStudio.DevShell.dll` 后 `Enter-VsDevShell <实例 ID>` | 每个 VS 安装各两个配置；后续阶段 |
| MSYS2、Cygwin | 常见安装目录 | 各自的 `bash.exe --login -i` | 后续阶段 |

检测只读文件系统和注册表，不启动任何 shell。检测在后台线程进行，启动时不阻塞界面；检测结果随软件安装变化而变化，在打开“新建终端”菜单或会话树展开“本地”分组时刷新（有 5 秒的结果缓存）。

## 3. 配置来源与可编辑性

沿用 SSH config 的原则（[V0 设计 §5](v0-design.md#5-会话管理与-ssh-config)）：**来自外部的配置只读使用，Muxora 不修改外部文件，也不用自己的设置覆盖外部定义的连接参数。**

| 来源 | 内容 | 可编辑性 |
| --- | --- | --- |
| 应用自建 | 用户在 Muxora 中新建的配置，或从自动检测的配置“复制为新配置”得到 | 全部可编辑，保存在 `User/terminals/<稳定 ID>.json`（与自建 SSH 会话一会话一文件的做法相同） |
| 自动检测 | §2 中检测到的 shell | 命令行、起始目录由检测决定，不可编辑；可以隐藏，可以按配置 ID 附加界面设置（显示名、分组、图标、字体、配色、标签颜色）。附加设置保存在 `User/terminal-profile-overrides.json`，身份为“生成器 ID + 稳定键”（例如 `wsl` + 发行版 GUID） |
| Windows Terminal 设置 | `%LOCALAPPDATA%\Packages\Microsoft.WindowsTerminal_8wekyb3d8bbwe\LocalState\settings.json`（以及预览版、非商店安装的对应路径）中的 profile | 与 SSH config 相同：动态读取、监听变化、只读；不写回 Windows Terminal 的文件；按 profile 的 `guid` 附加界面设置。默认关闭，设置项 `terminal.windowsTerminalProfiles.enabled` 开启 |

与 Windows Terminal 重复的配置（例如两者都检测到同一个 WSL 发行版）按命令行去重，只显示一个，来源显示在详情中。

自动检测或外部来源的配置消失（软件卸载、发行版注销、WT 中删除）时，处理规则与 SSH 别名删除（V0 设计 §5.4）相同：从会话树移除，不保留可启动的旧入口；已经打开的终端继续运行；附加设置随之失效，重新出现同一稳定键时按新条目处理。

## 4. 启动与进程生命周期

1. **启动**：用 `CreatePseudoConsole` 创建伪控制台，`CreateProcessW` 配合 `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE` 启动进程，并把进程放进一个 Job 对象（关闭标签时能结束整棵进程树）。环境变量：继承 Muxora 的环境，再应用配置中的增减；设置 `TERM_PROGRAM=Muxora`、`COLORTERM=truecolor`（对 WSL 通过 `WSLENV` 传入）。
2. **尺寸**：终端尺寸变化时调用 `ResizePseudoConsole`，与显示的列行数一致。
3. **编码**：ConPTY 输入输出为 UTF-8。
4. **进程退出**：设置项 `terminal.closeOnExit`，取值与 Windows Terminal 一致：`graceful`（退出码为 0 时关闭标签，默认）、`always`、`never`。不关闭时在终端末尾显示“进程已退出，代码 n”，按 `Enter` 用同一配置重新启动，标签和窗格位置不变。
5. **关闭标签**：仍有子进程在运行时（Job 中除 shell 外还有进程），通过窗口系统的关闭许可询问用户；确认后 `ClosePseudoConsole` 并结束 Job。设置项 `terminal.confirmOnKill` 可关闭询问。
6. **跨窗口移动**：只转移显示，不重启进程（与 SSH 会话相同的要求）。

### 4.1 管理员权限

普通权限的 Muxora 无法驱动以管理员身份运行的进程的伪控制台（Windows 的完整性级别隔离）。配置可以设置 `elevate: true`：此时打开一个以管理员身份运行的新 Muxora 进程来承载该终端，这个进程的窗口不能与普通窗口合并或互相拖动标签（两个进程之间不共享窗口系统）。这与 Windows Terminal 的行为一致，在界面上明确提示。属于后续阶段。

### 4.2 ConPTY 版本

默认使用系统自带的 ConPTY。Windows Terminal 随包带有更新的 OpenConsole（MIT 许可，`microsoft/terminal`），终端序列的透传更完整。是否随 Muxora 分发 OpenConsole，在阶段 7.3 用同一组测试（Unicode 宽字符、鼠标、颜色、`vim`／`htop` 在 WSL 中的表现）对比后决定，结论写入 ADR。

## 5. 当前目录与文件面板

本地终端与 SSH 终端一样支持文件侧栏跟随（[V0 设计 §10](v0-design.md#10-终端目录跟随)），但端点是本机文件系统，不经过 SFTP：

| shell | 目录报告 | 路径换算 |
| --- | --- | --- |
| PowerShell | 提示函数输出 OSC 9;9（Windows Terminal 约定）或 OSC 7 | 直接是 Windows 路径 |
| cmd | `PROMPT` 中加入 `$e]9;9;$P$e\` | 直接是 Windows 路径 |
| WSL | shell（bash／zsh／fish）输出 OSC 7 | `/home/u` → `\\wsl.localhost\<发行版>\home\u`（Windows 10 旧版用 `\\wsl$\<发行版>\...`）；`/mnt/c/...` → `C:\...` |
| Git Bash | bash 输出 OSC 7 | MSYS 路径：`/c/Users/u` → `C:\Users\u`；其他绝对路径相对 Git 安装目录换算 |

与 V0 设计相同，**不偷偷修改用户的 shell 配置**。没有目录报告时文件面板显示“目录跟随未启用”，并提供“配置 shell 集成…”操作：列出将要写入的文件和内容（PowerShell 的 `$PROFILE`、cmd 的 `AutoRun` 或一个 Muxora 专用批处理、bash 的 `~/.bashrc`），用户确认后才写入，并可以撤销。同一套集成脚本也输出 OSC 133，用于命令完成通知和提示符跳转。

## 6. 界面入口

- 标签栏“+”按钮旁的下拉菜单列出全部本地配置和最近使用的 SSH 会话，与 Windows Terminal 的新建标签菜单相同；“+”本身打开默认配置。
- 命令：`terminal.new`（打开默认配置；它是窗口系统“新建标签”的默认命令，所以 `Ctrl+T` 和阶段 3.5 为终端内容新增的 `Ctrl+Shift+T` 都打开默认配置）、`terminal.newWithProfile`（快速输入选择配置）、`terminal.openProfile.1` … `.9`（`Ctrl+Shift+1` … `9`，按新建菜单中的顺序，与 Windows Terminal 一致）。
- 会话树中有“本地”分组，按来源和类型排列；右键：在新标签／新窗口中打开、设为默认、复制为新配置、隐藏、在资源管理器中打开起始目录。
- 设置：`terminal.defaultProfile`、`terminal.closeOnExit`、`terminal.confirmOnKill`、`terminal.windowsTerminalProfiles.enabled`，以及每个配置的设置页（应用自建配置可编辑全部字段；其他来源只显示只读的命令行，可编辑附加的界面设置）。
- 标签标题：默认显示配置名；是否允许 shell 通过 OSC 0／2 修改标题，与 SSH 终端共用设置。

## 7. 规模与性能

本地终端也要支持同时打开数百个：

- 每个终端的 ConPTY 输出读取不各占一个线程。ConPTY 要求用管道连接，`CreatePipe` 创建的匿名管道不支持重叠 I/O，因此改为自己创建一对命名管道（`CreateNamedPipeW` 加 `FILE_FLAG_OVERLAPPED`，客户端一端交给 ConPTY），用一个 I/O 完成端口加少量工作线程处理全部终端的读写。
- 输出进入终端核心后按帧合并绘制；不可见的终端不生成渲染数据（[性能方案 §5](../roadmap/performance-plan.md#5-gpu-渲染与终端视图契约)）。
- 输入写入使用有界队列；进程不读输入时施加背压，不无限缓存。
