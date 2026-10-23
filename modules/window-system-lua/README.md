# WindowSystemLua

为 [WindowSystem](../window-system/README.md) 提供 Lua API 的可选适配模块。脚本使用值描述和稳定 ID 操作窗口、标签、窗格、停靠、工具条、状态项、设置和主题；命令、菜单、快捷键共用窗口模块的执行路径，关闭许可和布局校验仍然生效。

模块不依赖 Muxora 的配置目录、SSH、终端或传输服务。用户配置、弹窗、启动程序和声音等应用服务由 `src/app/` 接入；真实远程业务和日历触发器尚未实现。

## 构建与接入

| Target | 依赖与职责 |
| --- | --- |
| `LuaRuntime::Runtime` | Qt Core 与静态 Lua；解释器线程、保护调用、值转换、队列上限及请求停止。 |
| `WindowSystemLua::Host` | `WindowSystem::Widgets` 与 `LuaRuntime::Runtime`；公开窗口 API 适配和脚本资源生命周期。 |

源码宿主先添加 `modules/window-system`、`modules/lua-runtime`，再添加本目录，链接 `WindowSystemLua::Host`。当前两个 Lua 模块通过源码 target 接入，尚未提供安装后的 `find_package` 导出；WindowSystem 自身的安装包保持独立。需要 C++20、CMake 3.25 和 Qt 6.9 以上；窗口模块从源码构建还需要 LinguistTools。

首次构建下载固定的官方 Lua 5.5.1 源码并在构建目录编译，不要求系统安装 Lua。Lua 的许可和 `__gc` hook 构建适配见 [依赖说明](../../third_party/lua/README.md)。

在 GUI 线程创建 [ScriptHost](include/window_system_lua/script_host.hpp)，传入仍然有效的 `WindowManager`。通过 `setModuleDirectory()` 指定纯 Lua 模块目录，再调用 `initialize(source, name)`；结果由 `initialized` 异步返回。该目录的 `?.lua` 和 `?/init.lua` 模板放在标准 `package.path` 的默认路径之前，加载与缓存遵循 Lua。引导脚本以可让出的 Lua 调用标准搜索器和加载器，模块顶层可以直接调用 `muxora.*`，用法见 [Lua 使用说明](../../docs/development/lua-guide.md#标准库与模块)。`ready()` 为真后可调用 `run()` 或通过命令注册表执行脚本命令。脚本线程等待 UI 回执时让出，GUI 线程不等待 Lua 执行结束。

`scriptError` 与 `logMessage` 由宿主接收和展示。`unload()` 撤销脚本资源并取消旧任务；重新 `initialize()` 建立新一代 VM。运行时的重置完成通知核验最新代次，延迟到达的旧通知不会重新启动初始化。相同 ID 的工具条更新定义且保留布局，不再声明的贡献被移除。宿主应在清理脚本贡献前保存需要保留的窗口布局。

`windows.state(id)` 查询最小化、最大化和全屏范围；`windows.set_state(id, state)` 接受 `normal`、`maximized`、`minimized`，使用与菜单相同的窗口命令。`ui.command_palette(id, options)` 和 `ui.window_selector(id, options)` 的 `options` 接受 `modal`、`dismiss_on_outside`，默认分别为 `false`、`true`。面板打开后脚本继续执行，不使用嵌套事件循环。

`groups.to_tab`、`groups.into_tab`、`tabs.into_tab`、`tabs.to_group` 和 `panes.to_tab` 对应窗口模块的层级转换 API，保留 Pane ID 与原内容。并入操作明确指定目标标签及可选目标窗格，方向为 `left`、`right`、`top`、`bottom`，默认 `right`；组来源限中央或固定区域的可见组。详细参数与示例见 [Lua 使用说明](../../docs/development/lua-guide.md#标签组标签与窗格转换)。全屏、禅模式保持运行时状态，不随 `layout.snapshot()` 保存或恢复。

标题裁切通过 `tabs.appearance()`／`tabs.set_appearance()` 或 `settings.get()`／`settings.update()` 控制：`clip_tab_titles`、`maximum_tab_title_width` 用于普通及固定标签栏，`clip_sidebar_titles`、`maximum_sidebar_title_width` 用于非固定收纳条。两套值独立修改，和 C++、设置页共用校验与持久化，见 [Lua 使用说明](../../docs/development/lua-guide.md#标题裁切)。

## 组件扩展

组件可以分别贡献 UI 命令和 Lua 方法：

- `Command::menuPath` / `menuOrder` 控制主菜单，`contextMenus` 控制 Tab、Pane、Group、Window 右键菜单及 `contentType` 过滤。菜单、工具条和快捷键都引用稳定命令 ID。
- `CommandToolbar` 与 `StatusItem` 通过 WindowManager 注册；定义不保存 Qt 控件指针，窗口的排列和显隐仍由窗口模块维护。
- `registerService(method, handler, owner)` 增加组件命名空间的方法，例如未来的 `ssh.connect`，由 Lua 的 `muxora.call(method, ...)` 调用。名称必须带命名空间且不能覆盖内置前缀或已有方法；同一线程的 `owner` 销毁时自动移除服务。不传 owner 时由提供者调用 `removeService()`。
- `setServiceHandler()` 设置应用级兜底服务处理器。内置 API 和已注册组件方法优先，未识别方法才交给它；宿主负责返回明确错误。
- `publishEvent(name, payload, context)` 发布 JSON 风格值事件，回调收到 `{ name, data }` 及上下文。组件负责定义自己的事件字段、粒度和连接归属。

服务处理器在宿主线程执行，只应做短操作；耗时工作交给组件自己的异步设施，以请求 ID 和完成事件返回。组件提供者负责移除命令、工具条、状态项和事件源，不能把业务对象交给 Lua 长期持有。示例方法名不表示当前已接入 SSH／SFTP。

## 数据与生命周期

跨线程仅传 nil、布尔、有限数值、UTF-8 字符串、数组、对象及 UUID 字符串。循环表、混合／稀疏数组、函数和 userdata 不能作为普通宿主值传输。注册函数单独保存在当前 VM；注销、初始化失败、重载和退出均释放所属引用，旧代回执不能进入新 VM。

脚本可用的标准库、模块加载和停止方式见 [Lua 使用说明](../../docs/development/lua-guide.md#标准库与模块)，决定见 [ADR 0008](../../docs/adr/0008-full-standard-library-for-local-scripts.md)。`execute`／`initialize`／`run` 的直接源代码参数是 UTF-8 编码的 Qt 文本。

`package.cpath` 初始为空。Lua 按 C++ 编译并静态链接，没有供外部模块链接的 Lua DLL，因此不支持 C 模块；`package.loadlib` 保留并返回标准加载错误。这个限制来自构建和 ABI，不是权限控制。本模块供用户选择运行的本地脚本使用，不做进程级隔离。

`os.exit` 和 `muxora.app.exit()` 转交宿主的 `app.exit` 服务。Muxora 的服务执行正常退出命令，先保存再申请关闭许可，忽略退出码及 `close` 参数；关闭被拒绝时返回 `false`。独立宿主未提供服务时报告未知方法错误，不结束宿主进程。`print` 经 `logMessage` 写入脚本日志；`io.write` 保持标准输出语义，不显示在图形界面。

需要标准库处理中文路径的 Windows 宿主，应在自身应用清单中声明 UTF-8 进程代码页；模块不改变宿主的进程代码页。Muxora 的应用和相关应用测试使用该声明，Windows 10 1903 之前的系统会忽略它。

宿主可调用 `runtime()->cancelAll()` 请求停止；重载、卸载和退出也会请求停止。Lua 在下一个检查点响应，`pcall` 和用户钩子不能吞掉请求。标准库阻塞 C 调用返回前无法停止，其他脚本需要等待，GUI 不等待。运行时提供 `isRunning()`／`isLongRunning()`、状态变化信号和 `resetBlocked`；Muxora 在连续执行超过 2 秒时显示可点击停止的状态项，并在重载仍被阻塞时显示提示。析构最多等待线程约 2 秒，仍在阻塞时让工作线程保留自身状态直至返回，不强杀线程。

跨线程队列、日志和宿主界面注册都有上限，默认值见 [Lua 使用说明](../../docs/development/lua-guide.md#队列传输与资源计数)，宿主可以通过 [RuntimeLimits](../lua-runtime/include/lua_runtime/script_runtime.hpp) 调整。

事件订阅和计时器的语义见 [Lua 使用说明](../../docs/development/lua-guide.md#状态项与定时回调)；宿主用 `publishEvent` 注入事件。

## 使用与验证

用户脚本、快捷键、工具条和事件示例见 [Lua 使用说明](../../docs/development/lua-guide.md)，架构约束见 [设计文档](../../docs/design/lua-scripting-design.md)。

本模块的 CTest 入口为 `window-system.lua`、`window-system.lua-lifecycle`，运行时入口为 `lua-runtime.runtime`；应用接入由 `muxora.scripting` 覆盖。测试以程序断言检查脚本结果、上下文、资源清理、注册重载和窗口内容身份，不依赖截图检查。

生命周期场景在完整窗口和浮动窗口上运行 24 代重载、768 个有序事件。每代断言 3 个回调，以及各 1 个命令、工具条、状态项、计时器和订阅，排空后的事件积压为零；同时核验工具条布局、内容实例及预热后的 QObject 类型数量。卸载后 VM 与注册资源归零，原生内存和句柄另作记录。窗口未显示，该检查不代表实际应用峰值或长期资源验收。实际执行与通过情况以 [预览状态](../../docs/development/window-system-preview.md) 和测试日志为准。

[`tests/fixtures/external-host`](tests/fixtures/external-host) 使用已安装的 WindowSystem 包，加上 LuaRuntime／WindowSystemLua 源码 target，验证组件服务、脚本命令、标题、内容身份和动态工具区。它不链接 Muxora 应用，也不作为另一个用户产品交付。
