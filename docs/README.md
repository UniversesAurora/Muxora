# 文档入口

Muxora 当前进入窗口系统开发阶段。首个预览就是 Muxora 应用，后续在同一套代码上继续开发；窗口模块的独立复用由测试夹具验证。设计文档描述最终行为与约束，不能据此判断功能已经完成；实际预览范围和验证结果见 [窗口系统预览说明](development/window-system-preview.md)。

## 设计

| 文档 | 内容 |
| --- | --- |
| [V0 总览](design/v0-design.md) | 产品范围、技术选型、业务流程与阶段验收 |
| [通用窗口系统](design/window-system-design.md) | 独立模块、公开 API、窗口壳、主题与 DPI |
| [工作区与导航](design/workspace-design.md) | 四边栏、中央标签组、标签内窗格与导航 |
| [浮动与完整窗口](design/floating-window-design.md) | 两种窗口、窗口族、整窗拖拽与跨窗口合并 |
| [窗口交互修订](design/window-interaction-design.md) | 条内标签预览、离条浮窗、明确投放、逐标签固定、滚动标签与工具条 |
| [设置与命令](design/settings-and-commands-design.md) | JSONC、图形设置、命令面板与快捷键 |
| [终端命令补全](design/command-autocomplete-design.md) | 参考 Termius：输入命令时弹出历史、路径、命令参数和片段的补全列表；可选，默认关闭 |
| [内置编辑器](design/text-editor-design.md) | Scintilla 编辑器：编辑 Lua 和 settings.json／keybindings.json，补全、悬停说明、诊断，标签页或弹窗打开，按需加载 |
| [Lua 扩展 API](design/lua-extension-api-design.md) | 对话框与通知、自定义界面、终端读写、会话与文件等扩展接口，以及运行时的多任务挂起 |
| [Lua 脚本与组件扩展](design/lua-scripting-design.md) | 运行时、脚本 API、资源预算及未来组件扩展 |
| [终端 Normal / Visual 模式](design/terminal-mark-mode-design.md) | 本地键盘浏览、选择、搜索与复制 |
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
| [Muxora 窗口系统预览](development/window-system-preview.md) | 当前实现范围、构建、运行、快捷键试用与验证记录 |
| [Lua 使用说明](development/lua-guide.md) | 用户配置、脚本命令、API 分组、重载与宿主扩展 |
| [开发经验](development/lessons/README.md) | 踩过的坑和有效的排查方法，按技术、模块和流程分类，随模块增加扩展 |
| [WindowSystem 模块入口](../modules/window-system/README.md) | 模块构建与宿主使用方式 |

## 审查与路线

| 文档 | 内容 |
| --- | --- |
| [未来规划](roadmap/future-plans.md) | Windows 7 Aero 风格外观、AI 接入等以后要做的事项（只记录需求与约束） |
| [审查结论与后续路线](roadmap/README.md) | 2026-10 整体代码审查、优先级，以及性能、交互、Lua、工程和业务对标的实现方案 |

设计文档使用小写英文加连字符命名。产品与接口设计放在 `design/`，开发规范和操作说明放在 `development/`；审查结论和按优先级排列的实现方案放在 `roadmap/`；后续需要长期记录的架构决定放在 `adr/`。引用仓库文件使用相对路径，机器 SDK 路径只作为本地示例出现。
