# 阶段 7：终端与会话接入

**目的**：在窗口系统上接入真实业务：终端核心与显示、本地终端、SSH 会话、SFTP 文件面板、X11，以及命令行启动请求。技术选型以 [V0 设计](../../design/v0-design.md) 第 2、6、7、9–11、17 节为准；对标清单见 [业务功能对标](../feature-parity-plan.md)。

本阶段除 7.3（本地终端）和 7.4（会话目录与 SSH config）已经展开到子步骤外，其余步骤比前几个阶段粗一级：每一步都会产出新的模块或子系统，开始前要先把该步骤展开成与阶段 1 同样粒度的子步骤文档（`phase-7.<n>-<名称>.md`），经评审后再实现。下面给出每一步的边界、接口草案、验证方式和完成标准，作为展开的依据。

**阶段约束**：

- 遵守 [工程设计](../../development/engineering-design.md) §2–§4：协议与解析不占 GUI 线程；所有队列、缓存、历史有上限；异步结果携带实例 ID 和代次；跨窗口移动只转移归属，不重建连接。
- 每个第三方库通过一个适配层接入，第三方类型不出现在适配层之外的头文件中。
- 未完成真实协议验证的功能，不在预览说明中写成“支持”。
- 前置：阶段 1.5–1.7（内容不再因无关操作改挂、可见性通知）、阶段 3.1–3.5（上下文键、内容优先按键）。阶段 2 和 4 不是硬性前置，但终端配色和终端设置项依赖它们。

---

## 7.1 终端核心适配（libghostty-vt）

**产出**：`third_party/ghostty/`（获取与构建）、`modules/terminal-core/`（适配层，只依赖 Qt Core）。

**获取与构建**：

- 按 V0 设计固定 Ghostty 上游提交和 Zig 版本；`FetchContent` 下载源码，带 `URL_HASH`（与 `third_party/lua` 的做法一致）。Zig 是开发工具，构建脚本检测其版本，不匹配时明确报错。
- 先走 DLL 路线（V0 设计 §6）；DLL 与许可证随部署目录分发，`deployment-files.json` 和 `THIRD_PARTY_NOTICES.md` 同步更新。

**适配层接口草案**：

```cpp
namespace terminal_core {
struct Size { int columns; int rows; int cellWidthPx; int cellHeightPx; };
struct DirtyRows { QVector<int> rows; bool full = false; };
class TerminalCore {             // 单线程使用；由所属会话的工作线程调用
public:
    explicit TerminalCore(const TerminalLimits& limits);   // 历史行数、字节上限
    void feed(QByteArrayView bytes);                        // 远端输出
    QByteArray encodeKey(const KeyInput& key);             // 本地按键 → 发给远端的字节
    QByteArray encodePaste(QStringView text, bool bracketed);
    void resize(Size size);
    DirtyRows takeDirtyRows();
    RenderSnapshot snapshot(const DirtyRows& rows) const;   // 只复制可见区域的脏行
    // 回调：标题（OSC 0/2）、当前目录（OSC 7）、铃声、剪贴板请求（OSC 52，需用户策略）、通知（OSC 9/777）、提示符标记（OSC 133）
};
}
```

`RenderSnapshot` 是值类型，只包含可见行的单元格（码点或字形簇、属性、颜色），可以跨线程传递；不持有 Ghostty 内部指针（工程设计 §3.1 的要求）。

**验证**：适配层单元测试使用固定字节序列（光标移动、SGR 颜色、宽字符与组合字符、备用屏幕、滚动区域、OSC 7／8／133）断言快照内容；历史上限测试（灌入超过上限的行，内存回落）；`vttest` 的可自动化部分作为参考。

**完成标准**：Windows Release 构建与部署通过；适配层测试通过；DLL 加载失败时应用能给出明确错误而不是崩溃。

---

## 7.2 终端显示模块与渲染选型

**产出**：`modules/terminal-view/`（依赖 Qt Widgets、`terminal-core`；不依赖 SSH），以及一个基准程序。

**选型方法**：V0 设计写的是“Qt 字体与绘制组件”。两种 Qt 方案都满足这一点，用测量决定：

- **方案 P**：`QWidget` + `QPainter`，按行缓存 `QGlyphRun`／`QTextLayout` 结果，只重绘脏行区域。
- **方案 R**：`QRhiWidget`，字形图集纹理 + 实例化四边形；每个顶层窗口一个图集（纹理不能跨 `QRhi` 共享），按字节上限淘汰。

两者实现同一个 `TerminalRenderer` 接口，基准程序（不依赖网络）用本地生成器以最大速度灌入：大量普通文本、带颜色文本、全屏刷新（类似 `cmatrix`）、长行换行、Unicode 混排，测量吞吐（MB/s）、帧时间 p95、CPU、GPU 内存、私有内存，以及 50 个终端同时可见时的帧时间。参照 vtebench 的场景设计负载。结果和决定写入 ADR；默认采用胜出的方案，另一方案若无维护价值则删除。

**必须具备的显示能力**：字形回退链与宽字符；可选连字（默认关闭）；光标样式；选区；超链接下划线；按 DPR 渲染（每个屏幕各自的 DPR，不按主屏处理）；不可见时（1.7 可见性通知）释放渲染资源。

**抗锯齿方式**（2026-10-20 加入，参照 Windows Terminal 的 `antialiasingMode`）：设置项 `terminal.antialiasingMode`，取值 `grayscale`（默认，与 Windows Terminal 一致）、`cleartype`、`aliased`，可按会话或配置覆盖，切换后立即重绘，不重建连接。两种渲染方案都要从设计上支持三种方式：

- 对照：Xshell 的“字体质量”列出 GDI 的七个 `lfQuality` 值（Default、Draft、Proof、NonAntiAliased、AntiAliased、ClearType、Natural ClearType）。其中 Default 跟随系统；Draft、Proof 只影响旧式点阵字体，对 TrueType 字体与 Default 相同；其余四个归并为这里的三种方式。ClearType 与 Natural ClearType 的区别是字宽：前者按整像素对齐字宽（与老程序兼容），后者保留字体设计的字宽。这里的 `cleartype` 用 DirectWrite 的自然模式（`DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC`），相当于 Natural ClearType；终端是等宽网格，不需要另设整像素字宽的 GDI 兼容模式。
- 字形光栅化用 DirectWrite（`IDWriteGlyphRunAnalysis`：灰度用 `DWRITE_TEXTURE_ALIASED_1x1` 配合抗锯齿渲染模式；ClearType 用 `DWRITE_TEXTURE_CLEARTYPE_3x1`），遵守系统的 ClearType 伽马、对比度和像素排列（RGB／BGR）设置，不自己猜。
- 方案 R 的字形图集要能存放三通道覆盖率，ClearType 用双源混合（dual-source blending）或在已知不透明背景色上合成；方案 P 用 `QPainter` 时要确认 Qt 在目标上确实输出子像素结果。
- 背景透明或亚克力（7.9）时 ClearType 无法正确合成，自动退回灰度，与 Windows Terminal 的行为一致，并在设置说明中写明。
- 基准程序对三种方式分别测吞吐和帧时间；像素测试分别检查三种方式的输出（灰度无彩色像素、ClearType 有子像素彩边、无抗锯齿只有前景和背景两种颜色）。

**完成标准**：基准结果与 ADR；在 1.5 倍、2 倍缩放下像素对齐测试通过；三种抗锯齿方式都能切换并通过各自的像素检查。

---

## 7.3 本地终端（ConPTY）

设计见 [本地终端设计](../../design/local-terminal-design.md)。支持 Windows PowerShell、PowerShell 7、cmd、各 WSL 发行版、Git Bash，后续加入 VS 开发者命令行、MSYS2、Cygwin。WSL 通过 `wsl.exe` 在 ConPTY 中运行，**不经过 SSH**。

**为什么先做本地终端**：不依赖网络和服务器，就能在真实应用中验证终端显示、输入、选区、复制粘贴、标题、链接、搜索，以及窗口系统的按键优先级、可见性通知、跨窗口移动不重建会话。SSH 终端（7.6）复用这里的终端内容控件，只替换传输层。

**前置**：7.1、7.2；阶段 1.5–1.7、3.1–3.5。

### 7.3a 传输接口与 ConPTY 传输

**产出**：`src/terminal/terminal_transport.hpp`（SSH 与本地共用的接口）、`src/platform/windows/conpty_transport.{hpp,cpp}`、`src/platform/windows/io_completion_pool.{hpp,cpp}`。

**接口**：

```cpp
class TerminalTransport {          // 线程安全约定见注释：write/resize/close 可在 GUI 线程调用
public:
    virtual ~TerminalTransport() = default;
    virtual bool write(QByteArray bytes) = 0;          // 有界队列；满时返回 false 并发出 backpressure 信号
    virtual void resize(terminal_core::Size size) = 0;
    virtual void close(CloseMode mode) = 0;            // Graceful：关闭输入并等待；Kill：结束进程树
    // 回调在 I/O 线程触发，接收方负责转到会话的工作线程
    std::function<void(QByteArray)> received;
    std::function<void(TransportExit)> exited;         // 退出码或连接关闭原因
};
```

**ConPTY 实现要点**：

1. 管道：用 `CreateNamedPipeW`（名称含进程 ID 与随机数，`PIPE_ACCESS_INBOUND/OUTBOUND | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE`，`PIPE_REJECT_REMOTE_CLIENTS`）创建服务端，`CreateFileW` 打开客户端一端交给 `CreatePseudoConsole`。不使用 `CreatePipe`：匿名管道不支持重叠 I/O，数百个终端会需要数百个阻塞读线程。
2. 进程：`STARTUPINFOEXW` + `PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE`；进程以挂起方式创建，先加入 Job 对象（`JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`）再恢复，保证关闭时能结束整棵进程树。
3. I/O：`IoCompletionPool` 一个完成端口，工作线程数 `min(4, 逻辑核数)`；每个传输各有一个读缓冲（64 KiB）常驻投递读请求；写入使用有界队列（默认 1 MiB），超限时发出背压信号，由终端内容停止接受粘贴等大块输入。
4. 退出检测：用 `RegisterWaitForSingleObject` 等待进程句柄，在线程池回调中读取退出码并触发 `exited`；随后排空管道剩余输出再关闭。
5. 关闭顺序：`Graceful` 关闭写管道 → 等待进程退出（上限 2 秒）→ 超时则按 `Kill` 处理；`Kill` 结束 Job → `ClosePseudoConsole` → 关闭管道 → 取消未完成的 I/O 并等待其完成回调返回（完成回调持有传输的弱引用，传输销毁后的迟到回调直接丢弃）。
6. 所有句柄用 RAII 包装（`UniqueHandle`、`UniquePseudoConsole`），不可复制。

**测试**（`src/app/tests/conpty_transport_test.cpp`；不需要真实桌面，归入 model 层，只在 Windows 构建）：

- 用测试辅助程序（仿照现有 `muxora-script-child`）：回显输入；按命令打印 `GetConsoleScreenBufferInfo` 得到的尺寸；按命令启动孙进程后自身退出或挂起。
- 断言：写入的字节被回显；`resize(100×40)` 后辅助程序报告 100×40；退出码正确传出；`Kill` 后孙进程也被结束（用进程 ID 检查）；关闭后迟到回调不访问已销毁对象（在 ASan 诊断构建中再跑一次，阶段 6.4）。
- 规模：同时启动 200 个回显会话，进程中线程数不超过“基线 + 完成端口线程数 + 常量”；全部关闭后句柄数回到基线（`GetProcessHandleCount`）。

### 7.3b 本地终端配置模型与自动检测

**产出**：`src/terminal/profiles/`：`terminal_profile.{hpp,cpp}`（模型与 JSON）、`profile_catalog.{hpp,cpp}`（汇总、去重、附加设置）、生成器 `powershell_generator`、`cmd_generator`、`wsl_generator`、`git_bash_generator`、`windows_terminal_source`；`system_probe.{hpp,cpp}`（注册表与文件系统访问的抽象）。

**实现**：

1. 模型：

   ```cpp
   struct TerminalProfile {
       QString id;                // 来源 ID + ":" + 稳定键，例如 "wsl:{发行版 GUID}"
       QString source;            // "user" / "generator:wsl" / "windowsTerminal"
       QString name; QString commandLine; QString startingDirectory; QString icon;
       QHash<QString, QString> environment; bool hidden = false;
       bool editable = false;     // 只有 source == "user" 时为真
   };
   ```

2. `SystemProbe` 接口提供读注册表值、枚举子键、判断文件存在、读取文件版本；生产实现调用 Win32 API，测试注入假实现。检测逻辑只依赖这个接口，因此可以在没有安装 WSL、Git 的机器上完整测试。
3. 各生成器按设计 §2 的规则检测。WSL 读取注册表，不运行 `wsl.exe`；Git Bash 必须指向 `bin\bash.exe`。
4. Windows Terminal 来源：读取设计 §3 中的路径（用阶段 3.6 的 JSONC 模块解析），只取 `profiles.list` 中未隐藏的项；监听文件变化（与 SSH config 相同的父目录监听与去抖动）；**从不写入该文件**。
5. 去重：按规范化后的命令行（小写、去掉多余空格和引号、展开环境变量）比较；保留优先级“应用自建 > Windows Terminal > 自动检测”的那一个，详情中列出所有来源。
6. 附加设置：`User/terminal-profile-overrides.json`，键为 `profile.id`，值为显示名、分组、图标、隐藏、终端外观；配置消失时按设计 §3 删除对应附加设置（原子提交）。
7. 自建配置：`User/terminals/<稳定 ID>.json`；“复制为新配置”从任意来源生成一个可编辑的副本。

**测试**：每个生成器用假 `SystemProbe` 覆盖：没有安装、单个、多个版本（PowerShell 7 多版本只保留最高稳定版与预览版各一个）、WSL 中的 docker 发行版被跳过、Git 只在 `HKCU` 注册、路径带空格；Windows Terminal 来源的解析、隐藏项、文件变化、文件损坏（保留上次有效结果并报告）；去重优先级；附加设置随配置消失而删除；**只读保证**：对 Windows Terminal 设置文件执行全部操作（读取、刷新、隐藏、附加设置、删除附加设置）后，文件字节与修改时间不变。

### 7.3c 本地终端内容与生命周期

**产出**：`src/terminal/terminal_content.{hpp,cpp}`（SSH 与本地共用的工作区内容控件）、`src/terminal/local_terminal_session.{hpp,cpp}`。

**实现**：

1. 内容控件：持有 `TerminalView`（7.2）和会话指针；设置 `windowSystemCapturesKeys = true`、上下文键 `terminalFocus`、`terminalMode`；内容类型 `terminal.local`。
2. PuTTY 风格操作：选中即复制（`terminal.copyOnSelect`）、右键行为（`terminal.rightClickBehavior`：`paste`／`menu`）、中键粘贴、多行粘贴确认（`terminal.confirmMultilinePaste`）、括号粘贴。
3. 退出行为：`terminal.closeOnExit`（`graceful`／`always`／`never`）；不关闭时显示退出提示，`Enter` 用同一配置重启，标签和窗格位置不变、内容 ID 不变。
4. 关闭许可：在应用的 `CloseGuard` 中合并判断：Job 中仍有 shell 以外的进程时询问（统一对话框，8.6）；`terminal.confirmOnKill` 为假时不询问。
5. 跨窗口移动不重启进程：测试中记录进程 ID，移动后不变。
6. 标签标题：`terminal.tabTitle` 模板，默认 `${profile}`；是否允许 OSC 0／2 修改标题由 `terminal.allowTitleChange` 决定。

**测试**（应用级）：驱动真实 `cmd.exe` 和 `powershell.exe`（两者在所有 Windows 上都存在）：输入命令读到输出；调整窗格大小后 shell 看到的尺寸正确；复制粘贴；`exit 0` 关闭、`exit 1` 保留并可重启；拖到另一窗口后进程 ID 不变；仍有子进程时关闭会询问。WSL 和 Git Bash 在测试机已安装时运行对应用例，未安装时该行报告“环境不具备”，不能写成通过。

### 7.3d 当前目录与路径换算

**产出**：`src/terminal/cwd_mapping.{hpp,cpp}`（纯函数）、`src/terminal/shell_integration/`（各 shell 的集成脚本与写入器）。

**实现**：

1. 终端核心回调中识别 OSC 7（`file://host/path`）与 OSC 9;9（`"C:\path"`）；目录报告绑定到具体终端实例（V0 设计 §10.2）。
2. `cwd_mapping`：WSL `/home/u` → `\\wsl.localhost\<发行版>\home\u`（检测到旧版 WSL 时用 `\\wsl$\`），`/mnt/<盘符>/...` → `<盘符>:\...`；MSYS `/c/...` → `C:\...`，其他绝对路径相对 Git 安装目录；Windows 路径原样。主机名与本机不符的 OSC 7 不采用（可能来自嵌套的 SSH）。
3. 文件面板（7.7）对本地终端使用本机文件系统端点；7.7 完成前只记录目录，在状态栏显示。
4. Shell 集成：命令 `terminal.configureShellIntegration`。打开一个快速输入列出目标 shell，选择后显示将写入的文件路径和完整内容预览（统一对话框），确认后写入并在文件中用标记注释包住，便于撤销；命令 `terminal.removeShellIntegration` 按标记删除。脚本同时输出 OSC 7／9;9 与 OSC 133。PowerShell 写 `$PROFILE`；cmd 不改注册表 `AutoRun`，而是在配置的命令行中加 `/k <Muxora 集成批处理>`（只影响 Muxora 启动的 cmd）；bash／zsh 写 `~/.bashrc`／`~/.zshrc`（WSL 中写发行版内的文件，Git Bash 写 `%USERPROFILE%\.bashrc`）。

**测试**：`cwd_mapping` 数据驱动（各种路径、空格、Unicode、非法输入）；OSC 解析；集成写入与撤销后文件恢复原样（逐字节比较）；未确认时不写任何文件。

### 7.3e 界面入口

**实现**：

1. 标签栏“+”旁的下拉（窗口系统已有 `setTabCreationCommands` 和新建标签菜单）列出：本地配置（按“检测到的 shell”“自建”“Windows Terminal”分组）、最近的 SSH 会话（7.4 之后）、“设置…”。`terminal.new` 设为默认新建命令。
2. 命令 `terminal.newWithProfile`（快速输入选择配置）、`terminal.openProfile.1` … `.9`（默认 `Ctrl+Shift+1` … `9`，按新建菜单顺序）。
3. 会话树“本地”分组（7.4 的会话树完成后）；右键菜单按设计 §6。
4. 设置项通过阶段 4 的注册表声明：`terminal.defaultProfile`、`terminal.closeOnExit`、`terminal.confirmOnKill`、`terminal.copyOnSelect`、`terminal.rightClickBehavior`、`terminal.confirmMultilinePaste`、`terminal.tabTitle`、`terminal.allowTitleChange`、`terminal.windowsTerminalProfiles.enabled`。
5. 每个配置的详情／设置页：自建配置可编辑全部字段；其他来源只读显示命令行与来源，只能编辑附加的界面设置。

**测试**：菜单分组与顺序；`Ctrl+Shift+1` 打开第一个配置；默认配置变化后“+”跟随；只读配置的设置页没有可编辑的命令行字段。

### 7.3f ConPTY 版本评估

对比系统 ConPTY 与 Windows Terminal 随包的 OpenConsole（MIT）：在 WSL 中运行 `vim`、`htop`、`tmux`，以及宽字符、组合字符、Emoji、鼠标、真彩色样例，记录差异。决定是否随 Muxora 分发 OpenConsole（需要同时更新部署清单和第三方声明），结论写入 ADR。

### 7.3g 管理员权限配置（后续）

`elevate: true` 的配置在一个以管理员身份运行的新 Muxora 进程中打开（设计 §4.1）。需要先设计两个进程之间的布局与设置共享方式，再拆分步骤。

---

## 7.4 会话目录与 SSH config

**要求来源**：[V0 设计 §5](../../design/v0-design.md#5-会话管理与-ssh-config) 与 §15，以及 [SSH config 修改与同步设计](../../design/ssh-config-sync-design.md)。核心是：**SSH config 不是导入，而是动态读取的来源；默认只读**。7.4a–7.4f 实现默认的只读行为；7.4h–7.4m 实现两个可选开关 `ssh.config.allowEditing`、`ssh.config.syncAppSessions`。以下默认规则在两个开关都关闭时成立：Muxora 不修改 `.ssh/config` 及其 `Include` 文件，不提供任何修改入口，也不用自己的设置覆盖其中的地址、用户、端口、密钥、代理或认证参数；只能按“配置源 ID + 原始别名”在 Muxora 自己的目录中附加界面设置。

**产出**：`src/sessions/`：`session_catalog`、`app_session_store`、`ssh_config_source`、`ssh_config_indexer`、`alias_overrides`、`session_tree_model`。

### 7.4a 应用自建会话存储

一会话一文件 `sessions/items/<稳定 ID>.json`，分组与顺序在 `sessions/catalog.json`（V0 设计 §15）。原子写入（`QSaveFile`）；外部修改检测；损坏文件单独报告，不影响其他会话。秘密只保存凭据管理器中的引用。

**测试**：读写、损坏文件隔离、并发修改检测、秘密不进入 JSON（扫描写出的文件内容）。

### 7.4b SSH config 结构索引

1. 读取根配置（用户主目录下的 `.ssh/config`，不硬编码盘符）及其 `Include` 文件；`Include` 的相对路径、通配展开和顺序按 OpenSSH 规则（用户配置中相对 `~/.ssh`），不是相对当前文件。
2. 只做结构索引，**不执行命令**：不运行 `Match exec`，不运行 `ssh -G`，不做 DNS 解析。
3. 规则：`Host dev build` 产生两个入口；`Host *`、`Host *.example.com`、否定模式不产生入口；同一别名出现在多个块中只生成一个入口，参数由后端在连接时求值，索引不自己合并。
4. 入口身份：“配置源 ID + 原始别名”，不用解析后的地址。

**测试**（固定样例放在 `tests/fixtures/ssh-config/`）：多别名、通配、否定、重复块、嵌套 `Include`、通配 `Include`、相对路径、`Match` 块（不展开、不执行——用一个 `Match exec` 指向会留下痕迹的命令，断言痕迹不存在）、CRLF 与 UTF-8 BOM。

### 7.4c 监听与刷新

监听根配置、已发现的 `Include` 文件及其父目录（Qt 在文件被重命名或删除后停止监听，需要监听父目录并重新登记）；事件去抖动后重建完整索引，一次性更新界面；读取期间文件变化时有界重试；读取失败显示来源错误，不用旧参数建立新连接。

**测试**：原子替换保存、删除后重建、新增 `Include`、移除 `Include`、目录重命名、刷新期间连续修改。

### 7.4d 别名附加设置与删除语义

1. 附加设置保存在 `ssh-alias-overrides.json`：显示名、分组、收藏、排序、标签颜色、字体、终端配色、快捷键方案、文件浏览器偏好；不包含任何连接参数。
2. 别名在一次完整有效的刷新中消失：从会话树移除，不显示“已删除”分组，不保留可连接入口；同次更新中原子删除附加设置；已打开的终端继续工作但不作为配置会话恢复；之后同名别名重新出现时作为新条目。
3. 工作区恢复时，布局中引用已删除别名的标签不会重新生成配置会话（V0 设计 §15 最后一段）。

**测试**：删除后附加设置消失；同名重建为新条目；打开中的终端不受影响；布局恢复不复活已删除别名。

### 7.4e 只读保证

1. 代码层面：`ssh_config_source` 只以只读方式打开文件；会话详情页只读显示来源文件、别名和有效连接信息（由后端求值得到，只在用户打开详情或连接时求值）。
2. 界面层面：SSH config 会话的设置页中没有地址、用户、端口、密钥、代理、认证字段的编辑控件；“复制为应用会话”可以生成一个可编辑的自建会话副本（与原文件从此无关）。
3. **自动化检查**：在两个开关都关闭时，对样例配置目录执行全部会话操作（浏览、搜索、附加设置、隐藏、删除附加设置、连接尝试、刷新）后，比较每个文件的字节和修改时间，必须完全不变。打开开关后，这项检查改为：只有用户发起的修改涉及的文件发生变化，其余文件不变（7.4j）。

### 7.4f 会话树与快速连接

会话树（文件夹、标签颜色、收藏、搜索、排序）作为侧栏内容，按来源分组：“应用会话”“SSH config”“本地”（7.3）；快速连接栏输入 `user@host:port` 或任意名称（可匹配通配规则），不会把已删除的别名恢复到会话树。

### 7.4g 一次性导入（后续）

PuTTY 注册表、Xshell `.xsh`、MobaXterm `.mxtsessions` 导入为应用自建会话；导入完成后与原文件无关。这与 SSH config 的动态只读来源是两回事，界面和文档中分开描述。

### 7.4h 无损 ssh_config 语法树

**产出**：`src/sessions/ssh_config/ssh_config_document.{hpp,cpp}`（只依赖 Qt Core，可单测）。

**实现**：

1. 按行解析，每行保存：原始文本（含换行符）、行类型（空行、注释、指令、块头 `Host`／`Match`、`Include`）、关键字原文与规范化名称、值的原文、分隔符原文（空格或 `=` 及其两侧空白）、行尾注释、缩进。续行与引号按 OpenSSH 的分词规则解析，但原文不变。
2. 块：从一个 `Host`／`Match` 行开始，到下一个 `Host`／`Match` 行之前；文件开头到第一个块头之前是全局区。`Include` 行记录其所在位置（全局区或某个块内）。
3. 文档级元数据：换行符风格（按出现最多的一种）、是否有 BOM、末尾是否有换行。
4. `QString toText() const`：逐行拼回原文。

**测试**：建立样例语料 `tests/fixtures/ssh-config/roundtrip/`（至少 40 个文件：各种注释位置、`=` 与空格分隔、混合大小写、引号与转义、续行、CRLF、BOM、无末尾换行、`Match` 与 `Include`、空文件、只有注释），断言 `parse(x).toText() == x` 逐字节相等。

### 7.4i 修改操作与有效值分析

**产出**：`src/sessions/ssh_config/ssh_config_edit.{hpp,cpp}`。

**实现**：

1. 操作类型：`SetOption{alias, keyword, values}`、`RemoveOption{alias, keyword}`、`AddHost{alias, options, file, position}`、`RenameHost{from, to, scope}`、`RemoveHost{alias}`、`SplitAlias{alias}`、`MoveBlockBefore{alias, blockIndex}`、`InsertInclude{path}`、`RemoveMuxoraInclude{}`。
2. `analyze(document, alias, keyword)`：静态找出“当前生效的值来自哪个块”（`Host` 模式匹配、否定模式、文件顺序，跨 `Include` 文件按包含顺序展开），以及路径上是否有无法判断的 `Match` 块。不执行任何命令。
3. `plan(documents, operation)` 返回一个修改计划：要写哪些文件、每个文件的新文本、需要提示用户的情况（通配块优先、多别名块、`Match` 不确定、受保护文件）以及备选操作（例如“拆出别名”“移动块”）。按 [设计 §3](../../design/ssh-config-sync-design.md#3-修改用户自己的-configallowediting) 的表格逐项实现。
4. 新增行的格式跟随规则（缩进、关键字大小写、分隔符）按设计 §2 第 3 条。
5. 计划是纯数据，界面负责展示和确认，写入器（7.4j）负责执行。

**测试**（数据驱动，输入为样例文件加操作，输出与期望文本逐字节比较，即“黄金文件”测试）：原地替换；块内新增；通配块在前时返回“需要移动”且不修改；确认移动后的结果；多别名块的“同时修改”与“拆出”；`Match` 存在时的提示；重命名在多个块中出现时的两种范围；删除单别名块连同其块头注释、删除多别名块中的一项；新建主机插在 `Host *` 之前；`Include` 文件中的块只改该文件；受保护文件拒绝；每个用例都检查“除目标行外其余文本不变”。

### 7.4j 安全写入器

**产出**：`src/sessions/ssh_config/ssh_config_writer.{hpp,cpp}`、`src/platform/windows/file_replace.{hpp,cpp}`。

**实现**：按 [设计 §5](../../design/ssh-config-sync-design.md#5-安全写入)：

1. 写入前重新读取并重新解析目标文件；把计划中的语义操作重新应用到最新内容上（不是直接写计划时算好的文本）。如果外部修改触及同一个块的同一个参数，返回冲突。
2. 写临时文件（同目录，`FILE_ATTRIBUTE_HIDDEN`），`FlushFileBuffers`，然后 `ReplaceFileW(target, temp, nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, ...)`；目标不存在时用 `MoveFileExW(MOVEFILE_WRITE_THROUGH)`。符号链接先用 `GetFinalPathNameByHandleW` 解析到目标。
3. 写后重新解析并校验（设计 §5 第 5 条），失败时用备份恢复。
4. 备份到 Muxora 状态目录 `ssh-config-backups/`，每个文件保留 `ssh.config.backupCount` 份；命令 `ssh.config.undoLastWrite`。
5. 写入期间暂停该文件的监听回调（写完后按新内容刷新一次索引），避免把自己的写入当成外部修改。

**测试**：并发外部修改（不同块保留、同块同参数报冲突）；ACL 保留（测试中用 `SetNamedSecurityInfoW` 给目标设置一个特殊的 ACE，写入后读回仍在）；符号链接目标被写入而链接保持；CRLF、BOM 保持；写后校验失败时恢复（注入故障）；备份数量上限；撤销恢复原文；写入不会触发一次“外部修改”同步。

### 7.4k 允许修改的界面（`allowEditing`）

**产出**：SSH config 会话的设置页、`src/sessions/ui/ssh_config_diff_view.{hpp,cpp}`（差异视图，基于阶段 8.6 的对话框）。

**实现**：

1. 开关关闭时保持 7.4e 的只读界面。打开后，SSH config 会话的设置页显示设计 §3.4 中的参数编辑控件，旁边标注当前值来自哪个文件的哪个块（7.4i 的分析结果）。
2. 保存时生成计划；有提示项时按提示让用户选择（多步快速输入或对话框）；`ssh.config.confirmWrites` 打开时显示差异视图，确认后写入。
3. 会话树右键：新建主机（选择目标文件）、重命名、删除；SSH config 会话也有“复制为应用会话”。
4. 受保护文件中的主机显示为只读，说明原因。
5. 设置通过阶段 4 的注册表声明：`ssh.config.allowEditing`、`ssh.config.confirmWrites`、`ssh.config.protectedFiles`、`ssh.config.backupCount`。

**测试**：开关关闭时不出现编辑控件、所有写入入口被禁用；打开后编辑端口、增加跳板、重命名、删除各一次并检查文件；取消预览时不写入；受保护文件只读。

### 7.4l 应用会话同步（`syncAppSessions`）

**产出**：`src/sessions/ssh_config/managed_config_sync.{hpp,cpp}`。

**实现**：按 [设计 §4](../../design/ssh-config-sync-design.md#4-同步应用会话syncappsessions)：

1. 管理文件格式：文件头说明注释；每个会话一个块，块头上方 `# muxora-session: <会话 ID>`；只写设计 §3.4 中的连接参数，绝不写秘密。
2. 别名生成：由会话名生成合法主机名（小写、空格换成 `-`、去掉 `*?!,` 等字符），冲突时加 `-2`、`-3`；会话设置中可以修改别名；显示名与别名分开保存。
3. Muxora → 文件：会话变化合并到下一个事件循环批量写入（7.4j 写入器，管理文件不需要确认预览）；只改变化的块，保留用户手写在管理文件中的其他内容。
4. 文件 → Muxora：监听管理文件；按标记对应会话，更新连接参数；无标记的新块导入为新会话并补标记；消失的块对应的会话删除并进入“最近删除”。
5. 三方合并：每个会话保存最近一次同步的参数快照（保存在会话文件中）。只有一边改的项自动采用；两边改成不同值的项、以及删除与修改冲突，按 [设计 §4.3](../../design/ssh-config-sync-design.md#43-双向同步) 弹出冲突对话框（阶段 8.6 的统一对话框，内容为逐项对比表）由用户选择，不自动决定。
   - **必须马上处理**：对话框没有“稍后处理”和取消，`Esc` 与关闭按钮禁用，每项都选定后“确认”才可用；对整个应用模态（`Qt::ApplicationModal`，会话在窗口之间共享），用 `open()` 异步显示；只阻止用户操作，终端收发与传输继续；Muxora 不在前台时不抢焦点，只闪烁任务栏按钮。
   - 对话框期间：管理文件中该块不被改写；该会话暂停同步；其他会话的外部修改先读入等待，对话框关闭后统一应用；新出现的冲突加入同一对话框分页。
   - 冲突状态持久化在会话文件中（`syncConflict`：冲突项列表、文件值、Muxora 值、上次同步值）；程序在处理完之前异常退出时，下次启动在主窗口显示后立即重新弹出。
   - 冲突未处理完时退出程序：先显示对话框，处理完才继续退出（接入 `requestCloseAllWindows` 的关闭许可）。
6. “最近删除”：保留 30 天，恢复时重新写入管理文件。

**测试**：新增、修改、删除、重命名在两个方向上各同步一次；外部调整块顺序、删掉标记注释（按别名回退匹配并补回标记）；三方合并：一边修改自动采用；两边冲突时弹出对话框且在确认前不写文件；逐项选择、全部用文件、全部用 Muxora；`Esc` 与关闭按钮无效、未选完时“确认”不可用；对话框期间所有完整窗口和浮窗都不接受输入，而终端仍在接收并显示数据（用一个持续输出的本地终端验证）；Muxora 不在前台时不抢焦点；新冲突加入同一对话框；模拟异常退出后重启立即重新弹出；冲突未处理时退出程序会先弹出对话框；删除与修改冲突的两种选择；管理文件中的用户注释保留；秘密扫描（文件中不出现任何密码或凭据引用）；同一轮多次修改只写一次。

### 7.4m 开关切换流程与 Lua 接口

**实现**：

1. 打开 `syncAppSessions`（设计 §4.4）：检查 `~/.ssh/config` 开头是否已有指向管理文件的 `Include`，没有时生成 `InsertInclude` 计划并单独确认（即使 `allowEditing` 关闭）；检查别名冲突，逐项让用户选择改名、不同步或以 Muxora 为准；确认后首次写入。任何一步取消都不写文件，开关保持关闭。
2. 关闭 `syncAppSessions`（设计 §4.5）：开关被关闭的那一刻弹出选择对话框，三个按钮“保留文件”“移除文件”“取消”，不预选；`Esc` 等于取消。
   - 设置注册表（阶段 4.1）需要支持“需要确认的设置变更”：设置定义增加 `confirmChange` 回调，返回“接受”“拒绝”或“稍后（异步确认）”；异步确认期间界面显示“正在关闭”，确认完成后才提交新值。这个能力在 4.1 中实现，本步骤使用。
   - 选择“取消”：开关恢复为打开。
   - 用户直接在 `settings.json` 中把值改为 `false`：检测到后弹出同样的对话框；“取消”时用 JSONC 局部修改把文件中的值改回 `true`。
   - 冲突对话框显示期间，设置页中的这个开关暂时禁用。
3. 打开或关闭 `allowEditing` 不改变任何文件。
4. 设置项 `ssh.config.managedFile` 修改时：迁移管理文件（新位置写入、更新 `Include`、旧文件删除前备份），整个过程预览并确认。
5. Lua：`muxora.ssh.config.hosts()`、`.get(alias)`、`.set(alias, options)`、`.add(alias, options, file)`、`.remove(alias)`；受两个开关约束，修改经过同样的计划、确认（`confirmWrites` 打开时在界面中确认）和写入器。

**测试**：打开流程中途取消不写任何文件；冲突三种选择的结果；关闭开关时立即弹出对话框，三个按钮各自的结果，`Esc` 等于取消且开关恢复为打开；通过 `settings.json` 关闭时同样弹出，取消后文件中的值被改回且注释保留；冲突对话框显示期间开关禁用；`Include` 插入位置在第一个 `Host` 之前且保留文件开头的注释；Lua 在开关关闭时返回错误。

---

## 7.5 SSH 连接层（libssh）

**产出**：`src/ssh/`：`ConnectionManager`、`SshConnection`、`SshChannel`、认证与主机密钥、跳板链、代理、端口转发、保活与重连。

**线程模型**：固定数量的工作线程（例如 CPU 核数，最多 8 个），每个连接固定在一个线程上，所有 libssh 调用在该线程执行；GUI 线程通过有界队列发送请求，结果带连接 ID 和代次回到 GUI 线程。上千个会话不对应上千个线程。

**要点**：认证顺序与用户提示（密码、私钥口令、keyboard-interactive）走异步对话，不阻塞工作线程以外的其他连接；`ProxyJump` 多级；SOCKS／HTTP 代理；本地／远程／动态转发；断线按退避重连；所有缓冲有上限并有背压（工程设计 §3.2）。

**验证**：按 [测试设计](../../development/testing-design.md) 第 63、70 行的要求，使用可复现的 SSH 测试服务（Linux 虚拟机或容器，配置脚本放在 `tests/fixtures/`）；覆盖各种认证、主机密钥变化、跳板、代理、转发、断线重连、并发 200 个连接的资源占用。

---

## 7.6 SSH 终端内容

**产出**：SSH 传输实现 7.3a 的 `TerminalTransport` 接口，复用 7.3c 的 `terminal_content` 内容控件（内容类型 `terminal.ssh`）；PTY 通道 ↔ `TerminalCore`；尺寸同步；环境变量（`TERM`、`COLORTERM`）按 V0 设计请求并显示远端是否接受。

**要点**：登录脚本（Lua，`session.expect`／`session.send`，见 [Lua 方案](../review-2026-10/lua-plan.md#1-运行时审查结论)）；Shell 集成（OSC 133）带来的命令完成通知和提示符跳转；会话日志；触发器；广播输入（窗口模块的窗格选择集 + 内容层发送）。

**验证**：在测试服务上运行 `vim`、`htop`、`tmux`、`less`，以及宽字符和颜色样例；断线时内容显示状态并可重连，重连不新建标签。

---

## 7.7 SFTP 与文件面板

**产出**：`src/sftp/`（文件端点、传输队列）、`src/files/`（文件列表内容，model/view）。

**要点**：目录跟随（OSC 7）；侧栏紧凑列表、独立标签、双栏三种呈现共用一个端点模型；`QAbstractItemModel` + `canFetchMore`／`fetchMore` 分批加载；传输队列有并发上限、断点续传、限速、冲突策略；远程编辑自动上传；从资源管理器拖入上传；拖出下载需要延迟渲染（平台层）。

**验证**：测试服务上的大目录（10 万项）首批显示时间与内存；1 GB 文件传输时内存不随文件大小增长；取消、失败恢复和续传。

---

## 7.8 OpenSSH 后端与 X11

**产出**：`src/ssh/openssh_backend`、`src/x11/`。

**要点**：按 V0 设计第 6.3、11 节；检测已安装的 VcXsrv（或用户指定路径），按需启动，多窗口模式；SSH 会话勾选 X11 转发后设置 `DISPLAY` 与 xauth；不受信与受信转发可选。

**验证**：xclock、xterm、GTK 和 Qt 程序；多显示器和 DPI 变化；X server 未安装时的引导提示。

---

## 7.9 后续完善

ZMODEM（`rz`／`sz`）、Kitty 图形协议显示、下拉终端（全局热键）、背景透明与亚克力、ConPTY 之外的串口与 Telnet。每项展开为子步骤后再实现。

---

## 7.10 启动时恢复会话

设计见 [启动时恢复会话设计](../../design/session-restore-design.md)。要点：恢复的是布局、连接目标、终端历史和工作目录；连接**重新建立**；会话对象在启动时为全部标签创建，界面控件按需创建（[1.8](phase-1-layout-performance.md#18-启动时恢复会话对象界面按需创建)）。

**前置**：1.8、7.3、7.4、7.5、7.6；阶段 4（设置注册表）、8.6（统一对话框）。

### 7.10a 窗口模块：标签状态标记

**产出**：`WindowManager` 新增通用 API（不含业务含义）：

```cpp
enum class TabIndicator { None, Activity, Bell, Busy, NeedsAttention, Warning, Error, Exited };
bool setTabIndicator(TabId tab, TabIndicator indicator, QString tooltip = {});
```

在中央标签、固定标签、收纳条标签和标签列表中以小标记显示（颜色来自主题颜色 ID `muxora.tabIndicator.*`，阶段 2）。宿主映射：等待连接、正在连接 → `Busy`；需要登录 → `NeedsAttention`；主机密钥变化 → `Warning`；失败 → `Error`；进程退出 → `Exited`；后台有新输出 → `Activity`。标签切到前台时 `Activity` 和 `Bell` 自动清除。

**测试**：每种标记在四种标签呈现中显示与清除；标签移动到其他窗口后标记保留；外部宿主夹具调用一次。

### 7.10b 会话描述的保存与读取

**产出**：`src/sessions/restore/session_descriptor.{hpp,cpp}`。

每个窗格的 `TabOptions::state`（已随布局保存）中记录：内容类型、会话配置 ID 或本地终端配置 ID、上次的终端尺寸、上次报告的工作目录、文件面板路径与跟随状态、tmux 会话名（§6）、历史文件 ID。不保存任何秘密和连接参数（连接参数在恢复时从会话配置重新读取；配置已删除时按 V0 设计 §5.4 不复活，标签显示“会话配置已不存在”并只保留历史）。

**测试**：保存与读取往返；配置已删除的处理；状态中不出现密码类字段（扫描）。

### 7.10c 恢复协调器

**产出**：`src/sessions/restore/restore_coordinator.{hpp,cpp}`。

1. 接收 1.8 的 `contentRestored` 列表，为每项创建会话对象（不连接），并按设置决定：`none` 不会走到这里（应用启动时直接用默认布局）；`layout` 全部置为“未连接”；`reconnect` 全部进入连接队列。
2. 队列：优先级为“可见 > 访问时间”；用户切到某个标签（1.7 的可见性通知）或在其中按键时提到队首。
3. 并发：全局 `ssh.restore.maxConcurrent`（默认 8）；同一主机、同一跳板机各 2；本地进程 4。
4. 网络：监听 `QNetworkInformation::reachabilityChanged`；不可达时暂停队列，恢复后继续。
5. 进度：恢复期间状态栏显示一行“正在恢复连接 n/m”，完成后移除。失败的会话在自己的标签中显示错误标记、原因和“重试”，不另设列表或通知。
6. 认证（[设计 §4.1](../../design/session-restore-design.md#41-需要输入密码或验证码的会话)）：需要交互输入的会话排成单独一队，同一时间只处理一个；轮到时才握手，握手完成立即弹出输入框（统一对话框），弹出前切到该会话的标签。按钮“登录”“跳过”“全部跳过”；选项“记住密码”、同主机同用户时“其余 n 个会话也使用此密码”（仅本次运行，内存中）。用户输入超时导致服务器断开时，输入框保留并在提交时重新握手；密码错误在同一输入框中重试。其他会话在此期间继续并行连接。
7. 主机密钥变化：停在警告状态，不重试。认证被拒绝：不重试。其他失败按自动重连策略退避。
8. 共享跳板连接：同一跳板机的多个会话共用一条 libssh 会话（多个通道）；跳板连接断开时，依赖它的会话一起进入重连。

**测试**（用可复现 SSH 测试服务，7.5）：200 个会话恢复时同时握手数不超过上限；同主机限制；可见标签先连；切到排队中的标签立即连接；需要密码的会话一个接一个弹出、未轮到的不提前握手（服务端日志确认）、跳过与全部跳过、复用密码选项、等待超过服务器 `LoginGraceTime` 后提交仍能重新握手登录、密码错误重试；主机密钥变化不重试；断网暂停、恢复继续；`layout` 模式不发起任何连接（抓包或服务端日志确认）。

### 7.10d 终端历史保存与显示

**产出**：`src/terminal/history_store.{hpp,cpp}`。

1. 格式：终端核心的单元格格式（码点或字形簇、属性、颜色），每个终端一个文件 `sessions-history/<历史 ID>.bin`，直接保存，不加密。
2. 上限：每终端 `terminal.restoreHistoryLines`（默认 1000）行且 ≤ 512 KiB；全局 `terminal.restoreHistoryBudget`（默认 256 MiB），超出时按访问时间从旧到新截短。
3. 写入：后台线程；正常退出时写全部；运行中每 60 秒写有新输出的终端；标签关闭时删除文件；关闭 `terminal.restoreHistory` 时删除全部。
4. 显示：恢复后历史放在新连接上方，中间分隔线“以下为新连接 · <时间> 恢复”；历史只读，可滚动、搜索、复制、Normal 模式浏览。

**测试**：往返（颜色、宽字符、组合字符）；单终端与全局上限；关闭设置后文件被删除；崩溃模拟（强制结束）后重启最多缺 60 秒内容。

### 7.10e 工作目录与 tmux 选项

1. 本地终端在上次目录启动（ConPTY 工作目录；WSL `--cd`）。
2. SSH：`ssh.restore.restoreDirectory` 打开且有 Shell 集成（OSC 133）时，在第一个提示符后发送安全引用的 `cd`；否则只在分隔线上显示上次目录。
3. 会话选项“在 tmux 中运行”：连接后执行 `tmux new-session -A -s muxora-<标签 ID 短码>`；重连时重新接入；远端无 tmux 时提示并按普通 shell 打开。命令在会话设置中可见。

**测试**：本地终端目录恢复（含空格、Unicode 路径、WSL）；SSH 有无 Shell 集成两种情况；tmux 重新接入后远端进程仍在（测试服务上运行 `sleep` 并检查 PID）。

### 7.10f 设置与异常退出

1. 声明设置：`workbench.restoreSessions`（`none`／`layout`／`reconnect`，默认 `reconnect`）、`terminal.restoreHistory`、`terminal.restoreHistoryLines`、`terminal.restoreHistoryBudget`、`ssh.restore.maxConcurrent`、`ssh.restore.restoreDirectory`；命令“重新连接全部已断开的会话”“断开全部会话”。
2. 异常退出：不需要单独处理，下次启动按同样的设置恢复（历史最多缺最后 60 秒）。

**测试**：三种模式各自的启动结果；强制结束进程后再次启动能按设置恢复。

---

## 7.11 终端命令补全

设计见 [终端命令补全设计](../../design/command-autocomplete-design.md)（参考 Termius 的 Autocomplete）。可选功能，默认关闭；关闭时不加载任何相关代码和数据。

**前置**：7.5、7.6（OSC 133 Shell 集成）；路径来源复用 7.7 的 SFTP 通道（没有时用 `exec` 通道）；阶段 3（快捷键上下文）、阶段 4（设置注册表）、8.4（快速输入的列表样式）。

开始前按本阶段开头的约定展开为子步骤文档，建议的切分：

1. **命令行识别**：从终端核心取得 `133;B`／`133;C` 之间的命令行文本与起点；备用屏幕、命令运行中、复制模式下的排除；没有集成时的手动模式。
2. **历史存储与索引**：按主机的历史文件、上限与淘汰、前缀索引、排序打分；“清除补全历史”命令。
3. **弹窗与按键**：弹窗控件、只发送按键的接受方式、`Enter` 不替换、行内提示；快捷键上下文 `terminalAutocompleteVisible`。
4. **远端来源**：辅助 `exec` 通道（每连接最多一个、空闲关闭、被拒绝后降级）、远端历史、路径。
5. **命令规格**：Fig autocomplete 规格的转换工具、按命令名按需加载、动态参数。
6. **Shell 集成注入**：bash、zsh、fish 的内联注入，不写远端文件；失败退回普通 shell。
7. **片段、密码提示与 Lua 来源**。

**验证**：设计文档 §5；真实主机为本机 Windows OpenSSH、WSL、Ubuntu 虚拟机，测试中对 `~/.ssh/config` 和服务器的任何临时改动在结束后还原。

---

## 7.12 命令行启动请求

**目标**：落实 [命令行启动设计](../../design/settings-and-commands-design.md#71-命令行启动)，从应用启动参数打开一个或多个会话、执行 Lua 脚本，复用现有应用服务。

**前置**：6.1（启动参数与应用入口拆分）、7.3–7.6（本地终端、会话目录及 SSH 打开流程）、7.10（与恢复协调器衔接）、9.0（Lua 按需加载）。不依赖编辑器或 Lua 会话扩展 API，排在 M4 的会话恢复之后。

**涉及文件**：6.1 产出的 `src/app/startup_options.{hpp,cpp}`、`src/app/main.cpp`、`src/app/scripting_setup.{hpp,cpp}`；应用侧启动请求分发、会话打开服务和恢复协调器；`src/app/tests/`；`tools/tests/` 中的部署验证入口。实际文件和测试套件在展开子步骤时按当时源码核对。

**实现**（开始前按本阶段约定展开并评审）：

1. 先列出已支持参数和部署夹具依赖，补齐参数语法及兼容表；明确会话标识、批量输入形式、Lua 文件路径、混合请求顺序、目标窗口／实例选择、错误结果和退出语义。把用户行为写回设计，不能直接把接口草案当成已发布契约。
2. 将参数解析为有类型的启动请求，与执行服务分开。语法错误不执行任何请求；支持带空格与 Unicode 的参数，避免重新拼接后交 shell 解释。
3. 工作区和相应服务就绪后分发一次。会话请求调用统一打开服务，沿用认证和并发队列；Lua 请求调用同一个按需加载入口，等待初始化成功后执行，错误或取消按正常任务生命周期收尾。
4. 与 7.10 明确协调顺序，防止重复恢复、重复分发或在恢复未完成时丢失目标上下文；不带新参数时保持原有流程。
5. `--help` 说明实际参数、示例和退出语义；更新预览说明中已完成的参数范围。Lua 文件入口实际可用时再补 Lua 使用说明，部署夹具通过正常的应用启动路径验证。

**测试**（函数名在子步骤中固定）：

- 解析：单个／重复会话请求、批量顺序、Lua 文件、混合请求、空格与 Unicode、未知参数、缺值及无参数兼容；`--help`／`--version` 不加载 Lua 或发起连接。
- 分发：配置、恢复、会话服务和 Lua 以不同顺序就绪，每项请求仍只执行一次；使用可注入服务断言目标、顺序、错误和取消，不能依赖截图或真实网络来判断解析与调度。
- 集成：可复现会话服务验证单个／批量打开及并发限制；Lua 临时脚本产生可断言的结果，覆盖文件不存在、语法错误、运行错误、初始化失败和停止。
- 部署：启动已部署程序完成同样请求并检查退出结果；原 `--command`、状态目录、保存恢复与退出相关夹具继续通过。

**验证**：新增参数解析与调度套件、应用脚本套件、会话打开及恢复相关套件；桌面和部署部分经 [统一隔离入口](../../development/testing-design.md#3-工具与统一入口)。真实 SSH 验证遵守仓库的目标授权与还原要求，不用占位内容冒充连接成功。

**完成标准**：设计中的参数与失败语义已明确，单个／批量会话和 Lua 文件请求均经部署验证，程序断言覆盖顺序、单次分发与恢复衔接；帮助、设计、预览和实际能力一致。

**风险**：在解析层复制会话逻辑、把“已入队”当成“已完成”、Lua 未就绪时丢请求、恢复信号导致重复执行。先用可控服务验证状态变化，再接真实连接。

**状态**：未开始（需先展开子步骤并评审）。
