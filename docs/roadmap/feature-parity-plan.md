# 业务功能对标

终端、SSH、SFTP 和 X11 都还没有接入；本文只是对标清单和接入顺序。已定的技术选择以 [V0 设计](../design/v0-design.md) 为准（Ghostty VT 核心、libssh 与 OpenSSH 双后端、默认 `TERM=xterm-256color`、VcXsrv 等），这里不重复论证。每项都要等实际实现和验证后，才能在预览说明里写成“已支持”。

## 1. 终端

对标 Windows Terminal、iTerm2、Alacritty、Tabby、Ghostty。

| 能力 | 方案要点 |
| --- | --- |
| TERM | 默认 `xterm-256color`；会话可选 `xterm`、`vt100`、`vt220`、`linux`、`screen-256color`、`tmux-256color` 以适配老系统；`COLORTERM=truecolor` 按 V0 设计请求 |
| 连字 | 可选，默认关闭（终端列对齐优先）。开启时按连续同属性文本段整形，结果按“字体 × 文本段”缓存，缓存有字节上限 |
| PuTTY 风格选择 | 选中即复制（可关）；右键粘贴（可选“右键菜单”或“右键粘贴”）；中键粘贴；多行粘贴确认；括号粘贴模式 |
| 链接 | OSC 8 超链接；正则识别 URL、路径；`Ctrl+单击` 打开；悬停下划线 |
| 标签标题 | 模板，例如 `${profile}`、`${title}`、`${host}`、`${cwd}`；默认显示会话配置名；可选择是否允许远端通过 OSC 0/2 修改；模板按会话配置保存 |
| Shell 集成 | OSC 133 提示符标记：在提示符之间跳转、选中整条命令输出、记录退出码和耗时 |
| 完成通知 | 长命令结束（OSC 133 测得超过阈值）、OSC 9 / OSC 777 通知、OSC 9;4 进度（任务栏进度条）；窗口不在前台时发系统通知 |
| 触发器 | 参照 iTerm2：正则匹配输出后高亮、通知、执行 Lua；规则数量和每行匹配耗时有上限 |
| 搜索 | 回滚内搜索，支持正则、大小写；结果高亮；与 Normal／Visual 模式（[设计](../design/terminal-mark-mode-design.md)）共用 |
| 图形 | Kitty 图形协议（Ghostty 核心支持）；Sixel 不在 Ghostty 支持范围，见 V0 设计 |
| 会话日志 | 按会话写日志文件，可选时间戳与纯文本／原始字节；大小与轮转上限 |
| 广播输入 | 多选窗格后同时输入，见 [交互 §5](review-2026-10/window-system-ux-plan.md#5-其他交互问题) |
| 外观 | 配色方案独立于界面主题；背景透明、亚克力（DWM）、背景图；光标样式；铃声（视觉、声音、任务栏闪烁） |
| 下拉终端 | 全局热键呼出的下拉窗口（Windows Terminal 的 Quake 模式），复用轻量窗口 |
| ZMODEM | `rz`/`sz`（Xshell、MobaXterm、Tabby 都支持），检测到 ZMODEM 起始序列后接管传输并显示进度 |
| 性能 | 以 vtebench 和 alacritty 的 benchmark 为基准；解析不占 GUI 线程，按帧合并绘制，见 [性能 §5](review-2026-10/performance-plan.md#5-gpu-渲染与终端视图契约) |

## 2. SSH 会话管理

对标 Xshell、MobaXterm、SecureCRT、Tabby。

| 能力 | 方案要点 |
| --- | --- |
| 会话树 | 文件夹、标签、颜色、搜索；拖动排序；快速连接栏（`user@host:port`） |
| SSH config | **不是导入**：按 [V0 设计 §5](../design/v0-design.md#5-会话管理与-ssh-config) 动态读取 `~/.ssh/config` 及其 `Include`，运行中监听变化，只读使用；Muxora 不修改该文件，也不用自己的设置覆盖其中的连接参数，只按“配置源 + 别名”附加界面设置（2026-10-19 更正：此前误写在“导入”一行）。同日新增两个可选开关：允许修改并写回原文件、应用会话与 SSH config 双向同步，见 [修改与同步设计](../design/ssh-config-sync-design.md) |
| 导入（一次性） | PuTTY 注册表、Xshell `.xsh`、MobaXterm `.mxtsessions` 导入为应用自建会话，导入后与原文件无关 |
| 认证 | 密码（存 Windows 凭据管理器，只保存引用）、私钥、OpenSSH agent 与 Pageant、keyboard-interactive、多因素 |
| 跳板 | `ProxyJump` 多级链；每一级单独显示状态和失败原因 |
| 代理 | SOCKS4/5、HTTP CONNECT、`ProxyCommand` |
| 端口转发 | 本地、远程、动态；会话内转发列表，显示状态和流量 |
| 保活与重连 | keepalive；断线后按退避策略自动重连，可设最大次数；重连后可选重新执行登录脚本 |
| 登录脚本 | Lua：`expect`／`send` 风格，带超时和取消，见 [Lua §1](review-2026-10/lua-plan.md#1-运行时审查结论) |
| 自定义 shell | 登录后执行的命令、远端 shell 路径、环境变量、工作目录 |
| 主机密钥 | `known_hosts` 管理界面；首次连接和密钥变化的明确提示 |
| 会话操作 | 复制会话、在新标签／新窗口打开、批量连接文件夹 |
| 本地终端 | 见 [本地终端设计](../design/local-terminal-design.md)：ConPTY 运行 Windows PowerShell、PowerShell 7、cmd、各 WSL 发行版（不经过 SSH）、Git Bash、VS 开发者命令行；自动检测、自建配置、可选读取 Windows Terminal 配置（只读）；串口和 Telnet 放在后续 |

## 3. SFTP

对标 MobaXterm（终端旁随目录跟随的文件面板）和 WinSCP。

| 能力 | 方案要点 |
| --- | --- |
| 目录跟随 | OSC 7 优先（V0 设计）；未配置 shell 集成时提示一键配置，不偷偷向 shell 注入命令 |
| 呈现 | 侧栏紧凑列表、独立标签、双栏（本地／远端或远端／远端），共用同一个文件端点模型 |
| 列表 | model/view，按需加载，大目录分批；排序、过滤、掩码；书签 |
| 传输队列 | 并发数、暂停／继续／重试、断点续传、限速、冲突策略（覆盖、跳过、按时间或大小）；内存不随文件大小增长 |
| 同步 | 比较目录（时间、大小，可选校验和）后预览再执行；“保持远端最新”监听本地目录，变化后上传 |
| 远程编辑 | 下载到临时目录，用内置或外部编辑器打开，保存后自动上传；冲突时提示 |
| 属性 | 权限（chmod）、所有者、符号链接、时间戳保留 |
| 资源管理器拖放 | 上传直接拖入；下载拖到资源管理器需要延迟渲染（`CFSTR_FILEDESCRIPTOR`／`CFSTR_FILECONTENTS`），放在平台层实现 |
| sudo | 支持 `sudo` 启动 `sftp-server`（WinSCP、MobaXterm 都提供） |
| 回退 | 服务器不提供 SFTP 时回退 SCP |
| 远端到远端 | V0 设计已有方案（经本机中转，按块背压） |

## 4. X11

对标 MobaXterm 的内置 X server 体验。

- **组件**：按 V0 设计，Windows 首选验收过的 VcXsrv；分发前核对其许可证和随包义务。
- **启动**：首次需要时启动，多个会话共享一个显示；默认 rootless 多窗口模式，X 窗口作为普通 Windows 窗口出现在任务栏。
- **转发**：SSH 会话勾选 X11 转发后自动设置 `DISPLAY` 和 xauth；可以选择不受信（`-X`）或受信（`-Y`）。
- **体验细节**：剪贴板双向同步、按显示器设置 DPI、键盘布局同步、间接 GLX 的兼容提示。
- **高延迟网络**：X11 协议在高延迟链路上本来就慢，不能靠调参解决。作为可选后端评估 Xpra（GPL，独立进程，许可单独核对），在设置里说明两者的区别。
- **验证**：xclock、xterm、一个 GTK 程序、一个 Qt 程序，以及多显示器和 DPI 变化场景。

## 5. 接入顺序

与 V0 设计的阶段 A–D 一致，补充窗口系统侧的前置条件：

1. 窗口系统完成增量调和、延迟视图和可见性回调（[性能 §1–2](review-2026-10/performance-plan.md)），再接入第一个真实终端；
2. `when` 条件和终端按键冲突的处理（[交互 §3](review-2026-10/window-system-ux-plan.md#3-快捷键对标-vs-code)）与终端同时上线；
3. 会话管理与 SSH；
4. SFTP 面板与传输队列；
5. X11 与其余完善项。
