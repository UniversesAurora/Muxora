# 阶段 7：终端与会话接入

**目的**：在窗口系统上接入真实业务：终端核心与显示、本地终端、SSH 会话、SFTP 文件面板、X11。技术选型以 [V0 设计](../../design/v0-design.md) 第 2、6、7、9–11、17 节为准；对标清单见 [业务功能对标](../feature-parity-plan.md)。

本阶段的步骤比前几个阶段粗一级：每一步都会产出新的模块或子系统，开始前要先把该步骤展开成与阶段 1 同样粒度的子步骤文档（`phase-7.<n>-<名称>.md`），经评审后再实现。下面给出每一步的边界、接口草案、验证方式和完成标准，作为展开的依据。

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

**产出**：`src/platform/windows/conpty_process.{hpp,cpp}`、`src/terminal/terminal_session.{hpp,cpp}`、`src/terminal/terminal_content.{hpp,cpp}`（工作区内容）。

**为什么先做本地终端**：不依赖网络和服务器，就能在真实应用中验证终端显示、输入、选区、复制粘贴、标题、链接、搜索，以及窗口系统的按键优先级、可见性通知、跨窗口移动不重建会话。

**要点**：

- 每个会话一个读取线程（或共享的少量线程，用 `QSocketNotifier` 不适用于管道时用重叠 I/O），把字节交给会话的 `TerminalCore`；GUI 线程只取快照。
- 内容控件设置 `windowSystemCapturesKeys = true`、上下文键 `terminalFocus`、`terminalMode`。
- PuTTY 风格：选中即复制（设置项）、右键粘贴或右键菜单（设置项）、中键粘贴、多行粘贴确认、括号粘贴。
- 标签标题模板：`terminal.tabTitle`，默认 `${profile}`；是否允许远端改标题由设置决定。
- 内容类型 `terminal.local`；命令 `terminal.new`（新建本地终端）注册到“文件 → 新建标签”。

**验证**：应用级测试驱动真实 `cmd.exe`／`pwsh`：输入命令、读到输出、调整大小后 `$Host.UI.RawUI.WindowSize` 正确、复制粘贴、标签拖到另一窗口后会话继续（进程 ID 不变、历史保留）。

---

## 7.4 会话目录与存储

**产出**：`src/sessions/`。按 V0 设计第 5 节实现会话来源（Muxora 会话文件、`~/.ssh/config` 只读来源）、分文件存储、动态索引和只读规则；凭据只保存 Windows 凭据管理器中的引用。

**界面**：会话树（文件夹、标签、颜色、搜索）作为侧栏内容；快速连接栏；导入（PuTTY 注册表、Xshell `.xsh`、MobaXterm `.mxtsessions`）作为后续子步骤。

**验证**：模型层测试覆盖读写、只读来源、删除别名、文件变化后的增量刷新；导入用固定样例文件。

---

## 7.5 SSH 连接层（libssh）

**产出**：`src/ssh/`：`ConnectionManager`、`SshConnection`、`SshChannel`、认证与主机密钥、跳板链、代理、端口转发、保活与重连。

**线程模型**：固定数量的工作线程（例如 CPU 核数，最多 8 个），每个连接固定在一个线程上，所有 libssh 调用在该线程执行；GUI 线程通过有界队列发送请求，结果带连接 ID 和代次回到 GUI 线程。上千个会话不对应上千个线程。

**要点**：认证顺序与用户提示（密码、私钥口令、keyboard-interactive）走异步对话，不阻塞工作线程以外的其他连接；`ProxyJump` 多级；SOCKS／HTTP 代理；本地／远程／动态转发；断线按退避重连；所有缓冲有上限并有背压（工程设计 §3.2）。

**验证**：按 [测试设计](../../development/testing-design.md) 第 63、70 行的要求，使用可复现的 SSH 测试服务（Linux 虚拟机或容器，配置脚本放在 `tests/fixtures/`）；覆盖各种认证、主机密钥变化、跳板、代理、转发、断线重连、并发 200 个连接的资源占用。

---

## 7.6 SSH 终端内容

**产出**：`src/terminal/ssh_terminal_content`：PTY 通道 ↔ `TerminalCore`；尺寸同步；环境变量（`TERM`、`COLORTERM`）按 V0 设计请求并显示远端是否接受。

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
