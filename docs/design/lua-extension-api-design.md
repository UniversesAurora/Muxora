# Lua 扩展 API 设计

状态：设计稿，2026-10-20。实现步骤见 [阶段 9](../roadmap/implementation/phase-9-lua-extension-api.md)。现有能力见 [Lua 使用说明](../development/lua-guide.md)，运行时设计见 [Lua 脚本与组件扩展](lua-scripting-design.md)。

## 1. 目标

Lua 是 Muxora 的主要扩展方式。目标是让脚本能做到用户在界面里能做的几乎所有事，并且能补充界面里没有的功能：

- 弹出对话框（模态、非模态）、输入框、选择列表、系统通知；
- 打开新窗口、标签和窗格，按需要排布（现有窗口 API 已经能做大部分）；
- **自定义界面**：用 Lua 描述一个界面里有哪些控件、怎么排布，放在工作区标签、侧栏或对话框中，响应用户操作；
- **读写终端**：读取任意标签、任意窗格中终端某个位置或某个范围的文字，订阅输出，发送输入；
- 操作会话、文件传输、剪贴板、持久化存储、网络请求、外部程序。

## 2. 不变的原则

1. **Lua 不在界面线程运行**。解释器在独立线程（现有设计），脚本卡住或死循环不会冻结界面；Qt 对象不暴露给 Lua，脚本只拿到 ID 和值。
2. **界面由宿主创建**：自定义界面用“描述”（Lua 表）表示，宿主按描述创建 Muxora 自己的控件。因此脚本做出来的界面与 Muxora 其他界面外观一致，跟随主题，符合 [界面设计规范](ui-design-system.md)，键盘操作和无障碍名称也一致。
3. **有上限**：每类资源（控件数量、更新频率、订阅数、读取的文字量、存储大小）都有上限，超出时明确报错，不会拖垮程序。
4. **重载安全**：脚本重载或卸载时，它创建的界面、订阅、计时器、对话框全部撤销（现有机制扩展）。
5. **本地脚本可信**：用户自己放在 `User/` 下的脚本默认拥有全部能力，不弹权限确认（用户自己写的脚本不需要防着）。以后如果支持安装别人发布的扩展包，再为扩展包加权限声明。

## 3. 运行时的两项改动

当前运行时同一时间只运行一个任务，任务等待宿主回复时其他任务排队；宿主回复超过 30 秒会中止任务。等待用户回答对话框的脚本会让全部 Lua 功能停住，并且很快被中止。改为：

1. **多个任务可以同时挂起**：任务等待宿主回复时让出执行权，其他任务继续运行。同时挂起的任务数有上限（默认 64），各自有自己的协程和截止时间。执行预算（指令数、时间）只计算真正执行 Lua 的时间，挂起等待的时间不计。
2. **交互调用不设超时**：宿主把“等待用户操作”的调用（对话框、输入框、选择列表等）标记为交互调用，不受 30 秒限制；脚本重载、卸载或对应窗口关闭时取消，Lua 端收到错误 `"cancelled"`。

同一个功能提供两种写法，脚本按习惯选择：

```lua
-- 等待写法：在当前任务中等待用户回答（任务挂起，不阻塞其他脚本和界面）
local answer = muxora.ui.confirm{ title = '重启服务', text = '确定在 3 台服务器上重启 nginx？' }
if answer then ... end

-- 回调写法：立即返回，用户回答后调用回调
muxora.ui.confirm({ title = '重启服务', text = '...' }, function(answer) ... end)
```

## 4. 弹窗与通知

| API | 说明 |
| --- | --- |
| `ui.notify{ title, text }` | Windows 系统通知（[界面规范 §4.4](ui-design-system.md#44-通知使用-windows-系统通知)）；可选 `on_click` 回调 |
| `ui.message{ title, text, severity, buttons, modal }` | 统一对话框（界面规范 §4.3）；`buttons` 为按钮文字列表，返回被点按钮的序号；`modal` 取 `false`（默认）、`"window"`（只挡住当前窗口）、`"app"`（挡住整个程序） |
| `ui.confirm{ title, text, modal }` | 确认／取消，返回布尔值 |
| `ui.input{ title, prompt, value, password, validate }` | 快速输入的输入框模式（界面规范 §4.1）；`validate` 是 Lua 函数，返回错误文字或 `nil`；返回输入值，取消返回 `nil` |
| `ui.pick{ title, items, multiple, placeholder }` | 快速输入的列表模式；`items` 为 `{ label, description, detail, id }`；返回选中项 |
| `ui.dialog{ title, content, buttons, modal, size }` | 内容为自定义界面描述（§5）的对话框 |
| `ui.status(text, timeout_ms)` | 在状态栏显示一条消息 |

## 5. 自定义界面

### 5.1 放在哪里

| 位置 | API | 说明 |
| --- | --- | --- |
| 工作区标签 | `ui.panel{ id, title, content, icon }` 返回标签 ID | 和其他标签一样可以拆分、拖动、浮动、固定到侧栏、跟随布局保存（保存的是面板 ID，重启后由脚本重新提供内容） |
| 对话框 | `ui.dialog{ ... }` | 见 §4 |
| 工具条、状态栏、右键菜单 | 现有 API | 已经支持 |

一个面板就是一个普通的工作区标签，所以“打开一个新窗口，左边放会话列表、右边放自定义面板、底下放两个终端”这样的布局，用现有的 `windows.new`、`tabs.new`、`panes.split`、`groups.move` 加上 `ui.panel` 就能组合出来。

### 5.2 怎么描述

界面用嵌套的 Lua 表描述，每个节点有类型、可选的 `id` 和属性：

```lua
local view = {
  type = 'column', spacing = 8, children = {
    { type = 'row', children = {
        { type = 'label', text = '主机' },
        { type = 'input', id = 'host', placeholder = 'user@host', stretch = 1 },
        { type = 'button', id = 'go', text = '连接', primary = true },
    }},
    { type = 'table', id = 'results', columns = { '主机', '状态', '耗时' }, rows = {} , stretch = 1 },
    { type = 'progress', id = 'progress', value = 0, max = 100 },
  }
}

local panel = muxora.ui.panel{ id = 'user.health', title = '健康检查', content = view }

muxora.ui.on(panel, 'go', 'clicked', function(event)
  local host = muxora.ui.get(panel, 'host').text
  muxora.ui.update(panel, 'results', { rows_append = { { host, '检查中', '' } } })
end)
```

**控件**（第一批）：`label`、`button`、`input`（单行，可设为密码）、`textarea`、`number`、`checkbox`、`switch`、`radio`、`select`（下拉）、`slider`、`progress`、`separator`、`link`、`image`（只接受本地文件或内置图标名）、`list`、`table`、`tree`、`code`（只读或可编辑的代码文本，复用阶段 5 的编辑器控件）、`terminal`（嵌入一个已有终端会话的视图）。

**布局**：`row`、`column`、`grid`、`form`（标签与控件两列对齐）、`group`（带标题的分组）、`tabs`、`split`（可拖动的分隔）、`scroll`。每个节点可设 `stretch`、`min_width`、`min_height`、`align`、`visible`、`enabled`、`tooltip`。

**不允许**：任意样式表、绝对坐标、任意颜色值（颜色只能用主题颜色 ID，例如 `color = 'errorForeground'`）。这样保证脚本界面与程序其他部分风格一致，深色主题下也协调。

### 5.3 交互与更新

- **事件**：`ui.on(panel, id, event, fn)`。事件类型：`clicked`、`changed`（附新值）、`submitted`（输入框回车）、`selected`（列表、表格、树的选择变化）、`activated`（双击或回车）、`closed`（面板或对话框关闭）。同一控件的 `changed` 事件合并到下一帧，避免连续输入时产生大量回调。
- **读取**：`ui.get(panel, id)` 返回控件当前值；`ui.values(panel)` 返回全部带 `id` 控件的值。
- **更新**：`ui.update(panel, id, props)` 只改变给出的属性；列表、表格支持增量操作（`rows_append`、`rows_update`、`rows_remove`、`rows_set`），不必每次传整张表。`ui.replace(panel, id, subtree)` 替换一个子树。
- **大数据**：`table` 和 `list` 用 Qt 的 model/view 显示，十万行也只创建可见行的绘制；数据保存在宿主侧。

### 5.4 上限

| 项目 | 默认上限 |
| --- | --- |
| 每个面板或对话框的节点数 | 2000 |
| 嵌套深度 | 32 |
| 表格或列表的行数 | 100000（数据总量另受值传输上限约束，分批追加） |
| 每个脚本同时打开的面板 | 32 |
| 每秒更新次数（每个面板） | 60，超出的更新合并 |

### 5.5 为什么不用 QML 或网页

QML 或嵌入浏览器（WebView）都能做任意界面，但会带来另一套渲染和运行时：网页内核一个进程就是上百 MB 内存，与“开成百上千个会话”的内存目标冲突；而且界面风格很难与 Muxora 统一。声明式控件树复用 Muxora 自己的控件，成本低、风格一致，能覆盖工具类扩展绝大多数需要。

## 6. 终端接口（阶段 7 之后可用）

### 6.1 定位

- `terminal.list()`：全部终端（会话 ID、窗格 ID、标签 ID、窗口 ID、标题、类型、连接状态、尺寸）。
- 终端用窗格 ID 指定；`terminal.current()` 返回当前焦点所在的终端。

### 6.2 读取

| API | 说明 |
| --- | --- |
| `terminal.size(pane)` | 列数、行数、回滚历史行数 |
| `terminal.cursor(pane)` | 光标行列 |
| `terminal.text(pane, { from, to })` | 读取一段行（行号：0 为屏幕第一行，负数为回滚历史，例如 `-100`），返回字符串数组，每项一行；行尾空白去掉 |
| `terminal.cell(pane, row, col)` | 一个单元格的文字和属性（前景、背景、粗体等，颜色为主题颜色 ID 或 `#rrggbb`） |
| `terminal.region(pane, { row, col, rows, cols })` | 一个矩形区域的文字 |
| `terminal.screen(pane)` | 当前整屏文字 |
| `terminal.selection(pane)` | 当前选中的文字 |
| `terminal.find(pane, pattern, { regex, case, from })` | 在屏幕和历史中查找，返回匹配位置列表 |

宽字符（中文）占两列：按列定位时第二列返回空字符串，读取整行时按字符拼接。一次读取最多 10000 行，且受值传输的字节上限约束；需要更多时分批读取。读取是一个时间点的快照，不会读到半行更新。

### 6.3 写入

- `terminal.send(pane, text)`：发送文字（与用户键入相同的编码）；
- `terminal.send_keys(pane, keys)`：发送按键，例如 `'ctrl+c'`、`'enter'`；
- `terminal.paste(pane, text)`：按粘贴发送（遵守括号粘贴模式）；
- `terminal.clear(pane)`、`terminal.scroll(pane, lines)`。

### 6.4 订阅

| API | 说明 |
| --- | --- |
| `terminal.on_output(pane, fn, { pattern })` | 有新输出时回调；给出 `pattern`（正则）时，只在新输出的行匹配时回调，匹配在 C++ 中完成，Lua 只处理命中的行。同一终端的输出按 100 ms 合并成一批 |
| `terminal.on_command(pane, fn)` | Shell 集成（OSC 133）报告的命令结束：命令文字、退出码、耗时、输出范围 |
| `terminal.on_title(pane, fn)`、`terminal.on_cwd(pane, fn)`、`terminal.on_bell(pane, fn)`、`terminal.on_exit(pane, fn)` | 标题、目录、响铃、进程退出 |
| `terminal.expect(pane, pattern, timeout_ms)` | 等待写法：等到输出匹配或超时（用于登录脚本和自动化，阶段 7.6） |

数百个终端同时订阅时，回调频率按全局上限合并（默认每秒最多 200 批），超出时把多批合成一批并标记 `dropped_batches`，保证界面和终端处理不被脚本拖慢；脚本始终能通过 `terminal.text` 读到完整内容。

## 7. 其他接口

| 分组 | 能力 |
| --- | --- |
| `windows`／`tabs`／`groups`／`panes`／`sidebar`／`layout` | 已有：创建、移动、拆分、合并、固定、保存与恢复布局 |
| `sessions` | 列出、查询、新建、修改（受 SSH config 开关约束）、删除、连接、断开、重连；事件：连接、断开、需要登录、失败 |
| `local_terminals` | 本地终端配置列表，用某个配置打开终端 |
| `sftp` | 列目录、上传、下载、删除、重命名、创建目录；传输任务的进度事件 |
| `files` | 读写本机文件（文本或二进制，单次大小上限）、列目录、监听变化 |
| `clipboard` | 读写剪贴板文字 |
| `storage` | 每个脚本独立的持久化键值存储（JSON 值，每脚本上限 10 MiB），保存在 `User/lua-storage/<脚本名>.json` |
| `http` | 异步网络请求（GET／POST 等，超时、响应大小上限），基于 Qt Network |
| `app` | 启动外部程序（已有）、读取其输出（新增：`app.run` 等待结束并返回输出，大小有上限）、播放声音（已有） |
| `commands`／`keymap`／`context` | 已有命令和快捷键；新增 `when` 条件与上下文键（阶段 3） |
| `settings`／`theme` | 已有；新增声明设置项（阶段 4.5）、主题颜色覆盖（阶段 2.8） |
| `events` | 统一事件：窗口、标签、焦点、布局、主题、设置变化、会话、终端、传输 |
| `timers`／`clock` | 已有 |

## 8. 开发体验

- 阶段 5 的编辑器、控制台、补全和类型声明文件覆盖全部新接口（API 描述单一来源，阶段 5.4）。
- `lua-guide.md` 为每个分组给出可直接运行的示例；“工具 → Lua → 示例”可以把示例复制到 `User/scripts/` 并打开。
- 脚本出错时，错误堆栈中的位置可点击，在编辑器中打开。
