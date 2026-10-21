# 阶段 5：Lua 工具（控制台、编辑器、补全、脚本管理）

**目的**：在应用内完成 Lua 的编写、运行、调试和管理，不依赖外部编辑器。方案与选型理由见 [Lua 方案](../review-2026-10/lua-plan.md)。

**阶段约束**：

- 编辑器和控制台都是普通工作区内容（`TabOptions::contentType` 分别为 `lua.editor`、`lua.console`），通过窗口系统获得拆分、浮动、侧栏固定等能力，不另做标签控件。
- 代码放在应用侧 `src/editor/`；只依赖 Qt Widgets 和 `lua_runtime`，不依赖 Muxora 的会话或终端。
- 编辑器使用 Scintilla + Lexilla（宽松许可，见 [内置编辑器设计](../../design/text-editor-design.md)），编译为按需加载的插件 DLL；不引入 QScintilla（GPL）和 QtWebEngine。
- 运行时的沙箱和预算不因编辑器而放宽。语法检查只编译、不执行。

**依赖**：5.2 的编辑器快捷键（`Ctrl+S` 保存文件等）与全局 `Ctrl+S` 冲突，必须在 [3.5](phase-3-keybindings.md#35-内容优先按键与默认绑定调整) 之后进行，或者编辑器暂时在自己的 `event()` 中处理 `ShortcutOverride` 抢先接收这些键（现有 `ShortcutCapture` 用的就是这种方式）。推荐等 3.5 完成。

---

## 5.1 运行结果信号与 Lua 控制台

**目标**：替换“运行 Lua 脚本…”的 `QInputDialog`；脚本输出、返回值和错误在控制台中可见。

**涉及文件**：

- `modules/window-system-lua/include/window_system_lua/script_host.hpp`、`src/script_host.cpp`：新增信号；
- 新建 `src/editor/lua_console.{hpp,cpp}`、`src/editor/output_buffer.{hpp,cpp}`；
- `src/app/scripting_setup.{hpp,cpp}`（`lua.run` 改为打开控制台）、`src/app/CMakeLists.txt`；
- 新测试 `src/app/tests/lua_console_test.cpp`、`src/app/tests/output_buffer_test.cpp`。

**实现**：

1. `ScriptHost` 新增信号 `void runFinished(quint64 job, lua_runtime::ScriptResult result);`。在现有 `ScriptRuntime::finished` 处理函数（`script_host.cpp` 约 202 行）中，对“由 `run()` 发起的任务”发出：`run()` 把返回的任务 ID 记入 `d->runJobs`（`QSet<quint64>`，容量受运行时的待处理任务上限约束），完成时 `take` 并发出。回调、计时器、事件任务不发出。
2. `logMessage` 已存在，控制台直接连接。
3. `OutputBuffer`：环形缓冲，按条数（默认 5000 条）和总字节（默认 2 MiB）双重上限淘汰最旧条目；每条记录时间、类别（输出、结果、错误、命令）和文本。纯数据类，可单测。
4. `LuaConsole`（`QWidget`）：
   - 上部 `QPlainTextEdit`（只读，`setMaximumBlockCount` 与缓冲上限一致），按类别着色；
   - 下部输入框（多行 `QPlainTextEdit`，最小两行），`Enter` 运行、`Shift+Enter` 换行、在首行／末行按上下键翻历史；历史最多 200 条，保存在 `User/lua-history.json`（应用负责，写入使用 `QSaveFile`）；
   - 运行时调用 `ScriptHost::run(source, "@console", context)`，`context` 用管理器当前上下文；
   - 结果格式：`ScriptResult::values` 逐个转为可读文本（字符串加引号，表和列表用紧凑 JSON），错误显示完整信息；
   - 错误中的 `@文件:行号` 或 `文件:行号:` 识别为链接，点击时发出 `openLocationRequested(QString path, int line)`，5.2 完成后由应用打开编辑器。
5. 应用：`lua.run` 命令无参数时打开（或激活已有的）控制台标签；带 `code`／`file` 参数时保持现有行为。新增命令 `lua.console.open`（工具 → Lua）。控制台关闭不影响运行中的任务。
6. 删除 `scripting_setup.cpp` 中的 `QInputDialog` 路径和 `input_` 成员；对应测试按新入口修订（`scripting_test.cpp` 中如有依赖 `LuaScriptInput` 对象名的断言，改为检查控制台，等效覆盖“输入脚本并运行”）。

**测试**：

- `output_buffer_test`：条数上限、字节上限、超长单条（截断并标记）、淘汰顺序。
- `lua_console_test`：输入 `return 1 + 1` 得到结果行 `2`；`print("x")` 得到输出行；语法错误显示错误行并识别位置链接；历史翻页；同时运行两段脚本时结果按任务 ID 对应，不串行。
- `script_host_test`：`runFinished` 只对 `run()` 发出一次；重载后旧任务的结果不发出（与现有代次规则一致）。

**验证**：`window-system.lua`、`window-system.lua-lifecycle`、`muxora.scripting`、新套件。

**状态**：未开始。

---

## 5.2 内置编辑器

> 2026-10-20 修订：改用成熟的编辑器内核 Scintilla + Lexilla，并按需加载；同一个编辑器用于 Lua 和配置文件（JSON），可以以标签页或弹窗打开。设计见 [内置编辑器设计](../../design/text-editor-design.md)。配置文件的智能提示在 [4.7](phase-4-settings.md#47-配置文件的智能提示)，Lua 的补全与提示在 5.3。

### 5.2a 引入 Scintilla 与 Lexilla，编译为插件

**产出**：`third_party/scintilla/CMakeLists.txt`（`FetchContent` 固定版本与 `URL_HASH`，构建 Scintilla 核心、Qt 平台层 `ScintillaEditBase` 和 Lexilla 中需要的词法器：Lua、JSON；其他词法器不编译）；`src/editor/plugin/`（插件工程，输出 `muxora-editor.dll`）；`src/editor/editor_factory.hpp`（应用侧接口）；`tools/build.ps1` 与部署清单加入该 DLL；`THIRD_PARTY_NOTICES.md` 与 `third_party/licenses/` 加入两个库的许可证。

**实现**：

1. Scintilla 的 Qt 平台层自带的是 qmake 工程（`qt/ScintillaEditBase/ScintillaEditBase.pro`），改写成 CMake target；只编译 `ScintillaEditBase`（不需要代码生成步骤），不编译需要 Python 生成的 `ScintillaEdit`。
2. 第一步先确认它在 Qt 6.12 + MSVC 下能编译并运行一个最小窗口；失败时在 ADR 中记录原因，回到 `QPlainTextEdit` 方案，后续步骤相应调整。
3. 插件：`muxora-editor.dll` 实现 `EditorFactory`（设计 §7），用 `Q_PLUGIN_METADATA` 导出；应用在第一次需要编辑器时用 `QPluginLoader` 加载，之后不卸载。
4. 应用测试：启动、打开设置、运行占位标签等普通操作后，进程模块列表中没有 `muxora-editor.dll`（`EnumProcessModules` 检查）；打开编辑器后才出现。

### 5.2b 编辑器控件与基础功能

**产出**：`src/editor/plugin/code_editor.{hpp,cpp}`（包装 `ScintillaEditBase`）、`editor_document.{hpp,cpp}`（文件读写）、`editor_theme.{hpp,cpp}`（主题颜色 ID → Scintilla 样式）。

**实现**：

1. 设计 §4 的全部基础功能，使用 Scintilla 自带能力：行号边栏、折叠边栏、括号匹配、缩进参考线、多选区、按行移动、注释切换（按语言的注释符号）。
2. 颜色：从主题颜色 ID 设置 Scintilla 样式（`SCI_STYLESETFORE`／`SETBACK` 等）；主题变化时重新设置。阶段 2 完成前先用 `Theme` 令牌。
3. 文件：UTF-8（无状态解码校验，沿用 `scripting_setup.cpp` 中 `readScript()` 的规则）；`QSaveFile` 保存；`QFileSystemWatcher` 检测外部修改并提示重新加载；自己保存引起的通知忽略。
4. 快捷键：控件声明 `windowSystemCapturesKeys = true` 和上下文键 `editorFocus`、`editorLanguage`；编辑器命令（保存、查找、替换、转到行等）以 `when: editorFocus` 注册到命令注册表，因而出现在命令面板和快捷键编辑器中。阶段 3.5 完成前，先在控件的 `ShortcutOverride` 中抢先接收这些键。

### 5.2c 查找与替换

查找栏使用 Muxora 自己的控件（遵守界面规范），用 Scintilla 的搜索接口实现：区分大小写、整词、正则；`F3`／`Shift+F3`；全部替换包在 `SCI_BEGINUNDOACTION`／`SCI_ENDUNDOACTION` 中，一次撤销；计数超过 10000 时显示“10000+”。

### 5.2d 打开方式：标签页与弹窗

1. 标签页：内容类型 `editor.file`，`TabOptions::state` 保存文件路径和光标位置；同一文件只开一个标签；修改时标题加 `●`；关闭时询问未保存修改（合并进应用的关闭许可）。
2. 弹窗：统一对话框（阶段 8.6）中放入编辑器，非模态；用于快捷键“编辑参数…”、Lua 控制台多行输入等（设计 §3）。
3. 命令：`editor.openFile`、`editor.openSettingsJson`、`editor.openKeybindingsJson`、`editor.openDefaultSettings`（只读）、`editor.openDefaultKeybindings`（只读）、`lua.editor.openConfig`；`editor.revealSetting`（在 settings.json 中定位或插入某个设置，供设置页齿轮菜单使用）。

**测试**（5.2 整体）：插件只在打开编辑器时加载；基础编辑操作；三套主题下的颜色；外部修改提示；未保存修改的关闭询问；同一文件只开一个标签；弹窗模式保存与取消；`editor.revealSetting` 在已有和缺失两种情况下的结果（缺失时插入，其余文本不变）。

**状态**：未开始。

---

## 5.3 Lua 补全与悬停说明

**前置**：5.2；补全数据在 5.4 完成前从运行时读取，5.4 完成后改用 API 描述。

**实现**：

1. 数据源（纯函数，可单测）：`QVector<CompletionItem> complete(const QString& text, int cursor)`。光标前的限定名（`a.b.c`，忽略字符串和注释内）决定范围：没有点时给 Lua 关键字、当前文件的标识符、顶层 API 表名；`muxora.` 后给 API 分组；`muxora.tabs.` 后给该分组的函数。
2. 5.4 之前的 API 名称：Lua 加载后执行一次只读查询（遍历 `muxora` 表），结果缓存，重载后刷新；Lua 未加载时（阶段 9.0 的按需加载）不为了补全而加载 Lua，只提供关键字和当前文件的标识符。
3. 显示：用 Scintilla 的补全列表（`SCI_AUTOCSHOW`）；`Ctrl+Space` 触发，输入 `.` 后自动触发；悬停说明和参数提示用 Scintilla 的提示框（`SCN_DWELLSTART` + `SCI_CALLTIPSHOW`）。
4. 语法检查：编辑停止 500 毫秒后在沙箱中只编译（`muxora._compile`），错误用波浪下划线指示器标出并在悬停时显示。Lua 未加载时，检查请求会触发按需加载（这是用户主动在编辑 Lua，属于“需要时才加载”）。

**测试**：各上下文的补全结果；字符串和注释中不触发；悬停与参数提示；语法错误标记与清除；API 缓存刷新。

**状态**：未开始。

---

## 5.4 API 描述单一来源

**目标**：一份 API 描述同时生成运行时的分组声明、补全数据、LuaLS 类型声明和文档中的 API 表。

**涉及文件**：新建 `modules/window-system-lua/src/api_catalog.{hpp,cpp}`；修改 `script_host.cpp`（现在第 31–66 行附近用 `api('tabs','list new close …')` 字符串表声明分组）；新建工具目标 `window-system-lua-api-docs`（只在显式构建时运行）；`docs/development/lua-guide.md`。

**实现**：

1. 描述结构：

   ```cpp
   struct LuaApiParameter { const char* name; const char* type; bool optional; const char* description; };
   struct LuaApiFunction { const char* group; const char* name; std::span<const LuaApiParameter> parameters; const char* returns; const char* description; };
   std::span<const LuaApiFunction> luaApiCatalog();
   ```

   说明文字用英文（类型声明文件面向编辑器），界面中文说明仍放在 `lua-guide.md`。

2. `script_host.cpp` 的引导脚本中，`api(prefix, names)` 的名字字符串改为由目录生成：C++ 在初始化时拼出每个分组的名字列表并作为参数传入引导脚本。生成的结果必须与现有字符串逐个一致——先写一个测试比较两者，迁移完成后删除旧字符串。
3. 每个宿主处理函数（`handlers["tabs.list"]` 等）必须在目录中有对应项。测试：遍历处理函数表和目录，二者集合相等（缺少描述或多余描述都失败）。处理函数表目前是 `ScriptHost::Private` 内部的 `QHash`，为测试提供一个只读的名称列表函数。
4. 生成 LuaLS 声明文件：应用首次启动或 Lua 版本号变化时写入 `User/types/muxora.d.lua`：

   ```lua
   ---@meta
   muxora = {}
   muxora.tabs = {}
   ---@param window? string
   ---@return table[]
   function muxora.tabs.list(window) end
   ```

   这样用户在 VS Code 中打开 `User/` 目录、安装 Lua 插件即可补全。
5. 5.3 的补全改为直接读取目录（包括参数提示）。

**测试**：目录与处理函数集合相等；生成的引导分组与旧字符串一致；声明文件可被 `luaL_loadbufferx` 编译（语法正确）。

**状态**：未开始。

---

## 5.5 多脚本单元

**目标**：除 `init.lua` 外，`User/scripts/` 下的每个脚本是一个可独立启用、停用和重载的单元；每项注册知道自己来自哪个单元。

**涉及文件**：`window-system-lua/src/script_host.cpp` 与 `script_host_p.hpp`（注册记录增加来源）、`script_api.cpp`（各注册处理函数）、`src/app/scripting_setup.cpp`（加载顺序）、`lua-guide.md`、测试 `script_host_test.cpp`、`script_lifecycle_test.cpp`、`src/app/tests/scripting_test.cpp`。

**实现**：

1. 单元：`User/scripts/<name>.lua` 或 `User/scripts/<name>/init.lua`；`<name>` 匹配 `[a-z0-9_-]{1,64}`。文件开头可选元信息注释：

   ```lua
   --- @script name: 工作区快捷操作
   --- @script description: ……
   --- @script version: 1.0
   ```

2. 加载顺序：`init.lua` → 按名称排序的已启用单元。每个单元作为独立的运行任务执行，任务名为 `@scripts/<name>.lua`。一个单元出错不影响其他单元。
3. **来源追踪**：运行时上下文中加入当前单元名（宿主在 `run()` 时传入 `context.unit`）。回调执行时也带着注册它的单元名：`registerCallback` 时把单元名与回调 ID 一起记录。`commands.register`、`keymap.set`、`toolbars.register`、`status.register`、`timers.create`、`events.on`、`settings.register` 在宿主侧登记 `unit`。
4. `ScriptHost` 新增：

   ```cpp
   bool unloadUnit(const QString& unit);           // 撤销该单元的全部注册，释放回调
   [[nodiscard]] QVariantMap unitResources(const QString& unit) const; // 各类注册的数量与 ID
   [[nodiscard]] QStringList units() const;
   ```

   撤销顺序与现有整体卸载相同（先停计时器和订阅，再移除界面贡献，最后释放回调），保证与 `lua-lifecycle` 测试中的资源归零检查一致。
5. 单元重载 = `unloadUnit` + 重新运行该文件；VM 不重建。全部重载仍然重建 VM（现有行为）。
6. 启用状态保存在 `settings.json`：`lua.scripts.<name>.enabled`（默认启用），通过阶段 4 的注册表声明。

**测试**：两个单元各注册命令、快捷键、工具条、计时器；停用一个后只有它的资源归零，另一个照常工作；单元出错时其他单元加载；单元重载后命令 ID 不变、工具条布局保留（复用现有“相同 ID 保留布局”的逻辑）。扩展 `lua-lifecycle`：在 24 代重载中交替单元重载与全部重载，资源仍然归零。

**状态**：未开始。

---

## 5.6 脚本管理界面

**目标**：一个工作区标签集中显示脚本状态、注册内容、运行时统计和错误。

**涉及文件**：新建 `src/editor/script_manager_page.{hpp,cpp}`、`script_manager_model.{hpp,cpp}`；`scripting_setup.cpp`（命令 `lua.scripts.manage`）；测试 `src/app/tests/script_manager_test.cpp`。

**实现**：

1. 左侧列表（`QListView` + 模型）：`init.lua` 与各单元；列：启用开关、名称、状态（运行中、出错、已停用、加载中）、注册数量。
2. 右侧详情：元信息；按类别列出注册（命令可直接执行、快捷键可跳到快捷键编辑器并按命令过滤）；最近 20 条错误（时间、信息、位置链接打开编辑器）。
3. 顶部运行时统计：内存、峰值、待处理任务、保留回调数（来自 `ScriptRuntime` 的统计函数和 `ScriptHost::resourceCounts()`），每秒刷新一次，标签不可见时停止刷新。
4. 操作：编辑、重载此脚本、停用／启用、全部重载、打开脚本目录、新建脚本（从模板创建并在编辑器打开）。
5. 能力概览：详情中列出该脚本用到的能力类别（启动程序、文件、网络、终端读写等），只作说明，不弹权限确认（2026-10-20 修订，本地脚本默认可信，见 [Lua 方案 §3.3](../review-2026-10/lua-plan.md#33-权限)）。

**测试**：列表反映加载、出错、停用状态；停用开关调用 `unloadUnit`；统计刷新在标签隐藏时停止；能力概览与脚本实际调用一致。

**状态**：未开始。

---

## 5.7 可选语言服务

**前置**：5.2–5.4。

**实现要点**：检测 `PATH` 或设置项 `lua.languageServer.path` 中的 lua-language-server；默认关闭（`lua.languageServer.enabled`）。启用后按需启动一个进程，用 `QProcess` 走标准输入输出的 LSP（JSON-RPC，带 `Content-Length` 头）。只实现 `initialize`、`textDocument/didOpen`、`didChange`、`publishDiagnostics`、`completion`、`hover`。所有编辑器标签关闭 60 秒后结束进程。消息队列和诊断数量都有上限。

**状态**：未开始。
