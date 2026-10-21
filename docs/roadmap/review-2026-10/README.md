# 2026-10 整体审查（快照）

> 这是 2026-10-18 至 10-19 整体审查时写下的结论和方案，**之后不再更新**。其中可执行的部分已经拆进 [实现计划](../implementation/README.md) 的各个阶段，执行先后以实现计划的 [推荐执行顺序](../implementation/README.md#推荐执行顺序) 为准；已经做出的决定见 [ADR](../../adr/README.md)。部分内容已被后来的决定取代，例如 Lua 沙箱与执行预算（[ADR 0008](../../adr/0008-full-standard-library-for-local-scripts.md)）、工具链（[ADR 0009](../../adr/0009-msvc-toolchain.md)）。

范围：WindowSystem、Lua 运行时与窗口脚本适配、应用入口、构建与测试。SSH、SFTP、终端和 X11 当时尚未接入，相关部分只给方案。

| 文档 | 内容 |
| --- | --- |
| [代码审查记录](code-review.md) | 已确认的问题、证据位置、严重程度和当时的处理状态 |
| [界面审查](ui-audit.md) | 弹窗、选择器、设置页、快捷键页、菜单、面板与界面规范的差距（2026-10-19） |
| [性能与内存方案](performance-plan.md) | 面向成百上千个会话的窗口重建、模型索引、主题、GPU 渲染和测量方法，以及 2026-10-19 的基线数据 |
| [窗口系统与交互改进](window-system-ux-plan.md) | 深色／黑色主题、菜单结构、设置和快捷键界面、`when` 条件等 |
| [Lua 编辑器与脚本管理](lua-plan.md) | 当时的运行时审查结论、内置编辑器、补全、脚本管理界面 |
| [工程结构与测试](engineering-plan.md) | 目录、构建、测试冗余与缺口、静态检查、CI |

## 当时的优先级

P0 指不先解决就会在“成百上千会话”目标下返工的结构问题；P1 是用户体验上的明显缺口；P2 是完善项。

| 级别 | 事项 | 文档 |
| --- | --- | --- |
| P0 | 布局变更时不再重建全部标签视图和重设全部内容 parent（增量调和） | [性能](performance-plan.md#1-布局变化改为增量调和) |
| P0 | 非活动标签延迟创建视图；隐藏内容停止绘制 | [性能](performance-plan.md#2-非活动标签的视图按需创建) |
| P0 | 标签激活、焦点切换不再走整模型事务（R17） | [实现 1.2](../implementation/phase-1-layout-performance.md#12-激活与分割比例快速路径r17) |
| P0 | 模型增加 Tab→Group、Pane→Tab 索引；拖动分隔条不做整模型校验 | [性能](performance-plan.md#3-布局模型的查找与事务成本) |
| P0 | 终端渲染走 GPU（QRhi）路线前先定下内容控件的接入契约 | [性能](performance-plan.md#5-gpu-渲染与终端视图契约) |
| P1 | 主题系统按 VS Code 重做：颜色注册表、Muxora Dark／Black、跟随系统与首选深浅主题、颜色覆盖 | [主题设计](../../design/theme-system-design.md)、[实现阶段 2](../implementation/phase-2-themes.md) |
| P1 | 统一快速输入替换命令面板、窗口选择、目标选择；统一浮层、对话框、通知 | [界面审查](ui-audit.md)、[实现阶段 8](../implementation/phase-8-ui-quality.md) |
| P1 | 侧栏临时面板八向调整；窗口缩放区移到外侧不可见边框 | [窗口交互设计](../../design/window-interaction-design.md#窗口缩放边框)、[实现 8.1–8.2](../implementation/phase-8-ui-quality.md) |
| P1 | 快捷键 `when` 条件与上下文键服务 | [交互](window-system-ux-plan.md#3-快捷键对标-vs-code) |
| P1 | 设置改为 schema 驱动，窗口行为设置移出布局文件 | [交互](window-system-ux-plan.md#4-设置界面与存储) |
| P1 | 内置 Lua 编辑器（标签页、高亮、行号、查找替换、补全） | [Lua](lua-plan.md#2-内置编辑器) |
| P1 | 测试可执行文件合并、加入 ASan/UBSan 诊断构建 | [工程](engineering-plan.md) |
| P2 | 菜单与命令面板细化、工作区预设、布局命名保存 | [交互](window-system-ux-plan.md#2-菜单与命令结构) |
| P2 | Lua 脚本管理器、API 类型声明文件 | [Lua](lua-plan.md#3-脚本管理界面) |

审查当轮只改了两处键盘与焦点热路径（行为不变），并修复了全量测试中发现的侧栏重新显示缺陷，详见 [代码审查记录](code-review.md#本轮修改)。
