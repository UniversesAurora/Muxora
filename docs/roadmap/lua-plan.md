# Lua：审查结论、编辑器与脚本管理

相关文档：[Lua 设计](../design/lua-scripting-design.md)、[Lua 使用说明](../development/lua-guide.md)。

## 1. 运行时审查结论

**做得好的部分**（保持）：

- 解释器在独立线程运行，Qt 对象不跨线程；Lua 只看到 ID 和值描述。
- 内存、指令、时间、队列、回调、值大小都有上限；超限明确报错，不会无限积压。
- `pcall`/`xpcall` 包装后，预算检查不能被脚本绕过；GC 期间的钩子也保留了（`third_party/lua/CMakeLists.txt` 对 `lgc.c` 的修改有失败保护）。
- 重载按代次失效旧回调，连续重置只通知最新一代，已有回归。
- 沙箱去掉了 `io`、`os`、`load`、`dofile`、`string.dump`，`require` 只从 `User/lua` 加载。

**本轮没有发现缺陷**。需要关注的点：

| 点 | 说明 | 建议 |
| --- | --- | --- |
| 事件频率 | `layout.changed` 在每次 `layoutChanged` 时发布。标签多、操作频繁时，跨线程转换 `QVariant` 的成本会上升 | 已有合并机制；在 [性能 §6](performance-plan.md#6-测量与预算) 的基准里加一项“1000 个标签 + 一个订阅者”的事件耗时 |
| 单一 init.lua | 所有用户逻辑在一个入口里，无法单独启用、停用某个功能，也无法知道某条快捷键来自哪段脚本 | 见 §3 的多脚本模型 |
| 运行入口 | “运行 Lua 脚本…”用 `QInputDialog` 的多行文本框，没有输出区，也没有历史 | 换成 §2.4 的控制台 |
| 日志 | 脚本 `print` 只进 `qInfo`，用户在界面上看不到 | 控制台输出区，按条数和字节数设上限的环形缓冲 |
| 未来的登录脚本 | SSH 接入后，登录脚本需要“等待输出匹配 → 发送”这样的流程 | 运行时已经用协程处理宿主调用的挂起和恢复，可以自然提供 `await` 风格的 `session.expect(pattern, timeout)`、`session.send(text)`；超时和取消沿用现有预算 |

## 2. 内置编辑器

> 2026-10-20 修订：编辑器改用 Scintilla + Lexilla（成熟、宽松许可、自带 Qt 控件），按需加载，并同时用于编辑 `settings.json`、`keybindings.json` 等配置文件。以 [内置编辑器设计](../design/text-editor-design.md) 为准，下面 2.1–2.3 中与之冲突的内容（自写编辑器、`QCompleter` 补全）不再适用。

### 2.1 组件选型

| 方案 | 结论 |
| --- | --- |
| 基于 `QPlainTextEdit` 自写编辑器 + `QSyntaxHighlighter` | **采用**。无新依赖、许可干净、内存小；行号、括号匹配、查找替换都有成熟写法（Qt 的 Code Editor 示例） |
| KSyntaxHighlighting（KDE Frameworks，MIT） | 作为第二阶段可选：需要高亮 JSON、SSH config、shell 等多种格式时再引入 |
| QScintilla | 不采用：GPL 或商业许可，与 MIT 分发冲突 |
| Monaco / QtWebEngine | 不采用：要带 Chromium，单进程就是上百 MB，违背内存目标 |

### 2.2 功能与结构

编辑器作为普通工作区内容接入窗口系统：每个文件一个标签，内容类型 `lua.editor`，自动获得拆分、浮动、移动、固定到侧栏等能力，不另做一套标签控件。

代码放在应用侧新目录 `src/editor/`（与业务无关的部分以后可以抽成模块）：

```
src/editor/
  code_editor.{hpp,cpp}        QPlainTextEdit 子类：行号栏、当前行高亮、括号匹配、自动缩进、注释切换
  lua_highlighter.{hpp,cpp}    词法着色：关键字、字符串、长括号字符串与注释（按块状态记住层级）、数字
  find_bar.{hpp,cpp}           查找／替换栏：大小写、整词、正则，全部替换合并为一次撤销
  editor_document.{hpp,cpp}    文件读写（QSaveFile、UTF-8 校验、1 MiB 上限）、修改状态、外部修改检测
  lua_completion.{hpp,cpp}     补全数据源与弹出列表
```

功能清单：

- 行号、当前行高亮、括号匹配、缩进参考线；
- `Ctrl+F` 查找、`Ctrl+H` 替换、`F3`/`Shift+F3` 跳转、`Ctrl+G` 转到行、`Ctrl+/` 切换注释；
- 修改标记显示在标签标题上；关闭时如有未保存修改，通过窗口模块已有的 `CloseGuard` 询问；
- 文件被外部修改时提示重新加载；
- 保存后可选自动重载 Lua 配置（设置项 `lua.reloadOnSave`）；
- **语法检查不执行脚本**：运行时已提供 `muxora._compile`，在沙箱中只编译、不运行，把错误行在行号栏标红。

这些快捷键只在编辑器获得焦点时生效，依赖 [`when` 条件](window-system-ux-plan.md#32-when-条件与上下文键)，例如 `editorFocus`。`when` 完成之前，编辑器在自己的 `keyPressEvent` 中处理，并通过 `ShortcutOverride` 优先拿到这些键。

### 2.3 补全

分三步：

1. **静态补全**：Lua 关键字、当前文件中出现的标识符、`muxora.*` API 名。API 名现在写在 `script_host.cpp` 的 `api('tabs','list new close …')` 字符串表里，先从这里解析出来。
2. **API 描述单一来源**：把每个 API 的名称、参数、返回值和说明整理成一份数据表（C++ 常量或 JSON 资源），同时用来：
   - 生成运行时的 `api(...)` 声明；
   - 生成 `User/types/muxora.d.lua`（LuaLS `---@meta` 注解），让 VS Code + Lua 插件也能补全；
   - 生成 `lua-guide.md` 中的 API 表，避免文档和实现不一致。
3. **可选语言服务**：用户安装了 lua-language-server（MIT）时，按需启动、通过 LSP 走标准输入输出，提供诊断、悬停和跳转；默认关闭，空闲一段时间后结束进程，不常驻。

### 2.4 Lua 控制台

替换现在的 `QInputDialog`：

- 内容类型 `lua.console`，可以停靠在底部；
- 上半部分是输出（`print`、错误和堆栈、返回值），下半部分是输入行，`Enter` 运行、`Shift+Enter` 换行、上下键翻历史；
- 输出和历史各有条数与字节上限；
- 错误堆栈中的 `文件:行号` 可点击，在编辑器中打开对应位置。

## 3. 脚本管理界面

### 3.1 多脚本模型

现在只有 `User/init.lua` 一个入口。建议增加 `User/scripts/<name>.lua`（或 `User/scripts/<name>/init.lua`）：

- 每个脚本是一个独立单元，可以单独启用或停用；启用状态保存在 `settings.json`（`lua.scripts.<name>.enabled`）。
- 文件头可以写元信息注释（名称、说明、作者、版本）。
- 运行时记录每项注册（命令、快捷键、工具条、状态项、计时器、订阅）来自哪个单元；停用时只撤销该单元的注册。`ScriptHost` 已经能按代次整体撤销，扩展到按单元撤销需要在注册表中多记一个来源字段。
- `init.lua` 保留为最先执行的用户入口。

### 3.2 界面

“工具 → Lua → 脚本管理”打开一个工作区标签：

| 区域 | 内容 |
| --- | --- |
| 列表 | 每个脚本：启用开关、名称、状态（运行中、出错、已停用）、注册数量 |
| 详情 | 该脚本注册的命令、快捷键、工具条、状态项、计时器、订阅；最近错误及堆栈 |
| 运行时 | 内存、峰值、待处理任务、回调数（来自 `ScriptRuntime` 统计） |
| 操作 | 编辑、重载此脚本、全部重载、打开脚本目录、新建脚本（带模板） |

### 3.3 权限

> 2026-10-20 修订：用户自己放在 `User/` 下的脚本默认拥有全部能力，不弹权限确认（见 [Lua 扩展 API 设计 §2](../design/lua-extension-api-design.md#2-不变的原则)）。以后如果支持安装别人发布的扩展包，再为扩展包设计权限声明。

Lua 能力的完整范围（对话框、自定义界面、终端读写、会话、文件、网络等）见 [Lua 扩展 API 设计](../design/lua-extension-api-design.md) 和 [实现阶段 9](implementation/phase-9-lua-extension-api.md)。

## 4. 实施顺序

1. Lua 控制台（替换输入框，同时解决日志不可见的问题）；
2. 编辑器基础：行号、高亮、查找替换、保存与外部修改检测、编译检查；
3. 静态补全；
4. API 描述单一来源，生成类型声明和文档表；
5. 多脚本模型和脚本管理界面；
6. 可选语言服务。

每步的验证：编辑器逻辑（高亮状态机、查找替换、补全数据源）用无界面的单元测试；界面只测关键交互（打开、修改标记、关闭询问、外部修改提示）。
