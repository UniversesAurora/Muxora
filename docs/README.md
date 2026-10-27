# 文档入口

Muxora 当前进入窗口系统开发阶段。首个预览就是 Muxora 应用，后续在同一套代码上继续开发；窗口模块的独立复用由测试夹具验证。设计文档描述最终行为与约束，不能据此判断功能已经完成；实际预览范围和验证结果见 [窗口系统预览说明](development/window-system-preview.md)。

## 设计

| 文档 | 内容 |
| --- | --- |
| [V0 总览](design/v0-design.md) | 产品范围、技术选型、业务流程与阶段验收 |
| [通用窗口系统](design/window-system-design.md) | 独立模块、公开 API、窗口壳、主题与 DPI |
| [工作区与导航](design/workspace-design.md) | 四边栏、中央标签组、标签内窗格与导航 |
| [浮动与完整窗口](design/floating-window-design.md) | 两种窗口、窗口族、整窗拖拽与跨窗口合并 |
| [窗口交互细则](design/window-interaction-design.md) | 拖拽各阶段与投放、边栏与临时面板、工具条、显隐与全屏、窗口缩放边框等具体规则和尺寸 |
| [标签栏设计](design/tab-strip-design.md) | 标签栏的外观、横向与竖向排列、置顶标签、收起与展开、拖动和动画 |
| [表面与阴影](design/surface-elevation-design.md) | 标签组卡片和活动标签接成一体、用阴影高度表示焦点、阴影等级与动画 |
| [Quick 呈现](design/quick-rendering-design.md) | 保持现有外观与交互的 GPU 呈现、帧驱动、资源预算与测量 |
| [设置与命令](design/settings-and-commands-design.md) | JSONC、图形设置、命令面板、快捷键、界面翻译与命令行启动 |
| [终端命令补全](design/command-autocomplete-design.md) | 参考 Termius：输入命令时弹出历史、路径、命令参数和片段的补全列表；可选，默认关闭 |
| [内置编辑器](design/text-editor-design.md) | Scintilla 编辑器：编辑 Lua 和 settings.json／keybindings.json，补全、悬停说明、诊断，标签页或弹窗打开，按需加载 |
| [Lua 扩展 API](design/lua-extension-api-design.md) | 对话框与通知、自定义界面、终端读写、会话与文件等扩展接口，以及运行时的多任务挂起 |
| [Lua 脚本与组件扩展](design/lua-scripting-design.md) | 运行时、脚本 API、停止与资源上限及未来组件扩展 |
| [终端 Normal / Visual 模式](design/terminal-mark-mode-design.md) | 本地键盘浏览、选择、搜索与复制 |
| [窗格活动](design/pane-activity-design.md) | 通知、进度和运行状态：以窗格为单位记录，显示在标签、状态栏、任务栏和 Windows 通知中 |
| [SSH 私钥存放与代理](design/ssh-key-storage-design.md) | 私钥放 `~/.ssh` 或 Muxora 保管库；切换时的迁移；让外部工具使用保管库密钥的独立代理 |
| [SSH config 修改与同步](design/ssh-config-sync-design.md) | 默认只读；可选的写回原文件与应用会话双向同步，无损解析、最小修改、`Include` 接入、冲突与开关切换 |
| [启动时恢复会话](design/session-restore-design.md) | 恢复布局、连接目标与终端历史，重新连接的顺序、限流与登录处理，tmux 选项 |
| [本地终端](design/local-terminal-design.md) | WSL、Git Bash、cmd、PowerShell 等本机 shell 的配置来源、ConPTY 启动、目录跟随与界面入口 |
| [主题系统](design/theme-system-design.md) | 颜色注册表、主题定义、与 VS Code 相同的深浅色设置和切换逻辑、深色配色方法 |
| [界面设计规范](design/ui-design-system.md) | 以 VS / VS Code 为标准的尺寸、字体、交互规则与组件规格 |

## 开发

| 文档 | 内容 |
| --- | --- |
| [工程、资源与依赖](development/engineering-design.md) | 所有权、模块目录、资源约束、SDK 与依赖升级 |
| [自动化测试要求](development/testing-design.md) | 测试链路、场景矩阵、证据与失败处理 |
| [Muxora 窗口系统预览](development/window-system-preview.md) | 当前实现范围、构建、运行、快捷键试用与最近一次验证 |
| [验证日志](development/verification-log/2026-10.md) | 每一轮修订说明与验证记录，按月分文件、按日期倒序 |
| [Lua 使用说明](development/lua-guide.md) | 用户配置、脚本命令、API 分组、重载与宿主扩展 |
| [交接记录](development/handoff.md) | 助手轮流开发时的交接入口：当前状态、进行中的工作、下一步、待用户决定的问题 |
| [文档维护规范](development/documentation.md) | 每类信息的权威位置、索引规则、改动时要同步更新的文档、提交前检查 |
| [开发经验](development/lessons/README.md) | Qt Widgets、Windows、Lua 运行时、构建、桌面测试及调试中的问题与排查方法 |
| [WindowSystem 模块](../modules/window-system/README.md) | 窗口模块的构建、公开 API 与宿主接入 |
| [LuaRuntime 模块](../modules/lua-runtime/README.md) | 在工作线程运行 Lua 的异步运行时 |
| [WindowSystemLua 模块](../modules/window-system-lua/README.md) | 窗口 API 的 Lua 适配与宿主接入 |

## 架构决定

| 文档 | 内容 |
| --- | --- |
| [架构决定记录](adr/README.md) | 自研停靠、Lua 编译方式与标准库范围、Windows 工具链、标题按钮命中、组件按需加载、系统通知等已做出决定的背景、取舍与后果 |

## 路线

| 文档 | 内容 |
| --- | --- |
| [实现计划](roadmap/implementation/README.md) | 按阶段拆分的步骤、推荐执行顺序和当前位置 |
| [业务功能对标](roadmap/feature-parity-plan.md) | 终端、SSH、SFTP、X11 的对标清单与接入顺序 |
| [未来规划](roadmap/future-plans.md) | Windows 7 Aero 风格外观、AI 接入等以后要做的事项（只记录需求与约束） |
| [2026-10 整体审查](roadmap/review-2026-10/README.md) | 当时的代码审查、界面审查和各方面方案（快照，不再更新） |

每类信息放在哪份文档、改动时要同步更新什么、提交前怎么检查，见 [文档维护规范](development/documentation.md)。
