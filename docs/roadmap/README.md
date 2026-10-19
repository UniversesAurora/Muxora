# 审查结论与后续路线

日期：2026-10-18\
范围：WindowSystem、Lua 运行时与窗口脚本适配、应用入口、构建与测试。SSH、SFTP、终端和 X11 尚未接入，相关部分只给方案，不代表已有实现。

这一组文档记录 2026-10 的整体审查结果，以及按优先级排好的实现方案。设计文档（`../design/`）描述最终行为；这里回答“现在离目标差什么、先做什么、怎么做”。每项方案落地后，应把结论并回对应设计文档，并在 [预览说明](../development/window-system-preview.md) 记录实际验证结果。

| 文档 | 内容 |
| --- | --- |
| [代码审查记录](code-review-2026-10.md) | 已确认的问题、证据位置、严重程度和本轮处理状态 |
| [性能与内存方案](performance-plan.md) | 面向成百上千个会话的窗口重建、模型索引、主题、GPU 渲染和测量方法 |
| [窗口系统与交互改进](window-system-ux-plan.md) | 深色／黑色主题、菜单结构、设置和快捷键界面、`when` 条件等 |
| [Lua 编辑器与脚本管理](lua-plan.md) | 运行时审查结论、内置编辑器、补全、脚本管理界面 |
| [工程结构与测试](engineering-plan.md) | 目录、构建、测试冗余与缺口、静态检查、CI |
| [业务功能对标](feature-parity-plan.md) | 终端、SSH 管理、SFTP、X11 的对标清单与接入顺序 |
| [界面审查](ui-audit-2026-10.md) | 现有弹窗、选择器、设置页、快捷键页、菜单、面板与规范的差距（2026-10-19） |
| [主题系统设计](../design/theme-system-design.md) | 按 VS Code 的颜色注册表、主题与设置逻辑；深色配色方法 |
| [界面设计规范](../design/ui-design-system.md) | 尺寸、字体、圆角阴影、交互规则和各组件规格 |
| [未来规划](future-plans.md) | 以后要做、现在不设计实现的事项：Windows 7 Aero 风格外观、AI 接入，以及其他已推迟的事项 |
| [实现计划](implementation/README.md) | 按阶段拆分的逐步实现说明：改哪些文件、怎么改、加什么测试、怎么验证、完成标准；供 Agent 按步骤执行 |

## 优先级

P0 指不先解决就会在“成百上千会话”目标下返工的结构问题；P1 是用户体验上的明显缺口；P2 是完善项。

| 级别 | 事项 | 文档 |
| --- | --- | --- |
| P0 | 布局变更时不再重建全部标签视图和重设全部内容 parent（增量调和） | [性能](performance-plan.md#1-布局变化改为增量调和) |
| P0 | 非活动标签延迟创建视图；隐藏内容停止绘制 | [性能](performance-plan.md#2-非活动标签的视图按需创建) |
| P0 | 标签激活、焦点切换不再走整模型事务（R17） | [实现 1.2](implementation/phase-1-layout-performance.md#12-激活与分割比例快速路径r17) |
| P0 | 模型增加 Tab→Group、Pane→Tab 索引；拖动分隔条不做整模型校验 | [性能](performance-plan.md#3-布局模型的查找与事务成本) |
| P0 | 终端渲染走 GPU（QRhi）路线前先定下内容控件的接入契约 | [性能](performance-plan.md#5-gpu-渲染与终端视图契约) |
| P1 | 主题系统按 VS Code 重做：颜色注册表、Muxora Dark／Black、跟随系统与首选深浅主题、颜色覆盖 | [主题设计](../design/theme-system-design.md)、[实现阶段 2](implementation/phase-2-themes.md) |
| P1 | 统一快速输入替换命令面板、窗口选择、目标选择；统一浮层、对话框、通知 | [界面审查](ui-audit-2026-10.md)、[实现阶段 8](implementation/phase-8-ui-quality.md) |
| P1 | 侧栏临时面板八向调整；窗口缩放区移到外侧不可见边框 | [窗口交互设计](../design/window-interaction-design.md#窗口缩放边框)、[实现 8.1–8.2](implementation/phase-8-ui-quality.md) |
| P1 | 快捷键 `when` 条件与上下文键服务 | [交互](window-system-ux-plan.md#3-快捷键对标-vs-code) |
| P1 | 设置改为 schema 驱动，窗口行为设置移出布局文件 | [交互](window-system-ux-plan.md#4-设置界面与存储) |
| P1 | 内置 Lua 编辑器（标签页、高亮、行号、查找替换、补全） | [Lua](lua-plan.md#2-内置编辑器) |
| P1 | 测试可执行文件合并、加入 ASan/UBSan 诊断构建 | [工程](engineering-plan.md) |
| P2 | 菜单与命令面板细化、工作区预设、布局命名保存 | [交互](window-system-ux-plan.md#2-菜单与命令结构) |
| P2 | Lua 脚本管理器、API 类型声明文件 | [Lua](lua-plan.md#3-脚本管理界面) |

## 本轮已经修改的代码

本轮改了两处键盘与焦点热路径（行为不变），并修复了全量测试中发现的侧栏重新显示缺陷；其余问题记录为方案。详情和验证见 [代码审查记录](code-review-2026-10.md#本轮修改)。

## 文字说明

仓库规则要求对外文本按本地 `shuorenhua` skill 处理；本次会话中没有这个 skill，文档按同样原则直接写成：保留术语、数字和真实状态，不写空泛总结。后续若有该 skill，可以再过一遍。
