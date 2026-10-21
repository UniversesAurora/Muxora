# 阶段 9：Lua 扩展 API

**目的**：按 [Lua 扩展 API 设计](../../design/lua-extension-api-design.md) 让本地脚本像 Neovim 一样使用完整的 Lua 标准库（9.8），并让 Lua 能弹出各种对话框和系统通知、创建自定义界面、读写终端，以及操作会话、剪贴板、存储和网络。文件读写、执行命令、加载模块直接用标准库，不另造接口。

**阶段约束**：

- Lua 仍在独立线程运行，Qt 对象不暴露给 Lua；界面由宿主按描述创建，使用 Muxora 的统一组件和主题颜色 ID，遵守 [执行规则中的界面标准](README.md#界面标准)。
- 每项新能力都有上限，并在 `script_lifecycle_test` 的重载循环中验证资源归零。
- 新接口加入 API 描述单一来源（阶段 5.4），同步生成类型声明与 `lua-guide.md` 中的表格。
- 窗口模块（`modules/window-system`）只提供通用能力（例如“宿主提供内容的标签”），不出现 Lua 相关代码；Lua 适配放在 `modules/window-system-lua`，业务接口（终端、会话）放在应用侧。

**依赖**：

| 步骤 | 内容 | 前置 |
| --- | --- | --- |
| 9.0 | 按需加载：没有脚本时不创建 Lua 线程和解释器 | 无 |
| 9.1 | 运行时：多任务挂起与交互调用 | 无 |
| 9.2 | 对话框、输入、选择、系统通知、状态栏消息 | 9.1；8.4（快速输入）、8.6（统一对话框）、8.7（系统通知） |
| 9.3 | 自定义界面：描述、构建、事件、更新 | 9.1；2.4（颜色 ID）为佳 |
| 9.4 | 自定义界面的宿主位置：面板标签与对话框 | 9.3 |
| 9.5 | 存储、剪贴板、文件、网络、外部程序输出 | 9.1 |
| 9.6 | 终端读取、写入与订阅 | 9.1；7.3、7.6 |
| 9.7 | 会话、本地终端、SFTP 接口 | 9.1；7.4–7.7 |
| 9.8 | 本地脚本使用完整标准库（与 Neovim 一致），去掉执行预算 | 无；建议在 9.1 之前 |

9.8 没有前置，建议最先做（它决定了 9.1 要沿用的宿主让出方式）。9.2、9.3、9.5 可以并行（改不同的文件）；9.6、9.7 等阶段 7 相应部分完成后进行。

---

## 9.0 按需加载

**目标**：没有任何脚本需要运行时，Lua 的线程和解释器都不存在（[设计 §9](../../design/lua-extension-api-design.md#9-按需加载)）。

**现状**：`AppScriptingSetup` 构造时创建 `ScriptHost`，后者在构造函数中创建 `ScriptRuntime`，`ScriptRuntime::Private` 构造时立即启动 `LuaScriptThread` 线程；`main.cpp` 调用 `scripting.start()` → `reload()` → `ScriptHost::initialize()`，即使没有 `init.lua` 也会创建解释器并执行引导脚本。另外，用户快捷键文件是在 `ScriptHost::initialized` 回调里才读取的（`scripting_setup.cpp` 中 `captureDefaultBindings`、`loadKeybindings`），`--command` 启动参数也等待这个信号。

**涉及文件**：`modules/lua-runtime/src/script_runtime.cpp`（线程延迟启动）、`modules/window-system-lua/src/script_host.{hpp,cpp}`（运行时延迟创建、`ensureLoaded()`）、`src/app/scripting_setup.{hpp,cpp}`、`src/app/main.cpp`、测试 `src/app/tests/scripting_test.cpp`、`modules/window-system-lua/tests/script_host_test.cpp`、`modules/lua-runtime/tests/script_runtime_test.cpp`。

**实现**：

1. `ScriptRuntime`：线程在第一次 `execute()`／`invoke()`／`reset()` 时才启动；新增 `bool isLoaded() const` 和 `void releaseIfIdle()`（没有排队、挂起任务和保留回调时停止线程、释放解释器）。
2. `ScriptHost`：`ScriptRuntime` 改为第一次需要时创建（`ensureLoaded()`）；`ready()` 在未加载时返回假，但 `run()`、`invokeCommand()`、`initialize()` 会先加载。
3. `AppScriptingSetup::start()`：
   - 先读取用户快捷键文件（不再等待 Lua 初始化），并发出 `initialized`，让 `--command` 等待者继续；
   - 检查 `User/init.lua` 与启用的 `User/scripts/*`（阶段 5.5 之前只检查 `init.lua`），存在时才调用 `reload()` 加载 Lua；
   - 布局中有 Lua 面板标签（阶段 9.4）时也加载。
   - Lua 加载完成后，脚本注册的默认快捷键按现有流程登记，再重新应用用户快捷键规则（现有的 `captureDefaultBindings`／`loadKeybindings` 顺序保持，只是多了一次在 Lua 加载后的重新应用）。
4. 卸载：脚本没有任何注册（`ScriptHost::resourceCounts()` 全为零）且空闲 5 分钟时，调用 `releaseIfIdle()`。有注册时不卸载。
5. 测量：在预览说明中记录“无脚本启动”与“有 `init.lua` 启动”的线程数和私有内存差异，以及第一次加载的耗时。

**测试**：

- 没有 `init.lua` 启动：进程中不存在名为 `LuaScriptThread` 的线程（通过 `QThread` 对象名或运行时 `isLoaded()` 断言），用户快捷键照常生效，`--command` 执行内置命令正常。
- 有 `init.lua`：照常加载，脚本命令和快捷键可用；用户快捷键覆盖脚本默认快捷键的顺序不变。
- 没有脚本时执行“运行 Lua 脚本”：按需加载并执行。
- 只在控制台临时运行一次代码后空闲：5 分钟后释放（测试中缩短空闲时间）；有注册时不释放。
- 现有 `muxora.scripting`、`window-system.lua`、`window-system.lua-lifecycle`、`lua-runtime.runtime` 全部通过。

**状态**：未开始。

---

## 9.1 运行时：多任务挂起与交互调用

**目标**：一个脚本等待宿主回复（尤其是等待用户回答对话框）时，其他脚本照常运行；交互调用不受 30 秒超时限制。

**涉及文件**：`modules/lua-runtime/src/script_worker.{hpp,cpp}`、`include/lua_runtime/script_runtime.hpp`（`RuntimeLimits`、`HostHandler` 的回复方式）、`modules/window-system-lua/src/script_host.cpp`、`modules/lua-runtime/tests/script_runtime_test.cpp`。

**现状**：`ScriptWorker` 只有一个 `active_`；`pump()` 在 `active_` 存在时直接返回，所以任务在 `hostCall` 中挂起等待回复时，队列中的其他任务全部排队；`hostDeadline_` 在 `hostReplyTimeoutMs`（30 秒）后中止任务。

**实现**：

1. `active_` 改为“正在执行的任务”（最多一个）加“挂起的任务表” `QHash<quint64, std::unique_ptr<ActiveJob>> suspended_`。任务在 `hostCall` 中让出后移入挂起表，`pump()` 继续取下一个排队任务执行。
2. 宿主回复到达时（`hostReply`），把对应任务放回“可恢复”队列；`pump()` 优先恢复可恢复任务，再取新任务。所有任务共用一个 `lua_State`，各自是独立协程（现有设计已经是每任务一个协程），因此不需要多个解释器。
3. 每个挂起任务有自己的截止时间：普通宿主调用沿用 `hostReplyTimeoutMs`；交互调用无截止时间。用一个按截止时间排序的计时器统一检查，不为每个任务建一个 `QTimer`。
4. 交互调用的标记：`HostHandler` 返回值增加一种“稍后回复”的形式（例如返回 `ScriptResult` 中带 `deferred = true` 和一个回复令牌），宿主之后用令牌调用 `ScriptRuntime::reply(token, result)`；交互调用在请求中声明 `interactive = true`。
5. 上限：`RuntimeLimits::maximumSuspendedJobs`（默认 64）；超出时新的宿主调用立即失败并报错。
6. 取消：脚本重载、卸载、`cancelAll()` 时，所有挂起任务以 `"cancelled"` 错误结束；交互界面（对话框等）由宿主关闭。
7. 9.8 之后没有执行预算；挂起中的任务同样可以被“请求停止”结束。
8. 回调写法：`ui.confirm(spec, fn)` 这类接口由 Lua 引导脚本实现为“启动一个新任务执行等待写法，完成后调用 `fn`”，不另写一套宿主逻辑。

**测试**：

- 任务 A 等待一个延迟回复的宿主调用，期间任务 B、C 完成；A 随后恢复并得到正确结果。
- 交互调用等待 60 秒（测试中用可注入的时钟或缩短的普通超时对照）不被中止；普通调用照常超时。
- 64 个挂起任务后第 65 个立即失败；全部回复后计数归零。
- 重载时挂起任务以 `cancelled` 结束，`lua-lifecycle` 的资源归零检查通过。
- 原有运行时测试（取消、洪泛等，9.8 之后的版本）全部通过。

**状态**：未开始。

---

## 9.2 对话框、输入、选择、系统通知、状态栏消息

**涉及文件**：`modules/window-system-lua/src/script_api.cpp`（`ui.*` 处理函数）、`script_host.cpp`（引导脚本中的 `muxora.ui`）、`src/app/script_services.cpp`（`ui.notify` 改为系统通知，阶段 8.7）、测试 `script_host_test.cpp`、`src/app/tests/scripting_test.cpp`。

**实现**：按设计 §4 实现 `ui.message`、`ui.confirm`、`ui.input`、`ui.pick`、`ui.status`，以及 `ui.notify` 的 `on_click`。

1. `ui.message`／`ui.confirm` 使用统一对话框（8.6）；`modal` 取 `false`／`"window"`／`"app"`，对应非模态、窗口模态、应用模态；对话框所属窗口取调用上下文的窗口。
2. `ui.input`／`ui.pick` 使用快速输入（8.4）。`validate` 是 Lua 函数：宿主在输入变化时（合并到下一帧）调用它，结果显示为校验信息；校验回调运行超过 1 秒时界面不再等待它，先按“无错误”显示并记录日志，回调结束后再更新校验信息（不中止回调）。
3. 每个脚本同时打开的对话框最多 8 个；超出时报错。
4. 全部为交互调用（9.1），支持等待写法和回调写法。

**测试**：每个接口的等待写法和回调写法；三种模态；`validate` 阻止提交；取消返回 `nil`；重载时打开的对话框被关闭且任务得到 `cancelled`；上限。

**状态**：未开始。

---

## 9.3 自定义界面：描述、构建、事件、更新

**涉及文件**：新建 `modules/window-system-lua/src/ui/`：`ui_description.{hpp,cpp}`（校验与规范化）、`ui_builder.{hpp,cpp}`（创建控件）、`ui_instance.{hpp,cpp}`（一个面板或对话框的控件表、事件、更新）、`ui_table_model.{hpp,cpp}`（表格、列表、树的模型）；测试 `tests/ui_description_test.cpp`（model 层）、`tests/ui_builder_test.cpp`（widget 层）。

**实现**：

1. **校验**（`ui_description`）：把 Lua 传来的嵌套表（`QVariantMap`）转成内部节点树；检查类型、属性名与类型、`id` 唯一、节点数与深度上限（设计 §5.4）；颜色属性只接受已注册的主题颜色 ID。非法描述整体拒绝并给出出错路径（例如 `children[2].children[1].type`）。
2. **构建**（`ui_builder`）：每种节点对应一个 Muxora 控件（`ThemedCheckBox`、`ThemedComboBox`、主题数字框等现有控件，以及阶段 8 的统一组件）；布局节点映射到 `QHBoxLayout`／`QVBoxLayout`／`QGridLayout`／`QFormLayout`／`QTabWidget`／`QSplitter`／`QScrollArea`。不允许任意样式表。`code` 复用阶段 5 的编辑器控件；`terminal` 节点通过宿主回调嵌入一个已有终端会话的视图（阶段 7 之后可用）。
3. **事件**：`ui_instance` 为带 `id` 的控件连接信号，转换为事件（设计 §5.3），通过现有的回调机制发给 Lua；同一控件的 `changed` 合并到下一帧。
4. **更新**：`ui.update` 只设置给出的属性；表格与列表的增量操作直接作用于 `ui_table_model`；`ui.replace` 替换子树（旧子树的控件删除，带 `id` 的旧控件从表中移除）。每个实例每秒最多 60 次更新，超出的合并到下一帧。
5. **读取**：`ui.get`／`ui.values` 在界面线程读取控件当前值并返回。

**测试**：每种控件和布局能构建；非法描述（未知类型、重复 `id`、超深、超量、非法颜色）被拒绝并报告路径；事件与合并；更新、增量行操作（10 万行追加后滚动、选择正常）；替换子树后旧 `id` 不可用；三套主题下组件画廊中加入一个示例面板截图检查。

**状态**：未开始。

---

## 9.4 自定义界面的宿主位置：面板标签与对话框

**涉及文件**：`modules/window-system`（通用的“宿主提供内容的标签”已经存在：`addTab` 加内容控件；需要确认布局恢复时能用 `ContentFactory` 按内容类型 `lua.panel` 和面板 ID 重建）、`window-system-lua/src/script_api.cpp`（`ui.panel`、`ui.dialog`）、测试。

**实现**：

1. `ui.panel{ id, title, content, icon }`：创建内容类型为 `lua.panel` 的标签，`TabOptions::state` 保存面板 ID 和所属脚本；同一 ID 已存在时更新其内容并激活它。
2. 布局恢复：面板标签随布局保存；重启后脚本加载完成前，标签显示“等待脚本 <名称>”；脚本调用同 ID 的 `ui.panel` 时填入内容；脚本不存在或没有提供该面板时，标签显示说明并可关闭。
3. `ui.dialog{ title, content, buttons, modal, size }`：统一对话框（8.6）中放入自定义内容，按钮回传序号与 `ui.values`。
4. 面板关闭时发 `closed` 事件；脚本卸载时关闭其全部对话框，面板标签保留并显示“等待脚本”（不关闭用户的布局）。

**测试**：面板可以拆分、浮动、固定、跨窗口移动且内容身份不变；重启恢复流程三种情况；对话框三种模态；卸载与重载行为。

**状态**：未开始。

---

## 9.5 存储、剪贴板、文件监听、网络、外部程序输出

> 文件读写、列目录用标准库 `io`／`os`（9.8），本步不提供 `files.read/write/list/stat`，只提供标准库没有的文件监听。

**涉及文件**：`src/app/script_services.cpp`（这些属于应用服务）、新建 `src/app/script_services/` 下按分组拆分的文件、测试 `src/app/tests/scripting_test.cpp` 或新测试。

**实现**：

1. `storage.get/set/remove/keys`：每脚本一个 JSON 文件，`QSaveFile` 写入，合并到下一事件循环批量保存；每脚本上限 10 MiB。
2. `clipboard.text()`／`clipboard.set_text(text)`：`QGuiApplication::clipboard()`，大小上限与值传输一致。
3. `files.watch(path, fn)`／`files.unwatch(id)`：基于 `QFileSystemWatcher` 并去抖动，每脚本最多 64 个监听；文件读写用标准库 `io`。
4. `http.request{ method, url, headers, body, timeout_ms }`：`QNetworkAccessManager`，异步（普通宿主调用，不是交互调用），响应体上限 16 MiB，超时默认 30 秒。
5. `app.run{ program, args, cwd, timeout_ms }`：启动进程并在结束后返回退出码与输出（标准输出和错误合计上限 16 MiB）；与现有 `app.launch`（不等待）共用参数校验。它和标准库的 `io.popen` 的区别是：按参数数组启动、不经过 shell，等待期间任务挂起而不阻塞其他脚本（需要 9.1）。

**测试**：每项的正常与上限情况；存储在重载后保留、卸载脚本后文件保留；`http` 使用本地 `QTcpServer` 测试桩；`app.run` 使用现有的测试子程序。

**状态**：未开始。

---

## 9.6 终端读取、写入与订阅

**前置**：7.3（终端内容与会话对象）、7.6。

**涉及文件**：`src/terminal/` 中会话对象新增只读快照接口；`src/app/script_services/terminal_api.cpp`；测试使用本地终端（ConPTY）或测试用的假会话。

**实现**：

1. 定位：`terminal.list`、`terminal.current`，用窗格 ID 找到会话对象（不需要界面控件存在：阶段 1.8 的会话对象始终存在）。
2. 读取：会话对象在其工作线程上生成所需行的快照（只复制请求的行），通过宿主回复返回；坐标规则、宽字符处理和上限按设计 §6.2。
3. 写入：`send`／`send_keys`／`paste` 走与用户输入相同的编码路径（终端核心的输入编码），按键名称与快捷键解析共用一套。
4. 订阅：`on_output` 的正则在会话工作线程中匹配新完成的行；按 100 ms 合并成批；全局每秒最多 200 批，超出时合并并标记 `dropped_batches`。`on_command` 依赖 Shell 集成的 OSC 133。`expect` 为普通宿主调用，带超时。
5. 订阅数上限：每脚本 256 个；终端关闭时自动移除对应订阅并发 `on_exit`。

**测试**：在本地终端中执行固定输出的命令，读取屏幕、历史、单元格、区域、选区；中文宽字符与组合字符；`find`；`send` 后读回；`on_output` 正则匹配与合并；200 个终端同时输出时回调频率不超过上限、程序保持响应；`expect` 成功与超时；终端关闭后订阅清理。

**状态**：未开始。

---

## 9.7 会话、本地终端、SFTP 接口

**前置**：7.3–7.7。

**实现**：按设计 §7 实现 `sessions`、`local_terminals`、`sftp` 分组。修改 SSH config 会话的接口受 `ssh.config.allowEditing` 约束（阶段 7.4m 已规划 `muxora.ssh.config`，在此并入 `sessions`）；连接相关事件与阶段 7.10 的状态一致；SFTP 传输复用阶段 7.7 的传输队列，进度事件按每秒最多 10 次合并。

**测试**：在可复现 SSH 测试服务上完成：列出、新建、连接、断开、重连；SFTP 列目录、上传、下载、进度事件；开关关闭时修改 SSH config 的调用返回错误。

**状态**：未开始。

---

## 9.8 本地脚本使用完整标准库（与 Neovim 一致）

**目标**：`User/` 下的脚本像 Neovim 的 Lua 一样直接使用完整的 Lua 标准库：`io`、`os`、`debug`、`package`／`require`、`coroutine`、`load`（含二进制块）、`string.dump`、`collectgarbage` 全部可用；文件、命令、模块这类能力用标准库，不另造 `muxora.*` 接口；不做权限管理，也不设执行预算：脚本不会因为运行太久或占用内存而被自动中止，运行时只保留“请求停止”（重载、退出、停止命令）。决定见 [ADR 0008](../../adr/0008-full-standard-library-for-local-scripts.md)。

**前置**：无。建议在 9.1 之前做：9.1 改造宿主让出与挂起，需要沿用这里的让出标记。

**实施前的现状**：

- `modules/lua-runtime/src/script_worker.cpp` 的 `initialize()` 只打开基础库、`string`、`table`、`math`、`utf8`，并移除 `load`、`loadfile`、`dofile`、`collectgarbage`、`string.dump`；`print` 由宿主实现、写入脚本日志；`pcall`／`xpcall` 包装为调用后检查预算（`guardedCalls`）。
- 每个任务在独立协程中运行，`prepare()` 用 `lua_sethook` 给它装计数钩子（每 1000 条指令检查一次）。钩子按线程（协程）生效；`lua_newthread` 会把创建者的钩子复制给新协程，所以用户自建的协程也受预算约束。
- `hostCall()` 用 `lua_yieldk(state, 0, ...)` 让出，等宿主回复后由运行时恢复任务协程。用户在自己的协程里调用 `muxora.*` 时，这次让出会被用户的 `coroutine.resume` 接住，运行时看不到，宿主调用因此失效。
- `require` 是 `modules/window-system-lua/src/script_host.cpp` 引导脚本里自己写的：经宿主方法 `module.read`（`script_api.cpp`）读取模块目录下的文件，限制路径必须在目录内、严格 UTF-8、不超过 1 MiB，并检测循环依赖。
- 默认预算（`RuntimeLimits`）：每次运行 100 ms 执行时间（按墙钟计）、200 万条指令、32 MiB 内存。阻塞的 `io` 调用和 `os.execute`、`io.popen` 等待外部程序的时间也会计入，一次普通的 `git status` 就会超时。

**涉及文件**：`modules/lua-runtime/` 的 worker、runtime、参数转换和公开限制，`modules/window-system-lua/src/script_host.cpp`（引导脚本、`require`）、`script_api.cpp`（移除 `module.read`），`src/app/` 的脚本服务、菜单命令、退出流程与应用清单，WindowSystem 通用可点击状态项，以及这些模块的测试、Lua 指南和脚本设计。当前没有预算设置项；本步不增加相关设置。

**实现**：

1. **打开全部标准库**：`initialize()` 改用 `luaL_openlibs`（基础库、`package`、`coroutine`、`table`、`io`、`os`、`string`、`math`、`utf8`、`debug`），不再移除任何函数。`print` 仍写入脚本日志，`pcall`／`xpcall` 的包装保留，用于在被保护的调用返回后检查停止请求。
2. **宿主让出带标记**：`hostCall()` 改为让出一个只有运行时持有的标记值（私有 `lightuserdata`），即 `lua_yieldk(state, 1, ...)`。`resume()` 在任务因宿主调用挂起时，先把协程栈上让出的值弹掉，再在回复到达时恢复。钩子因停止请求让出时只能不带值（Lua 规定钩子只能 `lua_yield(L, 0)`），这种情况由下一条的停止检查处理。
3. **协程包装**：像 `guardedCalls` 一样用一段私有引导代码替换 `coroutine.resume` 和 `coroutine.wrap`，原函数、标记和停止检查函数作为上值：
   - `resume(co, ...)`：调用原 `resume` 后先检查是否已请求停止（中止状态是粘滞的，已请求时在外层继续让出或报错，与 `pcall` 包装相同）；若内层让出的第一个值是标记，说明内层发起了宿主调用：外层用原 `yield` 把标记继续往外传，被恢复后再用原 `resume` 恢复内层，直到内层不再让出标记，最后把结果按标准语义返回。嵌套多层协程时每层都这样转发。
   - `wrap(f)`：基于上面的 `resume` 实现，出错时按标准语义关闭协程并抛出错误。
   - `coroutine.yield`、`status`、`running`、`isyieldable`、`close`、`create` 保持原样。
4. **`debug.sethook`／`debug.gethook` 与运行时钩子共存**：脚本或第三方库（例如覆盖率工具）调用 `debug.sethook` 会替换掉线程上运行时用来检查停止请求的钩子，导致死循环停不下来。用 C 实现替换这两个函数，其余 `debug` 函数原样：
   - 用户的钩子（函数、事件掩码、计数）按线程保存在注册表中一张弱键表里，协程被回收时条目自动消失。
   - 线程上始终安装运行时的 C 钩子：保留计数、调用和返回检查，加上用户请求的事件；计数间隔取运行时间隔与用户计数的最大公约数，既及时检查停止又准确触发用户计数。
   - 运行时钩子先检查是否请求停止，需要停止时立即让出或报错，不再调用用户钩子；否则按用户请求的事件调用用户钩子（`call`、`tail call`、`return`、`line`，以及按用户自己的计数递减触发的 `count`），参数与标准 `debug.sethook` 相同。清理阶段（`cleanup_`）不调用用户钩子。
   - `debug.gethook` 返回用户设置的钩子、掩码和计数；`debug.sethook()` 不带参数时只清除用户钩子。
5. **去掉执行预算，只保留请求停止**（用户 2026-10-20 决定）：
   - 删除时间、指令数、内存三项预算（`RuntimeLimits` 的 `timeLimitMs`、`instructionLimit`、`memoryBytes` 及分配器中的上限判断），脚本不会被自动中止。分配器只保留内存统计，供资源检查使用。
   - 计数钩子保留，但只检查“是否请求停止”：脚本重载、卸载、程序退出时，以及新增命令“停止正在运行的 Lua 脚本”（`lua.stop`，菜单“工具 → Lua”和命令面板）。请求后，正在执行 Lua 代码的任务在下一个检查点停止，`pcall` 不能吞掉（沿用粘滞中止）。
   - 任务卡在阻塞的 C 调用里（`os.execute`、`io.popen` 等待外部程序、读管道）时，调用返回前无法停止。程序退出时等待 Lua 线程最多 2 秒，仍未结束就照常退出进程，不强杀线程、不阻塞关闭；重载时则报告“脚本仍在运行，已请求停止”。
   - 状态栏在某个任务连续执行超过 2 秒时显示“Lua 脚本运行中”，点击执行 `lua.stop`；这只是提示，不中止脚本。
   - 其他数量上限逐项复核：保留防止程序本身出问题的（等待显示的日志条数、宿主待处理队列长度），把会挡住正常用途的（单个字符串、和宿主之间传递的数据大小、回调数量）去掉或放宽到不影响正常脚本的值，并在 Lua 指南中列出保留的上限及原因。
   - 宿主回复超时（30 秒）不在本步调整，由 9.1 处理交互调用。
6. **标准 `require`**：删除引导脚本中自写的 `require` 和宿主方法 `module.read`，使用标准 `package`：
   - 初始化时由宿主提供用户 Lua 目录，引导脚本把 `package.path` 设为 `<目录>/?.lua;<目录>/?/init.lua`，放在默认路径之前。
   - `package.cpath` 设为空字符串：**不支持 Lua C 模块（DLL）的 ABI**。Lua 按 C++ 编译后静态链接在程序中（[ADR 0002](../../adr/0002-lua-as-cxx-with-gc-hook.md)），没有 Lua DLL 供外部模块链接，按 C 编译的模块也不能和它混用。`package.loadlib` 保留标准行为，不把它描述成任意 DLL 的安全隔离或保证调用失败。
   - 标准 `require` 取消原来的路径限制、严格 UTF-8 检查和 1 MiB 上限，支持 Lua 二进制块。应用直接运行文件和 `execute(QString)` 仍接收严格 UTF-8 文本，源码上限为 64 MiB，与标准加载入口分别说明。
   - ~~标准 `require` 的 C 加载器不能让出；模块返回初始化函数，在 `require` 返回后调用 `muxora.*`。~~ 这一限制不符合 ADR 0008，按下文返工 R2 改为可让出的 `require`。不能让出的调用（例如在元方法中）在提交宿主请求前拒绝，不能先产生 GUI 副作用再报错。
7. **中文路径**：Lua 标准库在 Windows 上通过 C 运行库的 `fopen`、`getenv`、`system`、`_popen` 访问系统，路径和环境变量按进程的 ANSI 代码页解释。给应用和相应测试程序添加 `<activeCodePage>UTF-8</activeCodePage>`（Windows 10 1903 起支持；更早系统忽略该项）。按 [ADR 0009](../../adr/0009-msvc-toolchain.md) 使用 MSVC，由链接器嵌入唯一清单并保留原有 UAC、兼容性和长路径声明。不更改用户系统代码页或字体设置，中文路径、Qt 窗口和 `QProcess` 通过相关自动化检查，真实字体与文件对话框试用单独记录。
8. **`os.exit`**：引导脚本把 `os.exit` 改为调用宿主方法 `app.exit`；应用侧在 `script_services.cpp` 中实现，执行现有命令 `application.exit`（先保存工作区、按窗口族申请关闭许可）。被关闭许可拒绝时返回 `false`，程序继续运行；退出码参数忽略并在文档中写明。没有实现该方法的宿主（独立模块的外部宿主）调用时报“未知宿主方法”。直接调用 C 的 `exit()` 会跳过保存，并在其他线程仍在工作时结束整个进程。
9. **标准输入输出**：Muxora 是窗口程序，没有控制台：`io.read` 读不到输入，`io.write` 写到进程的标准输出，界面中看不到。不重定向，在 Lua 指南中写明“看输出请用 `print`，它写入脚本日志”。
10. **文档**：`lua-guide.md` 写明完整标准库、`require` 的搜索与初始化方式、不支持的 C 模块 ABI、`os.exit`、标准输入输出、停止边界和保留的传输／队列限制；删除已不存在的执行预算描述。`lua-scripting-design.md` 的沙箱描述改为指向 ADR 0008。

**测试**：

- 运行时：全部标准库和函数存在（替换 `script_runtime_test.cpp` 中断言“这些库为 nil”的那一行，按用户 2026-10-20 的决定修改）；`io` 在临时目录中写入、读回；`string.dump` 生成的二进制块可以用 `load` 加载执行；`collectgarbage("count")` 返回数值。
- 请求停止：用户协程里的死循环、`coroutine.wrap` 里的死循环、三层嵌套协程里的死循环、循环中反复 `pcall`，在执行 `lua.stop`、重载和卸载时都能停下；停下后同一运行时能继续执行新任务。没有停止请求时，运行 30 秒的纯 Lua 循环不被中止（测试中用可注入的检查点计数代替真实等待）。
- 宿主调用：在用户协程中、嵌套协程中、协程内的 `pcall` 中调用 `muxora.*` 都得到正确结果；协程中等待宿主回复时取消任务，任务以取消结束、协程被清理。
- 钩子：用户 `debug.sethook` 设置 `line` 钩子和 `count` 钩子后照常触发，死循环仍能被请求停止；`debug.gethook` 返回用户设置；`debug.sethook()` 只清除用户钩子，请求停止仍有效；对另一个协程设置钩子；带 `__gc` 的对象在关闭解释器时不会调用用户钩子（沿用 `cleanupCannotExecuteUnboundedFinalizers` 的场景）。
- 不再有执行预算：`os.execute` 运行一个耗时 3 秒的测试子程序、分配 100 MiB 的字符串都能正常完成；退出时任务卡在 `os.execute` 中，程序在 2 秒内退出且不崩溃（应用测试）；状态栏提示在任务运行超过 2 秒时出现、结束后消失。
- `require`：从用户目录加载 `a`、`a.b`、`a/init.lua`，结果被 `package.loaded` 缓存；不存在的模块报标准错误；`package.cpath` 为空，`package.loadlib` 返回错误。`script_host_test.cpp` 中原有的“目录外路径、无效 UTF-8、截断 UTF-8 必须失败”三组断言按本次决定删除，在提交说明中写明原因。
- 中文路径（`muxora-scripting-test`，该测试程序同样嵌入 UTF-8 清单）：在名称含中文的临时目录中 `io.open` 读写、`require` 模块、`os.rename`、`os.getenv` 读取含中文的环境变量。
- `os.exit`：应用测试中调用后执行一次 `application.exit`；关闭许可拒绝时返回 `false`，测试进程不退出。
- `window-system.lua-lifecycle` 的重载循环与资源归零检查照常通过；运行时原有的取消、洪泛、终结函数用例通过。原有的指令数、执行时间、内存上限用例按本次决定删除或改为“请求停止”的对应用例，在提交说明中逐条写明。

**完成标准**：上述测试通过；Release 全量测试通过；部署后的程序中，一个 `init.lua` 用 `io` 读写含中文路径的文件、用 `os.execute` 运行命令、`require` 一个用户模块，都能正常工作（真实桌面试用，结果写入验证日志）。

**状态**（2026-10-21）：首轮实现已提交，审查后需要返工，见下面的“返工”。用户真实桌面试用未执行。

- 首轮实现（Codex）：Release 首次全量 57/61，之后只补测了失败和相关的套件，最终代码没有完整跑过全量。Claude 接手后在提交 `0966631` 上重跑 Release 全量，结果见 [验证日志](../../development/verification-log/2026-10.md)。
- 独立模块构建、安装后的 C++／Lua 宿主、固定部署中的 Lua 脚本、退出保存和重启检查通过；这些是自动化检查，不替代用户真实桌面试用。
- 标准库和执行预算相关旧断言按 ADR 0008 迁移，保留取消、洪泛、终结函数和重载资源归零检查。

### 返工（2026-10-21 审查）

Claude 审查首轮实现，发现两处不符合本步的要求。返工完成后才算 9.8 完成，然后再进行 1.7。

**R1. 去掉常驻的调用／返回钩子**

- **问题**：`script_worker.cpp` 的 `runtimeHookMask` 为每个任务协程常驻安装 `LUA_MASKCOUNT | LUA_MASKCALL | LUA_MASKRET`。每次函数调用和返回都会进入 C 钩子，函数调用密集的脚本慢约 3.2 倍（2000 万次调用 0.44 s → 1.42 s，同一台机器，Release）；纯循环不受影响。实现第 4 条只要求“保留计数检查，加上用户请求的事件”，没有要求常驻调用钩子。
- **要求**：
  - 没有停止请求、用户也没有设置钩子时，只装计数钩子。
  - 用户通过 `debug.sethook` 请求 `call`／`return` 时才加上对应事件；清除后恢复为只有计数。
  - 收到停止请求后，可以按需要加装更密的检查（现有代码已在停止时改为每条指令检查）；如果某种情况确实需要调用钩子才能停下（例如用调试接口取出原始 `pcall` 在循环里反复吞掉停止），只在停止请求之后加装，并为这种情况补一个用例。
  - 在同一台机器上对比改动前后的耗时（函数调用密集的循环、纯循环各一组），把数字写进验证日志。
- **测试**：现有请求停止、用户钩子、协程和 `pcall` 相关用例全部照常通过；新增断言：未设置用户钩子时 `debug.gethook` 和运行时实际掩码不含调用／返回事件。

**R2. 被 `require` 的模块可以在顶层调用 `muxora.*`**

- **问题**：上面第 6 条最后一项（“模块返回初始化函数，在 `require` 返回后调用 `muxora.*`”）是首轮实现时自行加的限制，与 [ADR 0008](../../adr/0008-full-standard-library-for-local-scripts.md) 的“与 Neovim 一致”不符：Neovim 插件在 `require` 时就能调用 `vim.api`。原因是标准 `require` 由 C 实现，模块顶层运行在 C 调用里，不能让出给宿主。这一点应交给用户决定，而不是写成用法。用户此前已明确要求按 Neovim 的方式处理，所以按下面的办法返工，不再另问。
- **要求**：在引导脚本中用 Lua 实现 `require`，行为与标准一致：
  - 依次使用 `package.searchers`（包括 `package.preload` 和按 `package.path` 查找的 Lua 文件搜索器；C 搜索器保持标准行为，`cpath` 为空时自然找不到），向加载器传入模块名和搜索器返回的额外参数；
  - 结果写入 `package.loaded`，加载器返回 `nil` 且 `package.loaded[name]` 仍为空时存 `true`；返回第二个值（加载器数据）；
  - 找不到时的错误信息与标准一致（列出各搜索器的说明）；
  - 模块顶层的代码由 Lua 调用，因此可以让出给宿主，`muxora.*` 在顶层可用；
  - 循环 `require` 的行为与标准一致（标准 Lua 不检测循环，第二次进入时 `package.loaded` 尚未设置，会再次加载；照此实现，不另加检测）。
- **测试**：模块顶层调用 `muxora.*` 得到正确结果；`a.b`、`a/init.lua`、`package.preload`、自定义搜索器插入 `package.searchers`、加载器返回 `nil` 和返回值缓存、第二个返回值、找不到模块的错误信息；保留现有“不能让出时在提交宿主请求前拒绝”的检查（用于其他不能让出的位置，例如元方法）。
- **文档**：删除第 6 条最后一项和 Lua 指南中“模块返回初始化函数”的用法，确认 `modules/window-system-lua/README.md` 与 [Lua 脚本设计](../../design/lua-scripting-design.md) 没有相反的说法；改写 [Lua 运行时经验](../../development/lessons/lua-runtime.md) 中对应一条（现象保留，做法改为本办法）。

完成 R1、R2 后，重跑 Release 全量（按 AGENTS.md 的桌面测试规则），在验证日志中写清运行和对应提交，再更新本状态。
