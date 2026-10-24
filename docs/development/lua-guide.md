# 使用 Lua 自定义 Muxora

从“工具 → Lua → 打开 Lua 配置文件”进入 `User/init.lua`，保存后选择“重新加载 Lua 配置”。没有配置文件时不会自动创建或执行其他目录的脚本。“运行 Lua 脚本…”可以运行一次输入的代码。

普通安装的 `User` 在系统用户配置目录，便携运行在 `data/User`；指定 `--state-dir` 时使用该隔离目录的 `User`。`init.lua` 和“运行 Lua 脚本”的文件入口使用 UTF-8 文本；模块默认放在 `User/lua`。

首次从菜单创建配置文件时，模板的说明和示例命令标题使用当前界面语言。切换语言不会修改已有 `init.lua`。Muxora 自有的接口诊断随界面语言显示；脚本输出、用户抛出的错误及 Lua 标准错误保留原文。

## 标准库与模块

本地脚本可以使用完整的 Lua 5.5 标准库，包括 `io`、`os`、`debug`、`package`、`coroutine`、`load`、`loadfile`、`dofile`、`string.dump` 和 `collectgarbage`。`load` 可以加载二进制块。文件读写和 shell 命令直接使用标准库，不需要申请权限；脚本拥有与当前用户相同的权限。决定见 [ADR 0008](../adr/0008-full-standard-library-for-local-scripts.md)。

`require` 使用标准 Lua 搜索器和缓存。初始化时，`package.path` 在默认路径之前加入：

```text
User/lua/?.lua
User/lua/?/init.lua
```

例如 `require('workspace')` 查找 `workspace.lua` 或 `workspace/init.lua`，`require('workspace.tools')` 查找 `workspace/tools.lua` 或 `workspace/tools/init.lua`。脚本可以修改 `package.path`、使用 `package.preload`，或修改 `package.loaded` 缓存；重新加载配置会建立新 VM，旧缓存随之清除。

标准 `require`、`loadfile` 和 `dofile` 按 Lua 自身规则读取文件，没有原来的目录边界、严格 UTF-8 检查或 1 MiB 上限。`init.lua` 和应用的一次性脚本入口仍是经过 UTF-8 解码的文本传输，默认上限为 64 MiB；这个上限不限制标准库在脚本内部加载文件。

`package.cpath` 初始为空。当前 Lua 按 C++ 编译并静态链接，没有供外部模块链接的 Lua DLL，因此不支持 C 模块；这是构建与 ABI 条件，不是文件权限限制。`package.loadlib` 保留，加载失败时返回标准错误。纯 Lua 模块可使用文本或与嵌入版本匹配的字节码。

模块顶层可以直接调用 `muxora.*`，例如加载模块时注册命令；宿主回执到达后继续加载，完成结果按标准语义缓存：

```lua
-- User/lua/workspace.lua
muxora.commands.register({ id = 'user.module_action', title = '模块命令' }, function()
  muxora.commands.execute('host.newTab')
end)
return { title = '模块命令' }
```

```lua
-- User/init.lua
local workspace = require('workspace')
```

`require` 保留标准搜索器、加载器参数、返回值和缓存规则，也支持自定义 Lua 搜索器中的宿主调用。不额外检测循环加载；有循环依赖时可按 Lua 惯例先写入 `package.loaded`。只有实际不可让出的调用位置（如 `tostring` 的 `__tostring` 回调）不能等待宿主；这些位置的调用会在提交宿主请求前失败。

用户协程中的 `muxora.*` 调用会逐层转交宿主并恢复，普通 `coroutine.yield` 的返回值遵循 Lua 语义。`debug.sethook` 和 `debug.gethook` 管理用户钩子；清除用户钩子不影响运行时的停止检查。

在支持 UTF-8 进程代码页的 Windows 10 1903 及后续版本上，应用清单让标准库的文件路径和环境变量按 UTF-8 处理，可用于中文路径。更早系统会忽略这项声明。

`print` 写入 Muxora 的脚本日志。`io.write` 写到进程标准输出，不显示在界面中；普通图形启动没有控制台输入，`io.read` 不会弹出输入框。

`os.exit(code, close)` 和 `muxora.app.exit()` 都走应用的正常退出命令，先保存工作区，再申请关闭所有窗口。退出码和 `close` 参数忽略；关闭许可拒绝时返回 `false`，应用继续运行。独立使用窗口模块的宿主需要提供 `app.exit` 服务；未提供时调用报“未知宿主方法”，不会直接结束进程。

## 停止脚本

脚本没有执行时间、指令数和 Lua 内存预算。耗时计算不会自动被中止；某次连续执行超过 2 秒时，状态栏显示“Lua 脚本运行中”，点击它或执行“工具 → Lua → 停止正在运行的 Lua 脚本”（`lua.stop`）可请求停止。脚本结束后提示消失。

停止请求在下一个 Lua 检查点生效，嵌套协程、`pcall`、`xpcall` 和用户 debug 钩子不能吞掉请求。重新加载、卸载和程序退出也会请求停止；运行时仍可执行之后提交的新任务。

所有脚本共用一个线程。`os.execute`、`io.popen` 或其他阻塞的 C 调用返回前无法停止，其他脚本也需要等待，界面继续响应。此时重载会提示“脚本仍在运行，已请求停止”，调用返回后再完成重载。程序退出最多等待 Lua 线程约 2 秒，然后继续退出，不强杀线程。

## 快捷键与命令

```lua
muxora.commands.register({
  id = 'user.prepare_workspace',
  title = '准备工作区',
  context_menus = {
    { target = 'tab', path = { 'Lua' } }
  }
}, function(context)
  assert(muxora.commands.execute('host.newTab'))
  assert(muxora.commands.execute('pane.split.right'))
end)

muxora.keymap.set({ 'ctrl+alt+k ctrl+alt+j', 'ctrl+alt+f8' },
                  'user.prepare_workspace')
```

每个序列最多四段，可混用组合键与普通字母／数字。后续按键可以与独立快捷键相同；例如 `ctrl+k x`、`ctrl+j x`、`ctrl+k y` 和独立的 `x` 可以同时存在。

`keymap.set` 默认拒绝完整序列重复占用，以及短序列恰好是长序列开头的冲突，同一命令的多个绑定也检查后者。传入 `{ replace = true }` 会移除冲突的旧绑定，卸载脚本会还原。一次提供的按键列表内部不能包含前缀冲突，开启替换也会整次拒绝，不部分修改其他命令。同一命令再次设置完全相同的序列可更新其 `args`，不会增加重复绑定。

命令描述可用 `menu = { '工具', 'Lua' }` 和 `order` 指定主菜单路径与顺序。`context_menus` 接受 `tab`、`pane`、`group`、`window` 目标、子菜单 `path`、`order` 和可选 `content_type` 过滤条件。脚本提供的标题、菜单名和通知文字按原文显示，不会自动翻译。

也可以把函数直接传给 `keymap.set`，并通过第三个参数指定稳定的 `id` 和 `title`。用户快捷键编辑器仍可以覆盖脚本默认值。对一次性脚本，给 `lua.run` 绑定参数 `{ "file": "my-script.lua" }` 或 `{ "code": "..." }`；相对路径从 `User` 解析。不要同时提供 `code` 和 `file`。

回调参数 `context` 是触发时捕获的窗口／组／标签／窗格 ID 和参数表；`muxora.context()` 返回来源窗口当前的活动目标。`commands.execute(id, args, context)` 的第三参数可明确指定捕获目标；省略时使用当前活动目标，因此可以连续“新建标签 → 操作新标签”。目标消失时报告失败，不默默切到别的窗口。

## 当前 API 分组

| 分组 | 操作 |
| --- | --- |
| `commands` | 列出、注册、注销和执行命令，复用菜单、快捷键与命令面板路径。 |
| `windows` | 查询、新建、关闭、激活、最小化／最大化／还原、升降级、窗口族、几何、标题、栏显隐、全屏和禅模式。 |
| `tabs` / `groups` / `panes` | 查询、创建、分割、重命名、移动、合并、交换、组／标签／窗格转换、拆出、关闭、颜色、图标及附加栏。 |
| `sidebar` | 收纳、固定、整侧固定及独立记忆的临时展开宽高。 |
| `toolbars` / `status` | 注册命令工具条和状态项，调整位置、宽度、弹性、按钮、文本和显隐。 |
| `layout` | `snapshot()` / `restore(snapshot)`，保持宿主内容工厂与布局校验。 |
| `settings` / `theme` | 已有行为、标签外观、颜色预设、主题令牌和尺寸；语言请求重启生效。 |
| `ui` / `app` | 非模态通知、设置／命令面板、正常退出、打开外部程序、beep 和 Windows 本地 WAV。 |
| `events` / `timers` | 订阅宿主事件、自定义 `user.*` 事件、一次／重复计时器及取消。 |
| `clock` / `runtime` | 本地／UTC 时间、日期字段，以及 Lua 分配、队列和注册资源计数。 |

接口传值和 UUID 字符串，不传 Qt 对象。省略目标 ID 的操作使用来源窗口当前目标；显式有效 ID 可以操作其他窗口。关闭仍经过宿主关闭许可。命令返回的布尔值表示操作是否接受；Lua 命令本身异步执行，后续失败会在诊断中报告。

## 标题裁切

普通及固定标签栏使用 `clip_tab_titles`／`maximum_tab_title_width`，四边非固定收纳条独立使用 `clip_sidebar_titles`／`maximum_sidebar_title_width`。两套值均可通过标签外观或设置接口读取、修改；单位、默认值、范围见 [标签外观设置](../design/settings-and-commands-design.md#31-标签外观与颜色)。只限制显示范围，不修改标题字符串，提示和完整标签列表仍显示完整标题。

```lua
assert(muxora.tabs.set_appearance({
  clip_tab_titles = true,
  maximum_tab_title_width = 120,
  clip_sidebar_titles = true,
  maximum_sidebar_title_width = 80
}))
-- The settings API edits the same appearance; sidebar values stay unchanged.
assert(muxora.settings.update({ clip_tab_titles = false }))
```

非法类型、非整数或越界长度会报告错误，不部分应用其他修改；关闭开关保留设置值。`layout.snapshot()`／`layout.restore()` 同样保存和恢复此策略。

## 窗口状态与浮动面板

| 调用 | 参数与结果 |
| --- | --- |
| `windows.state(window_id)` | 返回 `{ minimized, maximized, mode }`；目标窗口不存在时返回 `nil`。 |
| `windows.set_state(window_id, state)` | `state` 为 `'normal'`、`'maximized'` 或 `'minimized'`，返回操作是否接受。 |
| `windows.set_mode(window_id, mode)` | 设置显示范围：`'normal'`、`'window'`、`'group'` 或 `'pane'`。 |
| `ui.command_palette(window_id, options)` | 打开命令面板，`options` 可配置模态与外点关闭行为。 |
| `ui.window_selector(window_id, options)` | 打开窗口选择面板，使用相同 `options`。 |

`window_id` 传 `nil` 时使用来源窗口。需要传后续参数时保留这个 `nil` 位置，例如：

```lua
assert(muxora.windows.set_state(nil, 'maximized'))
muxora.ui.command_palette(nil, {
  modal = false,
  dismiss_on_outside = true
})
```

最小化／最大化／还原使用 `window.minimize`、`window.maximize`、`window.restore` 命令，与菜单和快捷键共用执行路径。最大化和还原会先退出全屏范围；最小化保留当前范围，以便恢复。`state()` 的 `mode` 表示全屏范围，和 `minimized`、`maximized` 分开查询。

面板默认 `modal = false`、`dismiss_on_outside = true`，即非模态且点击其他区域关闭。两项只接受布尔值，不识别的选项会报错。打开面板后 Lua 可以继续执行；即使设为模态，也不会启动嵌套事件循环或等待用户关掉面板。

布局快照只面向当前格式，不导入或转换旧配置；缺失必需结构时整次恢复失败。临时展开尺寸继续按左右／上下两套独立策略读写，必须指定方向。

全屏和禅模式是本次运行的临时状态，`layout.snapshot()` 不保存它们，恢复布局或重启不会重新进入全屏。正常几何、最大化状态、栏显隐和启动禅模式的默认范围继续保存。

## 钉住标签

`tabs.sticky(tab_id, sticky)` 钉住或取消钉住标签，`sticky` 省略时为 `true`，`tab_id` 为 `nil` 时使用当前标签；非固定收纳条里的标签不能钉住，返回 `false`。`groups.list()` 每个组的 `sticky_tabs` 是钉住标签的数量，它们就是 `tabs` 列表开头的那几个。规则见 [交互细则 · 钉住标签](../design/window-interaction-design.md#钉住标签)。

```lua
assert(muxora.tabs.sticky())        -- 钉住当前标签
assert(muxora.tabs.sticky(nil, false))
```

钉住标签的尺寸是标签外观字段 `sticky_tab_sizing`，取 `'normal'`、`'compact'` 或 `'shrink'`；布尔字段 `sticky_tabs_on_separate_row` 让钉住标签单独占一行；`prevent_sticky_close` 取 `'keyboard_and_mouse'`、`'keyboard'`、`'mouse'` 或 `'never'`，决定关闭快捷键和中键是否放过钉住标签。三者都能用 `tabs.appearance()`／`tabs.set_appearance()` 和 `settings.get()`／`settings.update()` 读写，含义见 [标签外观设置](../design/settings-and-commands-design.md#31-标签外观与颜色)；类型或取值不对时报告错误，不部分应用。

```lua
assert(muxora.tabs.set_appearance({ sticky_tab_sizing = 'compact' }))
```

## 标签组、标签与窗格转换

| 调用 | 结果 |
| --- | --- |
| `groups.to_tab(group_id, direction)` | 将组内所有标签的窗格树合成一个多窗格标签，返回保留的活动 Tab ID。 |
| `groups.into_tab(group_id, target_tab_id, target_pane_id, direction)` | 将整组内容并入组外目标标签的指定窗格旁，返回是否接受。 |
| `tabs.into_tab(tab_id, target_tab_id, target_pane_id, direction)` | 将来源标签的完整窗格树并入另一标签，返回是否接受。 |
| `tabs.to_group(tab_id, direction)` | 每个 Pane 转为一个单窗格标签，返回目标 Group ID；原组还有其他标签时在指定方向新建组，否则复用原组。 |
| `panes.to_tab(pane_id)` | 将窗格提取为同组标签，返回 Tab ID；原标签只有该窗格时返回原 Tab ID。 |

`direction` 支持 `'left'`、`'right'`、`'top'`、`'bottom'`，省略时为 `'right'`。来源 ID 为 `nil` 时使用当前上下文；目标标签必须明确指定，目标窗格为 `nil` 时使用目标的活动窗格。例如，将当前标签并入另一个标签的活动窗格左侧：

```lua
-- target_tab_id 来自 tabs 查询结果，且必须与来源标签不同。
assert(muxora.tabs.into_tab(nil, target_tab_id, nil, 'left'))
```

这些操作移动原 Pane 和内容，不重新创建连接。组来源必须是中央或固定区域中的可见组；目标可以跨窗口，必须仍然有效，目标窗格必须属于指定标签。整组来源不能并入自己的标签。转换后活动内容继续保留，来源空组或空浮窗按既有规则清理；来源标签附加栏转移到其活动窗格，与原专属栏组合并采用逐窗格显示。取消 Chrome 式标签集合的决定不受影响，这里的组仍是拥有标签栏的分割区域。

## 状态项与定时回调

```lua
muxora.status.register({ id = 'user.clock', text = '等待更新' })
local clock_timer = muxora.timers.every(1000, function()
  local now = muxora.clock.now()
  muxora.status.update('user.clock', now.local_time)
end)

-- 不再需要时：muxora.timers.cancel(clock_timer)
```

命令也可以放入自定义工具条，按钮通过同一个命令 ID 执行：

```lua
muxora.toolbars.register({
  id = 'user.workspace_tools',
  title = '工作区操作',
  commands = { 'user.prepare_workspace', 'host.newTab' },
  area = 'top',
  row = 1
})
```

`toolbars.state(window_id, toolbar_id)` 查询窗口内的布局；`move`、`width`、`elastic`、`visible` 和 `item_visible` 修改布局与按钮。自定义状态项显示在完整窗口的全局状态栏，轻量浮窗沿用其无全局状态栏的设计。

`after(ms, fn)` 运行一次，`every(ms, fn)` 按固定间隔运行；最短 10 ms。回调未完成时不会为同一个计时器排队积压。按月／年、时区和夏令时的日历规则尚未提供，不应把一天固定毫秒数当作日历重复。

未捕获错误会停用对应计时器或事件订阅并报告一次，避免持续报错。需要重试的业务可以在回调内用 `pcall` 处理预期失败，再按自己的规则继续。

```lua
muxora.events.on('terminal.output', function(context, event)
  if event.data.text:find('error', 1, true) then
    muxora.ui.notify({ title = '输出提醒', text = event.data.text })
  end
end)
```

以上订阅需要真实终端组件发布 `terminal.output`；当前预览尚未提供终端后端。宿主可以用同一入口接入连接、传输和其他业务事件，不需要修改 Lua 解释器。

`events.on` 返回订阅 ID，`events.off(id)` 撤销它；脚本可用 `events.emit('user.name', data)` 发布自己的事件。同一订阅串行处理；`layout.changed` 允许合并，其余事件按顺序有界排队。队列或数据超过传输上限会报告错误并撤销对应订阅，不会静默丢弃。高流量组件应按业务需要控制事件粒度。

`app.launch({ program = 'notepad.exe', args = {} })` 按独立参数打开程序，返回启动请求 ID；结果通过 `app.launch.completed` 事件返回。程序独立运行，Muxora 不在重载或退出时强杀它，也不捕获其输出。声音可用 `app.play_sound('beep')` 或 Windows 本地 WAV 路径。

## 组件扩展与重载

脚本命令使用 `user.*` ID。动态命令可以贡献主菜单路径和按内容类型过滤的右键菜单；自定义工具条引用命令 ID。未来 SSH／SFTP 等 C++ 组件可以通过公开命令、工具条／状态项 API 与 `ScriptHost::registerService` 注册自己的方法，例如由脚本调用 `muxora.call('ssh.connect', options)`；本例名称不代表当前已有 SSH 实现。

脚本重载会回收旧函数、计时器、订阅和不再声明的界面贡献。相同 ID 的工具条更新定义，保留各窗口及窗口形态的排列；状态项保留显隐。用户快捷键覆盖在新默认值注册后重施，重载中间状态不会写回用户文件。

本地脚本由用户选择运行，不做权限管理，也不作为运行恶意脚本的安全边界。初始化失败会清理本代注册资源，已经完成的普通 UI 操作不作事务回滚。

## 队列、传输与资源计数

宿主快照中的 JSON `null` 在 Lua 中使用不可变哨兵保留，区别于删除表项的 `nil`。直接把它放回列表或对象即可往返；空项不会让数组缩短。不接受其他 userdata 或指针。

为避免阻塞的宿主或日志接收者积压内存，运行时保留以下默认上限。它们约束跨线程数据和待处理资源，不限制 Lua 内部的计算或分配：

| 项目 | 默认上限 |
| --- | --- |
| 直接提交的源代码／单个传输字符串 | 各 64 MiB |
| 排队输入与待投递结果保留数据 | 合计 256 MiB，按保守传输记账计算 |
| 已接收且尚未交付结果的任务 | 128 个 |
| 保留的 Lua 回调 | 1,000,000 个 |
| 一次转换的值节点／嵌套深度 | 1,000,000 个／32 层 |
| 普通宿主回执等待 | 30 秒 |
| 单任务日志／单条日志 | 128 条／16 KiB |
| 等待宿主显示的日志 | 256 条 |

界面贡献另有限制：每代脚本最多 256 个命令、512 个快捷键目标、32 条工具条、64 个状态项、128 个事件订阅和 128 个计时器。事件积压最多 128 项、256 MiB；超过上限会报告并停用该订阅。窗口内容仍由窗口模块维护，不通过复制完整内容保存订阅状态。

`runtime.stats()` 提供 Lua 当前／峰值字节、待处理任务和各类注册资源计数。这些数字不包含 Qt 界面、应用缓存或外部程序占用，不能代表整个进程的内存。

应用部署检查可以独立运行 `pwsh tools/tests/lua-deployment-test.ps1`。它使用隔离状态目录和已部署的程序，断言 Lua 启动、命令、保存与重启后的工具区和栏显隐；不计算校验值或读取截图。执行前应关闭固定部署目录中的程序。
