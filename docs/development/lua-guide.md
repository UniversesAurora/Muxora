# 使用 Lua 自定义 Muxora

从“工具 → Lua → 打开 Lua 配置文件”进入 `User/init.lua`，保存后选择“重新加载 Lua 配置”。没有配置文件时不会自动创建或执行其他目录的脚本。“运行 Lua 脚本…”可以运行一次输入的代码。

普通安装的 `User` 在系统用户配置目录，便携运行在 `data/User`；指定 `--state-dir` 时使用该隔离目录的 `User`。脚本使用 UTF-8。模块放在 `User/lua`，例如 `User/lua/workspace.lua` 可以用 `require('workspace')` 加载；只支持这里的纯 Lua 文本模块。

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

每个序列最多四段，可混用组合键与普通字母／数字。后续按键可以与独立快捷键相同；完整序列的前缀冲突需要明确处理。`keymap.set` 默认拒绝冲突，传入 `{ replace = true }` 可以替换冲突绑定，卸载脚本会还原原绑定。

命令描述可用 `menu = { '工具', 'Lua' }` 和 `order` 指定主菜单路径与顺序。`context_menus` 接受 `tab`、`pane`、`group`、`window` 目标、子菜单 `path`、`order` 和可选 `content_type` 过滤条件。脚本提供的标题、菜单名和通知文字按原文显示，不会自动翻译。

也可以把函数直接传给 `keymap.set`，并通过第三个参数指定稳定的 `id` 和 `title`。用户快捷键编辑器仍可以覆盖脚本默认值。对一次性脚本，给 `lua.run` 绑定参数 `{ "file": "my-script.lua" }` 或 `{ "code": "..." }`；相对路径从 `User` 解析。不要同时提供 `code` 和 `file`。

回调参数 `context` 是触发时捕获的窗口／组／标签／窗格 ID 和参数表；`muxora.context()` 返回来源窗口当前的活动目标。`commands.execute(id, args, context)` 的第三参数可明确指定捕获目标；省略时使用当前活动目标，因此可以连续“新建标签 → 操作新标签”。目标消失时报告失败，不默默切到别的窗口。

## 当前 API 分组

| 分组 | 操作 |
| --- | --- |
| `commands` | 列出、注册、注销和执行命令，复用菜单、快捷键与命令面板路径。 |
| `windows` | 查询、新建、关闭、激活、升降级、窗口族、几何、标题、栏显隐、全屏和禅模式。 |
| `tabs` / `groups` / `panes` | 查询、创建、分割、重命名、移动、合并、交换、拆出、关闭、颜色、图标及附加栏。 |
| `sidebar` | 收纳、固定、整侧固定及独立记忆的临时展开宽高。 |
| `toolbars` / `status` | 注册命令工具条和状态项，调整位置、宽度、弹性、按钮、文本和显隐。 |
| `layout` | `snapshot()` / `restore(snapshot)`，保持宿主内容工厂与布局校验。 |
| `settings` / `theme` | 已有行为、标签外观、颜色预设、主题令牌和尺寸；语言请求重启生效。 |
| `ui` / `app` | 非模态通知、设置／命令面板、打开外部程序、beep 和 Windows 本地 WAV。 |
| `events` / `timers` | 订阅宿主事件、自定义 `user.*` 事件、一次／重复计时器及取消。 |
| `clock` / `runtime` | 本地／UTC 时间、日期字段，以及 Lua 分配、队列和注册资源计数。 |

接口传值和 UUID 字符串，不传 Qt 对象。省略目标 ID 的操作使用来源窗口当前目标；显式有效 ID 可以操作其他窗口。关闭仍经过宿主关闭许可。命令返回的布尔值表示操作是否接受；Lua 命令本身异步执行，后续失败会在诊断中报告。

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

`events.on` 返回订阅 ID，`events.off(id)` 撤销它；脚本可用 `events.emit('user.name', data)` 发布自己的事件。同一订阅串行处理；`layout.changed` 允许合并，其余事件按顺序有界排队。队列或数据超过预算会报告错误并撤销对应订阅，不会静默丢弃。高流量组件应按业务需要控制事件粒度。

`app.launch({ program = 'notepad.exe', args = {} })` 按独立参数打开程序，返回启动请求 ID；结果通过 `app.launch.completed` 事件返回。程序独立运行，Muxora 不在重载或退出时强杀它，也不捕获其输出。声音可用 `app.play_sound('beep')` 或 Windows 本地 WAV 路径。

## 组件扩展与重载

脚本命令使用 `user.*` ID。动态命令可以贡献主菜单路径和按内容类型过滤的右键菜单；自定义工具条引用命令 ID。未来 SSH／SFTP 等 C++ 组件可以通过公开命令、工具条／状态项 API 与 `ScriptHost::registerService` 注册自己的方法，例如由脚本调用 `muxora.call('ssh.connect', options)`；本例名称不代表当前已有 SSH 实现。

脚本重载会回收旧函数、计时器、订阅和不再声明的界面贡献。相同 ID 的工具条更新定义，保留各窗口及窗口形态的排列；状态项保留显隐。用户快捷键覆盖在新默认值注册后重施，重载中间状态不会写回用户文件。

默认不开放系统 shell、阻塞 IO、原生动态模块、debug 或未约束协程。请用宿主服务实现外部效果；VM 与传输资源都有预算。它用于用户信任的本地配置，不是运行恶意脚本的安全边界。初始化失败会清理本代注册资源，已经完成的普通 UI 操作不作事务回滚。

默认预算为 32 MiB Lua 分配、128 个待处理任务、8 MiB 任务传输保留数据、1024 个函数引用；源代码和单个字符串最多 1 MiB，值最多 32 层、4096 个节点。`runtime.stats()` 提供 Lua 当前／峰值字节、待处理任务和各类注册资源计数。这些数字不包含 Qt 界面、应用缓存或外部程序占用，不能用来代表整个进程的内存。

应用部署检查可以独立运行 `pwsh tools/tests/lua-deployment-test.ps1`。它使用隔离状态目录和已部署的程序，断言 Lua 启动、命令、保存与重启后的工具区和栏显隐；不计算校验值或读取截图。执行前应关闭固定部署目录中的程序。
