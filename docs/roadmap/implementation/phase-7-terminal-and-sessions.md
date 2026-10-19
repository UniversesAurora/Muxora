# 阶段 7：终端与会话接入

**目的**：在窗口系统上接入真实业务：终端核心与显示、本地终端、SSH 会话、SFTP 文件面板、X11。技术选型以 [V0 设计](../../design/v0-design.md) 第 2、6、7、9–11、17 节为准；对标清单见 [业务功能对标](../feature-parity-plan.md)。

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

**完成标准**：基准结果与 ADR；在 1.5 倍、2 倍缩放下像素对齐测试通过。

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

**要求来源**：[V0 设计 §5](../../design/v0-design.md#5-会话管理与-ssh-config) 与 §15。核心是：**SSH config 不是导入，而是动态、只读的来源**。Muxora 不修改 `.ssh/config` 及其 `Include` 文件，不提供任何修改入口，也不用自己的设置覆盖其中的地址、用户、端口、密钥、代理或认证参数；只能按“配置源 ID + 原始别名”在 Muxora 自己的目录中附加界面设置。

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
3. **自动化检查**：对样例配置目录执行全部会话操作（浏览、搜索、附加设置、隐藏、删除附加设置、连接尝试、刷新）后，比较每个文件的字节和修改时间，必须完全不变。

### 7.4f 会话树与快速连接

会话树（文件夹、标签颜色、收藏、搜索、排序）作为侧栏内容，按来源分组：“应用会话”“SSH config”“本地”（7.3）；快速连接栏输入 `user@host:port` 或任意名称（可匹配通配规则），不会把已删除的别名恢复到会话树。

### 7.4g 一次性导入（后续）

PuTTY 注册表、Xshell `.xsh`、MobaXterm `.mxtsessions` 导入为应用自建会话；导入完成后与原文件无关。这与 SSH config 的动态只读来源是两回事，界面和文档中分开描述。

---

## 7.5 SSH 连接层（libssh）

**产出**：`src/ssh/`：`ConnectionManager`、`SshConnection`、`SshChannel`、认证与主机密钥、跳板链、代理、端口转发、保活与重连。

**线程模型**：固定数量的工作线程（例如 CPU 核数，最多 8 个），每个连接固定在一个线程上，所有 libssh 调用在该线程执行；GUI 线程通过有界队列发送请求，结果带连接 ID 和代次回到 GUI 线程。上千个会话不对应上千个线程。

**要点**：认证顺序与用户提示（密码、私钥口令、keyboard-interactive）走异步对话，不阻塞工作线程以外的其他连接；`ProxyJump` 多级；SOCKS／HTTP 代理；本地／远程／动态转发；断线按退避重连；所有缓冲有上限并有背压（工程设计 §3.2）。

**验证**：按 [测试设计](../../development/testing-design.md) 第 63、70 行的要求，使用可复现的 SSH 测试服务（Linux 虚拟机或容器，配置脚本放在 `tests/fixtures/`）；覆盖各种认证、主机密钥变化、跳板、代理、转发、断线重连、并发 200 个连接的资源占用。

---

## 7.6 SSH 终端内容

**产出**：SSH 传输实现 7.3a 的 `TerminalTransport` 接口，复用 7.3c 的 `terminal_content` 内容控件（内容类型 `terminal.ssh`）；PTY 通道 ↔ `TerminalCore`；尺寸同步；环境变量（`TERM`、`COLORTERM`）按 V0 设计请求并显示远端是否接受。

**要点**：登录脚本（Lua，`session.expect`／`session.send`，见 [Lua 方案](../lua-plan.md#1-运行时审查结论)）；Shell 集成（OSC 133）带来的命令完成通知和提示符跳转；会话日志；触发器；广播输入（窗口模块的窗格选择集 + 内容层发送）。

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
