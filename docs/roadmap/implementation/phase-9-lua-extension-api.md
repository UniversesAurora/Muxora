# 阶段 9：Lua 扩展 API

**目的**：按 [Lua 扩展 API 设计](../../design/lua-extension-api-design.md) 让 Lua 能弹出各种对话框和系统通知、创建自定义界面、读写终端，以及操作会话、文件、剪贴板、存储和网络。

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

9.2、9.3、9.5 可以并行（改不同的文件）；9.6、9.7 等阶段 7 相应部分完成后进行。

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
7. 预算：指令数与执行时间只在任务真正执行 Lua 时累计（`resumeTimer_` 现有逻辑），挂起期间不计。
8. 回调写法：`ui.confirm(spec, fn)` 这类接口由 Lua 引导脚本实现为“启动一个新任务执行等待写法，完成后调用 `fn`”，不另写一套宿主逻辑。

**测试**：

- 任务 A 等待一个延迟回复的宿主调用，期间任务 B、C 完成；A 随后恢复并得到正确结果。
- 交互调用等待 60 秒（测试中用可注入的时钟或缩短的普通超时对照）不被中止；普通调用照常超时。
- 64 个挂起任务后第 65 个立即失败；全部回复后计数归零。
- 重载时挂起任务以 `cancelled` 结束，`lua-lifecycle` 的资源归零检查通过。
- 原有运行时测试（预算、OOM、取消、洪泛）全部通过。

**状态**：未开始。

---

## 9.2 对话框、输入、选择、系统通知、状态栏消息

**涉及文件**：`modules/window-system-lua/src/script_api.cpp`（`ui.*` 处理函数）、`script_host.cpp`（引导脚本中的 `muxora.ui`）、`src/app/script_services.cpp`（`ui.notify` 改为系统通知，阶段 8.7）、测试 `script_host_test.cpp`、`src/app/tests/scripting_test.cpp`。

**实现**：按设计 §4 实现 `ui.message`、`ui.confirm`、`ui.input`、`ui.pick`、`ui.status`，以及 `ui.notify` 的 `on_click`。

1. `ui.message`／`ui.confirm` 使用统一对话框（8.6）；`modal` 取 `false`／`"window"`／`"app"`，对应非模态、窗口模态、应用模态；对话框所属窗口取调用上下文的窗口。
2. `ui.input`／`ui.pick` 使用快速输入（8.4）。`validate` 是 Lua 函数：宿主在输入变化时（合并到下一帧）调用它，结果显示为校验信息；校验回调本身有执行预算，超出时视为无错误并记录日志。
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

## 9.5 存储、剪贴板、文件、网络、外部程序输出

**涉及文件**：`src/app/script_services.cpp`（这些属于应用服务）、新建 `src/app/script_services/` 下按分组拆分的文件、测试 `src/app/tests/scripting_test.cpp` 或新测试。

**实现**：

1. `storage.get/set/remove/keys`：每脚本一个 JSON 文件，`QSaveFile` 写入，合并到下一事件循环批量保存；每脚本上限 10 MiB。
2. `clipboard.text()`／`clipboard.set_text(text)`：`QGuiApplication::clipboard()`，大小上限与值传输一致。
3. `files.read/write/list/stat/watch`：文本（UTF-8）与二进制（Base64）；单次读写上限 16 MiB；`watch` 基于 `QFileSystemWatcher` 并去抖动，每脚本最多 64 个监听。
4. `http.request{ method, url, headers, body, timeout_ms }`：`QNetworkAccessManager`，异步（普通宿主调用，不是交互调用），响应体上限 16 MiB，超时默认 30 秒。
5. `app.run{ program, args, cwd, timeout_ms }`：启动进程并在结束后返回退出码与输出（标准输出和错误合计上限 16 MiB）；与现有 `app.launch`（不等待）共用参数校验。

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
