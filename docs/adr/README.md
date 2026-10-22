# 架构决定记录

这里记录已经做出、会长期约束实现的决定：当时的背景、选了什么、代价是什么、为什么没选别的。设计文档描述“系统应该怎样”，ADR 回答“为什么是这样、以后改它要先想清楚什么”。

## 目录

| 编号 | 决定 | 状态 |
| --- | --- | --- |
| [0001](0001-own-docking-implementation.md) | 自研停靠与窗口实现，不引入 KDDockWidgets | 采用 |
| [0002](0002-lua-as-cxx-with-gc-hook.md) | Lua 按 C++ 编译，`__gc` 期间保留指令钩子 | 采用 |
| [0003](0003-lua-sandbox-scope.md) | Lua 沙箱只开放纯计算库，能力经宿主接口提供 | 已被 0008 取代 |
| [0004](0004-windows-toolchain.md) | Release 用 Qt MinGW 工具链，MSVC 只用于诊断构建 | 已被 0009 取代 |
| [0005](0005-caption-button-hit-testing.md) | 只有最大化／还原走原生标题按钮命中 | 采用 |
| [0006](0006-load-components-on-demand.md) | 可选组件按需加载，用完销毁 | 采用 |
| [0007](0007-windows-system-notifications.md) | 消息通知使用 Windows 系统通知，不做通知中心 | 采用 |
| [0008](0008-full-standard-library-for-local-scripts.md) | 本地脚本使用完整 Lua 标准库，与 Neovim 一致 | 采用（实现见阶段 9.8） |
| [0009](0009-msvc-toolchain.md) | Release 改用 MSVC 工具链 | 采用 |
| [0010](0010-english-translation-source.md) | 界面翻译使用英文源文本 | 采用（迁移见阶段 8.12） |

## 写法

- 文件名 `NNNN-英文短名.md`，编号递增，不复用。
- 每条包含五节：**背景**、**决定**、**后果**、**替代方案**、**日期**。日期写决定做出的时间和记录时间；补记的决定写明是补记。
- 只记录已经做出的决定。还在讨论的方案写在设计文档或 [实现计划](../roadmap/implementation/README.md) 里。
- 决定被推翻时不删除原记录：把状态改为“已被 NNNN 取代”，新决定另起一个编号，并说明变化原因。
- 内容以代码和设计文档的实际状态为准；两者不一致时先修正，再记录。
- 引入会改变发行许可条件的依赖（例如 GPL 库），必须先取得用户明确同意，再写 ADR。
